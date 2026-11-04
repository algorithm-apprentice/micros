"""Actual native MEM01 execution and independent work envelopes."""

from dataclasses import asdict, dataclass, replace
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tests.host.bootstrap_allocation_authority import ORACLE_RULES
from tests.host.bootstrap_allocation_source import RECIPE_ADMISSION, STATE_WORK_CONTRACT
from tests.host.evidence_authority_types import ObservedResult, OracleResult, RunStatus
from tests.host.runtime_memory_evidence import native_compiler
from tests.host.work_count_snapshots import compose_snapshots
from tests.host.work_count_types import Capture, Kind, Owner, Snapshot, Stage, Status
from tools.generate_bootstrap_allocation_fixture import prepare_sources, source_bundle
from tools.stack_artifact_files import read_regular
from tools.stack_bound_manifest import snapshot_files


@dataclass(frozen=True)
class Measurement:
    operation: str
    frames: int
    allocations: int
    memory_ranges: int
    reservations: int
    managed_ranges: int
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
    "tests/host/bootstrap_allocation_test.c", "tests/host/bootstrap_allocation_test.h",
    "tests/host/bootstrap_allocation_source.py", "tests/host/bootstrap_allocation_evidence.py",
    "tests/host/bootstrap_allocation_authority.py", "tools/generate_bootstrap_allocation_fixture.py",
    "tests/work/work_counts.c", "tests/work/work_counts.h",
)


def json_pairs(items):
    result = {}
    for key, value in items:
        if key in result:
            raise ValueError("MEM01 record has a duplicate field")
        result[key] = value
    return result


def case_inventory():
    cases = []
    for frames in STATE_WORK_CONTRACT["dense_frames"]:
        for operation, count in (
            ("bootstrap-free", 0),
            ("bootstrap-live", min(frames, STATE_WORK_CONTRACT["live_prefix"])),
            ("allocate", min(frames, STATE_WORK_CONTRACT["allocation_prefix"])),
            ("owned-allocate", min(frames, STATE_WORK_CONTRACT["allocation_prefix"])),
        ):
            cases.append((operation, frames, count, 1, 0, 1))
    cases.extend((
        ("bootstrap-split-free", 80, 0, 16, 64, 80),
        ("bootstrap-split-live", 80, 33, 16, 64, 80),
        ("allocate-split", 96, 96, 16, 80, 96),
    ))
    return tuple(cases)


def decode_measurements(output, stage, *, observed):
    if stage not in (Stage.RED, Stage.GREEN) or type(observed) is not bool:
        raise ValueError("MEM01 measurements have an unknown stage/mode")
    lines = output.splitlines()
    expected = case_inventory()
    if len(lines) != len(expected) + 1:
        raise ValueError("MEM01 native execution omitted or duplicated a case")
    guards = json.loads(lines[0], object_pairs_hook=json_pairs)
    if guards != {
        "operation": "guards", "semantic": True,
        "cursor_layout": "absent" if stage is Stage.RED else "current",
    } or guards["semantic"] is not True:
        raise ValueError("MEM01 semantic/rollback guards are incomplete or borrow a layout")
    result = []
    fields = ("operation", "frames", "allocations", "memory_ranges", "reservations", "managed_ranges")
    for index, (line, case) in enumerate(zip(lines[1:], expected), 1):
        value = json.loads(line, object_pairs_hook=json_pairs)
        if (
            type(value) is not dict or set(value) != {*fields, "semantic", "capture"}
            or tuple(value[name] for name in fields) != case or value["semantic"] is not True
            or any(type(value[name]) is not int for name in fields[1:])
        ):
            raise ValueError("MEM01 native record failed semantics or changed its fixed input")
        capture = None
        if observed:
            data = value["capture"]
            if type(data) is not dict or set(data) != {"status", "snapshot"} or type(data["status"]) is not int:
                raise ValueError("MEM01 capture is not a complete typed collector result")
            snapshot = data["snapshot"]
            if type(snapshot) is not dict or set(snapshot) != {
                "version", "reserved0", "producer_label", "capture_label", "owner_mask", "reserved1", "counts",
            } or type(snapshot["counts"]) is not list:
                raise ValueError("MEM01 capture omitted collector fields")
            capture = Capture(Status(data["status"]), Snapshot(
                **{**snapshot, "counts": tuple(tuple(row) for row in snapshot["counts"])},
            ))
            compose_snapshots(
                (capture,), capture_label=index,
                expected_producers={STATE_WORK_CONTRACT["work"]["producer_label"]: 1 << Owner.KERNEL_MEMORY},
            )
            row = capture.snapshot.counts[Owner.KERNEL_MEMORY]
            allowed = {Kind[name] for name in STATE_WORK_CONTRACT["work"]["kinds"]}
            if (
                any(count for kind, count in enumerate(row) if kind not in allowed)
                or not row[Kind.BITMAP_WORD_LOAD] or not row[Kind.RANGE_VISIT]
                or case[0] in ("allocate", "owned-allocate", "allocate-split") and not row[Kind.INDEX_PROBE]
                or case[0] == "owned-allocate" and not row[Kind.RECORD_VISIT]
            ):
                raise ValueError("MEM01 work capture omits actual primitives or contains unowned work")
        elif value["capture"] is not None:
            raise ValueError("MEM01 ordinary execution fabricated collector state")
        result.append(Measurement(*case, capture))
    return tuple(result)


