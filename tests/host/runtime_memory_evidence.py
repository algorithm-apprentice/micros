"""Real prefixed memory execution and independently defined work properties."""

from collections import deque
from dataclasses import asdict, dataclass, replace
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest

from tests.host.runtime_memory_authority import (
    MEMORY_PATH, ORACLE_DECLARATIONS, baseline_memory, record_fields, record_strings,
    red_translation_unit, validate_green_source,
)
from tests.host.evidence_authority_types import ObservedResult, OracleResult, RunStatus
from tests.host.work_count_snapshots import compose_snapshots
from tests.host.work_count_types import (
    Capture, Kind, Owner, Snapshot, Stage, Status,
)
from tools.stack_artifact_files import read_regular
from tools.stack_artifact_build import ninja_path
from tools.stack_bound_flow import function_flow
from tools.stack_bound_collect import image_map_text
from tools.stack_bound_link import text_owners
from tools.stack_bound_machine import CALLER_SAVED, is_call, signed
from tools.stack_bound_objects import function_symbols, load_object
from tools.stack_bound_pointer import arithmetic_result, memory_action
from tools.stack_bound_relocations import verify_linked_object
from tools.user_elf_parse import load_elf
from tools.user_elf_instructions import _instruction_destination
from tools.validation_context import compiler_version, target_compiler, validate_environment
from tools.validation_inputs import _file_identity, _run


@dataclass(frozen=True)
class Probe:
    operation: str
    length: int
    destination_offset: int
    source_offset: int
    value: int
    capture: Capture | None


@dataclass(frozen=True)
class NativeRun:
    stage: Stage
    records: tuple
    source_sha256: str
    compiler: dict
    command: tuple
    output: str
    translation_unit: dict
    executable: dict
    execution_command: tuple
    build_output: str


def native_compiler(root):
    environment = dict(os.environ)
    validate_environment(environment, target=False)
    cache = Path(root) / "build/host-debug/CMakeCache.txt"
    if not cache.is_file():
        raise ValueError("COPY01 native evidence requires cmake --preset host-debug")
    values = [
        line.partition("=")[2] for line in read_regular(cache)[0].decode().splitlines()
        if re.match(r"^CMAKE_C_COMPILER:(?:FILEPATH|STRING)=", line)
    ]
    if len(values) != 1 or not Path(values[0]).is_absolute():
        raise ValueError("COPY01 lacks the selected absolute native compiler")
    selected = Path(values[0])
    resolved = selected.resolve(strict=True)
    identity = _file_identity(resolved)
    version = _run((str(selected), "--version")).decode()
    compiler_version(version, target=not version.startswith("Apple clang version"))
    return {"path": str(selected), "resolved": str(resolved), "identity": identity, "version": version}


def json_pairs(items):
    result = {}
    for key, value in items:
        if key in result:
            raise ValueError("duplicate runtime memory evidence field")
        result[key] = value
    return result


def case_inventory():
    lengths = (*range(18), 23, 24, 31, 32, 63, 64, 65, 255, 256, 257, 4095, 4096)
    result = []
    for length in lengths:
        result.extend(("copy", length, target, source, 421)
                      for target in range(8) for source in range(8))
        result.extend(("fill", length, target, 0, byte)
                      for target in range(4) for byte in (421, 0, -1, 256, -128))
    result.extend((
        ("copy", 364704, 0, 0, 421), ("copy", 364704, 3, 3, 421),
        ("fill", 364704, 0, 0, 421),
    ))
    return tuple(result)


