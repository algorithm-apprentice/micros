"""Actual MEM03 native execution, work classification and strict replay."""

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
from tests.host.selected_ownership_source import (
    RECIPE_ADMISSION, SOURCE_PROJECTION, STATE_WORK_CONTRACT, assert_complete_helper_tokens,
    declared_audit_boundaries, measure_audit_boundaries,
)
from tests.host.work_count_evidence import Capture, Kind, Owner, Snapshot, Stage, Status, compose_snapshots
from tools.generate_selected_ownership_fixture import prepare_sources, source_bundle
from tools.stack_artifact_files import read_regular
from tools.validation_inputs import _file_identity


FIXTURE_PATHS = (
    "tests/host/selected_ownership_test.c",
    "tests/host/selected_ownership_test.h",
    "include/micros/work_observation.h",
    "tests/work/work_counts.h",
    "tests/work/work_counts.c",
    "tests/host/selected_ownership_source.py",
    "tests/host/selected_ownership_evidence.py",
    "tests/host/selected_ownership_authority.py",
    "tests/host/test_selected_ownership.py",
    "tools/generate_selected_ownership_fixture.py",
    "tools/check_selected_ownership.py",
)


def snapshot_files(root, paths):
    return {path: read_regular(Path(root) / path)[1] for path in paths}


def native_symbols_tool():
    nm = shutil.which("nm")
    if nm is None:
        raise ValueError("MEM03 observation absence requires the existing native nm")
    if sys.platform == "darwin":
        nm = subprocess.check_output(("/usr/bin/xcrun", "--find", "nm"), text=True, timeout=30).strip()
    nm = Path(nm).resolve(strict=True)
    return {"path": str(nm), "identity": _file_identity(nm)}


def native_command(root, compiler, generated, executable, stage, channel):
    observed = channel != "ordinary"
    return (
        compiler["path"], "-std=c17", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
        "-Wmissing-prototypes", "-Wstrict-prototypes", "-fno-builtin",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
        "-I", str(generated), "-I", str(generated / "include"),
        "-I", str(root), "-I", str(root / "include"),
        *(("-DMICROS_SELECTED_OWNERSHIP_BASELINE=1",) if stage is Stage.RED else ()),
        *(("-DMICROS_TEST_WORK_COUNTS=1", "-DMICROS_TEST_SELECTED_OWNERSHIP_COUNTS=1") if observed else ()),
        *(str(generated / name) for name in RECIPE_ADMISSION["native_source_members"]),
        str(root / "tests/host/selected_ownership_test.c"),
        *((str(root / "tests/work/work_counts.c"),) if observed else ()),
        "-o", str(executable),
    )


def complete_helper_command(root, compiler, generated):
    return (
        compiler["path"], "--target=riscv64-unknown-elf", "-ffreestanding", "-nostdlibinc",
        "-std=c17", "-I", str(generated), "-I", str(generated / "include"),
        "-I", str(root), "-I", str(root / "include"), "-E", "-P",
        str(generated / "kernel/memory/frame_ownership_validation.c"),
    )


def measure_complete_helpers(root, stage):
    if stage not in (Stage.RED, Stage.GREEN):
        raise ValueError("MEM03 complete-helper proof has an unknown source stage")
    root = Path(root).resolve()
    compiler, fixtures = native_compiler(root), snapshot_files(root, FIXTURE_PATHS)
    with tempfile.TemporaryDirectory(prefix="micros-mem03-helper-tokens-") as temporary:
        generated = Path(temporary) / "source"
        _, inputs, outputs = prepare_sources(root, generated, stage, "ordinary")
        command = complete_helper_command(root, compiler, generated)
        completed = subprocess.run(
            command, cwd=root, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30,
        )
        if completed.returncode or completed.stderr or not completed.stdout:
            raise ValueError("MEM03 complete-helper preprocessing failed:\n" + completed.stdout + completed.stderr)
        digests = {
            name: assert_complete_helper_tokens(completed.stdout, name)
            for name in SOURCE_PROJECTION["complete_helper_tokens"]
        }
        current, _ = source_bundle(root, stage, "ordinary")
        if (
            current != inputs or snapshot_files(generated, outputs) != outputs
            or snapshot_files(root, FIXTURE_PATHS) != fixtures or native_compiler(root) != compiler
        ):
            raise ValueError("MEM03 complete-helper source/compiler inputs changed during execution")
        return {
            "version": 1, "stage": stage.value, "compiler": compiler, "command": command,
            "exit_code": completed.returncode, "output": completed.stdout,
            "source_inputs": inputs, "generated_sources": outputs, "fixture_inputs": fixtures,
            "digests": digests,
        }


