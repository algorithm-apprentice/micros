"""MEM04 actual native and target semantic capture."""

from dataclasses import asdict
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

from tests.host.runtime_memory_evidence import native_compiler
from tests.host.selected_address_space_source import (
    CHANNELS, NATIVE_SOURCE_MEMBERS, OWNER_MASK, RECIPE_ADMISSION, SOURCE_ROLES,
    STATE_WORK_CONTRACT, extended_header, validate_retained_source,
)
from tests.host.evidence_authority_types import digest
from tests.host.work_count_evidence import Capture, Kind, Owner, Snapshot, Stage, Status, compose_snapshots
from tools.generate_selected_address_space_fixture import prepare_sources, source_bundle
from tools.stack_artifact_files import read_regular
from tools.validation_inputs import _file_identity


FIXTURE_PATHS = (
    "tests/host/selected_address_space_test.c",
    "tests/host/selected_address_space_test.h",
    "tests/host/selected_address_space_source.py",
    "tests/host/selected_address_space_evidence.py",
    "tests/host/selected_address_space_authority.py",
    "tests/host/test_selected_address_space.py",
    "tools/check_selected_address_space.py",
    "tools/generate_selected_address_space_fixture.py",
    "include/micros/work_observation.h",
    "tests/work/work_counts.h",
    "tests/work/work_counts.c",
)

CURSOR_FIELDS = frozenset((
    "version", "observed", "before", "after", "other_state_unchanged",
    "output_unchanged", "allocator_unchanged",
))
SCOPED_FIELDS = frozenset((
    "version", "phase", "diagnostic", "lookup", "physical", "mapped", "permissions",
    "absent", "absent_outputs_unchanged", "activation", "before_satp", "after_satp",
    "expected_satp", "restored_satp", "state_unchanged",
))
QUERY_FIELDS = frozenset((
    "case", "scenario", "operation", "phase", "frames", "ranges", "density", "address", "error",
    "result_matches", "permissions", "contiguous", "outputs_unchanged", "state_unchanged",
    "state_matches", "irq_unchanged", "root_unchanged", "root_matches",
    "allocation_limit", "active", "satp_writes",
))
PRODUCER_BASE = 0x4D454D3034000000


def parse_semantics(text):
    rows = []
    for line in text.splitlines():
        parts = line.split(" ")
        if not parts or parts[0] not in ("MEM04_CURSOR", "MEM04_SCOPED"):
            raise ValueError("MEM04 native capture contains an unknown record")
        values = {}
        for field in parts[1:]:
            match = re.fullmatch(r"([a-z_]+)=(0|[1-9][0-9]*)", field)
            if match is None or match[1] in values:
                raise ValueError("MEM04 native capture has an ambiguous field")
            values[match[1]] = int(match[2])
            if values[match[1]] >= 1 << 64:
                raise ValueError("MEM04 native capture value exceeds its width")
        expected = CURSOR_FIELDS if parts[0] == "MEM04_CURSOR" else SCOPED_FIELDS
        if set(values) != expected or values["version"] != 1:
            raise ValueError("MEM04 native capture omitted or changed its schema")
        if any(value not in (0, 1) for key, value in values.items() if key.endswith("_unchanged")):
            raise ValueError("MEM04 native capture has an invalid comparison result")
        rows.append((parts[0], values))
    if (
        len(rows) != 3 or rows[0][0] != "MEM04_CURSOR"
        or [row[0] for row in rows[1:]] != ["MEM04_SCOPED", "MEM04_SCOPED"]
        or [row[1]["phase"] for row in rows[1:]] != [1, 2]
    ):
        raise ValueError("MEM04 native semantic coverage/order is incomplete")
    return tuple(rows)


def semantic_results(rows):
    cursor = rows[0][1]
    scoped = tuple(row[1] for row in rows[1:])
    selected = (
        cursor["observed"] == 8 and cursor["before"] == cursor["after"] == 64
        and cursor["other_state_unchanged"] == cursor["output_unchanged"] == cursor["allocator_unchanged"] == 1
        and all(row["lookup"] == 0 and row["physical"] == row["mapped"] and row["permissions"] == 3
                and row["state_unchanged"] == 1 for row in scoped)
    )
    activation = all(
        row["activation"] == 0 and row["after_satp"] == row["expected_satp"]
        and row["restored_satp"] == row["before_satp"] and row["state_unchanged"] == 1
        for row in scoped
    )
    diagnostic = all(
        row["diagnostic"] == 12 and row["absent"] == 10
        and row["absent_outputs_unchanged"] == row["state_unchanged"] == 1
        for row in scoped
    )
    return {"selected": selected, "activation": activation, "scoped-diagnostic": diagnostic}


