"""Actual MEM05 work, semantic and target evidence."""

import hashlib
import json
from pathlib import Path
import re
import subprocess

from tests.host.selected_address_space_evidence import native_symbols_tool
from tests.host.work_count_evidence import Capture, Kind, Owner, Snapshot, Stage, Status, compose_snapshots
from tools.stack_artifact_files import read_regular


WORK_CHANNELS = ("frames", "plans", "context")
CASE_FIELDS = frozenset((
    "operation", "frames", "ranges", "selected", "words", "indexed",
    "owned", "tables", "roots",
))
OPERATIONS = frozenset((
    "release-set", "count-process", "release-process", "release-page",
    "destroy", "prepare-handoff", "complete-handoff", "release-scope",
))
NATIVE_FIXTURE_PATHS = (
    "tests/host/selected_release_handoff_test.c", "tests/host/selected_release_handoff_test.h",
    "tests/host/selected_release_handoff_source.py", "tests/host/selected_release_handoff_evidence.py",
    "tests/host/native_observations.py", "tests/host/native_observations.json",
    "tests/host/source_snapshot.py", "tests/work/work_counts.c", "tests/work/work_counts.h",
    "tools/generate_selected_release_handoff_fixture.py", "tools/check_selected_release_handoff.py",
    "tests/host/selected_address_space_source.py",
    "tests/host/c_source_records.py", "tests/host/work_count_lexical.py",
    "tests/host/work_count_projection.py", "tests/host/work_count_types.py",
    "tests/host/work_count_snapshots.py",
)

SELECTION_SHAPE_CASES = (
    "null", "word-count", "frame-count", "zero-words", "zero-frames", "frames-below-words",
    "word-range", "duplicate", "missing-inverse", "wrong-inverse", "zero-word", "population",
)


def expected_guards():
    rows = []

    def add(family, case, result=0, panic=""):
        rows.append({"family": family, "case": case, "result": result, "panic": panic})

    for case in ("empty", "low-bit", "re-add", "word-boundary", "last-bit", "all-words", "full-re-add", "clear"):
        add("selection-publication", case)
    for operation in ("release", "wired", "clear"):
        for case in SELECTION_SHAPE_CASES:
            add("selection-shape", operation + "/" + case, 1)
    for case in ("release/unmanaged", "wired/unmanaged"):
        add("selection-shape", case, 10)
    for case in ("add/unpublished-bitmap", "add/unpublished-inverse"):
        add("selection-shape", case, 1)
    for case in (
        "insert-empty", "insert-head", "remove-head", "remove-middle", "remove-tail",
        "remove-singleton", "adjacent-forward", "adjacent-reverse", "mixed-generation", "slot-reuse",
    ):
        add("index-mutation", case)
    for case in (
        "head-count", "count-head", "head-range", "link-range", "count-range", "head-previous",
        "self-link", "previous", "next", "foreign-neighbor", "free-neighbor",
        "release-count", "freeze-capacity",
    ):
        add("index-rejection", case, 14)
    for case in (
        "disconnected-cycle", "unindexed-owner", "cross-slot",
        "inactive-previous", "inactive-next", "nonprocess-links",
    ):
        add("complete-index", case, 14)
    for case in ("unrelated-slot", "inactive-link", "unindexed-unreachable"):
        add("complete-index", "selected/" + case)
    add("complete-index", "same-slot-invalid-target", 14)
    add("complete-index", "selected/valid-other-generation")
    add("complete-index", "selected/other-slot-invalid-target")
    for case in (
        "bootstrap-allocate", "bootstrap-release", "selected-release", "wired-handoff",
        "transferable-handoff", "kernel-transfer", "vm-claim", "vm-rollback", "vm-freeze", "vm-release",
    ):
        add("lifecycle", case)
    for case in (
        "allocation-compensation", "vm-claim-linked", "vm-rollback-linked",
        "vm-freeze-data-linked", "vm-freeze-table-linked", "vm-release-count",
    ):
        add("lifecycle", case, 14)
    for case, result in (
        ("foreign", 11), ("table", 11), ("stale", 11), ("free", 11), ("malformed", 12),
        ("cycle", 11), ("alias", 11), ("orphan", 11), ("shared-root", 14),
        ("indexed-orphan", 11), ("reachable-unindexed", 11), ("unindexed-unreachable", 0),
        ("live-thread", 5), ("active-root", 5), ("prune-keep", 0), ("prune-leaf", 0), ("prune-both", 0),
    ):
        add("root-atomicity", case, result)
    for operation in ("prepare", "complete"):
        for case in (
            "missing-context", "invalid-context", "rootless-thread", "rootless-endpoint",
            "rootless-owner", "wrong-kind-owner", "malformed-root", "alias", "indexed-orphan", "transferable-target",
        ):
            add("handoff-atomicity", operation + "/" + case, 11 if case == "transferable-target" else 5)
    for case, result in (
        ("phase-null-release", 7), ("phase-null-wired", 7), ("null-invalid-handle", 1),
        ("header-invalid-handle", 1), ("metadata-prefix", 14), ("active-absent", 5),
        ("active-malformed", 12), ("later-context-earlier-root", 5),
        ("earlier-target-later-root", 11), ("nested-selected", 13), ("nested-full", 13),
        ("unknown-mode", 14), ("idle-published", 14),
    ):
        add("precedence-lifetime", case, result)
    for case in ("wrong-mode", "wrong-storage", "wrong-bitmap", "inactive-lease", "second-prefix"):
        add("precedence-lifetime", case, panic="user-address-space-scratch-release")
    return tuple(rows)