def _complete_helper_digests(proof, stage):
    fields = {
        "version", "stage", "compiler", "command", "exit_code", "output",
        "source_inputs", "generated_sources", "fixture_inputs", "digests",
    }
    if (
        type(proof) is not dict or set(proof) != fields
        or type(proof["version"]) is not int or proof["version"] != 1
        or proof["stage"] != stage.value
        or type(proof["exit_code"]) is not int or proof["exit_code"] != 0
        or type(proof["output"]) is not str or not proof["output"]
    ):
        raise ValueError("MEM03 complete-helper proof is incomplete or borrowed")
    try:
        digests = {
            name: assert_complete_helper_tokens(proof["output"], name)
            for name in SOURCE_PROJECTION["complete_helper_tokens"]
        }
    except AssertionError as error:
        raise ValueError("MEM03 complete-helper output violates the exact retained identity") from error
    if proof["digests"] != digests:
        raise ValueError("MEM03 complete-helper result differs from its actual output")
    return digests


def _json_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("MEM03 native record has a duplicate field")
        result[key] = value
    return result


def expected_cases():
    result = []
    maximum = (1 << 64) - 1

    def add(operation, frames, ranges, q, free, owned, cursor, phase, output=maximum):
        result.append({
            "case": len(result) + 1, "operation": operation, "frames": frames,
            "ranges": ranges, "q": q, "result": 0, "output": output,
            "free": free, "owned": owned, "cursor": cursor, "phase": phase, "state": "PASS",
        })

    geometries = (
        *((frames, 1) for frames in STATE_WORK_CONTRACT["dense_frames"]),
        (STATE_WORK_CONTRACT["fragmented"]["managed_frames"], STATE_WORK_CONTRACT["fragmented"]["managed_ranges"]),
    )
    for frames, ranges in geometries:
        add(0, frames, ranges, 0, frames - 1, 1, 1, 1, STATE_WORK_CONTRACT["physical_base"])
        add(1, frames, ranges, 0, frames, 0, 0, 1, 0)
        add(2, frames, ranges, 0, frames, 0, 0, 1)
        add(3, frames, ranges, 0, frames - 1, 1, 1, 1)
        add(4, frames, ranges, 0, frames - 1, 1, 0, 2, frames - 1)
        for operation in (5, 6, 7, 8):
            add(operation, frames, ranges, 0, frames, 0, 0, 1)
    sets = STATE_WORK_CONTRACT["set_cases"]
    for frames in sets["managed_frames"]:
        for q in sets["data_counts"]:
            add(9, frames, 1, q, frames - q - 4, q + 4, 0, 2)
            add(10, frames, 1, q, frames, 0, 0, 2)
            add(11, frames, 1, q, frames, 0, 0, 2)
    for frames, indices in STATE_WORK_CONTRACT["bitmap_cases"]:
        q = len(indices)
        add(12, frames, 1, q, frames - 65 + q, 65 - q, 0, 1)
    for frames, indices in STATE_WORK_CONTRACT["bitmap_cases"]:
        for operation, phase in ((13, 1), (14, 2)):
            add(operation, frames, 1, len(indices), frames - 65, 65, 65, phase)
    if len(result) != 99:
        raise ValueError("MEM03 declared case matrix is incomplete")
    return tuple(result)


def expected_guards(stage):
    common = (
        *(f"diagnostic-corruption-{index}" for index in range(29)),
        "arguments", "exhaustion", "cursor-rollback", "empty-bootstrap-release",
        "empty-transferable-release", "empty-wrong-phase", "selection-bounds",
        "duplicate-vm-data", "overlapping-vm-tables", "duplicate-vm-tables",
        "vm-wrong-owner", "stale-and-foreign-process", "whole-active-counts",
    )
    selected = (
        *(f"selected-fields-{index}" for index in range(8)),
        "selected-bounds", "metadata-errors", "unselected-active", "inactive-capacity",
        "runtime-metadata-authority", "selected-mutation-preflights",
        "selected-process-count", "vm-set-failure-atomicity",
    )
    if stage not in (Stage.RED, Stage.GREEN):
        raise ValueError("MEM03 guards have an unknown source stage")
    return common if stage is Stage.RED else (*common, *selected)


def _validate_snapshot(value, stage, channel, capture):
    if type(value) is not dict or set(value) != {
        "version", "reserved0", "reserved1", "producer_label", "capture_label", "owner_mask", "counts",
    } or type(value["counts"]) is not list or any(type(row) is not list for row in value["counts"]):
        raise ValueError("MEM03 native snapshot is incomplete or malformed")
    producer = (
        STATE_WORK_CONTRACT["producer_base"] + STATE_WORK_CONTRACT["channels"].index(channel) + 1
        + (STATE_WORK_CONTRACT["current_producer_offset"] if stage is Stage.GREEN else 0)
    )
    snapshot = Snapshot(**{**value, "counts": tuple(tuple(row) for row in value["counts"])})
    compose_snapshots(
        (Capture(Status.OK, snapshot),), capture_label=capture,
        expected_producers={producer: STATE_WORK_CONTRACT["owner_mask"]},
    )
    if channel != "aggregate":
        expected_kind = Kind.RECORD_VISIT if channel.endswith("-frame") else Kind.RANGE_VISIT
        if any(
            count for owner, row in enumerate(snapshot.counts) for kind, count in enumerate(row)
            if (owner, kind) != (Owner.KERNEL_MEMORY, expected_kind)
        ):
            raise ValueError("MEM03 component contains work outside its declared primitive slice")