def expected_queries(stage=Stage.RED):
    cases = []
    for phase in (1, 2):
        for frames, ranges in ((512, 1), (4096, 1), (262144, 1), (512, 16)):
            for density in (1, 32):
                for operation, address in (
                    (0, 0x40000000), (1, 0x40000011), (1, 0x7FFFFFFF),
                    (0, 0x40200000), (2, 0x40000000), (3, 0x40000000),
                ):
                    cases.append({
                        "case": len(cases) + 1, "scenario": 0, "operation": operation, "phase": phase,
                        "frames": frames, "ranges": ranges, "density": density, "address": address,
                    })
    for phase in (1, 2):
        for depth, address in ((1, 0x40000000), (2, 0x40200000), (3, 0x40001000)):
            for operation, offset in ((0, 0), (1, 17)):
                cases.append({
                    "case": len(cases) + 1, "scenario": depth, "operation": operation, "phase": phase,
                    "frames": 512, "ranges": 1, "density": 0, "address": address + offset,
                })
    for phase in (1, 2):
        for depth in (0, 1, 2):
            for operation in (0, 2):
                cases.append({
                    "case": len(cases) + 1, "scenario": 10 + depth, "operation": operation,
                    "phase": phase, "frames": 512, "ranges": 1, "density": 0, "address": 0x40000000,
                })
    for scenario in range(20, 30):
        for operation, address in (
            (0, 0x40000000), (0, 0x7FFFE000), (1, 0x7FFFE011),
            (2, 0x40000000), (3, 0x40000000),
        ):
            cases.append({
                "case": len(cases) + 1, "scenario": scenario, "operation": operation,
                "phase": 1 if scenario == 20 else 2, "frames": 512, "ranges": 1,
                "density": 0, "address": address,
            })
    for scenario in range(40, 53):
        operations = (
            ((0, 0x7FFFE000), (1, 0x7FFFE011), (2, 0x7FFFE000), (3, 0x7FFFE000))
            if scenario == 52 else
            ((0, 0x40000000), (0, 0x7FFFE000), (1, 0x7FFFE011),
             (2, 0x40000000), (3, 0x40000000))
        )
        for operation, address in operations:
            cases.append({
                "case": len(cases) + 1, "scenario": scenario, "operation": operation,
                "phase": 1 if scenario == 40 else 2, "frames": 512, "ranges": 1,
                "density": 0, "address": address,
            })
    for shape, address in enumerate((0x40000000, 0x40200000, 0x40001000)):
        for limit in range(4 - shape):
            cases.append({
                "case": len(cases) + 1, "scenario": 70 + shape, "operation": 4,
                "phase": 1, "frames": 512, "ranges": 1, "density": 0,
                "address": address, "allocation_limit": limit,
            })
    for phase in (1, 2):
        for scenario in range(80, 112):
            for operation in (0, 2, 3):
                cases.append({
                    "case": len(cases) + 1, "scenario": scenario, "operation": operation,
                    "phase": phase, "frames": 512, "ranges": 1, "density": 0, "address": 0x40000000,
                })
    for scenario in range(130, 140):
        cases.append({
            "case": len(cases) + 1, "scenario": scenario, "operation": 2 if scenario == 139 else 5,
            "phase": 2 if scenario in (136, 137) else 1, "frames": 512, "ranges": 1,
            "density": 0, "address": 0x40000000, "active": int(scenario >= 138),
        })
    for scenario in range(140, 156):
        address = (
            0 if scenario in (144, 145) else 0x40000001 if scenario == 146
            else 0x40002000 if scenario == 153 else 0x40001000 if scenario >= 154 else 0x40000000
        )
        for operation in ((4,) if scenario >= 154 else (0, 1, 4, 2)):
            cases.append({
                "case": len(cases) + 1, "scenario": scenario, "operation": operation,
                "phase": 3 if scenario in (140, 142) else 2 if scenario == 155 else 1,
                "frames": 512, "ranges": 1, "density": 0, "address": address,
                "active": int(scenario in (149, 151, 152)),
            })
    for scenario, phase in ((30, 1), (31, 2), (53, 1), (54, 2), (32, 2)):
        for operation, address in ((0, 0x7FFFE000), (1, 0x7FFFE011), (2, 0x7FFFE000), (3, 0x7FFFE000)):
            cases.append({
                "case": len(cases) + 1, "scenario": scenario, "operation": operation,
                "phase": phase, "frames": 512, "ranges": 1, "density": 0, "address": address,
            })
    if stage is Stage.GREEN:
        for scenario in range(120, 126):
            cases.append({
                "case": len(cases) + 1, "scenario": scenario, "operation": 6,
                "phase": 1, "frames": 512, "ranges": 1, "density": 0, "address": 0x40000000,
            })
    for case in cases:
        case.setdefault("allocation_limit", (1 << 64) - 1)
        case.setdefault("active", 0)
    return tuple(cases)


def _json_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("MEM04 work record contains a duplicate field")
        result[key] = value
    return result


def parse_bindings(lines, stage):
    expected_cases = (
        tuple((0, index) for index in range(16)) + tuple((1, index) for index in range(12))
        + ((2, 0), (3, 0))
    )
    if stage is Stage.RED:
        expected_cases = ()
    if len(lines) != len(expected_cases):
        raise ValueError("MEM04 current binding-interface coverage is incomplete")
    rows = []
    for line, (controller, case) in zip(lines, expected_cases, strict=True):
        if not line.startswith("MEM04_BINDING "):
            raise ValueError("MEM04 binding observation is missing")
        matches = [re.fullmatch(r"([a-z_]+)=(0|[1-9][0-9]*)", item)
                   for item in line.removeprefix("MEM04_BINDING ").split(" ")]
        if any(match is None for match in matches):
            raise ValueError("MEM04 binding field is malformed")
        row = _json_pairs((match[1], int(match[2])) for match in matches)
        if set(row) != {
            "controller", "case", "primary", "runtime",
            "output_matches", "output_unchanged", "state_unchanged",
        } or row["controller"] != controller or row["case"] != case:
            raise ValueError("MEM04 binding inputs differ from the frozen case")
        if any(value >= 1 << 64 for value in row.values()) or any(
            row[key] not in (0, 1) for key in ("output_matches", "output_unchanged", "state_unchanged")
        ):
            raise ValueError("MEM04 binding result is outside its schema")
        rows.append(row)
    return tuple(rows)


def binding_failures(rows):
    failures = []
    for row in rows:
        controller, case = row["controller"], row["case"]
        if controller == 0:
            primary = 0 if case < 6 else 7
            runtime = 2 if case == 4 else 1 if case < 6 else 0
        elif controller == 1:
            primary = runtime = 0 if case < 3 else 1 if case < 6 else 2
        elif controller == 2:
            primary, runtime = 1, 0
        else:
            primary = runtime = 0
        expected = {
            "primary": primary, "runtime": runtime, "output_matches": 1,
            "output_unchanged": int(case >= 6 or controller == 2), "state_unchanged": 1,
        }
        for key, value in expected.items():
            if row[key] != value:
                failures.append((controller, case, key, value, row[key]))
    return tuple(failures)


def parse_native_output(text, stage, channel):
    lines = text.splitlines()
    semantics = parse_semantics("\n".join(lines[:3]))
    step = 1 if channel == "ordinary" else 2
    cases = expected_queries(stage)
    query_end = 3 + step * len(cases)
    if len(lines) != query_end + (30 if stage is Stage.GREEN else 0):
        raise ValueError("MEM04 native case/work coverage is incomplete")
    queries, observations = [], []
    for offset, expected in enumerate(cases):
        line = lines[3 + step * offset]
        if not line.startswith("MEM04_QUERY "):
            raise ValueError("MEM04 query record is missing")
        fields = line.removeprefix("MEM04_QUERY ").split(" ")
        pairs = [re.fullmatch(r"([a-z_]+)=(0|[1-9][0-9]*)", item) for item in fields]
        if any(match is None for match in pairs):
            raise ValueError("MEM04 query field is malformed")
        values = _json_pairs((match[1], int(match[2])) for match in pairs)
        if set(values) != QUERY_FIELDS or any(values[key] != value for key, value in expected.items()):
            raise ValueError(
                f"MEM04 query input/identity differs at case {expected['case']}: "
                f"expected {expected}; observed {values}"
            )
        if any(value >= 1 << 64 for value in values.values()) or any(
            value not in (0, 1) for key, value in values.items()
            if key.endswith(("_matches", "_unchanged"))
        ):
            raise ValueError("MEM04 query result has an invalid width or comparison")
        queries.append(values)
        if channel != "ordinary":
            line = lines[4 + step * offset]
            if not line.startswith("MEM04_WORK "):
                raise ValueError("MEM04 native work snapshot is missing")
            value = json.loads(line.removeprefix("MEM04_WORK "), object_pairs_hook=_json_pairs)
            if set(value) != {
                "version", "reserved0", "producer_label", "capture_label",
                "owner_mask", "reserved1", "counts",
            }:
                raise ValueError("MEM04 native work schema changed")
            value["counts"] = tuple(tuple(row) for row in value["counts"])
            snapshot = Snapshot(**value)
            producer = PRODUCER_BASE + (256 if stage is Stage.GREEN else 0) + CHANNELS.index(channel)
            compose_snapshots(
                (Capture(Status.OK, snapshot),), capture_label=expected["case"],
                expected_producers={producer: OWNER_MASK},
            )
            observations.append(value)
    return {
        "semantics": semantics, "queries": tuple(queries), "observations": tuple(observations),
        "bindings": parse_bindings(lines[query_end:], stage),
    }