def expected_cases():
    rows = []

    def add(operation, frames, ranges, pattern, *, q=0, s=0, l=0, f=0, tables=0, roots=0, rootless=0):
        rows.append({
            "case": len(rows) + 1, "operation": operation, "frames": frames,
            "ranges": ranges, "pattern": pattern, "selected": q, "words": s,
            "indexed": l, "owned": f, "tables": tables, "roots": roots,
            "rootless": rootless,
        })

    for frames in (1, 63, 64, 65, 4096, 262144):
        add("release-set", frames, 1, "empty")
        add("release-set", frames, 1, "last", q=1, s=1)
    for frames in (65, 4096, 262144):
        add("release-set", frames, 1, "boundary-three", q=3, s=2)
        add("release-set", frames, 1, "boundary-four", q=4, s=2)
    add("release-set", 262144, 1, "every-word", q=4096, s=4096)
    add("release-set", 262144, 1, "all", q=262144, s=4096)

    for frames in (65, 4096, 262144):
        for pattern, length in (
            ("empty", 0), ("one", 1), ("sixteen", 16), ("sixteen-mixed", 16),
            ("sixty-four", 64), ("sixty-four-mixed", 64), ("sixty-four-foreign", 64),
        ):
            add("count-process", frames, 1, pattern, l=length)
    for frames in (65, 4096, 262144):
        add("release-process", frames, 1, "empty")
        add("release-process", frames, 1, "owned", l=1)

    geometries = tuple((frames, ranges) for frames in (4096, 262144) for ranges in (1, 8))
    for frames, ranges in geometries:
        for pattern, selected, owned, tables in (
            ("keep", 1, 5, 3), ("keep-dense", 1, 37, 3),
            ("prune-leaf", 2, 6, 4), ("prune-leaf-dense", 2, 38, 4),
            ("prune-both", 3, 4, 3),
        ):
            add("release-page", frames, ranges, pattern, q=selected, s=1, f=owned, tables=tables, roots=1)
        add("release-page", frames, ranges, "absent", f=4, tables=3, roots=1)
    for frames, ranges in geometries:
        for pattern, leaves, tables in (
            ("empty", 0, 1), ("one", 1, 3), ("dense32", 32, 3),
            ("sparse32", 32, 34), ("dense513", 513, 4),
        ):
            owned = leaves + tables
            add("destroy", frames, ranges, pattern, q=owned, s=owned // 64 + 1,
                l=owned, f=owned, tables=tables, roots=1)

    for frames in (4096, 16384):
        for ranges in (1, 8):
            for rooted, rootless in ((1, 0), (1, 63), (16, 0), (64, 0)):
                words = sum(len({process // 64, *(
                    (rooted + 1 + 4 * (process - 1) + offset) // 64 for offset in range(4)
                )}) for process in range(1, rooted + 1))
                for operation in ("prepare-handoff", "complete-handoff"):
                    add(operation, frames, ranges, "prepared", q=2 * rooted, s=words,
                        f=5 * rooted, tables=3 * rooted, roots=rooted, rootless=rootless)
    for pattern in ("foreign", "table", "malformed", "absent-orphan", "alias"):
        success = pattern == "alias"
        add("release-scope", 4096, 1, pattern, q=int(success), s=int(success), f=4, tables=3, roots=1)
    return tuple(rows)