def native_command(root, compiler, generated_root, executable, stage, observed):
    sources = tuple(generated_root / name for name in (
        *RECIPE_ADMISSION["native_source_members"], "kernel/memory/bootstrap_memory.c",
    ))
    return (
        compiler["path"], "-std=c17", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
        "-Wmissing-prototypes", "-Wstrict-prototypes", "-fno-builtin",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
        "-I", str(generated_root), "-I", str(generated_root / "include"),
        "-I", str(root), "-I", str(root / "include"),
        *(("-DMICROS_BOOTSTRAP_ALLOCATION_BASELINE=1",) if stage is Stage.RED else ()),
        *(("-DMICROS_TEST_WORK_COUNTS=1", "-DMICROS_TEST_BOOTSTRAP_MEMORY_COUNTS=1") if observed else ()),
        *(str(path) for path in sources),
        str(root / "tests/host/bootstrap_allocation_test.c"),
        *((str(root / "tests/work/work_counts.c"),) if observed else ()),
        "-o", str(executable),
    )


def measure_native(root, stage, *, observed=True):
    if stage not in (Stage.RED, Stage.GREEN) or type(observed) is not bool:
        raise ValueError("MEM01 native execution has an unknown stage/mode")
    root = Path(root).resolve()
    compiler = native_compiler(root)
    fixture_inputs = snapshot_files(root, FIXTURE_PATHS)
    with tempfile.TemporaryDirectory(prefix="micros-mem01-native-") as temporary:
        directory = Path(temporary)
        generated_root = directory / "source"
        _, source_inputs, generated = prepare_sources(root, generated_root, stage, observed=observed)
        executable = directory / "bootstrap-allocation"
        command = native_command(root, compiler, generated_root, executable, stage, observed)
        built = subprocess.run(
            command, cwd=root, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=120,
        )
        if built.returncode:
            raise ValueError("MEM01 native compile/link failure is not runtime Red:\n" + built.stdout + built.stderr)
        image = read_regular(executable)[1]
        execution_command = (str(executable),)
        completed = subprocess.run(
            execution_command, cwd=root, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=120,
        )
        if completed.returncode or completed.stderr:
            raise ValueError("MEM01 native semantic/execution failure is not work Red:\n" + completed.stdout + completed.stderr)
        nm = shutil.which("nm")
        if nm is None:
            raise ValueError("MEM01 ordinary observation absence requires the existing native nm")
        symbols = subprocess.check_output((nm, str(executable)), cwd=root, text=True, timeout=30)
        observation_symbols = tuple(sorted({
            line.split()[-1].lstrip("_") for line in symbols.splitlines()
            if line.split() and "micros_test_work_" in line.split()[-1]
        }))
        if bool(observation_symbols) is not observed:
            raise ValueError("MEM01 observation state differs from its declared compile mode")
        if (
            read_regular(executable)[1] != image
            or snapshot_files(generated_root, tuple(generated)) != generated
        ):
            raise ValueError("MEM01 native generated source/image changed during execution")
        if stage is Stage.GREEN:
            for name, identity in source_inputs.items():
                if read_regular(root / name)[1] != identity:
                    raise ValueError("MEM01 actual current source/header changed during execution")
    if native_compiler(root) != compiler or snapshot_files(root, FIXTURE_PATHS) != fixture_inputs:
        raise ValueError("MEM01 native compiler/fixture inputs changed during execution")
    measured = NativeRun(
        stage, observed, (), compiler, command, execution_command, source_inputs,
        fixture_inputs, generated, image, completed.stdout, built.stdout + built.stderr,
        observation_symbols,
    )
    return replace(measured, records=decode_measurements(completed.stdout, stage, observed=observed))