def decode_measurements(output, *, observed):
    result = []
    expected = case_inventory()
    lines = output.splitlines()
    if len(lines) != len(expected):
        raise ValueError("runtime memory fixture has incomplete case coverage")
    for index, (line, case) in enumerate(zip(lines, expected), 1):
        value = json.loads(line, object_pairs_hook=json_pairs)
        if type(value) is not dict or set(value) != {
            "operation", "length", "destination_offset", "source_offset", "value", "semantic", "capture",
        }:
            raise ValueError("invalid runtime memory fixture record")
        actual = tuple(value[name] for name in (
            "operation", "length", "destination_offset", "source_offset", "value",
        ))
        if (
            actual != case or value["semantic"] is not True
            or any(type(value[name]) is not int for name in (
                "length", "destination_offset", "source_offset", "value",
            ))
        ):
            raise ValueError("runtime memory fixture failed semantics or borrowed a case")
        capture = None
        if observed:
            data = value["capture"]
            if type(data) is not dict or set(data) != {"status", "snapshot"} or type(data["status"]) is not int:
                raise ValueError("invalid runtime memory capture")
            snapshot = data["snapshot"]
            if type(snapshot) is not dict or set(snapshot) != {
                "version", "reserved0", "producer_label", "capture_label", "owner_mask", "reserved1", "counts",
            }:
                raise ValueError("incomplete runtime memory snapshot")
            capture = Capture(Status(data["status"]), Snapshot(
                **{**snapshot, "counts": tuple(tuple(row) for row in snapshot["counts"])},
            ))
            compose_snapshots((capture,), capture_label=index, expected_producers={1: 1 << Owner.RUNTIME})
            units, volume = (
                (Kind.COPY_UNIT, Kind.COPIED_BYTE) if value["operation"] == "copy"
                else (Kind.CLEAR_UNIT, Kind.CLEARED_BYTE)
            )
            row = capture.snapshot.counts[Owner.RUNTIME]
            if (
                row[volume] != value["length"] or row[units] > value["length"]
                or bool(row[units]) != bool(value["length"])
                or any(count for kind, count in enumerate(row) if kind not in (units, volume))
            ):
                raise ValueError("runtime memory capture has missing volume or unowned work")
        elif value["capture"] is not None:
            raise ValueError("disabled runtime memory fixture fabricated observations")
        result.append(Probe(*case, capture))
    return tuple(result)


def native_command(root, compiler, unit, executable, observed):
    return (
        compiler["path"], "-std=c17", "-Wall", "-Wextra", "-Werror", "-Wshadow",
        "-Wconversion", "-Wstrict-prototypes", "-Wmissing-prototypes",
        "-fno-builtin", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
        "-I", str(root), "-I", str(root / "include"),
        *(("-DMICROS_TEST_WORK_COUNTS=1",) if observed else ()),
        str(unit), str(root / "tests/host/runtime_memory_fixture.c"),
        *((str(root / "tests/work/work_counts.c"),) if observed else ()),
        "-o", str(executable),
    )


def execute_fixture(root, stage, observed, *, access=False):
    if stage not in (Stage.RED, Stage.GREEN) or type(observed) is not bool:
        raise ValueError("unknown runtime memory evidence stage/mode")
    root = Path(root).resolve()
    original = baseline_memory(root) if stage is Stage.RED else read_regular(root / MEMORY_PATH)[0]
    source = red_translation_unit(root) if stage is Stage.RED else original.decode("utf-8")
    if stage is Stage.GREEN:
        validate_green_source(source)
    compiler = native_compiler(root)
    input_paths = (
        "lib/runtime/memory.h", "include/micros/work_observation.h",
        "tests/work/work_counts.h", "tests/work/work_counts.c",
        "tests/host/runtime_memory_fixture.c",
    )
    inputs = {path: read_regular(root / path)[1] for path in input_paths}
    with tempfile.TemporaryDirectory(prefix="micros-runtime-memory-") as temporary:
        directory = Path(temporary)
        translation_unit, executable = directory / "memory.c", directory / "fixture"
        translation_unit.write_text(source)
        command = native_command(root, compiler, translation_unit, executable, observed)
        built = subprocess.run(command, cwd=root, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=60)
        if built.returncode:
            raise ValueError("runtime memory fixture did not build:\n" + built.stdout + built.stderr)
        unit_identity = read_regular(translation_unit)[1]
        executable_identity = read_regular(executable)[1]
        execution_command = (str(executable), *(("--access",) if access else ()))
        completed = subprocess.run(
            execution_command, cwd=root, stdin=subprocess.DEVNULL,
            capture_output=True, text=True, timeout=60,
        )
        if completed.returncode or completed.stderr:
            raise ValueError("runtime memory fixture failed:\n" + completed.stdout + completed.stderr)
        if read_regular(executable)[1] != executable_identity:
            raise ValueError("runtime memory executable changed during execution")
        if read_regular(translation_unit)[1] != unit_identity:
            raise ValueError("runtime memory translation unit changed during execution")
    if native_compiler(root) != compiler or (
        stage is Stage.GREEN and read_regular(root / MEMORY_PATH)[0] != original
    ) or inputs != {path: read_regular(root / path)[1] for path in input_paths}:
        raise ValueError("runtime memory source/compiler changed during measurement")
    return NativeRun(
        stage, (), hashlib.sha256(original).hexdigest(), compiler, command, completed.stdout,
        unit_identity, executable_identity, execution_command, built.stdout + built.stderr,
    )