def _validate_work_inputs(case, counts):
    if type(case) is not dict or set(case) != CASE_FIELDS:
        raise ValueError("MEM05 work case has an incomplete shape")
    if type(case["operation"]) is not str or case["operation"] not in OPERATIONS:
        raise ValueError("MEM05 work case has an unknown operation")
    if any(type(case[name]) is not int or case[name] < 0 for name in CASE_FIELDS - {"operation"}):
        raise ValueError("MEM05 work case has an invalid dimension")
    handoff = case["operation"] in ("prepare-handoff", "complete-handoff")
    if (
        not 1 <= case["frames"] <= 262144
        or not 1 <= case["ranges"] <= min(96, case["frames"])
        or any(case[name] > case["frames"] for name in ("selected", "indexed", "owned"))
        or case["tables"] > case["owned"] or case["roots"] > 64
        or case["words"] > (case["owned"] if handoff else min(4096, case["selected"]))
        or not handoff and bool(case["selected"]) != bool(case["words"])
    ):
        raise ValueError("MEM05 work case leaves its declared domain")
    operation = case["operation"]
    if operation in ("release-set", "count-process", "release-process"):
        if any(case[name] != 0 for name in ("owned", "tables", "roots")):
            raise ValueError("MEM05 ownership capture cannot borrow a root-work budget")
        if operation != "release-set" and (case["selected"] or case["words"]):
            raise ValueError("MEM05 count capture cannot borrow a selection-work budget")
        if operation == "release-set" and case["indexed"]:
            raise ValueError("MEM05 selected release cannot borrow a whole-slot budget")
    elif not handoff and (
        case["roots"] != 1 or case["tables"] < 1
        or operation in ("release-page", "release-scope") and (case["selected"] > 3 or case["indexed"])
        or operation == "destroy" and not case["selected"] == case["owned"] == case["indexed"]
    ):
        raise ValueError("MEM05 root capture has contradictory topology bounds")
    if type(counts) is not dict or set(counts) != set(WORK_CHANNELS):
        raise ValueError("MEM05 work capture is incomplete")
    for matrix in counts.values():
        if type(matrix) not in (tuple, list) or len(matrix) != len(Owner):
            raise ValueError("MEM05 work capture has unknown owners")
        for row in matrix:
            if (
                type(row) not in (tuple, list) or len(row) != len(Kind)
                or any(type(value) is not int or not 0 <= value < 1 << 64 for value in row)
            ):
                raise ValueError("MEM05 work capture has an invalid primitive count")


