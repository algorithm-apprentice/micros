"""Actual native kernel-mapping measurements and independent topology/work oracles."""

from dataclasses import asdict, dataclass, replace
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import unittest
import uuid

from tests.host.bootstrap_allocation_evidence import json_pairs
from tests.host.kernel_mapping_authority import DESIGN_PINS, ORACLE_RULES
from tests.host.kernel_mapping_source import (
    RECIPE_ADMISSION, SOURCE_PROJECTION, STATE_WORK_CONTRACT, project_mmu_source_bytes,
)
from tests.host.runtime_memory_evidence import native_compiler
from tests.host.work_count_snapshots import compose_snapshots
from tests.host.work_count_types import Capture, Kind, Owner, Snapshot, Stage, Status
from tools.generate_kernel_mapping_fixture import prepare_sources
from tools.stack_artifact_files import read_regular
from tools.stack_bound_manifest import owned_artifact, snapshot_files


@dataclass(frozen=True)
class Case:
    frames: int
    ranges: tuple
    domains: tuple
    tables: int
    middle_tables: int
    leaves: tuple


@dataclass(frozen=True)
class Measurement:
    case: int
    operation: str
    frames: int
    managed_ranges: int
    tables: int
    middle_tables: int
    leaves: tuple
    capture: Capture | None


@dataclass(frozen=True)
class NativeRun:
    stage: Stage
    observed: bool
    records: tuple
    compiler: dict
    command: tuple
    execution_command: tuple
    source_inputs: dict
    fixture_inputs: dict
    generated_sources: dict
    executable: dict
    output: str
    build_output: str
    observation_symbols: tuple


@dataclass(frozen=True)
class TargetRun:
    stage: Stage
    workspace: str
    source_directory: str
    source_inputs: dict
    commands: tuple
    artifacts: dict
    binding: dict
    cases: tuple
    tools: dict
    firmware: object


FIXTURE_PATHS = (
    "tests/host/kernel_mapping_test.c", "tests/host/kernel_mapping_test.h",
    "tests/host/kernel_mapping_source.py", "tests/host/kernel_mapping_evidence.py",
    "tests/host/kernel_mapping_authority.py", "tools/generate_kernel_mapping_fixture.py",
    "tests/work/work_counts.c", "tests/work/work_counts.h",
)


def expected_topology(domains):
    middle, lower = set(), set()
    leaves = [0, 0, 0]
    sizes = STATE_WORK_CONTRACT["leaf_sizes"]
    for start, end, _ in domains:
        address = start
        while address < end:
            level = next(
                level for level in (2, 1, 0)
                if address % sizes[level] == 0 and end - address >= sizes[level]
            )
            leaves[level] += 1
            if level < 2:
                middle.add(address >> 30)
            if level == 0:
                lower.add((address >> 30, (address >> 21) & 511))
            address += sizes[level]
    return 1 + len(middle) + len(lower), len(middle), tuple(leaves)


def case_inventory():
    contract = STATE_WORK_CONTRACT
    base = contract["default_managed_base"]
    inputs = [
        (((base, count),), contract["default_linker_bounds"])
        for count in contract["dense_frames"]
    ]
    inputs.extend(
        (((start, count),), contract["default_linker_bounds"])
        for start, count in contract["boundary_cases"]
    )
    inputs.extend((
        (((base, 4096),), (*contract["default_linker_bounds"][:3], 0x80400000)),
        (((0x80401000, 4096),), (*contract["default_linker_bounds"][:3], 0x80401000)),
        (((base, 16), ((1 << 38) - 4096, 1)), contract["default_linker_bounds"]),
        (tuple((base + index * (1 << 30) + offset * 4096, 1)
               for index in range(16) for offset in (0, 2, 4, 6, 8, 10)),
         contract["default_linker_bounds"]),
    ))
    result = []
    for ranges, bounds in inputs:
        domains = (
            (bounds[0], bounds[1], 5), (bounds[1], bounds[2], 1), (bounds[2], bounds[3], 3),
            *((start, start + count * 4096, 3) for start, count in ranges),
            *((start, start + 4096, 3) for start in contract["device_pages"]),
        )
        result.append(Case(
            sum(count for _, count in ranges), ranges, domains, *expected_topology(domains),
        ))
    if len(result) != 17:
        raise ValueError("MEM02 reference input inventory is incomplete")
    return tuple(result)