def parse_native_output(output, stage, channel):
    if stage not in (Stage.RED, Stage.GREEN) or channel not in (*STATE_WORK_CONTRACT["channels"], "ordinary"):
        raise ValueError("MEM03 transcript has an unknown source stage or channel")
    if type(output) is not str:
        raise ValueError("MEM03 transcript must be complete text")
    cases, observations, guards = [], [], []
    marker = "MICROS_MEM03_SEMANTICS_PASS cases=99"
    lines = output.splitlines()
    if not output.endswith("\n") or lines.count(marker) != 1 or lines[-1] != marker:
        raise ValueError("MEM03 native execution lacks its complete terminal marker")
    for line in lines[:-1]:
        if line.startswith("MICROS_MEM03_CASE "):
            if guards or channel != "ordinary" and len(cases) != len(observations):
                raise ValueError("MEM03 case arrived before its preceding work capture")
            value = json.loads(line.removeprefix("MICROS_MEM03_CASE "), object_pairs_hook=_json_pairs)
            if (
                type(value) is not dict or set(value) != {
                    "case", "operation", "frames", "ranges", "q", "result", "output",
                    "free", "owned", "cursor", "phase", "state",
                }
                or value["state"] != "PASS" or value["result"] != 0
                or any(type(value[name]) is not int or value[name] < 0 for name in value if name != "state")
                or value["case"] != len(cases) + 1
                or value["free"] + value["owned"] != value["frames"]
                or value["cursor"] > value["frames"]
            ):
                raise ValueError("MEM03 native semantic record is incomplete or invalid")
            cases.append(value)
        elif line.startswith("MICROS_MEM03_WORK "):
            value = json.loads(line.removeprefix("MICROS_MEM03_WORK "), object_pairs_hook=_json_pairs)
            if guards or channel == "ordinary" or len(observations) + 1 != len(cases):
                raise ValueError("MEM03 observation has no matching case or duplicates a capture")
            _validate_snapshot(value, stage, channel, len(cases))
            observations.append(value)
        elif line.startswith("MICROS_MEM03_GUARD ") and line.endswith(" PASS"):
            if len(cases) != 99 or channel != "ordinary" and len(observations) != 99:
                raise ValueError("MEM03 guard interrupted its valid-case measurements")
            guards.append(line.removeprefix("MICROS_MEM03_GUARD ").removesuffix(" PASS"))
        else:
            raise ValueError("MEM03 native execution contains an unknown record")
    if len(cases) != STATE_WORK_CONTRACT["valid_cases"] or (
        len(observations) != (0 if channel == "ordinary" else len(cases))
    ):
        raise ValueError("MEM03 native execution omitted or duplicated a case/capture")
    if tuple(cases) != expected_cases():
        raise ValueError("MEM03 native semantics differ from the independently declared inputs")
    if tuple(guards) != expected_guards(stage):
        raise ValueError("MEM03 omitted, duplicated or changed a required semantic guard")
    return tuple(cases), tuple(observations)