def _work_limits(case):
    operation = case["operation"]
    q, s, g = case["selected"], case["words"], case["ranges"]
    p, l, f, r = case["frames"], case["indexed"], case["owned"], case["roots"]
    e = 1025 * r + 512 * (case["tables"] - r)
    if operation == "release-set":
        records, links, bitmap, proofs = 8 * q + 16, 8 * q + 16, 8 * s + 4 * q + 16, 8 * q + 8
    elif operation in ("count-process", "release-process"):
        records, links, bitmap, proofs = 4 * l + 16, 4 * l + 8, 2 * l + 8, 2
    elif operation in ("release-page", "release-scope"):
        records, links, bitmap, proofs = 16 * q + 32, 8 * q + 16, 16 * s + 8 * q + 32, 16 * q + 16
    elif operation == "destroy":
        records = 16 * f + 8 * l + 32
        links, bitmap, proofs = 8 * (f + l) + 16, 16 * s + 8 * (f + l) + 32, 16 * (f + l) + 16
    else:
        records, links = 16 * p + 32 * f + 64 * r + 256, 8 * p + 8 * f + 32
        bitmap, proofs = 16 * ((p + 63) // 64) + 16 * s + 8 * f + 64, 16 * (f + r) + 16

    handoff = operation in ("prepare-handoff", "complete-handoff")
    page = operation in ("release-page", "release-scope")
    z = max(0, q - 1) if page else f if operation == "destroy" else 0
    if handoff:
        indices = 32 * p + 128 * (g + 1) * (f + 1) + 8 * e + 64 * 64 + 16 * 128 + 1024
        ptes, clearing = 2 * e + 16 * f + 64 * r + 32, 64 * s + 16 * 64 + 512 * (r + 1)
    elif page or operation == "destroy":
        indices = 4096 * z + 128 * (g + 1) * (f + l + q + 1) + 8 * e + 1024
        ptes, clearing = 1027 if page else 2 * e + 16 * f + 32, 4096 * z + 64 * s + 512
    else:
        indices, ptes, clearing = 512 * (q + s + l + 1) + 16 * g, 0, 64 * s + 512
    return {
        "records": records, "links": links, "bitmap": bitmap, "indices": indices,
        "ranges": 8 * (g + 1) * (proofs + 1), "ptes": ptes, "clearing": clearing,
        "memory_audits": r if handoff else int(operation == "destroy"),
        "object_audits": int(handoff),
        "thread_records": 8 * 64 + 2 * 128 + 32 * r + 256 if handoff else None,
    }


def work_failures(case, counts):
    _validate_work_inputs(case, counts)
    limits = _work_limits(case)
    failures = []
    memory = Owner.KERNEL_MEMORY

    def total(kind, owner=memory):
        value = sum(counts[channel][owner][kind] for channel in WORK_CHANNELS)
        if value >= 1 << 64:
            raise ValueError("MEM05 composed primitive count overflowed")
        return value

    def bound(name, observed, maximum):
        if observed > maximum:
            failures.append((name, observed, maximum))

    bound("owner-record-visits", counts["frames"][memory][Kind.RECORD_VISIT], limits["records"])
    bound("link-traversals", total(Kind.LINK_TRAVERSE), limits["links"])
    bound("bitmap-word-loads", total(Kind.BITMAP_WORD_LOAD), limits["bitmap"])
    bound("context-index-probes", counts["context"][memory][Kind.INDEX_PROBE], limits["indices"])
    bound("active-geometry", total(Kind.RANGE_VISIT), limits["ranges"])
    bound("pte-visits", total(Kind.PTE_VISIT), limits["ptes"])
    bound("cleared-bytes", total(Kind.CLEARED_BYTE), limits["clearing"])
    bound("clearing-units", total(Kind.CLEAR_UNIT), limits["clearing"])
    if limits["thread_records"] is not None:
        bound("process-thread-record-visits", counts["context"][memory][Kind.RECORD_VISIT], limits["thread_records"])
    for owner in Owner:
        expected = (
            limits["memory_audits"] if owner == memory
            else limits["object_audits"] if owner in (Owner.KERNEL_OBJECTS, Owner.KERNEL_SCHEDULER)
            else 0
        )
        observed = total(Kind.COMPLETE_VALIDATOR_CALL, owner)
        if observed != expected:
            failures.append((owner.name + "-complete-audits", observed, expected))
    return tuple(failures)


TARGET_FIELDS = {
    "MEM04_TABLE": (1, (
        "case", "lookup", "activation", "before_satp", "after_satp",
        "poison_unchanged", "state_unchanged", "outputs_unchanged",
    )),
    "MEM04_ACTIVATION": (2, (
        "case", "diagnostic", "activation", "destroy", "before_satp",
        "after_satp", "process_satp", "restored_satp", "state_unchanged",
    )),
    "MEM04_HANDOFF": (1, (
        "diagnostic", "translated", "before_satp", "after_satp",
        "poison_unchanged", "state_unchanged", "outputs_unchanged",
    )),
    "MEM05_TABLE": (1, (
        "case", "release", "destroy", "before_satp", "after_satp",
        "poison_unchanged", "state_unchanged", "output_unchanged", "cleanup_complete",
    )),
    "MEM05_RELEASE": (1, (
        "case", "diagnostic", "release", "before_satp", "after_satp", "frame", "output",
        "selected_before", "selected_after", "owner_before", "owner_after",
        "state_unchanged", "effect_matches", "unrelated_unchanged", "data_unchanged", "cleanup_complete",
    )),
}


def _target_record(line):
    match = re.fullmatch(r"# (MEM0[45]_[A-Z_]+) version=([0-9]+) (.*)", line)
    if match is None or match[1] not in TARGET_FIELDS:
        raise ValueError("MEM05 target emitted an unknown semantic witness")
    version, fields = TARGET_FIELDS[match[1]]
    pairs = [re.fullmatch(r"([a-z_]+)=(0x[0-9a-f]{16})", field) for field in match[3].split(" ")]
    if (
        match[2] != str(version) or any(pair is None for pair in pairs)
        or tuple(pair[1] for pair in pairs) != fields
    ):
        raise ValueError("MEM05 target witness has changed or duplicate fields")
    row = {pair[1]: int(pair[2], 16) for pair in pairs}
    if any(value not in (0, 1) for name, value in row.items()
           if name.endswith("_unchanged") or name in ("effect_matches", "cleanup_complete")):
        raise ValueError("MEM05 target witness has a non-Boolean predicate")
    return match[1], row


def parse_target_output(output, workflow, stage, exit_code):
    if (
        type(output) is not str or type(stage) is not Stage or type(exit_code) is not int
        or workflow not in ("test-qemu-user-address-space", "test-qemu-address-space-handoff")
    ):
        raise ValueError("MEM05 target capture has an unknown workflow or stage")
    rows, positions = [], []
    lines = output.splitlines(keepends=True)
    for index, line in enumerate(lines):
        if not line.startswith(("# MEM04_", "# MEM05_")):
            continue
        if not line.endswith("\n"):
            raise ValueError("MEM05 target witness is unterminated")
        rows.append(_target_record(line.rstrip("\r\n")))
        positions.append(index)
    user = workflow == "test-qemu-user-address-space"
    expected = (
        tuple(("MEM04_TABLE", index) for index in range(2))
        + tuple(("MEM04_ACTIVATION", index) for index in range(5))
        + tuple(("MEM05_TABLE", index) for index in range(2))
        + tuple(("MEM05_RELEASE", index) for index in range(4 if stage is Stage.RED else 5))
        if user else (("MEM04_HANDOFF", None),)
    )
    if tuple((name, row.get("case")) for name, row in rows) != expected:
        raise ValueError("MEM05 target omitted, duplicated or reordered required witnesses")
    panics = [
        (index, line.rstrip("\r\n").removeprefix("# MICROS_PANIC reason="))
        for index, line in enumerate(lines) if line.startswith("# MICROS_PANIC reason=")
    ]
    marker = "MICROS_USER_ADDRESS_SPACE_TEST_PASS" if user else "MICROS_ADDRESS_SPACE_HANDOFF_TEST_PASS"
    passes = [index for index, line in enumerate(lines) if re.fullmatch(
        r"# " + marker + r" .+", line.rstrip("\r\n"),
    )]
    if user and stage is Stage.RED:
        if (
            exit_code not in (1, 2) or len(panics) != 1
            or panics[0][1] != "user-address-space-test" or passes
            or positions[-1] >= panics[0][0] or target_failures(rows, baseline=True)
        ):
            raise ValueError("MEM05 target Red lacks its actual witnessed release mismatch")
    else:
        if exit_code != 0 or panics or len(passes) != 1 or positions[-1] >= passes[0]:
            raise ValueError("MEM05 target lacks complete original acceptance")
        if not user and len(re.findall(r"^# MICROS_GRANT_SYSCALL_HANDOFF_PASS .+$", output, re.M)) != 1:
            raise ValueError("MEM05 handoff target omitted its original grant boundary")
    return tuple(rows)


def target_failures(rows, *, baseline=False):
    failures = []
    before = rows[0][1]["before_satp"] if rows else 0
    for name, row in rows:
        expected = {
            key: 1 for key in row
            if key.endswith("_unchanged") or key in ("effect_matches", "cleanup_complete")
        }
        expected["before_satp"] = before
        if row["before_satp"] >> 44 != 8 << 16:
            failures.append((name, row.get("case"), "kernel-satp-mode", row["before_satp"], 8 << 60))
        if name == "MEM04_TABLE":
            expected.update(lookup=11, activation=11, after_satp=before)
        elif name == "MEM04_ACTIVATION":
            diagnostic = 12 if row["case"] == 2 else 11
            expected.update(
                diagnostic=diagnostic, activation=0, destroy=diagnostic,
                after_satp=row["process_satp"], restored_satp=before,
            )
            if row["process_satp"] == before or row["process_satp"] >> 44 != 8 << 16:
                failures.append((name, row["case"], "distinct-selected-root", row["process_satp"], before))
        elif name == "MEM04_HANDOFF":
            expected.update(diagnostic=12, translated=10, after_satp=before)
        elif name == "MEM05_TABLE":
            expected.update(release=11, destroy=11, after_satp=before)
        elif name == "MEM05_RELEASE":
            case = row["case"]
            result = (11, 11, 12, 11 if baseline else 10, 0)[case]
            expected.update(
                diagnostic=12 if case == 2 else 11, release=result, after_satp=before,
                output=row["frame"] if result == 0 else 0xfeedfacefeedface,
                selected_after=0 if result == 0 else row["selected_before"],
                owner_before=5, owner_after=0 if result == 0 else 5,
                state_unchanged=int(result != 0),
            )
            if row["frame"] == 0 or row["frame"] % 4096:
                failures.append((name, case, "frame-alignment", row["frame"] % 4096, 0))
            if case == 3:
                expected["selected_before"] = 0
            elif row["selected_before"] == 0:
                failures.append((name, case, "present-selected-pte", 0, 1))
            if case == 4:
                physical = ((row["selected_before"] >> 10) & ((1 << 44) - 1)) << 12
                if physical != row["frame"]:
                    failures.append((name, case, "selected-frame", physical, row["frame"]))
        else:
            raise ValueError("MEM05 target semantics have an unknown record")
        for key, value in expected.items():
            if row[key] != value:
                failures.append((name, row.get("case"), key, row[key], value))
    return tuple(failures)


def _json_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("MEM05 record repeats a field")
        result[key] = value
    return result


def _expected_result(row, stage):
    operation, pattern = row["operation"], row["pattern"]
    result, output = 0, (1 << 64) - 1
    if operation == "count-process":
        output = 0 if pattern.endswith("foreign") else row["indexed"] // 2 if pattern.endswith("mixed") else row["indexed"]
    elif operation == "release-process":
        result = 9 if row["indexed"] else 0
    elif operation == "release-page":
        result, output = (10, output) if pattern == "absent" else (0, 4)
    elif operation == "release-scope":
        result = 12 if pattern == "malformed" else 11
        if stage is Stage.GREEN:
            if pattern == "absent-orphan":
                result = 10
            elif pattern == "alias":
                result, output = 0, 4
    return result, output


def parse_native_output(output, stage, channel):
    channels = ("aggregate", *WORK_CHANNELS, "ordinary")
    if type(output) is not str or type(stage) is not Stage or channel not in channels:
        raise ValueError("MEM05 native parser has an unadmitted stage/channel")
    lines = output.splitlines()
    if (
        not output.endswith("\n") or len(lines) < 2
        or lines[0] != "MICROS_MEM05_NATIVE version=1 stage=" + stage.value + " channel=" + channel
        or lines[-1] != "MICROS_MEM05_NATIVE_PASS cases=128"
    ):
        raise ValueError("MEM05 native output lacks its complete execution envelope")
    cases = expected_cases()
    guards = expected_guards() if stage is Stage.GREEN else ()
    observed = channel != "ordinary"
    width = 2 if observed else 1
    if len(cases) != 128 or len(lines) != 2 + 128 * width + len(guards):
        raise ValueError("MEM05 native execution omitted or added a common or current-only guard case")
    rows, observations = [], []
    for index, expected in enumerate(cases):
        line = lines[1 + index * width]
        prefix = "MICROS_MEM05_CASE "
        if not line.startswith(prefix):
            raise ValueError("MEM05 ordinary output has an unknown or observed record")
        row = json.loads(line[len(prefix):], object_pairs_hook=_json_pairs)
        if (
            type(row) is not dict or set(row) != set(expected) | {"result", "output", "state"}
            or any(type(row[key]) is not type(value) or row[key] != value for key, value in expected.items())
            or any(type(row[key]) is not int or not 0 <= row[key] < 1 << 64 for key in ("result", "output"))
            or type(row["state"]) is not bool or not row["state"]
        ):
            raise ValueError("MEM05 native case differs from its fixed inputs or complete state check")
        if (row["result"], row["output"]) != _expected_result(row, stage):
            raise ValueError("MEM05 native case lost its independently declared phase semantics")
        rows.append(row)
        if observed:
            prefix = "MICROS_MEM05_COUNTS "
            line = lines[2 + index * width]
            if not line.startswith(prefix):
                raise ValueError("MEM05 observed case has no complete counter snapshot")
            value = json.loads(line[len(prefix):], object_pairs_hook=_json_pairs)
            if type(value) is not dict or set(value) != {
                "version", "reserved0", "producer_label", "capture_label", "owner_mask", "reserved1", "counts",
            } or type(value["counts"]) is not list:
                raise ValueError("MEM05 counter snapshot has an unknown schema")
            if any(type(vector) is not list for vector in value["counts"]):
                raise ValueError("MEM05 counter snapshot lacks complete owner vectors")
            value["counts"] = tuple(tuple(vector) for vector in value["counts"])
            snapshot = Snapshot(**value)
            producer = 0x4d454d3035000000 + (256 if stage is Stage.GREEN else 0) + channels.index(channel) + 1
            compose_snapshots(
                (Capture(Status.OK, snapshot),), capture_label=expected["case"],
                expected_producers={producer: 11900},
            )
            observations.append(value)
    guarded = []
    for index, expected in enumerate(guards):
        line = lines[1 + 128 * width + index]
        prefix = "MICROS_MEM05_GUARD "
        if not line.startswith(prefix):
            raise ValueError("MEM05 current-only execution lacks its ordered guard record")
        row = json.loads(line[len(prefix):], object_pairs_hook=_json_pairs)
        if (
            type(row) is not dict or set(row) != set(expected) | {"state"}
            or any(type(row[key]) is not type(value) or row[key] != value for key, value in expected.items())
            or type(row["state"]) is not bool or not row["state"]
        ):
            raise ValueError("MEM05 guard changed its declared case/result or lost complete state preservation")
        guarded.append(row)
    return {"rows": tuple(rows), "observations": tuple(observations), "guards": tuple(guarded)}


def _parsed_failures(validated, stage):
    channels = ("aggregate", *WORK_CHANNELS, "ordinary")
    if type(stage) is not Stage or set(validated) != set(channels):
        raise ValueError("MEM05 parsed work lacks its exact phase/product set")
    for channel, parsed in validated.items():
        if (
            parsed["rows"] != validated["ordinary"]["rows"]
            or parsed["guards"] != validated["ordinary"]["guards"]
            or len(parsed["observations"]) != (0 if channel == "ordinary" else 128)
        ):
            raise ValueError("MEM05 observations are not passive complete same-phase executions")
    producer_base = 0x4d454d3035000000 + (256 if stage is Stage.GREEN else 0)
    declared_producers = {producer_base + channels.index(channel) + 1: 11900 for channel in WORK_CHANNELS}
    failures = {"owner-index": [], "selected-release": [], "wired-handoff": []}
    for index, row in enumerate(validated["ordinary"]["rows"]):
        snapshots = [Snapshot(**validated[channel]["observations"][index]) for channel in WORK_CHANNELS]
        composed = compose_snapshots(
            tuple(Capture(Status.OK, value) for value in snapshots),
            capture_label=row["case"],
            expected_producers=declared_producers,
        )
        if composed.per_owner != validated["aggregate"]["observations"][index]["counts"]:
            raise ValueError("MEM05 disjoint channels do not compose the actual aggregate")
        operation = row["operation"]
        group = (
            "owner-index" if operation in ("release-set", "count-process", "release-process")
            else "wired-handoff" if operation in ("prepare-handoff", "complete-handoff")
            else "selected-release"
        )
        counts = {channel: validated[channel]["observations"][index]["counts"] for channel in WORK_CHANNELS}
        failures[group].extend((row["case"], *failure) for failure in work_failures(
            {key: row[key] for key in CASE_FIELDS}, counts,
        ))
        expected = _expected_result(row, Stage.GREEN)
        if (row["result"], row["output"]) != expected:
            failures[group].append((row["case"], "selected-semantics", (row["result"], row["output"]), expected))
    return {key: tuple(value) for key, value in failures.items()}


def check_native_products(root, executable_paths):
    from tests.host.selected_release_handoff_source import (
        CHANNELS, RECIPE_ADMISSION, measure_audit_boundaries,
    )
    from tools.generate_selected_release_handoff_fixture import source_bundle

    root = Path(root).resolve()
    expected = tuple(root / "build/host-debug" / name for name in RECIPE_ADMISSION["native_targets"])
    if tuple(map(Path, executable_paths)) != expected:
        raise ValueError("MEM05 CTest requires its five exact declared products")
    identities = {str(path): read_regular(path)[1] for path in expected}
    fixtures = {path: read_regular(root / path)[1] for path in NATIVE_FIXTURE_PATHS}
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
            raise ValueError("MEM05 CTest generated inputs differ from actual current source")
        generated[channel], inputs[channel] = emitted, source_inputs
        symbols = subprocess.check_output((nm["path"], "-a", str(executable)), cwd=root, text=True, timeout=30)
        observed_symbols = tuple(sorted({
            line.split()[-1].lstrip("_") for line in symbols.splitlines()
            if line.split() and "micros_test_work_" in line.split()[-1]
        }))
        wanted = () if channel == "ordinary" else ("micros_test_work_begin", "micros_test_work_finish", "micros_test_work_record")
        if observed_symbols != wanted:
            raise ValueError("MEM05 CTest product has incorrect collector linkage")
        completed = subprocess.run(
            (str(executable), "green", channel), cwd=root, stdin=subprocess.DEVNULL,
            capture_output=True, text=True, timeout=60,
        )
        if completed.returncode or completed.stderr:
            raise ValueError("MEM05 CTest product failed its semantic capture:\n" + completed.stdout + completed.stderr)
        measured[channel] = parse_native_output(completed.stdout, Stage.GREEN, channel)
    failures = _parsed_failures(measured, Stage.GREEN)
    measure_audit_boundaries(root, Stage.GREEN)
    if any(failures.values()):
        raise ValueError("MEM05 CTest failed semantics/work or retained diagnostics: " + json.dumps(failures))
    if (
        {path: read_regular(root / path)[1] for path in NATIVE_FIXTURE_PATHS} != fixtures
        or native_symbols_tool() != nm or any(read_regular(path)[1] != identities[str(path)] for path in expected)
    ):
        raise ValueError("MEM05 CTest source, fixture, tool or product changed during execution")
    for channel in CHANNELS:
        directory = root / RECIPE_ADMISSION["generator_output"] / channel
        if {path: read_regular(root / path)[1] for path in inputs[channel]} != inputs[channel] or {
            path: read_regular(directory / path)[1] for path in generated[channel]
        } != generated[channel]:
            raise ValueError("MEM05 CTest source or generated inputs changed during execution")
    return 0
