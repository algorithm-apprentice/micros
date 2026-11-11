"""Actual native kernel-mapping measurements and independent topology/work oracles."""

from dataclasses import dataclass, replace
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

from tests.host.bootstrap_allocation_evidence import json_pairs
from tests.host.kernel_mapping_source import (
    RECIPE_ADMISSION, STATE_WORK_CONTRACT,
)
from tests.host.runtime_memory_evidence import native_compiler
from tests.host.work_count_snapshots import compose_snapshots
from tests.host.work_count_types import Capture, Kind, Owner, Snapshot, Stage, Status
from tools.generate_kernel_mapping_fixture import prepare_sources
from tools.stack_artifact_files import read_regular
from tools.stack_bound_manifest import snapshot_files


LOAD_PROBE_INSTRUCTIONS = ("00053503", "00008067")


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


FIXTURE_PATHS = (
    "tests/host/kernel_mapping_test.c", "tests/host/kernel_mapping_test.h",
    "tests/host/kernel_mapping_source.py", "tests/host/kernel_mapping_evidence.py",
    "tests/host/native_observations.py", "tests/host/native_observations.json",
    "tests/host/source_snapshot.py", "tools/generate_kernel_mapping_fixture.py",
    "tests/host/c_source_records.py", "tests/host/work_count_lexical.py",
    "tests/host/work_count_projection.py", "tests/host/work_count_types.py",
    "tests/host/work_count_snapshots.py",
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
    if stage is not Stage.GREEN or type(observed) is not bool:
        raise ValueError("kernel mapping evidence requires current source and a known observation mode")
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
            metadata = row.middle_tables
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
                        for word in LOAD_PROBE_INSTRUCTIONS)
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