def measure_native_channel(root, stage, channel):
    if stage not in (Stage.RED, Stage.GREEN) or channel not in (*STATE_WORK_CONTRACT["channels"], "ordinary"):
        raise ValueError("MEM03 native execution has an unknown source stage/channel")
    root = Path(root).resolve()
    compiler = native_compiler(root)
    fixtures = snapshot_files(root, FIXTURE_PATHS)
    symbol_tool = native_symbols_tool()
    nm = symbol_tool["path"]
    with tempfile.TemporaryDirectory(prefix="micros-mem03-native-") as temporary:
        directory = Path(temporary)
        generated = directory / "source"
        _, inputs, outputs = prepare_sources(root, generated, stage, channel)
        executable = directory / "selected-ownership"
        command = native_command(root, compiler, generated, executable, stage, channel)
        built = subprocess.run(
            command, cwd=root, stdin=subprocess.DEVNULL,
            capture_output=True, text=True, timeout=120,
        )
        if built.returncode:
            raise ValueError("MEM03 native build failed, not a work Red:\n" + built.stdout + built.stderr)
        executable_identity = read_regular(executable)[1]
        symbols = subprocess.check_output((nm, str(executable)), cwd=root, text=True, timeout=30)
        observation_symbols = tuple(sorted({
            line.split()[-1].lstrip("_") for line in symbols.splitlines()
            if line.split() and "micros_test_work_" in line.split()[-1]
        }))
        expected_symbols = () if channel == "ordinary" else (
            "micros_test_work_begin", "micros_test_work_finish", "micros_test_work_record",
        )
        if observation_symbols != expected_symbols:
            raise ValueError("MEM03 collector linkage differs from its declared native mode")
        if snapshot_files(generated, outputs) != outputs:
            raise ValueError("MEM03 generated source changed during compilation")
        execution = (str(executable), "red" if stage is Stage.RED else "green", channel)
        completed = subprocess.run(
            execution, cwd=root, stdin=subprocess.DEVNULL,
            capture_output=True, text=True, timeout=180,
        )
        if completed.returncode or completed.stderr:
            raise ValueError("MEM03 native semantics failed, not a work Red:\n" + completed.stdout + completed.stderr)
        cases, observations = parse_native_output(completed.stdout, stage, channel)
        current_inputs, _ = source_bundle(root, stage, channel)
        if (
            snapshot_files(root, FIXTURE_PATHS) != fixtures or native_compiler(root) != compiler
            or native_symbols_tool() != symbol_tool or read_regular(executable)[1] != executable_identity
            or snapshot_files(generated, outputs) != outputs or current_inputs != inputs
        ):
            raise ValueError("MEM03 source/compiler/fixture/artifact inputs changed during execution")
        return {
            "version": 1, "stage": stage.value, "channel": channel, "compiler": compiler,
            "command": command, "execution_command": execution,
            "build_exit": built.returncode, "execution_exit": completed.returncode,
            "build_output": built.stdout + built.stderr, "output": completed.stdout,
            "source_inputs": inputs, "generated_sources": outputs, "fixture_inputs": fixtures,
            "executable": executable_identity, "cases": cases, "observations": observations,
            "nm": symbol_tool, "observation_symbols": observation_symbols,
        }


def _count(measured, channel, case, kind, owner=Owner.KERNEL_MEMORY):
    return measured[channel]["observations"][case - 1]["counts"][owner][kind]


def validate_measurements(measured):
    channels = (*STATE_WORK_CONTRACT["channels"], "ordinary")
    if type(measured) is not dict or set(measured) != set(channels):
        raise ValueError("MEM03 measurement omitted or duplicated a native product")
    stage = Stage(measured["aggregate"]["stage"])
    fields = {
        "version", "stage", "channel", "compiler", "command", "execution_command",
        "build_exit", "execution_exit", "build_output", "output", "source_inputs",
        "generated_sources", "fixture_inputs", "executable", "cases", "observations",
        "nm", "observation_symbols",
    }
    for channel, execution in measured.items():
        if (
            type(execution) is not dict or set(execution) != fields
            or type(execution["version"]) is not int or execution["version"] != 1
            or execution["stage"] != stage.value or execution["channel"] != channel
            or any(type(execution[key]) is not int or execution[key] != 0 for key in ("build_exit", "execution_exit"))
        ):
            raise ValueError("MEM03 execution is incomplete, mixed-stage or unsuccessful")
        cases, work = parse_native_output(execution["output"], stage, channel)
        if cases != tuple(execution["cases"]) or work != tuple(execution["observations"]):
            raise ValueError("MEM03 execution metadata differs from its actual output")
        for key in ("source_inputs", "fixture_inputs", "compiler", "nm", "cases"):
            if execution[key] != measured["ordinary"][key]:
                raise ValueError("MEM03 slices did not execute the same source, inputs and semantic cases")
        expected_symbols = () if channel == "ordinary" else (
            "micros_test_work_begin", "micros_test_work_finish", "micros_test_work_record",
        )
        if tuple(execution["observation_symbols"]) != expected_symbols:
            raise ValueError("MEM03 execution has the wrong collector linkage")
    _validate_diagnostic_counts(measured)
    return stage