def measure_native(root, stage, *, observed=True):
    measured = execute_fixture(root, stage, observed)
    return replace(measured, records=decode_measurements(measured.output, observed=observed))


def measure_access(root, stage):
    measured = execute_fixture(root, stage, False, access=True)
    value = json.loads(measured.output, object_pairs_hook=json_pairs)
    if (
        type(value) is not dict or set(value) != {"null_zero", "typed", "guarded", "address_orders"}
        or any(result is not True for result in value.values())
    ):
        raise ValueError("runtime memory access controls did not completely pass")
    return measured


def probe(measured, operation, length, destination_offset, source_offset, *, value=421):
    selected = tuple(row for row in measured.records if (
        row.operation, row.length, row.destination_offset, row.source_offset, row.value,
    ) == (operation, length, destination_offset, source_offset, value))
    if len(selected) != 1:
        raise ValueError("runtime memory case is missing or duplicated")
    return selected[0]


def assert_aligned_work(measured, operation):
    if operation not in ("copy", "fill"):
        raise ValueError("unknown runtime memory work oracle")
    row = probe(measured, operation, 64, 0, 0)
    if row.capture is None:
        raise ValueError("runtime memory oracle requires actual observations")
    units, volume, expected = (
        (Kind.COPY_UNIT, Kind.COPIED_BYTE, 8) if operation == "copy"
        else (Kind.CLEAR_UNIT, Kind.CLEARED_BYTE, 16)
    )
    values = row.capture.snapshot.counts[Owner.RUNTIME]
    if values[volume] != 64:
        raise ValueError("runtime memory oracle lacks its complete byte volume")
    if values[units] != expected:
        raise AssertionError(
            f"{operation} aligned word work: expected {expected} units, observed {values[units]}"
        )


def assert_geometry_work(measured):
    for row in measured.records:
        if row.capture is None:
            raise ValueError("runtime memory geometry requires actual observations")
        width, kind = (8, Kind.COPY_UNIT) if row.operation == "copy" else (4, Kind.CLEAR_UNIT)
        words = max(0, (row.destination_offset + row.length) // width - (
            row.destination_offset + width - 1
        ) // width)
        if (
            row.operation == "copy" and row.destination_offset % 8 != row.source_offset % 8
            or row.operation == "fill" and row.length < 12
        ):
            words = 0
        expected = row.length - (width - 1) * words
        actual = row.capture.snapshot.counts[Owner.RUNTIME][kind]
        if actual != expected:
            raise AssertionError(
                f"{row.operation} complete aligned intervals: length={row.length} "
                f"destination={row.destination_offset} source={row.source_offset} "
                f"expected={expected} observed={actual}"
            )