def _baseline_query_error(row, desired):
    scenario = row["scenario"]
    if scenario in (10, 11, 12, 24, 25, 26, 45, 46, 47):
        return 11
    if scenario in (27, 28, 29, 48, 49, 50, 51):
        return 14
    if 80 <= scenario <= 111:
        if scenario == 110:
            return 0
        if scenario == 102:
            return 14
        return 12 if 80 <= scenario <= 93 or scenario in (101, 111) else 11
    if scenario == 134:
        return 9
    if scenario in (151, 152, 153):
        return 12
    return desired


def query_failures(queries, *, baseline=False):
    failures = []
    for row in queries:
        operation = row["operation"]
        scenario = row["scenario"]
        missing = row["address"] == 0x40200000 or scenario in (1, 2, 3)
        poison = scenario in (10, 11, 12) and (scenario != 12 or operation != 2)
        error = 11 if poison else 10 if missing else 0
        if 20 <= scenario <= 29:
            if scenario in (27, 29) or operation == 3 and scenario == 28:
                error = 14
            elif operation in (2, 3):
                error = 11 if scenario in (24, 25, 26) else 0
            elif row["address"] >= 0x7FFFE000:
                error = 10 if scenario in (20, 21, 22) else 11
            else:
                error = 0
        if 40 <= scenario <= 52:
            if scenario in (48, 49, 51) or operation == 3 and scenario == 50:
                error = 14
            elif operation in (2, 3):
                error = 11 if scenario in (45, 46, 47) else 0
            elif scenario == 52:
                error = 0
            elif row["address"] >= 0x7FFFE000:
                error = 10 if scenario in (40, 41) else 11
            else:
                error = 0
        if scenario in (70, 71, 72):
            error = 0 if row["allocation_limit"] == 73 - scenario else 8
        if 80 <= scenario <= 111:
            diagnostic = 12 if 80 <= scenario <= 93 or scenario in (101, 111) else 11
            diagnostic = 14 if scenario == 102 else 0 if scenario == 110 else diagnostic
            if operation == 3:
                error = diagnostic
            elif operation == 2:
                error = (
                    12 if scenario in (80, 83) else 14 if scenario in (102, 110)
                    else 11 if scenario in (94, 103, 104, 106, 108) else 0
                )
            else:
                error = 0 if scenario in (100, 101, 102, 110) else diagnostic
        if 130 <= scenario <= 139:
            error = 9 if scenario in (131, 132, 133, 135) else 0
        if 140 <= scenario <= 155:
            error = {
                140: 3 if operation == 2 else 1,
                141: 2, 142: 3, 143: 4, 144: 6 if operation == 4 else 5,
                145: 0 if operation == 2 else 6,
                146: 6 if operation in (0, 4) else 0,
                147: 13, 148: 7 if operation == 4 else 5,
                149: 5 if operation == 4 else 0,
                150: 9 if operation == 4 else 0,
                151: 0 if operation == 2 else 12,
                152: 5 if operation == 4 else 0,
                153: 10 if operation in (0, 1) else 0,
                154: 8, 155: 3,
            }[scenario]
        if 120 <= scenario <= 125:
            error = {120: 0, 121: 1, 122: 14, 123: 4, 124: 13, 125: 5}[scenario]
        if baseline:
            error = _baseline_query_error(row, error)
        read = operation in (0, 1) and error == 0
        activated = operation == 2 and error == 0
        allocated = operation == 4 and error == 0
        prepared = operation in (5, 6) and error == 0
        expected = {
            "error": error,
            "result_matches": int(read or allocated or prepared or operation in (2, 3)),
            "permissions": 3 if read else (1 << 32) - 1,
            "contiguous": 4096 - row["address"] % 4096 if operation == 1 and read else (1 << 64) - 1,
            "outputs_unchanged": int(not read and not allocated and not prepared),
            "state_unchanged": int(not allocated and not (operation == 5 and prepared)),
            "state_matches": 1, "irq_unchanged": 1,
            "root_unchanged": int(not activated or row["active"]),
            "root_matches": int(activated or row["active"]), "satp_writes": int(activated),
        }
        if baseline and scenario in (70, 71, 72) and 0 < row["allocation_limit"] < 73 - scenario:
            expected["state_unchanged"] = expected["state_matches"] = 0
        for key, value in expected.items():
            if row[key] != value:
                failures.append((row["case"], key, value, row[key]))
    return tuple(failures)


def native_command(root, compiler, generated, executable, stage, channel):
    observed = channel != "ordinary"
    return (
        compiler["path"], "-std=c17", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
        "-Wmissing-prototypes", "-Wstrict-prototypes", "-fno-builtin",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
        "-DMICROS_BUILD_USER_ADDRESS_SPACE_TEST=1",
        *(("-DMICROS_SELECTED_ADDRESS_SPACE_BASELINE=1",) if stage is Stage.RED else ()),
        *(("-DMICROS_TEST_WORK_COUNTS=1", "-DMICROS_AS_NATIVE_CHANNEL=" + str(CHANNELS.index(channel)))
          if observed else ()),
        "-I", str(generated), "-I", str(generated / "include"),
        "-I", str(root), "-I", str(root / "include"),
        *(str(generated / path) for path in NATIVE_SOURCE_MEMBERS),
        str(root / "tests/host/selected_address_space_test.c"),
        *((str(root / "tests/work/work_counts.c"),) if observed else ()),
        "-o", str(executable),
    )


def native_symbols_tool():
    selected = shutil.which("nm")
    if selected is None:
        raise ValueError("MEM04 ordinary observation absence requires the existing native nm")
    if sys.platform == "darwin":
        selected = subprocess.check_output(("/usr/bin/xcrun", "--find", "nm"), text=True, timeout=30).strip()
    selected = Path(selected).resolve(strict=True)
    return {"path": str(selected), "identity": _file_identity(selected)}