def decode_measurements(output, stage, *, observed):
    import json

    if stage not in (Stage.RED, Stage.GREEN) or type(observed) is not bool:
        raise ValueError("MEM02 measurements have an unknown source stage or mode")
    lines = output.splitlines()
    if not output.endswith("\n") or len(lines) != 86:
        raise ValueError("MEM02 native execution omitted or duplicated a case")
    guards = json.loads(lines[0], object_pairs_hook=json_pairs)
    if guards != {"operation": "guards", "semantic": True, "baseline": stage is Stage.RED}:
        raise ValueError("MEM02 native execution lacks its complete stage-specific guards")
    cases = case_inventory()
    operations = STATE_WORK_CONTRACT["captures"]
    fields = {
        "case", "operation", "frames", "managed_ranges", "tables", "middle_tables",
        "leaves", "expected_tables", "expected_leaves", "semantic", "capture",
    }
    result = []
    for label, line in enumerate(lines[1:], 1):
        value = json.loads(line, object_pairs_hook=json_pairs)
        case_index, operation_index = divmod(label - 1, 5)
        case = cases[case_index]
        if (
            type(value) is not dict or set(value) != fields
            or value["semantic"] is not True
            or any(type(value[name]) is not int or value[name] < 0
                   for name in ("case", "frames", "managed_ranges", "tables", "middle_tables", "expected_tables"))
            or value["case"] != case_index + 1 or value["operation"] != operations[operation_index]
            or value["frames"] != case.frames or value["managed_ranges"] != len(case.ranges)
            or value["expected_tables"] != case.tables or value["expected_leaves"] != list(case.leaves)
            or type(value["leaves"]) is not list or len(value["leaves"]) != 3
            or any(type(count) is not int or not 0 <= count <= 1 << 40 for count in value["leaves"])
            or value["tables"] > 1024 or value["middle_tables"] > value["tables"]
        ):
            raise ValueError("MEM02 native record changed its independent input/reference or failed semantics")
        if operation_index == 0 and (value["tables"] or any(value["leaves"])):
            raise ValueError("MEM02 pristine observation borrowed a completed tree")
        if operation_index != 0 and not value["tables"]:
            raise ValueError("MEM02 mapping observation has no actual table tree")
        capture = _decode_capture(value["capture"], label) if observed else None
        if not observed and value["capture"] is not None:
            raise ValueError("MEM02 ordinary execution fabricated collector state")
        result.append(Measurement(
            value["case"], value["operation"], case.frames, len(case.ranges),
            value["tables"], value["middle_tables"], tuple(value["leaves"]), capture,
        ))
    for start in range(0, 85, 5):
        shapes = {(row.tables, row.middle_tables, row.leaves) for row in result[start + 1:start + 5]}
        if len(shapes) != 1:
            raise ValueError("MEM02 read-only captures borrowed different completed trees")
    return tuple(result)


def _decode_capture(value, label):
    if type(value) is not dict or set(value) != {"status", "snapshot"} or type(value["status"]) is not int:
        raise ValueError("MEM02 work capture lacks its complete typed status")
    snapshot = value["snapshot"]
    if type(snapshot) is not dict or set(snapshot) != {
        "version", "reserved0", "producer_label", "capture_label", "owner_mask", "reserved1", "counts",
    } or type(snapshot["counts"]) is not list:
        raise ValueError("MEM02 work capture omitted collector fields")
    capture = Capture(Status(value["status"]), Snapshot(
        **{**snapshot, "counts": tuple(tuple(row) for row in snapshot["counts"])},
    ))
    compose_snapshots(
        (capture,), capture_label=label,
        expected_producers={STATE_WORK_CONTRACT["work"]["producer_label"]: 1 << Owner.KERNEL_MEMORY},
    )
    allowed = {Kind[name] for name in STATE_WORK_CONTRACT["work"]["kinds"]}
    if any(value for kind, value in enumerate(capture.snapshot.counts[Owner.KERNEL_MEMORY]) if kind not in allowed):
        raise ValueError("MEM02 work capture contains an undeclared primitive")
    return capture


def native_command(root, compiler, generated, executable, stage, observed):
    return (
        compiler["path"], "-std=c17", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
        "-Wmissing-prototypes", "-Wstrict-prototypes", "-fno-builtin",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
        "-I", str(generated), "-I", str(generated / "include"),
        "-I", str(root), "-I", str(root / "include"),
        *(("-DMICROS_KERNEL_MAPPING_BASELINE=1",) if stage is Stage.RED else ()),
        *(("-DMICROS_TEST_WORK_COUNTS=1", "-DMICROS_TEST_KERNEL_MAPPING_COUNTS=1") if observed else ()),
        *(str(generated / name) for name in RECIPE_ADMISSION["native_source_members"]),
        str(root / "tests/host/kernel_mapping_test.c"),
        *((str(root / "tests/work/work_counts.c"),) if observed else ()),
        "-o", str(executable),
    )