def run_oracles(measured):
    outcomes = []
    output = io.StringIO()
    for (identifier, test_id, property_name), operation in zip(ORACLE_DECLARATIONS, ("copy", "fill")):
        suite = unittest.defaultTestLoader.loadTestsFromName(test_id)
        if suite.countTestCases() != 1:
            raise ValueError("COPY01 replacement oracle has ambiguous test ownership")
        case, = suite
        case.supplied_measurement = measured
        result = unittest.TextTestRunner(stream=output, verbosity=2).run(suite)
        expected = 8 if operation == "copy" else 16
        discriminating = (
            measured.stage is Stage.RED and result.testsRun == 1
            and len(result.failures) == 1 and not result.errors
            and f"{operation} aligned word work: expected {expected} units, observed 64" in result.failures[0][1]
        )
        observed = (
            ObservedResult.EXPECTED_FAILURE if discriminating
            else ObservedResult.PASS if result.wasSuccessful() and result.testsRun == 1
            else ObservedResult.FAIL
        )
        status = RunStatus.SKIPPED if result.skipped else RunStatus.RUN
        outcomes.append(OracleResult(identifier, status, observed, property_name))
    expected_result = ObservedResult.EXPECTED_FAILURE if measured.stage is Stage.RED else ObservedResult.PASS
    if any(row.status is not RunStatus.RUN or row.observed is not expected_result for row in outcomes):
        raise ValueError("COPY01 actual replacement execution was not the required result:\n" + output.getvalue())
    return tuple(outcomes), output.getvalue()


def native_log(measured, results, oracle_output):
    return {
        "version": 1, "stage": measured.stage.value, "source_sha256": measured.source_sha256,
        "compiler": measured.compiler, "command": list(measured.command), "output": measured.output,
        "translation_unit": measured.translation_unit, "executable": measured.executable,
        "execution_command": list(measured.execution_command), "build_output": measured.build_output,
        "build_exit": 0, "execution_exit": 0,
        "oracle_results": [asdict(row) for row in results], "oracle_output": oracle_output,
    }


def validate_native_log(root, value, stage, compiler):
    record_fields(value, (
        "version", "stage", "source_sha256", "compiler", "command", "output", "translation_unit",
        "executable", "execution_command", "build_output", "build_exit", "execution_exit",
        "oracle_results", "oracle_output",
    ))
    if (
        type(value["version"]) is not int or value["version"] != 1 or value["stage"] != stage.value
        or value["compiler"] != compiler
        or any(type(value[name]) is not int or value[name] != 0 for name in ("build_exit", "execution_exit"))
        or any(type(value[name]) is not str for name in ("build_output", "output", "oracle_output"))
        or any(test not in value["oracle_output"] for _, test, _ in ORACLE_DECLARATIONS)
    ):
        raise ValueError("COPY01 native log has an unbound execution")
    root = Path(root)
    original = baseline_memory(root) if stage is Stage.RED else read_regular(root / MEMORY_PATH)[0]
    source = red_translation_unit(root).encode() if stage is Stage.RED else original
    if stage is Stage.GREEN:
        validate_green_source(source.decode())
    if value["source_sha256"] != hashlib.sha256(original).hexdigest():
        raise ValueError("COPY01 native log executed a different source stage")
    for name in ("translation_unit", "executable"):
        identity = value[name]
        record_fields(identity, ("kind", "mode", "size", "sha256"))
        if (
            identity["kind"] != "file" or type(identity["mode"]) is not int
            or not 0 <= identity["mode"] <= 0o777 or type(identity["size"]) is not int
            or identity["size"] <= 0 or type(identity["sha256"]) is not str
            or re.fullmatch(r"[a-f0-9]{64}", identity["sha256"]) is None
        ):
            raise ValueError("COPY01 native log has an incomplete compiled artifact")
    if (
        value["translation_unit"]["size"] != len(source)
        or value["translation_unit"]["sha256"] != hashlib.sha256(source).hexdigest()
    ):
        raise ValueError("COPY01 native translation unit differs from its admitted stage")
    execution = record_strings(value["execution_command"])
    executable = Path(execution[0])
    if (
        len(execution) != 1 or not executable.is_absolute() or executable.name != "fixture"
        or not executable.parent.name.startswith("micros-runtime-memory-")
        or record_strings(value["command"]) != native_command(
            root, compiler, executable.parent / "memory.c", executable, True,
        )
    ):
        raise ValueError("COPY01 native log has a foreign compile/execution command")
    measured = NativeRun(
        stage, decode_measurements(value["output"], observed=True), value["source_sha256"],
        compiler, tuple(value["command"]), value["output"], value["translation_unit"],
        value["executable"], execution, value["build_output"],
    )
    if stage is Stage.GREEN:
        assert_geometry_work(measured)
    expected = []
    for (identifier, _, property_name), operation in zip(ORACLE_DECLARATIONS, ("copy", "fill")):
        if stage is Stage.RED:
            row = probe(measured, operation, 64, 0, 0)
            kind = Kind.COPY_UNIT if operation == "copy" else Kind.CLEAR_UNIT
            if row.capture.snapshot.counts[Owner.RUNTIME][kind] != 64:
                raise ValueError("COPY01 recorded Red is not the measured byte-loop divergence")
            observed = ObservedResult.EXPECTED_FAILURE
        else:
            assert_aligned_work(measured, operation)
            observed = ObservedResult.PASS
        expected.append(OracleResult(identifier, RunStatus.RUN, observed, property_name))
    if value["oracle_results"] != json.loads(json.dumps([asdict(row) for row in expected])):
        raise ValueError("COPY01 recorded oracle outcomes contradict their actual captures")
    return tuple(expected)