def _validate_diagnostic_counts(measured):
    for case in expected_cases():
        label, operation = case["case"], case["operation"]
        active = _count(measured, "active-frame", label, Kind.RECORD_VISIT)
        inactive = _count(measured, "inactive-frame", label, Kind.RECORD_VISIT)
        geometry = _count(measured, "active-geometry", label, Kind.RANGE_VISIT)
        unused = _count(measured, "inactive-geometry", label, Kind.RANGE_VISIT)
        if (
            active + inactive > _count(measured, "aggregate", label, Kind.RECORD_VISIT)
            or geometry + unused != _count(measured, "aggregate", label, Kind.RANGE_VISIT)
        ):
            raise ValueError("MEM03 components do not partition their actual inclusive work")
        if operation in (6, 8):
            for owner in (Owner.KERNEL_OBJECTS, Owner.KERNEL_SCHEDULER):
                if (
                    _count(measured, "aggregate", label, Kind.COMPLETE_VALIDATOR_CALL, owner) != 1
                    or _count(measured, "aggregate", label, Kind.INDEX_PROBE, owner) == 0
                ):
                    raise ValueError("MEM03 omitted the retained complete object/scheduler primitive work")
        if operation not in (7, 8):
            continue
        frames, ranges = case["frames"], case["ranges"]
        expected = (
            (2 if operation == 7 else 3) * frames, 2 * (262144 - frames),
            3 * ranges, 2 * (96 - ranges), 4096 + (frames % 64 != 0),
            1 if operation == 7 else 2,
        )
        actual = (
            active, inactive, geometry, unused,
            _count(measured, "aggregate", label, Kind.BITMAP_WORD_LOAD),
            _count(measured, "aggregate", label, Kind.COMPLETE_VALIDATOR_CALL),
        )
        if actual != expected:
            raise ValueError(f"MEM03 diagnostic preservation differs at case {label}: {actual} != {expected}")


def measure_native(root, stage):
    measured = {
        channel: measure_native_channel(root, stage, channel)
        for channel in (*STATE_WORK_CONTRACT["channels"], "ordinary")
    }
    validate_measurements(measured)
    return measured


def check_current_fixture_syntax(root):
    from tests.host import selected_release_handoff_source as current_interfaces

    root = Path(root).resolve()
    compiler = native_compiler(root)
    paths = (*FIXTURE_PATHS, current_interfaces.SOURCE_PATH)
    fixtures = snapshot_files(root, paths)
    with tempfile.TemporaryDirectory(prefix="micros-mem03-fixture-syntax-") as temporary:
        generated = Path(temporary)
        prepare_sources(root, generated, Stage.RED, "ordinary")
        for path in (
            "include/micros/frame_ownership.h", "include/micros/frame_ownership_runtime.h",
            "kernel/memory/frame_ownership_internal.h",
        ):
            target = generated / path
            text = current_interfaces.declared_header(root, path)
            if hashlib.sha256(text.encode()).hexdigest() != current_interfaces.SOURCE_PROJECTION["declared_header_sha256"][path]:
                raise ValueError("MEM03 fixture syntax interface differs from its independent MEM05 declaration")
            target.write_text(text)
        command = native_command(root, compiler, generated, generated / "unused", Stage.GREEN, "ordinary")
        prefix = command[:command.index(str(generated / RECIPE_ADMISSION["native_source_members"][0]))]
        command = (
            *prefix, "-DMICROS_SELECTED_OWNERSHIP_LAUNCHER=1",
            '-DMICROS_SELECTED_OWNERSHIP_PYTHON="' + sys.executable + '"',
            '-DMICROS_SELECTED_OWNERSHIP_DRIVER="' + str(root / RECIPE_ADMISSION["driver"]) + '"',
            '-DMICROS_SELECTED_OWNERSHIP_ROOT="' + str(root) + '"',
            "-fsyntax-only", str(root / "tests/host/selected_ownership_test.c"),
        )
        checked = subprocess.run(
            command, cwd=root, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30,
        )
        if checked.returncode or checked.stdout or checked.stderr:
            raise ValueError("MEM03 current fixture does not compile against its declared interfaces:\n" + checked.stdout + checked.stderr)
        if snapshot_files(root, paths) != fixtures or native_compiler(root) != compiler:
            raise ValueError("MEM03 fixture syntax inputs changed during compilation")
        return command


def work_failures(measured, reference=None):
    stage = validate_measurements(measured)
    if stage is Stage.GREEN:
        if reference is None or validate_measurements(reference) is not Stage.RED:
            raise ValueError("MEM03 current work lacks the actual fixed-source preservation reference")
    return _work_failures(measured, stage, reference)