class BootstrapAllocationOracles(unittest.TestCase):
    measured = None

    def selected(self, operations):
        self.assertIsNotNone(self.measured)
        self.assertTrue(self.measured.observed)
        selected = tuple(row for row in self.measured.records if row.operation in operations)
        self.assertTrue(selected)
        return selected

    def test_bootstrap_range_work(self):
        for case in self.selected({
            "bootstrap-free", "bootstrap-live", "bootstrap-split-free", "bootstrap-split-live",
        }):
            counts = case.capture.snapshot.counts[Owner.KERNEL_MEMORY]
            m, g, r = case.memory_ranges, case.managed_ranges, case.reservations
            with self.subTest(operation=case.operation, frames=case.frames):
                self.assertLessEqual(counts[Kind.RANGE_VISIT], 192 + m * (m + g) + g * r)
                self.assertLessEqual(counts[Kind.BITMAP_WORD_LOAD], 4096)

    def test_allocator_prefix_work(self):
        for case in self.selected({"allocate", "allocate-split"}):
            counts = case.capture.snapshot.counts[Owner.KERNEL_MEMORY]
            with self.subTest(operation=case.operation, frames=case.frames):
                self.assertLessEqual(counts[Kind.BITMAP_WORD_LOAD], 2 * case.allocations)
                self.assertLessEqual(counts[Kind.INDEX_PROBE], 64 * case.allocations)
                self.assertLessEqual(counts[Kind.RANGE_VISIT], case.allocations * case.managed_ranges)

    def test_owner_cursor_composition(self):
        for case in self.selected({"owned-allocate"}):
            counts = case.capture.snapshot.counts[Owner.KERNEL_MEMORY]
            with self.subTest(frames=case.frames):
                self.assertLessEqual(counts[Kind.BITMAP_WORD_LOAD], 2 * case.allocations)
                self.assertLessEqual(counts[Kind.INDEX_PROBE], 64 * case.allocations)
                self.assertLessEqual(counts[Kind.RANGE_VISIT], case.allocations * (192 + 2 * case.managed_ranges))
                self.assertLessEqual(counts[Kind.RECORD_VISIT], case.allocations * (96 + case.managed_ranges))


def run_oracles(measured):
    if type(measured) is not NativeRun or not measured.observed:
        raise ValueError("MEM01 work oracles require one complete actual observed execution")
    outcomes = []
    output = io.StringIO()
    for identifier, test, property_name, _, _, _ in ORACLE_RULES:
        case = BootstrapAllocationOracles(test.rsplit(".", 1)[1])
        case.measured = measured
        result = unittest.TextTestRunner(stream=output, verbosity=2).run(unittest.TestSuite((case,)))
        if result.testsRun != 1 or result.errors or result.skipped:
            raise ValueError("MEM01 oracle was skipped or failed outside its measured assertion")
        passed = result.wasSuccessful()
        if measured.stage is Stage.RED:
            if passed:
                raise ValueError("MEM01 baseline did not demonstrate every required work divergence")
            observed = ObservedResult.EXPECTED_FAILURE
        else:
            observed = ObservedResult.PASS if passed else ObservedResult.FAIL
        outcomes.append(OracleResult(identifier, RunStatus.RUN, observed, property_name))
    return tuple(outcomes), output.getvalue()