def memory_arithmetic(width, word, registers):
    if width == 4 and word & 0x707f == 0x13 and word >> 20 == 0:
        return registers[(word >> 15) & 31]
    if width == 2 and word & 0xf003 == 0x8002 and (word >> 2) & 31:
        return registers[(word >> 2) & 31]
    exact = tuple(
        value if value is not None and (
            value[0] == "stack" or value[0] == "scalar" and value[1] is not None
        ) else None for value in registers
    )
    fixed = arithmetic_result(width, word, exact)
    if fixed is not None:
        return fixed
    operation = None
    values = ()
    if width == 4:
        opcode = word & 0x7f
        if opcode in (0x13, 0x1b):
            operation = "add" if (word >> 12) & 7 == 0 else "scalar"
            values = (registers[(word >> 15) & 31], ("scalar", signed(word >> 20, 12)))
        elif opcode in (0x33, 0x3b):
            operation = "add" if word & 0xfe00707f in (0x33, 0x3b) else "scalar"
            values = (registers[(word >> 15) & 31], registers[(word >> 20) & 31])
    else:
        quadrant, opcode = word & 3, word >> 13
        if quadrant == 1 and opcode in (0, 1):
            operation = "add"
            values = (registers[(word >> 7) & 31], ("scalar", None))
        elif quadrant == 2 and opcode == 4 and (word >> 2) & 31:
            operation = "add"
            values = (registers[(word >> 7) & 31], registers[(word >> 2) & 31])
        elif quadrant == 1 and opcode == 4:
            operation = "scalar"
            values = (registers[8 + ((word >> 7) & 7)], ("scalar", None))
        elif quadrant == 2 and opcode == 0:
            operation = "scalar"
            values = (registers[(word >> 7) & 31], ("scalar", None))
    if not values or any(value is None for value in values):
        return None
    if operation == "add":
        pointers = [value for value in values if value[0] == "pointer"]
        if len(pointers) == 1 and all(value[0] in ("pointer", "scalar") for value in values):
            return pointers[0]
    return ("scalar", None) if all(value[0] != "stack" for value in values) else None