def measure_semantics(root, stage, channel="ordinary"):
    if stage not in (Stage.RED, Stage.GREEN) or channel not in CHANNELS:
        raise ValueError("MEM04 native execution has an unknown stage")
    root = Path(root).resolve()
    compiler = native_compiler(root)
    nm = native_symbols_tool()
    fixtures = {path: read_regular(root / path)[1] for path in FIXTURE_PATHS}
    with tempfile.TemporaryDirectory(prefix="micros-mem04-semantic-") as directory:
        directory = Path(directory)
        generated, executable = directory / "source", directory / "semantic"
        inputs, outputs = prepare_sources(root, generated, stage, channel)
        command = native_command(root, compiler, generated, executable, stage, channel)
        build = subprocess.run(command, cwd=root, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=180)
        if build.returncode or build.stderr:
            raise ValueError("MEM04 native compilation failed:\n" + build.stdout + build.stderr)
        nm_command = (nm["path"], "-a", str(executable))
        symbols = subprocess.run(nm_command, cwd=root, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30)
        if symbols.returncode or symbols.stderr:
            raise ValueError("MEM04 native symbol observation failed:\n" + symbols.stdout + symbols.stderr)
        if channel == "ordinary" and re.search(r"\b_?micros_test_work_", symbols.stdout):
            raise ValueError("MEM04 ordinary product contains observation support")
        execution_command = (str(executable), stage.value, channel)
        execution = subprocess.run(
            execution_command, cwd=root, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=60,
        )
        if execution.returncode or execution.stderr:
            raise ValueError("MEM04 native capture failed:\n" + execution.stdout + execution.stderr)
        rows = parse_native_output(execution.stdout, stage, channel)
        current_inputs, _ = source_bundle(root, stage, channel)
        if (
            {path: read_regular(generated / path)[1] for path in outputs} != outputs
            or {path: read_regular(root / path)[1] for path in FIXTURE_PATHS} != fixtures
            or native_compiler(root) != compiler or native_symbols_tool() != nm or current_inputs != inputs
        ):
            raise ValueError("MEM04 native source/compiler/fixture changed during capture")
        return {
            "version": 1, "stage": stage.value, "channel": channel, "compiler": compiler, "command": command,
            "compile_exit_code": build.returncode, "build_output": build.stdout,
            "execution_command": execution_command,
            "exit_code": execution.returncode, "output": execution.stdout,
            "source_inputs": inputs, "generated_sources": outputs, "fixture_inputs": fixtures,
            "executable": read_regular(executable)[1],
            "native_nm": nm, "nm_command": nm_command, "nm_exit_code": symbols.returncode,
            "nm_output": symbols.stdout,
            "semantic_results": semantic_results(rows["semantics"]),
            "query_failures": query_failures(rows["queries"]),
            "binding_failures": binding_failures(rows["bindings"]), "rows": rows,
        }