def native_log(measured, results, oracle_output, *, phase, attempt):
    value = asdict(measured)
    del value["records"]
    value["stage"] = measured.stage.value
    return json.loads(json.dumps({
        "version": 1, "phase": phase, "attempt": attempt, "native": value,
        "results": [asdict(result) for result in results], "oracle_output": oracle_output,
    }, allow_nan=False))


def validate_execution_log(context, replay, *, phase, attempt):
    from tests.host.diagnostic_history_authority import record_fields, record_strings

    record_fields(replay, ("version", "phase", "attempt", "native", "results", "oracle_output"))
    if type(replay["version"]) is not int or replay["version"] != 1 or replay["phase"] != phase or replay["attempt"] != attempt:
        raise ValueError("MEM01 replay belongs to a different phase or attempt")
    native = replay["native"]
    record_fields(native, (
        "stage", "observed", "compiler", "command", "execution_command", "source_inputs",
        "fixture_inputs", "generated_sources", "executable", "output", "build_output", "observation_symbols",
    ))
    stage = Stage.RED if phase == "red" else Stage.GREEN
    if (
        native["stage"] != stage.value or native["observed"] is not True
        or native["compiler"] != context.native_compiler
        or type(native["output"]) is not str or type(native["build_output"]) is not str
        or type(replay["oracle_output"]) is not str
    ):
        raise ValueError("MEM01 replay has a borrowed source stage, compiler or incomplete output")
    execution = record_strings(native["execution_command"])
    if len(execution) != 1:
        raise ValueError("MEM01 replay lacks its exact executed native image")
    executable = Path(execution[0])
    directory = executable.parent
    if (
        not executable.is_absolute() or executable.name != "bootstrap-allocation"
        or not directory.name.startswith("micros-mem01-native-")
        or directory.parent.resolve() != Path(tempfile.gettempdir()).resolve()
    ):
        raise ValueError("MEM01 replay names an unowned native execution")
    command = record_strings(native["command"])
    if command != native_command(context.root, context.native_compiler, directory / "source", executable, stage, True):
        raise ValueError("MEM01 replay changed its exact compile/link command")
    inputs, outputs = source_bundle(context.root, stage)
    generated = {
        name: {"kind": "file", "mode": mode, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        for name, (data, mode) in outputs.items()
    }
    if (
        native["source_inputs"] != inputs or native["generated_sources"] != generated
        or native["fixture_inputs"] != {name: context.files[name] for name in FIXTURE_PATHS}
    ):
        raise ValueError("MEM01 replay substituted a source/header/observation/fixture input")
    identity = native["executable"]
    record_fields(identity, ("kind", "mode", "size", "sha256"))
    if (
        identity["kind"] != "file" or type(identity["mode"]) is not int
        or not 0 < identity["mode"] <= 0o777 or not identity["mode"] & 0o100
        or type(identity["size"]) is not int or identity["size"] <= 0
        or type(identity["sha256"]) is not str or len(identity["sha256"]) != 64
        or any(character not in "0123456789abcdef" for character in identity["sha256"])
    ):
        raise ValueError("MEM01 replay has no complete actual executable identity")
    symbols = record_strings(native["observation_symbols"])
    if set(symbols) != {"micros_test_work_begin", "micros_test_work_finish", "micros_test_work_record"}:
        raise ValueError("MEM01 replay omitted its actual linked observation owners")
    measured = NativeRun(
        stage, True, decode_measurements(native["output"], stage, observed=True),
        context.native_compiler, command, execution, inputs, native["fixture_inputs"],
        generated, identity, native["output"], native["build_output"], symbols,
    )
    results, _ = run_oracles(measured)
    expected = json.loads(json.dumps([asdict(result) for result in results]))
    if replay["results"] != expected:
        raise ValueError("MEM01 replay result does not follow its actual complete measurements")
    return results