def memory_transfer(state, width, word):
    registers, stored = state
    changed, memory = list(registers), dict(stored)
    action = memory_action(width, word)
    destination = _instruction_destination(word, width)
    if action is None:
        if destination not in (None, 0):
            changed[destination] = memory_arithmetic(width, word, registers)
    else:
        write, size, register, basis, offset = action
        base = registers[basis]
        if write and base is not None and base[0] == "stack":
            address = base[1] + offset
            memory = {
                location: value for location, value in memory.items()
                if location + value[0] <= address or address + size <= location
            }
            if registers[register] is not None:
                memory[address] = size, registers[register]
        elif not write and register:
            value = None
            if base is not None and base[0] == "stack":
                previous = memory.get(base[1] + offset)
                if previous is not None and previous[0] >= size and (
                    size == 8 or previous[1][0] == "scalar"
                ):
                    value = previous[1]
            elif base is not None and base[0] == "pointer":
                value = ("data", base[1], size)
            changed[register] = value
    if is_call(width, word):
        for register in CALLER_SAVED:
            changed[register] = None
    changed[0] = ("scalar", 0)
    return tuple(changed), tuple(sorted(memory.items()))


def memory_merge(previous, current):
    def merge(left, right):
        if left == right:
            return left
        if left is not None and right is not None and left[0] == right[0] == "scalar":
            return ("scalar", None)
        return None

    if previous[0][2] != current[0][2]:
        raise ValueError("runtime memory artifact has inconsistent stack paths")
    old, new = dict(previous[1]), dict(current[1])
    memory = []
    for address, (size, value) in old.items():
        if address in new and size == new[address][0]:
            merged = merge(value, new[address][1])
            if merged is not None:
                memory.append((address, (size, merged)))
    return (
        tuple(merge(left, right) for left, right in zip(previous[0], current[0])),
        tuple(sorted(memory)),
    )


def wide_data_transfers(obj, symbol, operation, *, observed=False):
    flow = function_flow(obj, symbol)
    allowed = {"__cyg_profile_func_enter", "__cyg_profile_func_exit"} if observed else set()
    if any(call.target_symbol not in allowed for call in flow.calls):
        raise ValueError("runtime memory artifact has a recursive or external memory helper")
    registers = [None] * 32
    registers[0], registers[2] = ("scalar", 0), ("stack", 0)
    registers[1] = ("entry-return-address",)
    registers[10] = ("pointer", 0)
    registers[11] = ("pointer", 1) if operation == "copy" else ("scalar", None)
    registers[12] = ("scalar", None)
    states = {0: (tuple(registers), ())}
    pending = deque((0,))
    while pending:
        address = pending.popleft()
        width, word = flow.instructions[address]
        current = memory_transfer(states[address], width, word)
        for successor in flow.successors[address]:
            previous = states.get(successor)
            merged = current if previous is None else memory_merge(previous, current)
            if previous != merged:
                states[successor] = merged
                pending.append(successor)
    matches = []
    for address, (registers, _) in states.items():
        action = memory_action(*flow.instructions[address])
        if action is None:
            continue
        write, size, register, basis, _ = action
        if write and registers[basis] == ("pointer", 0) and (
            operation == "copy" and size == 8 and registers[register] == ("data", 1, 8)
            or operation == "fill" and size == 4
        ):
            matches.append(address)
    if not matches:
        raise ValueError(f"{operation}: no proved wide data transfer (stack spills do not qualify)")
    return len(matches)


def validate_primitive_ir(content, name, size):
    bodies = re.findall(
        r"(?ms)^define[^\n]*@" + re.escape(name) + r"\([^\n]*\)[^\n]*\{\n(.*?)^\}", content,
    )
    if len(bodies) != 1:
        raise ValueError("runtime memory IR lacks its unique defined function")
    calls = re.findall(r"@llvm\.memcpy\.inline\.p0\.p0\.i64\(([^\n]*)\)", bodies[0])
    if len(calls) != 1:
        raise ValueError("runtime memory IR lacks its constant inline transfer")
    arguments = [argument.strip() for argument in calls[0].split(",")]
    if (
        len(arguments) != 4 or arguments[2:] != [f"i64 {size}", "i1 false"]
        or any(re.search(r"\bptr\b.*\balign " + str(size) + r"\b", argument) is None for argument in arguments[:2])
    ):
        raise ValueError("runtime memory IR lost its exact constant width/alignment")