def measure_native(root, stage, *, observed=True):
    root = Path(root).resolve()
    compiler = native_compiler(root)
    fixtures = snapshot_files(root, FIXTURE_PATHS)
    with tempfile.TemporaryDirectory(prefix="micros-mem02-native-") as temporary:
        directory = Path(temporary)
        generated = directory / "source"
        _, inputs, outputs = prepare_sources(root, generated, stage, observed=observed)
        executable = directory / "kernel-mapping"
        command = native_command(root, compiler, generated, executable, stage, observed)
        built = subprocess.run(
            command, cwd=root, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=120,
        )
        if built.returncode:
            raise ValueError("MEM02 native compile/link failure is not runtime Red:\n" + built.stdout + built.stderr)
        image = read_regular(executable)[1]
        execution = (str(executable),)
        run = subprocess.run(
            execution, cwd=root, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=120,
        )
        if run.returncode or run.stderr:
            raise ValueError("MEM02 native semantic/execution failure is not work Red:\n" + run.stdout + run.stderr)
        nm = shutil.which("nm")
        if nm is None:
            raise ValueError("MEM02 observation absence requires the existing native nm")
        symbols = subprocess.check_output((nm, str(executable)), cwd=root, text=True, timeout=30)
        observation_symbols = tuple(sorted({
            line.split()[-1].lstrip("_") for line in symbols.splitlines()
            if line.split() and "micros_test_work_" in line.split()[-1]
        }))
        if bool(observation_symbols) is not observed:
            raise ValueError("MEM02 observation symbols differ from its declared compile mode")
        if read_regular(executable)[1] != image or snapshot_files(generated, tuple(outputs)) != outputs:
            raise ValueError("MEM02 generated source/image changed during execution")
    if native_compiler(root) != compiler or snapshot_files(root, FIXTURE_PATHS) != fixtures:
        raise ValueError("MEM02 native compiler/fixture inputs changed during execution")
    measured = NativeRun(
        stage, observed, (), compiler, command, execution, inputs, fixtures, outputs,
        image, run.stdout, built.stdout + built.stderr, observation_symbols,
    )
    return replace(measured, records=decode_measurements(run.stdout, stage, observed=observed))


class KernelMappingOracles(unittest.TestCase):
    measured = None
    target = None

    def selected(self, operation):
        self.assertIsNotNone(self.measured)
        self.assertTrue(self.measured.observed)
        result = tuple(row for row in self.measured.records if row.operation == operation)
        self.assertEqual(17, len(result))
        return result

    def test_large_leaf_topology(self):
        cases = case_inventory()
        for row in self.selected("construct"):
            expected = cases[row.case - 1]
            with self.subTest(case=row.case):
                self.assertEqual(expected.leaves, row.leaves)
                self.assertEqual(expected.tables, row.tables)
                self.assertEqual(expected.middle_tables, row.middle_tables)

    def test_direct_table_work(self):
        for row in self.selected("pristine"):
            self.assertEqual(row.frames, row.capture.snapshot.counts[Owner.KERNEL_MEMORY][Kind.BITMAP_WORD_LOAD])
        for row in self.selected("construct"):
            counts = row.capture.snapshot.counts[Owner.KERNEL_MEMORY]
            leaves, tables, ranges = sum(row.leaves), row.tables, row.managed_ranges
            metadata = row.middle_tables if self.measured.stage is Stage.GREEN else 0
            with self.subTest(case=row.case, operation=row.operation):
                self.assertLessEqual(counts[Kind.INDEX_PROBE], 32 * (leaves + tables + ranges + 1))
                self.assertLessEqual(counts[Kind.PTE_VISIT], 4 * leaves + 2 * tables)
                self.assertLessEqual(counts[Kind.RANGE_VISIT], tables * (192 + 4 * ranges) + 8 * leaves + 2 * ranges + 32)
                self.assertLessEqual(counts[Kind.RECORD_VISIT], tables * (192 + 4 * ranges) + 16 * leaves + 16 * tables + 128)
                self.assertLessEqual(counts[Kind.BITMAP_WORD_LOAD], 8 * (leaves + tables + 1))
                self.assertEqual(0, counts[Kind.COMPLETE_VALIDATOR_CALL])
                self.assertEqual(512 * (tables + metadata), counts[Kind.CLEAR_UNIT])
                self.assertEqual(4096 * tables + 1024 * metadata, counts[Kind.CLEARED_BYTE])
        for row in self.selected("validate"):
            counts = row.capture.snapshot.counts[Owner.KERNEL_MEMORY]
            with self.subTest(case=row.case, operation=row.operation):
                self.assertEqual(1, counts[Kind.COMPLETE_VALIDATOR_CALL])
                self.assertLessEqual(counts[Kind.RANGE_VISIT], 384 + 4 * row.tables)
        for row in self.selected("tree"):
            counts = row.capture.snapshot.counts[Owner.KERNEL_MEMORY]
            with self.subTest(case=row.case, operation=row.operation):
                self.assertLessEqual(counts[Kind.INDEX_PROBE], 16 * (row.tables + 1))
                self.assertEqual(512 * row.tables, counts[Kind.PTE_VISIT])
        cases = case_inventory()
        for row in self.selected("coverage"):
            expected_leaves = sum(cases[row.case - 1].leaves)
            with self.subTest(case=row.case, operation=row.operation):
                self.assertLessEqual(
                    row.capture.snapshot.counts[Owner.KERNEL_MEMORY][Kind.PTE_VISIT],
                    3 * (expected_leaves + row.tables + 4),
                )

    def test_target_mmu_baseline(self):
        self.assertIsNotNone(self.target)
        self.assertEqual(("128M", "256M"), tuple(case["memory"] for case in self.target.cases))
        for case in self.target.cases:
            observation = case["observation"]
            with self.subTest(memory=case["memory"]):
                self.assertTrue(observation["largest"])
                self.assertEqual(observation["expected_tables"], observation["tables"])
                self.assertEqual(observation["expected_leaves"], observation["leaves"])


