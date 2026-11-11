"""Current ownership native records, independent work bounds and complete diagnostics."""

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

from tests.host.selected_ownership_source import (
    RECIPE_ADMISSION, STATE_WORK_CONTRACT, measure_audit_boundaries,
)
from tests.host.work_count_evidence import Capture, Kind, Owner, Snapshot, Stage, Status, compose_snapshots
from tools.generate_selected_ownership_fixture import source_bundle
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
    "tests/host/native_observations.py",
    "tests/host/native_observations.json",
    "tests/host/source_snapshot.py",
    "tests/host/c_source_records.py",
    "tests/host/work_count_lexical.py",
    "tests/host/work_count_projection.py",
    "tests/host/work_count_types.py",
    "tests/host/work_count_snapshots.py",
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


def _count(measured, channel, case, kind, owner=Owner.KERNEL_MEMORY):
    return measured[channel]["observations"][case - 1]["counts"][owner][kind]


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


# Reviewed #246 aggregate transcript e3de743c04001fce629375e3500e85089640de4143945c817acb873b698d4edb:
# every operation-6 case retains these object/scheduler observations.
PM_WORK_REFERENCE = {
    Owner.KERNEL_OBJECTS: (0, 0, 50632, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0),
    Owner.KERNEL_SCHEDULER: (0, 0, 264, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0),
}


def _work_failures(measured):
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
            for owner, expected in PM_WORK_REFERENCE.items():
                if tuple(measured["aggregate"]["observations"][label - 1]["counts"][owner]) != expected:
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
    failures = _work_failures(measured)
    measure_audit_boundaries(root, Stage.GREEN)
    if any(failures.values()):
        raise ValueError("MEM03 CTest failed selected work or audit boundaries: " + json.dumps(failures))
    if (
        snapshot_files(root, FIXTURE_PATHS) != fixtures or native_symbols_tool() != nm
        or any(read_regular(path)[1] != identities[str(path)] for path in expected)
    ):
        raise ValueError("MEM03 CTest inputs or products changed during execution")
    for channel in channels:
        directory = root / RECIPE_ADMISSION["generator_output"] / channel
        if snapshot_files(root, inputs[channel]) != inputs[channel] or snapshot_files(directory, generated[channel]) != generated[channel]:
            raise ValueError("MEM03 CTest source or generated inputs changed during execution")
    return 0