def encoded_capture(capture):
    return json.dumps(capture, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()


def check_current_fixture_syntax(root):
    root = Path(root).resolve()
    compiler = native_compiler(root)
    fixtures = {path: read_regular(root / path)[1] for path in FIXTURE_PATHS}
    with tempfile.TemporaryDirectory(prefix="micros-mem04-fixture-syntax-") as directory:
        generated = Path(directory)
        prepare_sources(root, generated, Stage.RED)
        for path in SOURCE_ROLES["production_paths"]:
            if not path.endswith(".h"):
                continue
            target = generated / path
            text = extended_header(path, target.read_text())
            validate_retained_source(root, path, text, Stage.GREEN)
            target.write_text(text)
        command = native_command(root, compiler, generated, generated / "unused", Stage.GREEN, "ordinary")
        prefix = command[:command.index(str(generated / NATIVE_SOURCE_MEMBERS[0]))]
        command = (
            *prefix, "-DMICROS_SELECTED_ADDRESS_SPACE_LAUNCHER=1",
            '-DMICROS_SELECTED_ADDRESS_SPACE_PYTHON="' + sys.executable + '"',
            '-DMICROS_SELECTED_ADDRESS_SPACE_DRIVER="' + str(root / RECIPE_ADMISSION["driver"]) + '"',
            '-DMICROS_SELECTED_ADDRESS_SPACE_ROOT="' + str(root) + '"',
            "-fsyntax-only", str(root / "tests/host/selected_address_space_test.c"),
        )
        result = subprocess.run(
            command, cwd=root, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30,
        )
        if result.returncode or result.stdout or result.stderr:
            raise ValueError("MEM04 current fixture does not compile against its declared interfaces:\n" + result.stdout + result.stderr)
        if {path: read_regular(root / path)[1] for path in FIXTURE_PATHS} != fixtures or native_compiler(root) != compiler:
            raise ValueError("MEM04 fixture syntax inputs changed during compilation")
        return command


def _normalized_semantics(rows):
    result = []
    for name, value in rows:
        row = dict(value)
        if name == "MEM04_SCOPED":
            before = row["before_satp"]
            if row["lookup"] == 0:
                row["physical"] -= row["mapped"]
            row["mapped"] = (row["mapped"] >> 12) - (before & ((1 << 44) - 1))
            for key in ("after_satp", "expected_satp", "restored_satp"):
                row[key] -= before
            row["before_satp"] = before >> 44
        result.append((name, row))
    return tuple(result)


def _validate_baseline_semantics(rows):
    cursor = rows[0][1]
    if cursor != {
        "version": 1, "observed": 8, "before": 64, "after": 128,
        "other_state_unchanged": 1, "output_unchanged": 1, "allocator_unchanged": 0,
    }:
        raise ValueError("MEM04 original execution did not reproduce the exact cursor regression")
    for _, row in _normalized_semantics(rows)[1:]:
        if any(row[key] != value for key, value in {
            "diagnostic": 12, "lookup": 12, "physical": 0xFEEDFACEFEEDFACE, "mapped": 4,
            "permissions": (1 << 32) - 1, "absent": 12, "absent_outputs_unchanged": 1,
            "activation": 12, "before_satp": 8 << 16, "after_satp": 0,
            "expected_satp": 1, "restored_satp": 0, "state_unchanged": 1,
        }.items()):
            raise ValueError("MEM04 original execution lost a scoped-result/state control")


def validate_measurements(measured):
    if type(measured) is not dict or set(measured) != set(CHANNELS):
        raise ValueError("MEM04 native execution omitted or added a product")
    stages = {native["stage"] for native in measured.values()}
    if len(stages) != 1 or next(iter(stages)) not in ("red", "green"):
        raise ValueError("MEM04 native products mix source stages")
    stage = Stage(next(iter(stages)))
    parsed = {}
    for channel, native in measured.items():
        if (
            native["channel"] != channel or type(native["compile_exit_code"]) is not int
            or native["compile_exit_code"] != 0 or type(native["exit_code"]) is not int or native["exit_code"] != 0
            or type(native["nm_exit_code"]) is not int or native["nm_exit_code"] != 0
            or type(native["nm_output"]) is not str or not native["nm_output"]
        ):
            raise ValueError("MEM04 native capture is not one complete actual successful execution")
        rows = parse_native_output(native["output"], stage, channel)
        if (
            digest(rows) != digest(native["rows"])
            or native["semantic_results"] != semantic_results(rows["semantics"])
            or digest(native["query_failures"]) != digest(query_failures(rows["queries"]))
            or digest(native["binding_failures"]) != digest(binding_failures(rows["bindings"]))
        ):
            raise ValueError("MEM04 native derived results differ from the actual output")
        if channel == "ordinary" and re.search(r"\b_?micros_test_work_", native["nm_output"]):
            raise ValueError("MEM04 ordinary product contains observation support")
        if stage is Stage.RED:
            _validate_baseline_semantics(rows["semantics"])
            failures = query_failures(rows["queries"], baseline=True)
            if failures:
                raise ValueError("MEM04 baseline lost an independent semantic control: " + repr(failures[:8]))
        parsed[channel] = rows
    _validate_composition(parsed)
    return stage


def _validate_composition(parsed):
    ordinary = parsed["ordinary"]
    for channel in CHANNELS[:-1]:
        rows = parsed[channel]
        if (
            rows["queries"] != ordinary["queries"] or rows["bindings"] != ordinary["bindings"]
            or _normalized_semantics(rows["semantics"]) != _normalized_semantics(ordinary["semantics"])
        ):
            raise ValueError("MEM04 observations changed actual operation results/state")
    for index, row in enumerate(ordinary["queries"]):
        captures, expected = [], {}
        for channel in ("path", "context"):
            value = parsed[channel]["observations"][index]
            snapshot = Snapshot(**value)
            captures.append(Capture(Status.OK, snapshot))
            expected[snapshot.producer_label] = OWNER_MASK
        composed = compose_snapshots(tuple(captures), capture_label=row["case"], expected_producers=expected)
        aggregate = parsed["aggregate"]["observations"][index]["counts"]
        if composed.per_owner != aggregate:
            raise ValueError("MEM04 path/context channels do not exactly compose their aggregate")


def _slot_count(row):
    scenario = row["scenario"]
    return int(
        scenario in (*range(20, 32), *range(40, 52), 53, 54, 136, 137)
    )


def work_failures(measured):
    validate_measurements(measured)
    return _work_failures({channel: value["rows"] for channel, value in measured.items()})


def _work_failures(measured):
    failures = {"selected": [], "root": []}
    pairs = {}
    for index, row in enumerate(measured["ordinary"]["queries"]):
        operation = row["operation"]
        if operation == 3:
            continue
        category = "root" if operation in (2, 5, 6) else "selected"
        path = measured["path"]["observations"][index]["counts"]
        context = measured["context"]["observations"][index]["counts"]
        aggregate = measured["aggregate"]["observations"][index]["counts"]
        root = operation in (2, 5, 6)
        slots = _slot_count(row)
        proofs = 3 + 4 * slots if root else 32 if operation == 4 else 4
        g, k = row["ranges"], STATE_WORK_CONTRACT["owner_kinds"]

        def bound(label, observed, maximum):
            if observed > maximum:
                failures[category].append((row["case"], label, observed, maximum))

        bound("complete-audits", sum(owner[Kind.COMPLETE_VALIDATOR_CALL] for owner in aggregate), 0)
        bound("scratch-stores", context[Owner.KERNEL_MEMORY][Kind.CLEAR_UNIT], 0)
        bound("pte-reads", path[Owner.KERNEL_MEMORY][Kind.PTE_VISIT], 1024 + 3 * slots if root else 3)
        bound("selected-owner-reads", path[Owner.KERNEL_MEMORY][Kind.RECORD_VISIT], proofs)
        bound("VM-binding-inspections", context[Owner.KERNEL_VM][Kind.RECORD_VISIT], 1)
        bound("TTY-binding-inspections", context[Owner.KERNEL_TTY][Kind.RECORD_VISIT], 1)
        bound("VM-header-work", context[Owner.KERNEL_VM][Kind.INDEX_PROBE], STATE_WORK_CONTRACT["vm_header_work"])
        bound("TTY-header-work", context[Owner.KERNEL_TTY][Kind.INDEX_PROBE], STATE_WORK_CONTRACT["tty_header_work"])
        bound("active-geometry", context[Owner.KERNEL_MEMORY][Kind.RANGE_VISIT], (proofs + 1) * (8 * g + 2 * k))
        allowance = 512 + 3 * slots if root else 3
        if operation == 4:
            allowance += 6 * 512 + row["frames"] // 64 + 16
        bound("memory-index-work", context[Owner.KERNEL_MEMORY][Kind.INDEX_PROBE],
              (proofs + 1) * (64 * g + 32 * k) + allowance)
        bound("other-capacity-work", sum(
            context[owner][Kind.INDEX_PROBE] for owner in
            (Owner.KERNEL_OBJECTS, Owner.KERNEL_SCHEDULER, Owner.KERNEL_ENDPOINT, Owner.KERNEL_BOOT)
        ), 0)
        bound("PTE-writes", path[Owner.KERNEL_MEMORY][Kind.COPY_UNIT], 3 if operation == 4 else 0)
        if row["scenario"] in (1, 2, 3):
            actual = path[Owner.KERNEL_MEMORY][Kind.PTE_VISIT]
            if actual != row["scenario"]:
                failures[category].append((row["case"], "exact-absence-prefix", actual, row["scenario"]))
        if row["scenario"] == 0 and operation in (0, 1) and row["address"] != 0x40200000:
            actual = path[Owner.KERNEL_MEMORY][Kind.PTE_VISIT]
            if actual != 3:
                failures[category].append((row["case"], "exact-present-path", actual, 3))
        if row["scenario"] == 0 and row["ranges"] == 1:
            key = row["phase"], operation, row["address"]
            if key in pairs and pairs[key] != aggregate:
                failures[category].append((row["case"], "independent-frame-and-leaf-scaling", 1, 0))
            pairs.setdefault(key, aggregate)
    return {key: tuple(value) for key, value in failures.items()}


def measure_native(root, stage):
    measured = {}
    for channel in CHANNELS:
        print("MICROS_MEM04_NATIVE_BEGIN " + channel, flush=True)
        measured[channel] = measure_semantics(root, stage, channel)
        print("MICROS_MEM04_NATIVE_CAPTURED " + channel, flush=True)
    validate_measurements(measured)
    return measured


TARGET_FIELDS = {
    "MEM04_TABLE": frozenset((
        "case", "lookup", "activation", "before_satp", "after_satp",
        "poison_unchanged", "state_unchanged", "outputs_unchanged",
    )),
    "MEM04_ACTIVATION": frozenset((
        "case", "diagnostic", "activation", "release", "destroy", "before_satp",
        "after_satp", "process_satp", "restored_satp", "state_unchanged", "output_unchanged",
    )),
    "MEM04_HANDOFF": frozenset((
        "diagnostic", "translated", "before_satp", "after_satp",
        "poison_unchanged", "state_unchanged", "outputs_unchanged",
    )),
}


def parse_target_output(output, workflow, stage, exit_code):
    if workflow not in STATE_WORK_CONTRACT["target_workflows"] or stage not in (Stage.RED, Stage.GREEN):
        raise ValueError("MEM04 target capture has an unknown workflow or stage")
    rows = []
    for line in output.splitlines():
        if not line.startswith("# MEM04_"):
            continue
        match = re.fullmatch(r"# (MEM04_TABLE|MEM04_ACTIVATION|MEM04_HANDOFF) version=1 (.*)", line)
        if match is None:
            raise ValueError("MEM04 target emitted an unknown semantic witness")
        pairs = [re.fullmatch(r"([a-z_]+)=(0x[0-9a-f]{16})", field) for field in match[2].split(" ")]
        if any(pair is None for pair in pairs):
            raise ValueError("MEM04 target witness has a malformed field")
        value = _json_pairs((pair[1], int(pair[2], 16)) for pair in pairs)
        if set(value) != TARGET_FIELDS[match[1]] or any(
            item not in (0, 1) for key, item in value.items() if key.endswith("_unchanged")
        ):
            raise ValueError("MEM04 target witness changed its schema")
        rows.append((match[1], value))
    user = workflow == "test-qemu-user-address-space"
    expected = (
        (("MEM04_TABLE", 0), ("MEM04_TABLE", 1))
        + tuple(("MEM04_ACTIVATION", index) for index in range(1 if stage is Stage.RED else 5))
        if user else (("MEM04_HANDOFF", None),)
    )
    if tuple((name, value.get("case")) for name, value in rows) != expected:
        raise ValueError("MEM04 target did not execute its exact semantic witness coverage")
    reason = "user-address-space-test" if user else "address-space-handoff-test"
    panics = re.findall(r"^# MICROS_PANIC reason=(.*)$", output, re.M)
    if stage is Stage.RED:
        if type(exit_code) is not int or exit_code not in (1, 2) or panics != [reason]:
            raise ValueError("MEM04 target Red lacks its witnessed component failure")
    else:
        marker = "MICROS_USER_ADDRESS_SPACE_TEST_PASS" if user else "MICROS_ADDRESS_SPACE_HANDOFF_TEST_PASS"
        if exit_code != 0 or panics or len(re.findall(r"^# " + marker + r" .+$", output, re.M)) != 1:
            raise ValueError("MEM04 current target lacks complete original acceptance")
        if not user and len(re.findall(r"^# MICROS_GRANT_SYSCALL_HANDOFF_PASS .+$", output, re.M)) != 1:
            raise ValueError("MEM04 handed-off target omitted its original grant boundary")
    return tuple(rows)


def target_failures(rows, *, baseline=False):
    failures = []
    for name, row in rows:
        expected = {key: 1 for key in row if key.endswith("_unchanged")}
        before = row["before_satp"]
        if before >> 44 != 8 << 16:
            failures.append((name, row.get("case"), "kernel-satp-mode", before, 8 << 60))
        if name == "MEM04_TABLE":
            expected.update(lookup=11, activation=11, after_satp=before)
        elif name == "MEM04_ACTIVATION":
            diagnostic = 12 if row["case"] == 2 else 11
            expected.update(
                diagnostic=diagnostic, release=diagnostic, destroy=diagnostic,
                activation=diagnostic if baseline else 0,
                after_satp=before if baseline else row["process_satp"], restored_satp=before,
            )
            if row["process_satp"] == before or row["process_satp"] >> 44 != before >> 44:
                failures.append((name, row["case"], "distinct-selected-root", row["process_satp"], before))
        else:
            expected.update(diagnostic=12, translated=12 if baseline else 10, after_satp=before)
        for key, value in expected.items():
            if row[key] != value:
                failures.append((name, row.get("case"), key, value, row[key]))
    return tuple(failures)


def _target_paths(workflow):
    if workflow not in STATE_WORK_CONTRACT["target_workflows"]:
        raise ValueError("MEM04 target path belongs to an undeclared workflow")
    name = workflow.removeprefix("test-qemu-")
    return "build/riscv64-" + name + "-test/kernel/micros.elf"


def _target_inputs(root):
    paths = (*SOURCE_ROLES["production_paths"], *(
        path for path in SOURCE_ROLES["fixed_adapters"] if path.startswith("kernel/tests/")
    ))
    return {path: read_regular(Path(root) / path)[1] for path in paths}


def measure_targets(root, stage):
    root = Path(root).resolve()
    result = {}
    for workflow in STATE_WORK_CONTRACT["target_workflows"]:
        inputs = _target_inputs(root)
        command = ("cmake", "--workflow", "--preset", workflow)
        print("MICROS_MEM04_TARGET_BEGIN " + workflow, flush=True)
        completed = subprocess.run(
            command, cwd=root, stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=600,
        )
        try:
            rows = parse_target_output(completed.stdout, workflow, stage, completed.returncode)
            if stage is Stage.RED and target_failures(rows, baseline=True):
                raise ValueError("MEM04 baseline target lost an independent semantic/state control")
            kernel = _target_paths(workflow)
            result[workflow] = {
                "version": 1, "stage": stage.value, "command": command, "exit_code": completed.returncode,
                "output": completed.stdout, "source_inputs": inputs, "kernel_path": kernel,
                "kernel": read_regular(root / kernel)[1],
                "rows": rows, "failures": target_failures(rows),
            }
            if _target_inputs(root) != inputs:
                raise ValueError("MEM04 target inputs changed during actual execution")
        except (OSError, ValueError):
            sys.stderr.write(completed.stdout)
            raise
        print("MICROS_MEM04_TARGET_CAPTURED " + workflow, flush=True)
    return result


def validate_targets(targets, stage):
    if type(targets) is not dict or set(targets) != set(STATE_WORK_CONTRACT["target_workflows"]):
        raise ValueError("MEM04 target execution omitted or added a workflow")
    for workflow, value in targets.items():
        if (
            value["version"] != 1 or value["stage"] != stage.value
            or tuple(value["command"]) != ("cmake", "--workflow", "--preset", workflow)
            or value["kernel_path"] != _target_paths(workflow)
        ):
            raise ValueError("MEM04 target evidence substituted its source stage or command")
        rows = parse_target_output(value["output"], workflow, stage, value["exit_code"])
        if digest(value["rows"]) != digest(rows) or digest(value["failures"]) != digest(target_failures(rows)):
            raise ValueError("MEM04 target derived results differ from the actual output")
        if stage is Stage.RED and target_failures(rows, baseline=True):
            raise ValueError("MEM04 target failure is not its declared original divergence")
    return targets


class SelectedAddressSpaceOracles(unittest.TestCase):
    def test_selected_path(self):
        self.assertTrue(self.semantics["selected"], "selected result/cursor compensation contract failed")
        self.assertFalse(self.work["selected"], repr(self.work["selected"][:10]))
        self.assertFalse(self.selected_semantics, repr(self.selected_semantics[:10]))
        self.assertFalse(self.bindings, repr(self.bindings[:10]))
        self.assertFalse(self.audits["migrated"])

    def test_root_activation(self):
        self.assertTrue(self.semantics["activation"], "bounded root activation contract failed")
        self.assertFalse(self.work["root"], repr(self.work["root"][:10]))
        self.assertFalse(self.root_semantics, repr(self.root_semantics[:10]))
        self.assertFalse(self.targets["test-qemu-user-address-space"]["failures"])
        self.assertFalse(self.bindings, repr(self.bindings[:10]))

    def test_scoped_diagnostic(self):
        self.assertTrue(self.semantics["scoped-diagnostic"], "scoped absence did not remain separate from diagnosis")
        self.assertFalse(self.targets["test-qemu-address-space-handoff"]["failures"])
        self.assertFalse(self.diagnostic_semantics, repr(self.diagnostic_semantics[:10]))
        self.assertFalse(self.bindings, repr(self.bindings[:10]))


def run_oracles(root, measured, targets, audits, reference=None):
    from tests.host.evidence_authority_types import ObservedResult, OracleResult, RunStatus
    from tests.host.selected_address_space_authority import ORACLE_RULES
    from tests.host.selected_address_space_source import declared_audit_boundaries, measure_audit_boundaries

    stage = validate_measurements(measured)
    validate_targets(targets, stage)
    if stage is Stage.GREEN:
        if reference is None or validate_measurements(reference) is not Stage.RED:
            raise ValueError("MEM04 current execution lacks its same-attempt actual baseline")
    elif reference is not None:
        raise ValueError("MEM04 baseline execution cannot borrow another reference")
    expected = declared_audit_boundaries(root)
    if digest(audits) != digest(measure_audit_boundaries(root, stage)) or digest(audits["retained"]) != digest(expected["retained"]):
        raise ValueError("MEM04 changed a retained diagnostic caller or substituted its source census")
    work = _work_failures({channel: value["rows"] for channel, value in measured.items()})
    if stage is Stage.RED and (not work["selected"] or not work["root"]):
        raise ValueError("MEM04 original work measurements did not discriminate both selected and root costs")
    rows = measured["ordinary"]["rows"]
    failures = query_failures(rows["queries"])
    outcomes, output = [], io.StringIO()
    for identifier, test, property_name, _, _, _ in ORACLE_RULES:
        case = SelectedAddressSpaceOracles(test.rsplit(".", 1)[1])
        case.semantics, case.work, case.targets, case.audits = semantic_results(rows["semantics"]), work, targets, audits
        case.bindings = binding_failures(rows["bindings"])
        case.selected_semantics = tuple(row for row in failures if rows["queries"][row[0] - 1]["operation"] in (0, 1, 4))
        case.root_semantics = tuple(row for row in failures if rows["queries"][row[0] - 1]["operation"] in (2, 5, 6))
        case.diagnostic_semantics = tuple(row for row in failures if rows["queries"][row[0] - 1]["operation"] == 3)
        result = unittest.TextTestRunner(stream=output, verbosity=2).run(unittest.TestSuite((case,)))
        if result.testsRun != 1 or result.errors or result.skipped:
            raise ValueError("MEM04 oracle was skipped or failed outside its measured assertion")
        if stage is Stage.RED:
            if result.wasSuccessful():
                raise ValueError("MEM04 original execution did not expose every declared divergence")
            observed = ObservedResult.EXPECTED_FAILURE
        else:
            observed = ObservedResult.PASS if result.wasSuccessful() else ObservedResult.FAIL
        outcomes.append(OracleResult(identifier, RunStatus.RUN, observed, property_name))
    return tuple(outcomes), output.getvalue()


def execution_log(measured, targets, audits, results, oracle_output, *, phase, attempt):
    return json.loads(json.dumps({
        "version": 1, "phase": phase, "attempt": attempt, "native": measured,
        "targets": targets, "audits": audits, "results": [asdict(result) for result in results],
        "oracle_output": oracle_output,
    }, allow_nan=False))


def _validate_artifact_identity(identity):
    from tests.host.diagnostic_history_authority import record_fields

    record_fields(identity, ("kind", "mode", "size", "sha256"))
    if (
        identity["kind"] != "file" or type(identity["mode"]) is not int
        or not 0 < identity["mode"] <= 0o777
        or type(identity["size"]) is not int or identity["size"] <= 0
        or type(identity["sha256"]) is not str or re.fullmatch(r"[a-f0-9]{64}", identity["sha256"]) is None
    ):
        raise ValueError("MEM04 execution lacks a complete ordinary artifact identity")


def _validate_native_replay(context, measured, stage):
    from tests.host.diagnostic_history_authority import record_fields, record_strings

    if type(measured) is not dict or set(measured) != set(CHANNELS):
        raise ValueError("MEM04 native replay omitted a product")
    directories = set()
    for channel, native in measured.items():
        record_fields(native, (
            "version", "stage", "channel", "compiler", "command", "compile_exit_code",
            "build_output", "execution_command", "exit_code", "output", "source_inputs",
            "generated_sources", "fixture_inputs", "executable", "native_nm", "nm_command",
            "nm_exit_code", "nm_output", "semantic_results", "query_failures", "binding_failures", "rows",
        ))
        if (
            native["version"] != 1 or native["stage"] != stage.value or native["channel"] != channel
            or native["compiler"] != context.native_compiler or native["native_nm"] != context.native_nm
        ):
            raise ValueError("MEM04 native replay borrowed another source stage or tool")
        execution = record_strings(native["execution_command"])
        if len(execution) != 3 or execution[1:] != (stage.value, channel):
            raise ValueError("MEM04 native replay changed its execution stage/channel")
        executable = Path(execution[0])
        directory = executable.parent
        if (
            not executable.is_absolute() or executable.name != "semantic"
            or not directory.name.startswith("micros-mem04-semantic-") or directory in directories
            or directory.parent.resolve() != Path(tempfile.gettempdir()).resolve()
        ):
            raise ValueError("MEM04 native replay names a borrowed or unowned execution")
        directories.add(directory)
        if record_strings(native["command"]) != native_command(
            context.root, context.native_compiler, directory / "source", executable, stage, channel,
        ) or record_strings(native["nm_command"]) != (context.native_nm["path"], "-a", str(executable)):
            raise ValueError("MEM04 native replay changed its exact compile/link/symbol command")
        inputs, outputs = source_bundle(context.root, stage, channel)
        generated = {
            path: {"kind": "file", "mode": mode, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
            for path, (data, mode) in outputs.items()
        }
        if (
            native["source_inputs"] != inputs or native["generated_sources"] != generated
            or native["fixture_inputs"] != {path: context.files[path] for path in FIXTURE_PATHS}
            or type(native["build_output"]) is not str
        ):
            raise ValueError("MEM04 native replay substituted a source/header/observation/fixture input")
        _validate_artifact_identity(native["executable"])
        if not native["executable"]["mode"] & 0o100:
            raise ValueError("MEM04 native replay artifact is not executable")


def _validate_target_replay(context, targets, stage):
    from tests.host.diagnostic_history_authority import record_fields
    from tests.host.selected_address_space_source import fixed_source

    validate_targets(targets, stage)
    expected = {path: context.files[path] for path in _target_inputs(context.root)}
    if stage is Stage.RED:
        for path in SOURCE_ROLES["production_paths"]:
            raw = fixed_source(str(context.root), path)
            expected[path] = {
                "kind": "file", "mode": context.files[path]["mode"], "size": len(raw),
                "sha256": hashlib.sha256(raw).hexdigest(),
            }
    for value in targets.values():
        record_fields(value, (
            "version", "stage", "command", "exit_code", "output",
            "source_inputs", "kernel_path", "kernel", "rows", "failures",
        ))
        if value["source_inputs"] != expected:
            raise ValueError("MEM04 target replay substituted its actual production/fixture source")
        _validate_artifact_identity(value["kernel"])
        if stage is Stage.GREEN and read_regular(context.root / value["kernel_path"])[1] != value["kernel"]:
            raise ValueError("MEM04 current target artifact changed after its actual execution")


def validate_execution_log(context, replay, *, phase, attempt, reference=None):
    from tests.host.diagnostic_history_authority import record_fields
    from tests.host.selected_address_space_source import measure_audit_boundaries

    if phase not in ("red", "green", "recertify") or type(attempt) is not str or re.fullmatch(r"[a-f0-9]{32}", attempt) is None:
        raise ValueError("MEM04 replay has an unadmitted phase or attempt")
    record_fields(replay, ("version", "phase", "attempt", "native", "targets", "audits", "results", "oracle_output"))
    if (
        type(replay["version"]) is not int or replay["version"] != 1
        or replay["phase"] != phase or replay["attempt"] != attempt
        or type(replay["oracle_output"]) is not str or not replay["oracle_output"]
    ):
        raise ValueError("MEM04 replay belongs to a different phase or attempt")
    stage = Stage.RED if phase == "red" else Stage.GREEN
    _validate_native_replay(context, replay["native"], stage)
    _validate_target_replay(context, replay["targets"], stage)
    if stage is Stage.GREEN:
        if reference is None:
            raise ValueError("MEM04 current replay lacks its same-attempt original execution")
        _validate_native_replay(context, reference, Stage.RED)
    elif reference is not None:
        raise ValueError("MEM04 original replay cannot borrow a different reference")
    audits = measure_audit_boundaries(context.root, stage)
    if digest(replay["audits"]) != digest(audits):
        raise ValueError("MEM04 replay substituted its complete current caller ledger")
    results, _ = run_oracles(context.root, replay["native"], replay["targets"], audits, reference)
    if replay["results"] != json.loads(json.dumps([asdict(result) for result in results])):
        raise ValueError("MEM04 replay outcomes do not follow the actual complete executions")
    return results


def check_native_products(root, executable_paths):
    from tests.host.selected_address_space_source import declared_audit_boundaries, measure_audit_boundaries

    root = Path(root).resolve()
    expected = tuple(root / "build/host-debug" / name for name in RECIPE_ADMISSION["native_targets"])
    if tuple(map(Path, executable_paths)) != expected:
        raise ValueError("MEM04 CTest requires its four exact declared build products")
    identities = {str(path): read_regular(path)[1] for path in expected}
    fixtures = {path: read_regular(root / path)[1] for path in FIXTURE_PATHS}
    dependencies = {}
    for path in SOURCE_ROLES["dependent_production_paths"]:
        data, identity = read_regular(root / path)
        validate_retained_source(root, path, data.decode(), Stage.GREEN)
        dependencies[path] = identity
    nm = native_symbols_tool()
    measured, generated, inputs = {}, {}, {}
    for channel, executable in zip(CHANNELS, expected, strict=True):
        source_inputs, outputs = source_bundle(root, Stage.GREEN, channel)
        directory = root / RECIPE_ADMISSION["generator_output"] / channel
        emitted = {
            path: {"kind": "file", "mode": mode, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
            for path, (data, mode) in outputs.items()
        }
        if {path: read_regular(directory / path)[1] for path in emitted} != emitted:
            raise ValueError("MEM04 CTest generated inputs differ from actual current source")
        generated[channel], inputs[channel] = emitted, source_inputs
        symbols = subprocess.check_output((nm["path"], "-a", str(executable)), cwd=root, text=True, timeout=30)
        observed_symbols = tuple(sorted({
            line.split()[-1].lstrip("_") for line in symbols.splitlines()
            if line.split() and "micros_test_work_" in line.split()[-1]
        }))
        wanted = () if channel == "ordinary" else ("micros_test_work_begin", "micros_test_work_finish", "micros_test_work_record")
        if observed_symbols != wanted:
            raise ValueError("MEM04 CTest product has the wrong collector linkage")
        completed = subprocess.run(
            (str(executable), "green", channel), cwd=root, stdin=subprocess.DEVNULL,
            capture_output=True, text=True, timeout=60,
        )
        if completed.returncode or completed.stderr:
            raise ValueError("MEM04 CTest product failed its semantic capture:\n" + completed.stdout + completed.stderr)
        measured[channel] = parse_native_output(completed.stdout, Stage.GREEN, channel)
    _validate_composition(measured)
    failures = _work_failures(measured)
    ordinary = measured["ordinary"]
    audits = measure_audit_boundaries(root, Stage.GREEN)
    if (
        any(failures.values()) or not all(semantic_results(ordinary["semantics"]).values())
        or query_failures(ordinary["queries"]) or binding_failures(ordinary["bindings"])
        or audits["migrated"] or audits["retained"] != declared_audit_boundaries(root)["retained"]
    ):
        raise ValueError("MEM04 CTest failed selected semantics, work or diagnostic boundaries: " + json.dumps(failures))
    if (
        {path: read_regular(root / path)[1] for path in FIXTURE_PATHS} != fixtures or native_symbols_tool() != nm
        or {path: read_regular(root / path)[1] for path in dependencies} != dependencies
        or any(read_regular(path)[1] != identities[str(path)] for path in expected)
    ):
        raise ValueError("MEM04 CTest inputs/products changed during execution")
    for channel in CHANNELS:
        directory = root / RECIPE_ADMISSION["generator_output"] / channel
        current, _ = source_bundle(root, Stage.GREEN, channel)
        if current != inputs[channel] or {
            path: read_regular(directory / path)[1] for path in generated[channel]
        } != generated[channel]:
            raise ValueError("MEM04 CTest source or generated inputs changed during execution")
    return 0