def _merge_intervals(intervals):
    result = []
    for start, end in sorted(intervals):
        if start >= end:
            continue
        if result and start <= result[-1][1]:
            result[-1] = (result[-1][0], max(result[-1][1], end))
        else:
            result.append((start, end))
    return tuple(result)


def target_domains(output, bounds):
    from tools.qemu_smoke_records import FDT_RANGE_PATTERN

    memory, reserved = [], [(0, bounds["__kernel_end"])]
    for line in output.splitlines():
        match = FDT_RANGE_PATTERN.fullmatch(line)
        if match is None:
            continue
        name, base, size = line.split()
        start, extent = int(base.split("=")[1], 16), int(size.split("=")[1], 16)
        if start + extent > (1 << 64) - 1:
            raise ValueError("MEM02 FDT expectation has an overflowing interval")
        if name == "MICROS_FDT_MEMORY":
            memory.append(((start + 4095) & ~4095, (start + extent) & ~4095))
        else:
            reserved.append((start & ~4095, (start + extent + 4095) & ~4095))
    if not memory:
        raise ValueError("MEM02 target expectation has no actual FDT memory")
    reservations = _merge_intervals(reserved)
    managed = []
    for start, end in _merge_intervals(memory):
        cursor = start
        for low, high in reservations:
            if high <= cursor or low >= end:
                continue
            if cursor < low:
                managed.append((cursor, min(low, end)))
            cursor = max(cursor, min(high, end))
        if cursor < end:
            managed.append((cursor, end))
    domains = (
        (bounds["__kernel_text_start"], bounds["__kernel_text_end"], 5),
        (bounds["__kernel_rodata_start"], bounds["__kernel_rodata_end"], 1),
        (bounds["__kernel_writable_start"], bounds["__kernel_writable_end"], 3),
        *((start, end, 3) for start, end in managed),
        *((start, start + 4096, 3) for start in STATE_WORK_CONTRACT["device_pages"]),
    )
    return tuple(managed), domains


def bind_target_image(path):
    from tools.user_elf_parse import load_elf

    image = load_elf(path)

    def symbol(name):
        selected = tuple(item for item in image.symbols if item.name == name and item.section_index != 0)
        if len(selected) != 1:
            raise ValueError("MEM02 ELF lacks a unique defined symbol: " + name)
        return selected[0]

    names = (
        "__kernel_start", "__kernel_text_start", "__kernel_text_end",
        "__kernel_rodata_start", "__kernel_rodata_end",
        "__kernel_writable_start", "__kernel_writable_end", "__kernel_end",
    )
    bounds = {name: symbol(name).value for name in names}
    ordered = (
        bounds["__kernel_text_start"], bounds["__kernel_text_end"],
        bounds["__kernel_rodata_end"], bounds["__kernel_writable_end"],
    )
    if (
        any(value % 4096 for value in bounds.values())
        or ordered != tuple(sorted(set(ordered)))
        or bounds["__kernel_start"] != ordered[0] or bounds["__kernel_end"] != ordered[-1]
        or bounds["__kernel_rodata_start"] != ordered[1]
        or bounds["__kernel_writable_start"] != ordered[2]
    ):
        raise ValueError("MEM02 ELF has inconsistent linker-domain inputs")
    probe = symbol("micros_mmu_large_load_test_trigger")
    section = image.sections[probe.section_index]
    offset = section.offset + probe.value - section.address
    expected = b"".join(int(word, 16).to_bytes(4, "little")
                        for word in SOURCE_PROJECTION["mmu_fixture_inputs"]["load_instructions"])
    if (
        probe.symbol_type != 2 or probe.size != 8 or section.section_type != 1 or section.flags != 6
        or not section.address <= probe.value <= probe.value + 8 <= section.address + section.size
        or image.data[offset:offset + 8] != expected
        or symbol("micros_mmu_large_load_fault").value != probe.value
        or symbol("micros_mmu_large_load_resume").value != probe.value + 4
    ):
        raise ValueError("MEM02 ELF load probe differs from its independent zero-stack instruction contract")
    return {"linker_bounds": bounds, "load_probe": probe.value, "load_bytes": expected.hex()}