def target_transfer_proofs(root, stage, *, fixture=False):
    if stage not in (Stage.RED, Stage.GREEN) or type(fixture) is not bool:
        raise ValueError("unknown runtime memory artifact scope")
    root = Path(root).resolve()
    validate_environment(dict(os.environ), target=True)
    compiler = target_compiler(dict(os.environ))
    identity = _file_identity(compiler.resolve())
    source = (
        read_regular(root / "tests/host/runtime_memory_fixture.c")[0] if fixture
        else baseline_memory(root) if stage is Stage.RED else read_regular(root / MEMORY_PATH)[0]
    )
    if stage is Stage.GREEN and not fixture:
        validate_green_source(source.decode("utf-8"))
    prefix = "memory_artifact_" if fixture else "micros_runtime_mem"
    names = {"copy": prefix + ("copy" if fixture else "cpy"), "fill": prefix + ("fill" if fixture else "set")}
    results = {}
    with tempfile.TemporaryDirectory(prefix="micros-memory-artifact-") as temporary:
        directory = Path(temporary)
        unit = directory / "memory.c"
        unit.write_bytes(source)
        for mode, optimize in (("debug", "-O0"), ("size", "-Os")):
            proofs = {}
            for variant, flags in (
                ("ordinary", ()),
                ("metadata", ("-fstack-usage", "-fstack-size-section")),
                ("observed", ("-fstack-usage", "-fstack-size-section", "-finstrument-functions-after-inlining")),
            ):
                output = directory / f"{mode}-{variant}.o"
                command = (
                    str(compiler), "--target=riscv64-unknown-elf", "-std=c17",
                    "-march=rv64imac_zicsr_zifencei", "-mabi=lp64", "-mcmodel=medany",
                    "-msmall-data-limit=0", "-mno-save-restore", "-ffreestanding", "-fno-builtin",
                    "-fno-pic", "-fno-pie", "-fno-stack-protector", "-fno-unwind-tables",
                    "-fno-asynchronous-unwind-tables", "-ffunction-sections", "-fdata-sections",
                    "-Wall", "-Wextra", "-Werror", "-Wmissing-prototypes", optimize, *flags,
                    *(("-DMICROS_RUNTIME_MEMORY_ARTIFACT_FIXTURE=1",) if fixture else ()),
                    "-I", str(root), "-I", str(root / "include"), "-c", str(unit), "-o", str(output),
                )
                completed = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=60)
                if completed.returncode:
                    raise ValueError("runtime memory artifact build failed:\n" + completed.stdout + completed.stderr)
                obj = load_object(output)
                symbols = function_symbols(obj)
                if any(symbol.name.startswith("micros_test_work_") for symbol in obj.symbols) or any(
                    section.flags & 3 == 3 and section.size for section in obj.sections
                ):
                    raise ValueError("ordinary runtime memory artifact retained an observer or writable state")
                proofs[variant] = {
                    operation: wide_data_transfers(obj, symbols[name], operation, observed=variant == "observed")
                    for operation, name in names.items()
                }
                ir = directory / f"{mode}-{variant}.ll"
                ir_command = (
                    *command[:-4], "-S", "-emit-llvm", "-Xclang", "-disable-llvm-passes",
                    str(unit), "-o", str(ir),
                )
                completed = subprocess.run(ir_command, cwd=root, capture_output=True, text=True, timeout=60)
                if completed.returncode:
                    raise ValueError("runtime memory frontend IR build failed:\n" + completed.stdout + completed.stderr)
                for operation, name in names.items():
                    validate_primitive_ir(ir.read_text(), name, 8 if operation == "copy" else 4)
            results[mode] = {**proofs["ordinary"], "variants": proofs}
    current_source = (
        read_regular(root / "tests/host/runtime_memory_fixture.c")[0] if fixture
        else baseline_memory(root) if stage is Stage.RED else read_regular(root / MEMORY_PATH)[0]
    )
    if _file_identity(compiler.resolve()) != identity or current_source != source:
        raise ValueError("runtime memory artifact compiler/source changed")
    return results