def _work_failures(measured, stage, reference):
    failures = {
        "mem03-selected-record-work": [],
        "mem03-runtime-owner-work": [],
        "mem03-consumer-audit-boundaries": [],
    }
    for case in expected_cases():
        label, operation = case["case"], case["operation"]
        if operation in (7, 8):
            continue
        oracle = (
            "mem03-runtime-owner-work" if operation == 5 else
            "mem03-consumer-audit-boundaries" if operation == 6 else "mem03-selected-record-work"
        )

        def bound(name, channel, kind, maximum, owner=Owner.KERNEL_MEMORY):
            actual = _count(measured, channel, label, kind, owner)
            if actual > maximum:
                failures[oracle].append((label, name, actual, maximum))

        bound("inactive-frame", "inactive-frame", Kind.RECORD_VISIT, 0)
        bound("inactive-geometry", "inactive-geometry", Kind.RANGE_VISIT, 0)
        bound("complete-memory-audit", "aggregate", Kind.COMPLETE_VALIDATOR_CALL, 0)
        p, g, q = case["frames"], case["ranges"], case["q"]
        record_bound = index_bound = range_bound = None
        if operation <= 4:
            active_bound, geometry_bound, bitmap_bound = 16, 8 * g + 16, 8
            record_bound = index_bound = range_bound = 64 * g + 256
        elif operation == 5:
            active_bound, geometry_bound, bitmap_bound = 4 * p + 32, 8 * g + 16, 8
            record_bound = 16 * p + 64 * g + 512
            for owner in (Owner.KERNEL_OBJECTS, Owner.KERNEL_SCHEDULER):
                bound(owner.name + "-complete-audit", "aggregate", Kind.COMPLETE_VALIDATOR_CALL, 0, owner)
        elif operation == 6:
            active_bound, geometry_bound, bitmap_bound = 0, 8 * g + 16, 0
            record_bound = index_bound = range_bound = 64 * g + 256
            if stage is Stage.GREEN:
                for owner in (Owner.KERNEL_OBJECTS, Owner.KERNEL_SCHEDULER):
                    if (
                        measured["aggregate"]["observations"][label - 1]["counts"][owner]
                        != reference["aggregate"]["observations"][label - 1]["counts"][owner]
                    ):
                        raise ValueError("MEM03 changed the independently retained PM object/scheduler work")
        elif operation in (9, 10, 11):
            n = q if operation == 10 else q + 4
            active_bound, geometry_bound, bitmap_bound = 16 * n + 32, 8 * (g + 1) * (n + 1), 8 * n + 16
            record_bound = index_bound = range_bound = 64 * (g + 1) * (n + 1) + 16 * n * n + 256
        elif operation == 12:
            active_bound, geometry_bound, bitmap_bound = 16 * q + 32, 8 * g + 16, 4 * 262144 + 8 * q + 16
        else:
            active_bound, geometry_bound = 32 * p + 64, None
            bitmap_bound = (4 * 262144 if operation == 13 else 0) + 16 * ((p + 63) // 64) + 64
            record_bound = 64 * p + 64 * g + 1024
        bound("active-frame", "active-frame", Kind.RECORD_VISIT, active_bound)
        bound("bitmap-words", "aggregate", Kind.BITMAP_WORD_LOAD, bitmap_bound)
        if geometry_bound is not None:
            bound("active-geometry", "active-geometry", Kind.RANGE_VISIT, geometry_bound)
        for name, kind, maximum in (
            ("record-work", Kind.RECORD_VISIT, record_bound),
            ("index-work", Kind.INDEX_PROBE, index_bound),
            ("range-work", Kind.RANGE_VISIT, range_bound),
        ):
            if maximum is not None:
                bound(name, "aggregate", kind, maximum)
    return {oracle: tuple(values) for oracle, values in failures.items()}


def check_native_products(root, executable_paths):
    root = Path(root).resolve()
    channels = (*STATE_WORK_CONTRACT["channels"], "ordinary")
    expected = tuple(root / "build/host-debug" / name for name in RECIPE_ADMISSION["native_targets"])
    if tuple(map(Path, executable_paths)) != expected:
        raise ValueError("MEM03 CTest requires its six exact declared build products")
    identities = {str(path): read_regular(path)[1] for path in expected}
    fixtures, nm = snapshot_files(root, FIXTURE_PATHS), native_symbols_tool()
    measured, generated, inputs = {}, {}, {}
    for channel, executable in zip(channels, expected):
        source_inputs, outputs = source_bundle(root, Stage.GREEN, channel)
        directory = root / RECIPE_ADMISSION["generator_output"] / channel
        emitted = {
            name: {"kind": "file", "mode": mode, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
            for name, (data, mode) in outputs.items()
        }
        if snapshot_files(directory, emitted) != emitted:
            raise ValueError("MEM03 CTest generated inputs differ from actual current source")
        generated[channel], inputs[channel] = emitted, source_inputs
        symbols = subprocess.check_output((nm["path"], str(executable)), cwd=root, text=True, timeout=30)
        observed_symbols = tuple(sorted({
            line.split()[-1].lstrip("_") for line in symbols.splitlines()
            if line.split() and "micros_test_work_" in line.split()[-1]
        }))
        wanted = () if channel == "ordinary" else (
            "micros_test_work_begin", "micros_test_work_finish", "micros_test_work_record",
        )
        if observed_symbols != wanted:
            raise ValueError("MEM03 CTest product has the wrong collector linkage")
        completed = subprocess.run(
            (str(executable), "green", channel), cwd=root, stdin=subprocess.DEVNULL,
            capture_output=True, text=True, timeout=180,
        )
        if completed.returncode or completed.stderr:
            raise ValueError("MEM03 CTest product failed its semantic guards:\n" + completed.stdout + completed.stderr)
        cases, work = parse_native_output(completed.stdout, Stage.GREEN, channel)
        measured[channel] = {"cases": cases, "observations": work}
    if any(item["cases"] != measured["ordinary"]["cases"] for item in measured.values()):
        raise ValueError("MEM03 CTest products disagree on their complete semantic transcript")
    _validate_diagnostic_counts(measured)
    _complete_helper_digests(measure_complete_helpers(root, Stage.GREEN), Stage.GREEN)
    reference = measure_native(root, Stage.RED)
    failures = _work_failures(measured, Stage.GREEN, reference)
    audits = measure_audit_boundaries(root, Stage.GREEN)
    _, retained, migrated = declared_audit_boundaries(str(root))
    if any(failures.values()) or audits["complete"] != retained or audits["migrated"] != migrated:
        raise ValueError("MEM03 CTest failed selected work or audit boundaries: " + json.dumps(failures))
    if (
        snapshot_files(root, FIXTURE_PATHS) != fixtures or native_symbols_tool() != nm
        or any(read_regular(path)[1] != identities[str(path)] for path in expected)
    ):
        raise ValueError("MEM03 CTest inputs or products changed during execution")
    for channel in channels:
        directory = root / RECIPE_ADMISSION["generator_output"] / channel
        current, _ = source_bundle(root, Stage.GREEN, channel)
        if current != inputs[channel] or snapshot_files(directory, generated[channel]) != generated[channel]:
            raise ValueError("MEM03 CTest source or generated inputs changed during execution")
    return 0


class SelectedOwnershipOracles(unittest.TestCase):
    failures = None
    audits = None
    expected_audits = None
    helpers = None

    def test_portable_selected_work(self):
        self.assertEqual(SOURCE_PROJECTION["complete_helper_tokens"], self.helpers)
        self.assertEqual((), self.failures["mem03-selected-record-work"])

    def test_runtime_owner_work(self):
        self.assertEqual((), self.failures["mem03-runtime-owner-work"])

    def test_consumer_audit_boundaries(self):
        with self.subTest(property="actual-PM-consumer"):
            self.assertEqual((), self.failures["mem03-consumer-audit-boundaries"])
        with self.subTest(property="eight-retained-audit-calls"):
            self.assertEqual(self.expected_audits[1], self.audits["complete"])
        with self.subTest(property="eighteen-migrated-call-sites"):
            self.assertEqual(self.expected_audits[2], self.audits["migrated"])


def run_oracles(root, measured, audits, helpers, reference=None):
    from tests.host.evidence_authority_types import ObservedResult, OracleResult, RunStatus
    from tests.host.selected_ownership_authority import ORACLE_RULES

    stage = validate_measurements(measured)
    if audits != measure_audit_boundaries(root, stage):
        raise ValueError("MEM03 caller ledger differs from its actual complete source namespace")
    failures = work_failures(measured, reference)
    helper_digests = _complete_helper_digests(helpers, stage)
    expected = declared_audit_boundaries(str(Path(root).resolve()))
    outcomes, output = [], io.StringIO()
    for identifier, test, property_name, _, _, _ in ORACLE_RULES:
        case = SelectedOwnershipOracles(test.rsplit(".", 1)[1])
        case.failures, case.audits, case.expected_audits = failures, audits, expected
        case.helpers = helper_digests
        result = unittest.TextTestRunner(stream=output, verbosity=2).run(unittest.TestSuite((case,)))
        if result.testsRun != 1 or result.errors or result.skipped:
            raise ValueError("MEM03 oracle was skipped or failed outside its measured assertion")
        if stage is Stage.RED:
            if result.wasSuccessful():
                raise ValueError("MEM03 original execution did not expose every declared work divergence")
            observed = ObservedResult.EXPECTED_FAILURE
        else:
            observed = ObservedResult.PASS if result.wasSuccessful() else ObservedResult.FAIL
        outcomes.append(OracleResult(identifier, RunStatus.RUN, observed, property_name))
    return tuple(outcomes), output.getvalue()


def execution_log(measured, audits, helpers, results, oracle_output, *, phase, attempt):
    return json.loads(json.dumps({
        "version": 1, "phase": phase, "attempt": attempt, "native": measured, "audits": audits, "helpers": helpers,
        "results": [asdict(result) for result in results], "oracle_output": oracle_output,
    }, allow_nan=False))


def _validate_native_replay(context, measured, stage):
    from tests.host.diagnostic_history_authority import record_fields, record_strings

    if validate_measurements(measured) is not stage:
        raise ValueError("MEM03 native replay borrowed another source stage")
    directories = set()
    for channel, native in measured.items():
        if native["compiler"] != context.native_compiler or native["nm"] != context.native_nm:
            raise ValueError("MEM03 native replay borrowed another compiler or symbol reader")
        execution = record_strings(native["execution_command"])
        if len(execution) != 3 or execution[1:] != (stage.value, channel):
            raise ValueError("MEM03 native replay changed its execution stage or channel")
        executable = Path(execution[0])
        directory = executable.parent
        if (
            not executable.is_absolute() or executable.name != "selected-ownership"
            or not directory.name.startswith("micros-mem03-native-") or directory in directories
            or directory.parent.resolve() != Path(tempfile.gettempdir()).resolve()
        ):
            raise ValueError("MEM03 native replay names a borrowed or unowned execution")
        directories.add(directory)
        if record_strings(native["command"]) != native_command(
            context.root, context.native_compiler, directory / "source", executable, stage, channel,
        ):
            raise ValueError("MEM03 native replay changed its exact compile/link command")
        inputs, outputs = source_bundle(context.root, stage, channel)
        generated = {
            name: {"kind": "file", "mode": mode, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
            for name, (data, mode) in outputs.items()
        }
        if (
            native["source_inputs"] != inputs or native["generated_sources"] != generated
            or native["fixture_inputs"] != {name: context.files[name] for name in FIXTURE_PATHS}
            or type(native["build_output"]) is not str
        ):
            raise ValueError("MEM03 native replay substituted a source/header/observation/fixture input")
        identity = native["executable"]
        record_fields(identity, ("kind", "mode", "size", "sha256"))
        if (
            identity["kind"] != "file" or type(identity["mode"]) is not int
            or not 0 < identity["mode"] <= 0o777 or not identity["mode"] & 0o100
            or type(identity["size"]) is not int or identity["size"] <= 0
            or type(identity["sha256"]) is not str or re.fullmatch(r"[a-f0-9]{64}", identity["sha256"]) is None
        ):
            raise ValueError("MEM03 native replay lacks a complete executable identity")
    return measured


def _validate_complete_helper_replay(context, proof, stage):
    from tests.host.diagnostic_history_authority import record_strings

    _complete_helper_digests(proof, stage)
    command = record_strings(proof["command"])
    if not command:
        raise ValueError("MEM03 complete-helper replay lacks its compiler command")
    source = Path(command[-1])
    if len(source.parents) < 4:
        raise ValueError("MEM03 complete-helper replay has no owned source directory")
    generated, directory = source.parents[2], source.parents[3]
    if (
        not source.is_absolute() or source.name != "frame_ownership_validation.c"
        or generated.name != "source" or not directory.name.startswith("micros-mem03-helper-tokens-")
        or directory.parent.resolve() != Path(tempfile.gettempdir()).resolve()
        or command != complete_helper_command(context.root, context.native_compiler, generated)
        or proof["compiler"] != context.native_compiler
    ):
        raise ValueError("MEM03 complete-helper replay changed its compiler, source or command")
    inputs, outputs = source_bundle(context.root, stage, "ordinary")
    expected = {
        name: {"kind": "file", "mode": mode, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        for name, (data, mode) in outputs.items()
    }
    if (
        proof["source_inputs"] != inputs or proof["generated_sources"] != expected
        or proof["fixture_inputs"] != {name: context.files[name] for name in FIXTURE_PATHS}
    ):
        raise ValueError("MEM03 complete-helper replay substituted a source/header/fixture input")


def validate_execution_log(context, replay, *, phase, attempt, reference=None):
    from tests.host.diagnostic_history_authority import record_fields

    if phase not in ("red", "green", "recertify") or type(attempt) is not str or re.fullmatch(r"[a-f0-9]{32}", attempt) is None:
        raise ValueError("MEM03 replay has an unadmitted phase or attempt")
    record_fields(replay, ("version", "phase", "attempt", "native", "audits", "helpers", "results", "oracle_output"))
    if (
        type(replay["version"]) is not int or replay["version"] != 1
        or replay["phase"] != phase or replay["attempt"] != attempt
        or type(replay["oracle_output"]) is not str or not replay["oracle_output"]
    ):
        raise ValueError("MEM03 replay belongs to a different phase or attempt")
    stage = Stage.RED if phase == "red" else Stage.GREEN
    measured = _validate_native_replay(context, replay["native"], stage)
    _validate_complete_helper_replay(context, replay["helpers"], stage)
    if stage is Stage.GREEN:
        if reference is None:
            raise ValueError("MEM03 current replay lacks its same-attempt original execution")
        reference = _validate_native_replay(context, reference, Stage.RED)
    elif reference is not None:
        raise ValueError("MEM03 original replay cannot borrow a different reference")
    audits = measure_audit_boundaries(context.root, stage)
    if replay["audits"] != json.loads(json.dumps(audits)):
        raise ValueError("MEM03 replay substituted its actual complete caller ledger")
    results, _ = run_oracles(context.root, measured, audits, replay["helpers"], reference)
    if replay["results"] != json.loads(json.dumps([asdict(result) for result in results])):
        raise ValueError("MEM03 replay outcomes do not follow the actual complete executions")
    return results