def decode_target_output(result, binding):
    from tools.qemu_smoke_outcome import classify_smoke, matches_expected_result
    from tools.qemu_smoke_platform import parse_frame_allocator_ready_counts, parse_mmu_ready
    from tools.qemu_smoke_records import MMU_TEST_PASS, SmokeOutcome, _split_output_records

    tty = (
        "MICROS_TTY_CONTROLLER_TEST_PASS mapping=exact priority=disabled enable=disabled "
        "context=supervisor seie=independent"
    )
    markers = ("MICROS_BOOT 0.1.0", "MICROS_FDT_READY", "MICROS_TRAP_READY", MMU_TEST_PASS, tty)
    outcome = classify_smoke(
        output=result.output, return_code=result.return_code, timed_out=result.timed_out,
        markers=markers, require_fdt_events=True, require_fdt_reservations=True,
    )
    if not matches_expected_result(
        result=result, observed_outcome=outcome, expected_outcome=SmokeOutcome.PASS,
        markers=markers, patterns=(), require_fdt_events=True, require_fdt_reservations=True,
        require_trap_ready=True, require_frame_allocator_ready=True, require_mmu_ready=True,
        require_frame_ownership_ready=True, require_mmu_test_report=True, require_objects_ready=True,
    ):
        raise ValueError("MEM02 target lost an original MMU/TTY guard or clean shutdown")
    lines, terminated = _split_output_records(result.output)
    selected = [(index, line) for index, line in enumerate(lines) if line.startswith("MICROS_KERNEL_MAPPING")]
    expression = (
        r"MICROS_KERNEL_MAPPING_OBSERVATION version=1 tables=0x([0-9a-f]{16}) "
        r"leaves-4k=0x([0-9a-f]{16}) leaves-2m=0x([0-9a-f]{16}) "
        r"leaves-1g=0x([0-9a-f]{16}) largest=([01])"
    )
    if len(selected) not in (2, 3) or any(not terminated[index] for index, _ in selected):
        raise ValueError("MEM02 target has missing, extra or unterminated observations")
    observation = re.fullmatch(expression, selected[0][1])
    guard = "MICROS_KERNEL_MAPPING_GUARDS aliases=2m,1g misaligned=rejected resources=preserved"
    if observation is None or selected[1][1] != guard:
        raise ValueError("MEM02 target observation/guard schema or order is invalid")
    tables, *leaves, largest = (int(value, 16) for value in observation.groups())
    if (len(selected) == 3) != bool(largest) or largest and selected[2][1] != STATE_WORK_CONTRACT["target"]["pass"]:
        raise ValueError("MEM02 target pass marker borrows an unsuccessful topology")
    ready = [index for index, line in enumerate(lines) if line.startswith("MICROS_FRAME_OWNERSHIP_READY")]
    if len(ready) != 1 or not ready[0] < selected[0][0] < selected[-1][0] < lines.index(MMU_TEST_PASS) < lines.index(tty):
        raise ValueError("MEM02 target records escaped their original readiness/completion interval")
    managed, domains = target_domains(result.output, binding["linker_bounds"])
    expected_tables, _, expected_leaves = expected_topology(domains)
    matching = tables == expected_tables and tuple(leaves) == expected_leaves
    if (
        not 0 < tables <= 1024 or parse_mmu_ready(result.output)[1] != tables
        or bool(largest) != matching
        or sum((end - start) // 4096 for start, end in managed) != parse_frame_allocator_ready_counts(result.output)[0]
        or sum(count * (512 ** level) for level, count in enumerate(leaves))
        != sum((end - start) // 4096 for start, end, _ in domains)
    ):
        raise ValueError("MEM02 target topology disagrees with actual ELF/FDT inputs or original counts")
    return {
        "tables": tables, "leaves": tuple(leaves), "largest": bool(largest),
        "expected_tables": expected_tables, "expected_leaves": expected_leaves,
    }


def _target_members(root):
    output = subprocess.check_output(
        ("git", "ls-tree", "-r", "-z", DESIGN_PINS["runtime_reference"]), cwd=root, timeout=30,
    )
    members = {}
    for row in filter(None, output.split(b"\0")):
        metadata, raw_path = row.split(b"\t", 1)
        mode, kind, oid = metadata.split()
        path = raw_path.decode()
        owned_artifact(root, path)
        if mode not in (b"100644", b"100755") or kind != b"blob" or path in members:
            raise ValueError("MEM02 target baseline has a nonordinary or duplicate source")
        members[path] = (int(mode, 8) & 0o777, oid.decode())
    return members


def _validate_target_baseline(root, source, members):
    seen = set()
    fixtures = set(RECIPE_ADMISSION["qemu_fixture_paths"])
    for path in source.rglob("*"):
        if path.is_symlink():
            raise ValueError("MEM02 target baseline contains a linked source")
        if path.is_dir():
            continue
        name = path.relative_to(source).as_posix()
        if name not in members:
            raise ValueError("MEM02 target baseline contains an unadmitted source")
        data, identity = read_regular(path)
        if name in fixtures:
            if (data, identity) != read_regular(root / name):
                raise ValueError("MEM02 baseline does not use the identical immutable MMU fixture")
            data = project_mmu_source_bytes(name, data)
        oid = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
        if (identity["mode"], oid) != members[name]:
            raise ValueError("MEM02 target baseline does not recover its fixed Git blob/mode: " + name)
        seen.add(name)
    if seen != set(members):
        raise ValueError("MEM02 target baseline omitted an original input")


def _prepare_target_baseline(root, workspace):
    members = _target_members(root)
    source = owned_artifact(workspace, "source")
    source.mkdir(mode=0o700)
    data = subprocess.check_output(
        ("git", "-c", "tar.umask=022", "archive", DESIGN_PINS["runtime_reference"]), cwd=root, timeout=30,
    )
    seen = set()
    with tarfile.open(fileobj=io.BytesIO(data)) as archive:
        for member in archive:
            path = owned_artifact(source, member.name.rstrip("/"))
            if member.isdir():
                path.mkdir(parents=True, exist_ok=True)
                continue
            if not member.isfile() or member.name not in members or member.name in seen:
                raise ValueError("MEM02 baseline archive has an unadmitted member")
            body = archive.extractfile(member).read()
            oid = hashlib.sha1(b"blob " + str(len(body)).encode() + b"\0" + body).hexdigest()
            if (member.mode & 0o777, oid) != members[member.name]:
                raise ValueError("MEM02 baseline archive changed a fixed source")
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(body)
            path.chmod(member.mode & 0o777)
            seen.add(member.name)
    if seen != set(members):
        raise ValueError("MEM02 baseline archive is incomplete")
    for name in RECIPE_ADMISSION["qemu_fixture_paths"]:
        body, identity = read_regular(root / name)
        project_mmu_source_bytes(name, body)
        (source / name).write_bytes(body)
        (source / name).chmod(identity["mode"])
    _validate_target_baseline(root, source, members)
    return source, members


def measure_target(root, stage):
    from tests.host.diagnostic_history_evidence import build_artifacts, closed_tool_inputs
    from tools.qemu_smoke_command import build_qemu_command
    from tools.qemu_smoke_execution import run_qemu
    from tools.stack_bound_fatal_firmware import resolve_default_firmware

    root = Path(root).resolve()
    if stage not in (Stage.RED, Stage.GREEN):
        raise ValueError("MEM02 target execution has an unknown source stage")
    tools = closed_tool_inputs(root)
    qemu = shutil.which("qemu-system-riscv64")
    if qemu is None:
        raise ValueError("MEM02 requires its existing configured QEMU")
    firmware = resolve_default_firmware(qemu)
    parent = owned_artifact(root, "build/kernel-mapping")
    parent.mkdir(parents=True, exist_ok=True)
    workspace = owned_artifact(parent, uuid.uuid4().hex)
    workspace.mkdir(mode=0o700)
    if stage is Stage.RED:
        source, members = _prepare_target_baseline(root, workspace)
    else:
        source = root
        members = {
            path.decode(): None for path in subprocess.check_output(
                ("git", "ls-files", "-z"), cwd=root, timeout=30,
            ).split(b"\0") if path
        }
    inputs = snapshot_files(source, tuple(sorted(members)))
    if (source / "CMakeUserPresets.json").exists() or (source / "CMakeUserPresets.json").is_symlink():
        raise ValueError("MEM02 does not admit caller-owned preset overrides")
    directory = owned_artifact(workspace, "target")
    commands = (
        ("cmake", "--fresh", "--preset", "riscv64-mmu-test", "-S", str(source), "-B", str(directory),
         "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"),
        ("cmake", "--build", str(directory), "--clean-first", "--target", "micros"),
    )
    records = []
    for command in commands:
        run = subprocess.run(
            command, cwd=source, stdin=subprocess.DEVNULL, capture_output=True, text=True,
        )
        records.append({"argv": command, "cwd": str(source), "exit_code": run.returncode, "output": run.stdout + run.stderr})
        if run.returncode:
            raise ValueError("MEM02 target configure/compile failure is not runtime Red:\n" + run.stdout + run.stderr)
    artifacts = snapshot_files(directory, build_artifacts(directory, configure="riscv64-mmu-test"))
    image = directory / "kernel/micros.elf"
    binding = bind_target_image(image)
    cases = []
    for memory in RECIPE_ADMISSION["qemu_memories"]:
        command = tuple(build_qemu_command(qemu=qemu, kernel=str(image), memory=memory))
        run = run_qemu(command, RECIPE_ADMISSION["qemu_timeout"])
        cases.append({
            "memory": memory, "command": command, "result": asdict(run),
            "observation": decode_target_output(run, binding),
        })
    if stage is Stage.RED:
        _validate_target_baseline(root, source, members)
    if (
        snapshot_files(source, tuple(inputs)) != inputs
        or snapshot_files(directory, tuple(artifacts)) != artifacts
        or closed_tool_inputs(root) != tools or resolve_default_firmware(qemu) != firmware
    ):
        raise ValueError("MEM02 target source, tools, firmware or actual artifacts changed during execution")
    return TargetRun(stage, str(workspace), str(source), inputs, tuple(records), artifacts,
                     binding, tuple(cases), tools, firmware)


def run_oracles(native, target):
    from tests.host.evidence_authority_types import ObservedResult, OracleResult, RunStatus

    if type(native) is not NativeRun or type(target) is not TargetRun or not native.observed or native.stage is not target.stage:
        raise ValueError("MEM02 requires complete matching actual native and target executions")
    output = io.StringIO()
    outcomes = []
    for identifier, test, property_name, _, _, _ in ORACLE_RULES:
        case = KernelMappingOracles(test.rsplit(".", 1)[1])
        case.measured = native
        case.target = target
        result = unittest.TextTestRunner(stream=output, verbosity=2).run(unittest.TestSuite((case,)))
        if result.testsRun != 1 or result.errors or result.skipped:
            raise ValueError("MEM02 oracle was skipped or failed outside its measured assertion")
        if native.stage is Stage.RED:
            if result.wasSuccessful():
                raise ValueError("MEM02 baseline did not demonstrate every required audited divergence")
            observed = ObservedResult.EXPECTED_FAILURE
        else:
            observed = ObservedResult.PASS if result.wasSuccessful() else ObservedResult.FAIL
        outcomes.append(OracleResult(identifier, RunStatus.RUN, observed, property_name))
    return tuple(outcomes), output.getvalue()


def execution_log(native, target, results, oracle_output, *, phase, attempt):
    native_value = asdict(native)
    del native_value["records"]
    native_value["stage"] = native.stage.value
    target_value = asdict(target)
    target_value["stage"] = target.stage.value
    return json.loads(json.dumps({
        "version": 1, "phase": phase, "attempt": attempt,
        "native": native_value, "target": target_value,
        "results": [asdict(result) for result in results], "oracle_output": oracle_output,
    }, allow_nan=False))


def _validate_native_replay(context, native, stage):
    from tests.host.diagnostic_history_authority import record_fields, record_strings
    from tools.generate_kernel_mapping_fixture import source_bundle

    record_fields(native, (
        "stage", "observed", "compiler", "command", "execution_command", "source_inputs",
        "fixture_inputs", "generated_sources", "executable", "output", "build_output", "observation_symbols",
    ))
    if (
        native["stage"] != stage.value or native["observed"] is not True
        or native["compiler"] != context.native_compiler
        or type(native["output"]) is not str or type(native["build_output"]) is not str
    ):
        raise ValueError("MEM02 native replay borrowed a source stage/compiler or omitted output")
    execution = record_strings(native["execution_command"])
    if len(execution) != 1:
        raise ValueError("MEM02 native replay lacks its exact executed image")
    executable = Path(execution[0])
    directory = executable.parent
    if (
        not executable.is_absolute() or executable.name != "kernel-mapping"
        or not directory.name.startswith("micros-mem02-native-")
        or directory.parent.resolve() != Path(tempfile.gettempdir()).resolve()
    ):
        raise ValueError("MEM02 native replay names an unowned execution")
    command = record_strings(native["command"])
    if command != native_command(context.root, context.native_compiler, directory / "source", executable, stage, True):
        raise ValueError("MEM02 native replay changed its exact compile/link command")
    inputs, outputs = source_bundle(context.root, stage)
    generated = {
        name: {"kind": "file", "mode": mode, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        for name, (data, mode) in outputs.items()
    }
    if (
        native["source_inputs"] != inputs or native["generated_sources"] != generated
        or native["fixture_inputs"] != {name: context.files[name] for name in FIXTURE_PATHS}
    ):
        raise ValueError("MEM02 native replay substituted a source/header/observation/fixture input")
    identity = native["executable"]
    record_fields(identity, ("kind", "mode", "size", "sha256"))
    if (
        identity["kind"] != "file" or type(identity["mode"]) is not int
        or not 0 < identity["mode"] <= 0o777 or not identity["mode"] & 0o100
        or type(identity["size"]) is not int or identity["size"] <= 0
        or type(identity["sha256"]) is not str or re.fullmatch(r"[a-f0-9]{64}", identity["sha256"]) is None
    ):
        raise ValueError("MEM02 native replay lacks a complete executable identity")
    symbols = record_strings(native["observation_symbols"])
    if set(symbols) != {"micros_test_work_begin", "micros_test_work_finish", "micros_test_work_record"}:
        raise ValueError("MEM02 native replay omitted its actual observation symbols")
    return NativeRun(
        stage, True, decode_measurements(native["output"], stage, observed=True),
        context.native_compiler, command, execution, inputs, native["fixture_inputs"], generated,
        identity, native["output"], native["build_output"], symbols,
    )


def _validate_target_replay(context, target, stage):
    from tests.host.diagnostic_history_authority import record_fields, record_strings
    from tests.host.diagnostic_history_evidence import build_artifacts, closed_tool_inputs
    from tests.host.evidence_authority_types import digest
    from tools.qemu_smoke_command import build_qemu_command
    from tools.qemu_smoke_records import QemuResult
    from tools.stack_bound_fatal_firmware import resolve_default_firmware

    record_fields(target, (
        "stage", "workspace", "source_directory", "source_inputs", "commands", "artifacts",
        "binding", "cases", "tools", "firmware",
    ))
    if target["stage"] != stage.value or type(target["workspace"]) is not str:
        raise ValueError("MEM02 target replay has a borrowed source stage")
    workspace = Path(target["workspace"])
    if (
        workspace.parent != context.root / "build/kernel-mapping"
        or re.fullmatch(r"[a-f0-9]{32}", workspace.name) is None
        or owned_artifact(context.root, workspace.relative_to(context.root).as_posix()) != workspace
    ):
        raise ValueError("MEM02 target replay leaves its owned workspace")
    source = workspace / "source" if stage is Stage.RED else context.root
    directory = workspace / "target"
    if target["source_directory"] != str(source):
        raise ValueError("MEM02 target replay has another source root")
    if stage is Stage.RED:
        members = _target_members(context.root)
        _validate_target_baseline(context.root, source, members)
        inputs = snapshot_files(source, tuple(sorted(members)))
    else:
        inputs = context.files
    if target["source_inputs"] != inputs:
        raise ValueError("MEM02 target replay changed its complete source inputs")
    commands = (
        ("cmake", "--fresh", "--preset", "riscv64-mmu-test", "-S", str(source), "-B", str(directory),
         "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"),
        ("cmake", "--build", str(directory), "--clean-first", "--target", "micros"),
    )
    if type(target["commands"]) is not list or len(target["commands"]) != 2:
        raise ValueError("MEM02 target replay omitted a build stage")
    for row, command in zip(target["commands"], commands):
        record_fields(row, ("argv", "cwd", "exit_code", "output"))
        if (
            record_strings(row["argv"]) != command or row["cwd"] != str(source)
            or type(row["exit_code"]) is not int or row["exit_code"] != 0
            or type(row["output"]) is not str or not row["output"]
        ):
            raise ValueError("MEM02 target replay changed or failed a canonical build command")
    artifacts = snapshot_files(directory, build_artifacts(directory, configure="riscv64-mmu-test"))
    image = directory / "kernel/micros.elf"
    binding = bind_target_image(image)
    qemu = shutil.which("qemu-system-riscv64")
    if (
        target["artifacts"] != artifacts or target["binding"] != binding or qemu is None
        or digest(target["tools"]) != digest(closed_tool_inputs(context.root))
        or digest(target["firmware"]) != digest(resolve_default_firmware(qemu))
    ):
        raise ValueError("MEM02 target replay changed actual artifacts, tool or firmware identities")
    if type(target["cases"]) is not list or len(target["cases"]) != 2:
        raise ValueError("MEM02 target replay omitted or duplicated a memory run")
    for case, memory in zip(target["cases"], RECIPE_ADMISSION["qemu_memories"]):
        record_fields(case, ("memory", "command", "result", "observation"))
        record_fields(case["result"], ("output", "return_code", "timed_out", "serial_input_sent"))
        if (
            case["memory"] != memory
            or record_strings(case["command"]) != tuple(build_qemu_command(qemu=qemu, kernel=str(image), memory=memory))
            or type(case["result"]["output"]) is not str
            or type(case["result"]["return_code"]) is not int
            or type(case["result"]["timed_out"]) is not bool
            or type(case["result"]["serial_input_sent"]) is not bool
        ):
            raise ValueError("MEM02 target replay has a foreign command or malformed run result")
        observed = decode_target_output(QemuResult(**case["result"]), binding)
        if case["observation"] != json.loads(json.dumps(observed)):
            raise ValueError("MEM02 target replay does not follow its actual complete output")
    return TargetRun(
        stage, str(workspace), str(source), inputs, tuple(target["commands"]), artifacts,
        binding, tuple(target["cases"]), target["tools"], target["firmware"],
    )


def validate_execution_log(context, replay, *, phase, attempt):
    from tests.host.diagnostic_history_authority import record_fields

    if phase not in ("red", "green", "recertify") or type(attempt) is not str or re.fullmatch(r"[a-f0-9]{32}", attempt) is None:
        raise ValueError("MEM02 replay has an unadmitted phase or attempt")
    record_fields(replay, ("version", "phase", "attempt", "native", "target", "results", "oracle_output"))
    if (
        type(replay["version"]) is not int or replay["version"] != 1
        or replay["phase"] != phase or replay["attempt"] != attempt
        or type(replay["oracle_output"]) is not str
    ):
        raise ValueError("MEM02 replay belongs to a different phase or attempt")
    stage = Stage.RED if phase == "red" else Stage.GREEN
    native = _validate_native_replay(context, replay["native"], stage)
    target = _validate_target_replay(context, replay["target"], stage)
    results, _ = run_oracles(native, target)
    if replay["results"] != json.loads(json.dumps([asdict(result) for result in results])):
        raise ValueError("MEM02 replay outcomes do not follow the actual complete executions")
    return results