def runtime_compile_commands(directory):
    rows = json.loads(_run((
        ninja_path(directory), "-C", directory, "-t", "compdb",
    )), object_pairs_hook=json_pairs)
    if type(rows) is not list or not rows:
        raise ValueError("COPY01 runtime has no actual Ninja command graph")
    for row in rows:
        record_fields(row, ("directory", "command", "file", "output"))
        if any(type(value) is not str for value in row.values()) or (
            not row["output"] or Path(row["directory"]).resolve() != Path(directory).resolve()
        ):
            raise ValueError("COPY01 runtime command graph is incomplete or foreign")
    return rows


def prove_linked_runtime(root):
    root = Path(root).resolve()
    directory = root / "build/riscv64-user-runtime-test"
    relative_image = "user-runtime/micros-user-runtime-service.elf"
    image_data, image_identity = read_regular(directory / relative_image)
    image = load_elf(directory / relative_image)
    map_text = image_map_text(directory, relative_image)
    owners = {name for name, _ in text_owners(map_text)}
    commands = runtime_compile_commands(directory)
    objects = []
    for command in commands:
        source = Path(command["file"])
        if not source.is_absolute():
            source = directory / source
        output = Path(command["output"])
        if not output.is_absolute():
            output = directory / output
        if source == root / MEMORY_PATH and str(output.relative_to(directory)) in owners:
            argv = tuple(shlex.split(command["command"]))
            if (
                not argv or argv[0] != str(target_compiler(dict(os.environ)))
                or argv.count("-c") != 1 or argv.count("-o") != 1
                or max(argv.index("-c"), argv.index("-o")) + 1 >= len(argv)
                or any(argument.startswith("@") for argument in argv)
            ):
                raise ValueError("COPY01 runtime object has no complete actual compiler command")
            operands = []
            for option in ("-c", "-o"):
                operand = Path(argv[argv.index(option) + 1])
                operands.append(operand if operand.is_absolute() else directory / operand)
            if tuple(operands) != (source, output):
                raise ValueError("COPY01 runtime compiler operands disagree with their linked owner")
            objects.append(output)
    if len(objects) != 1:
        raise ValueError("COPY01 runtime image lacks one exact production memory object")
    object_data, object_identity = read_regular(objects[0])
    obj = load_object(objects[0])
    if obj.data != object_data or image.data != image_data:
        raise ValueError("COPY01 runtime image/object changed before byte proof")
    retained = verify_linked_object(obj, str(objects[0].relative_to(directory)), image, map_text)
    symbols = function_symbols(obj)
    for operation, name in (("copy", "micros_runtime_memcpy"), ("fill", "micros_runtime_memset")):
        if symbols[name].section_index not in retained:
            raise ValueError("COPY01 word primitive is absent from the actual runtime image")
        wide_data_transfers(obj, symbols[name], operation)
    if any(symbol.name.startswith("micros_test_work_") for symbol in image.symbols):
        raise ValueError("COPY01 ordinary runtime image contains work observers")
    if (
        read_regular(directory / relative_image)[1] != image_identity
        or read_regular(objects[0])[1] != object_identity
        or image_map_text(directory, relative_image) != map_text
        or runtime_compile_commands(directory) != commands
    ):
        raise ValueError("COPY01 runtime image/object/recipe changed during byte proof")
    return {
        "image": image_identity, "object": object_identity,
        "map_sha256": hashlib.sha256(map_text.encode()).hexdigest(),
    }
