"""Actual MEM05 work, semantic and target evidence."""

from dataclasses import asdict
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

from tests.host.runtime_memory_evidence import native_compiler
from tests.host.evidence_authority_types import digest
from tests.host.selected_address_space_evidence import native_symbols_tool
from tests.host.selected_release_handoff_source import SOURCE_ROLES
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
    *SOURCE_ROLES["immutable_additions"], "tests/work/work_counts.c", "tests/work/work_counts.h",
    "tests/host/selected_address_space_source.py", "tests/host/selected_ownership_source.py",
    "tests/host/c_source_records.py", "tests/host/work_count_lexical.py",
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


def native_command(root, compiler, generated, executable, stage, channel):
    from tools.generate_selected_release_handoff_fixture import native_source_members

    channels = ("aggregate", *WORK_CHANNELS, "ordinary")
    if type(stage) is not Stage or channel not in channels:
        raise ValueError("MEM05 native command has an unknown stage/channel")
    return (
        compiler["path"], "-std=c17", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
        "-Wmissing-prototypes", "-Wstrict-prototypes", "-fno-builtin",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
        "-DMICROS_BUILD_USER_ADDRESS_SPACE_TEST=1",
        *(("-DMICROS_SELECTED_RELEASE_HANDOFF_BASELINE=1",) if stage is Stage.RED else ()),
        *(("-DMICROS_TEST_WORK_COUNTS=1", "-DMICROS_MEM05_NATIVE_CHANNEL=" + str(channels.index(channel)))
          if channel != "ordinary" else ()),
        "-I", str(generated), "-I", str(generated / "include"),
        "-I", str(root), "-I", str(root / "include"),
        *(str(generated / path) for path in native_source_members(str(root))),
        str(root / "tests/host/selected_release_handoff_test.c"),
        *((str(root / "tests/work/work_counts.c"),) if channel != "ordinary" else ()),
        "-o", str(executable),
    )


def _validate_fixture_syntax(root, sources, observation_modes):
    from tests.host import selected_release_handoff_source as source
    from tools.generate_selected_release_handoff_fixture import fixed_inputs

    root = Path(root).resolve()
    fixtures = (
        *sources, "tests/host/selected_release_handoff_test.h",
        "tests/host/bootstrap_allocation_test.h", "tests/host/frame_ownership_test_internal.h",
        "tests/host/selected_address_space_test.h", "tests/host/vm_bootstrap_test_internal.h",
        source.SOURCE_PATH, "tests/host/selected_release_handoff_evidence.py",
        "tools/generate_selected_release_handoff_fixture.py",
    )
    before = {path: read_regular(root / path)[1] for path in fixtures}
    compiler = native_compiler(root)
    interfaces = {path: source.declared_header(root, path).encode() for path in source.HEADER_PATHS}
    commands = []
    with tempfile.TemporaryDirectory(prefix="micros-mem05-interface-") as temporary:
        directory = Path(temporary).resolve()
        for path, (raw, _) in fixed_inputs(str(root)).items():
            if not path.endswith(".h"):
                continue
            target = directory / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(interfaces.get(path, raw))
        for observed in observation_modes:
            launcher = sources == ("tests/host/selected_release_handoff_test.c",) and not observed
            command = (
                compiler["path"], "-std=c17", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                "-Wmissing-prototypes", "-Wstrict-prototypes", "-fsyntax-only",
                "-DMICROS_BUILD_USER_ADDRESS_SPACE_TEST=1",
                *(("-DMICROS_TEST_WORK_COUNTS=1", "-DMICROS_MEM05_NATIVE_CHANNEL=0") if observed else ()),
                *((
                    "-DMICROS_SELECTED_RELEASE_HANDOFF_LAUNCHER=1",
                    '-DMICROS_SELECTED_RELEASE_HANDOFF_PYTHON="' + sys.executable + '"',
                    '-DMICROS_SELECTED_RELEASE_HANDOFF_DRIVER="' + str(root / "tools/check_selected_release_handoff.py") + '"',
                    '-DMICROS_SELECTED_RELEASE_HANDOFF_ROOT="' + str(root) + '"',
                ) if launcher else ()),
                "-I", str(directory), "-I", str(directory / "include"),
                "-I", str(root), "-I", str(root / "include"),
                *(str(root / path) for path in sources),
            )
            result = subprocess.run(
                command, cwd=root, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30,
            )
            if result.returncode or result.stdout or result.stderr:
                raise ValueError("MEM05 declared-interface syntax failed:\n" + result.stdout + result.stderr)
            commands.append(command)
    if {path: read_regular(root / path)[1] for path in fixtures} != before or native_compiler(root) != compiler:
        raise ValueError("MEM05 declared-interface inputs changed during syntax validation")
    return {
        "commands": tuple(commands), "compiler": compiler, "fixtures": before,
        "interfaces": {path: hashlib.sha256(raw).hexdigest() for path, raw in interfaces.items()},
    }


def validate_current_fixture_syntax(root):
    return _validate_fixture_syntax(
        root, ("tests/host/selected_release_handoff_test.c",), (False, True),
    )


def validate_legacy_fixture_syntax(root):
    return _validate_fixture_syntax(root, (
        "tests/host/bootstrap_allocation_test.c",
        "tests/host/frame_ownership_test_fixture.c",
        "tests/host/frame_ownership_test_bootstrap.c",
        "tests/host/frame_ownership_test_handoff.c",
        "tests/host/frame_ownership_test_model.c",
        "tests/host/frame_ownership_test_validation.c",
        "tests/host/vm_bootstrap_test_fixture.c",
        "tests/host/selected_address_space_test.c",
    ), (False,))


def measure_native_channel(root, stage, channel):
    from tools.generate_selected_release_handoff_fixture import prepare_sources, source_bundle

    root = Path(root).resolve()
    fixtures = NATIVE_FIXTURE_PATHS
    fixture_inputs = {path: read_regular(root / path)[1] for path in fixtures}
    compiler, nm = native_compiler(root), native_symbols_tool()
    with tempfile.TemporaryDirectory(prefix="micros-mem05-native-") as temporary:
        directory = Path(temporary).resolve()
        generated, executable = directory / "source", directory / "native"
        inputs, outputs = prepare_sources(root, generated, stage, channel)
        command = native_command(root, compiler, generated, executable, stage, channel)
        build = subprocess.run(
            command, cwd=root, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=180,
        )
        if build.returncode or build.stderr:
            raise ValueError("MEM05 native compilation failed:\n" + build.stdout + build.stderr)
        executable_identity = read_regular(executable)[1]
        nm_command = (nm["path"], "-a", str(executable))
        symbols = subprocess.check_output(nm_command, cwd=root, text=True, timeout=30)
        observation_symbols = tuple(sorted({
            line.split()[-1].lstrip("_") for line in symbols.splitlines()
            if line.split() and "micros_test_work_" in line.split()[-1]
        }))
        expected_symbols = () if channel == "ordinary" else (
            "micros_test_work_begin", "micros_test_work_finish", "micros_test_work_record",
        )
        if observation_symbols != expected_symbols:
            raise ValueError("MEM05 executable has incorrect collector linkage")
        arguments = (str(executable), stage.value, channel)
        completed = subprocess.run(
            arguments, cwd=root, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=60,
        )
        if completed.returncode or completed.stderr:
            raise ValueError("MEM05 native semantic capture failed:\n" + completed.stdout + completed.stderr)
        parsed = parse_native_output(completed.stdout, stage, channel)
        if (
            {path: read_regular(root / path)[1] for path in fixtures} != fixture_inputs
            or native_compiler(root) != compiler or native_symbols_tool() != nm
            or read_regular(executable)[1] != executable_identity
            or source_bundle(root, stage, channel)[0] != inputs
            or {path: read_regular(generated / path)[1] for path in outputs} != outputs
        ):
            raise ValueError("MEM05 native source, fixture, tool or artifact changed during execution")
        return {
            "version": 1, "stage": stage.value, "channel": channel,
            "source_inputs": inputs, "generated_inputs": outputs, "fixture_inputs": fixture_inputs,
            "compiler": compiler, "nm": nm, "command": command, "arguments": arguments,
            "executable": executable_identity, "build_stdout": build.stdout, "build_stderr": build.stderr,
            "build_exit": build.returncode, "execution_exit": completed.returncode,
            "nm_command": nm_command, "nm_output": symbols, "nm_exit": 0,
            "stdout": completed.stdout, "stderr": completed.stderr,
            "output_sha256": hashlib.sha256(completed.stdout.encode()).hexdigest(),
            "observation_symbols": observation_symbols, **parsed,
        }


def replacement_failures(measured):
    channels = ("aggregate", *WORK_CHANNELS, "ordinary")
    if type(measured) is not dict or set(measured) != set(channels):
        raise ValueError("MEM05 replacement evidence lacks an actual native product")
    ordinary = measured["ordinary"]
    if type(ordinary["stage"]) is not str or ordinary["stage"] not in ("red", "green"):
        raise ValueError("MEM05 replacement evidence has an unknown source stage")
    validated = {}
    for channel, result in measured.items():
        if result["channel"] != channel or any(
            result[key] != ordinary[key]
            for key in ("stage", "source_inputs", "fixture_inputs", "compiler", "nm", "rows", "guards")
        ):
            raise ValueError("MEM05 slices differ in source, tools, fixtures or actual semantics")
        if (
            any(type(result[name]) is not int or result[name] != 0 for name in ("build_exit", "execution_exit"))
            or result["build_stderr"] != "" or result["stderr"] != ""
            or hashlib.sha256(result["stdout"].encode()).hexdigest() != result["output_sha256"]
        ):
            raise ValueError("MEM05 slice lacks its actual successful compilation/execution")
        parsed = parse_native_output(result["stdout"], Stage(ordinary["stage"]), channel)
        if any(digest(parsed[key]) != digest(result[key]) for key in ("rows", "observations", "guards")):
            raise ValueError("MEM05 derived capture does not match its actual channel output")
        validated[channel] = parsed
        count = 0 if channel == "ordinary" else 128
        if len(result["observations"]) != count:
            raise ValueError("MEM05 native slice omitted a capture")
    if len(ordinary["rows"]) != 128:
        raise ValueError("MEM05 replacement evidence omitted a semantic case")
    return _parsed_failures(validated, Stage(ordinary["stage"]))


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


def measure_native(root, stage):
    channels = ("aggregate", *WORK_CHANNELS, "ordinary")
    measured = {channel: measure_native_channel(root, stage, channel) for channel in channels}
    replacement_failures(measured)
    return measured


def _target_paths(workflow):
    from tests.host.selected_release_handoff_source import STATE_WORK_CONTRACT

    if workflow not in STATE_WORK_CONTRACT["target_workflows"]:
        raise ValueError("MEM05 target path belongs to an undeclared workflow")
    return "build/riscv64-" + workflow.removeprefix("test-qemu-") + "-test/kernel/micros.elf"


def _target_inputs(root):
    from tests.host.selected_release_handoff_source import SOURCE_ROLES

    return {
        path: read_regular(Path(root) / path)[1]
        for path in (*SOURCE_ROLES["production_paths"], *SOURCE_ROLES["dependent_target_paths"])
    }


def measure_targets(root, stage):
    from tests.host.selected_release_handoff_source import STATE_WORK_CONTRACT

    if stage not in (Stage.RED, Stage.GREEN):
        raise ValueError("MEM05 target execution has an unknown stage")
    root = Path(root).resolve()
    result = {}
    for workflow in STATE_WORK_CONTRACT["target_workflows"]:
        inputs = _target_inputs(root)
        command = ("cmake", "--workflow", "--preset", workflow)
        print("MICROS_MEM05_TARGET_BEGIN " + workflow, flush=True)
        completed = subprocess.run(
            command, cwd=root, stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=600,
        )
        try:
            rows = parse_target_output(completed.stdout, workflow, stage, completed.returncode)
            if stage is Stage.RED and target_failures(rows, baseline=True):
                raise ValueError("MEM05 baseline target lost an independent preservation control")
            kernel = _target_paths(workflow)
            result[workflow] = {
                "version": 1, "stage": stage.value, "command": command, "exit_code": completed.returncode,
                "output": completed.stdout, "source_inputs": inputs, "kernel_path": kernel,
                "kernel": read_regular(root / kernel)[1],
                "rows": rows, "failures": target_failures(rows),
            }
            if _target_inputs(root) != inputs:
                raise ValueError("MEM05 target inputs changed during actual execution")
        except (OSError, ValueError):
            sys.stderr.write(completed.stdout)
            raise
        print("MICROS_MEM05_TARGET_CAPTURED " + workflow, flush=True)
    return result


def validate_targets(targets, stage):
    from tests.host.diagnostic_history_authority import record_fields
    from tests.host.selected_release_handoff_source import STATE_WORK_CONTRACT

    if type(stage) is not Stage or type(targets) is not dict or set(targets) != set(STATE_WORK_CONTRACT["target_workflows"]):
        raise ValueError("MEM05 target execution omitted or added a workflow")
    for workflow, value in targets.items():
        record_fields(value, (
            "version", "stage", "command", "exit_code", "output", "source_inputs",
            "kernel_path", "kernel", "rows", "failures",
        ))
        if (
            type(value["version"]) is not int or value["version"] != 1
            or value["stage"] != stage.value
            or tuple(value["command"]) != ("cmake", "--workflow", "--preset", workflow)
            or value["kernel_path"] != _target_paths(workflow)
        ):
            raise ValueError("MEM05 target evidence substituted its source stage or command")
        rows = parse_target_output(value["output"], workflow, stage, value["exit_code"])
        if digest(value["rows"]) != digest(rows) or digest(value["failures"]) != digest(target_failures(rows)):
            raise ValueError("MEM05 target derived results differ from the actual output")
        if stage is Stage.RED and target_failures(rows, baseline=True):
            raise ValueError("MEM05 target failure is not its declared original divergence")
    return targets


def _require_discriminating_red(measured, failures):
    cases = expected_cases()

    def witnessed(group, operation, metric, predicate):
        return any(
            row[1] == metric and cases[row[0] - 1]["operation"] == operation and predicate(cases[row[0] - 1])
            for row in failures[group]
        )

    controls = (
        witnessed("owner-index", "release-set", "bitmap-word-loads", lambda case: case["selected"] == 0),
        witnessed("owner-index", "count-process", "owner-record-visits",
                  lambda case: case["frames"] == 262144 and case["indexed"] <= 16),
        witnessed("selected-release", "destroy", "owner-record-visits",
                  lambda case: case["frames"] == 262144 and case["pattern"] == "one"),
        witnessed("selected-release", "release-scope", "selected-semantics",
                  lambda case: case["pattern"] == "absent-orphan"),
        witnessed("wired-handoff", "prepare-handoff", "owner-record-visits", lambda case: case["roots"] >= 16),
        witnessed("wired-handoff", "prepare-handoff", "process-thread-record-visits", lambda case: case["roots"] == 64),
    )
    if measured["ordinary"]["stage"] != "red" or not all(controls):
        raise ValueError("MEM05 actual fixed work did not expose every declared release/count/destruction/handoff divergence")


class SelectedReleaseHandoffOracles(unittest.TestCase):
    def test_owner_index(self):
        self.assertFalse(self.work["owner-index"], repr(self.work["owner-index"][:12]))

    def test_selected_release(self):
        self.assertFalse(self.work["selected-release"], repr(self.work["selected-release"][:12]))
        self.assertFalse(self.targets["test-qemu-user-address-space"]["failures"])
        self.assertFalse(self.audits["migrated"])

    def test_wired_handoff(self):
        self.assertFalse(self.work["wired-handoff"], repr(self.work["wired-handoff"][:12]))
        self.assertFalse(self.targets["test-qemu-address-space-handoff"]["failures"])
        self.assertFalse(self.audits["migrated"])


def run_oracles(root, measured, targets, audits, reference=None):
    from tests.host.evidence_authority_types import ObservedResult, OracleResult, RunStatus
    from tests.host.selected_release_handoff_authority import ORACLE_RULES
    from tests.host.selected_release_handoff_source import measure_audit_boundaries

    failures = replacement_failures(measured)
    stage = Stage(measured["ordinary"]["stage"])
    validate_targets(targets, stage)
    if stage is Stage.GREEN:
        if reference is None:
            raise ValueError("MEM05 current execution lacks its same-attempt actual baseline")
        _require_discriminating_red(reference, replacement_failures(reference))
    else:
        if reference is not None:
            raise ValueError("MEM05 baseline execution cannot borrow another reference")
        _require_discriminating_red(measured, failures)
        if tuple(map(tuple, targets["test-qemu-user-address-space"]["failures"])) != (
            ("MEM05_RELEASE", 3, "release", 11, 10),
        ):
            raise ValueError("MEM05 baseline target did not witness only the owned selected-release mismatch")
    if digest(audits) != digest(measure_audit_boundaries(root, stage)):
        raise ValueError("MEM05 execution substituted its complete retained/migrated/plan caller census")
    outcomes, output = [], io.StringIO()
    for identifier, test, property_name, _, _, _ in ORACLE_RULES:
        case = SelectedReleaseHandoffOracles(test.rsplit(".", 1)[1])
        case.work, case.targets, case.audits = failures, targets, audits
        result = unittest.TextTestRunner(stream=output, verbosity=2).run(unittest.TestSuite((case,)))
        if result.testsRun != 1 or result.errors or result.skipped:
            raise ValueError("MEM05 oracle was skipped or failed outside its measured assertion")
        if stage is Stage.RED:
            if result.wasSuccessful():
                raise ValueError("MEM05 fixed execution did not expose every owned divergence")
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
        raise ValueError("MEM05 execution lacks a complete ordinary artifact identity")


def _validate_native_replay(context, measured, stage):
    from tests.host.diagnostic_history_authority import record_fields, record_strings
    from tools.generate_selected_release_handoff_fixture import source_bundle

    channels = ("aggregate", *WORK_CHANNELS, "ordinary")
    if type(measured) is not dict or set(measured) != set(channels):
        raise ValueError("MEM05 native replay omitted a product")
    directories = set()
    for channel, native in measured.items():
        record_fields(native, (
            "version", "stage", "channel", "source_inputs", "generated_inputs", "fixture_inputs",
            "compiler", "nm", "command", "arguments", "executable",
            "build_stdout", "build_stderr", "build_exit", "execution_exit",
            "nm_command", "nm_output", "nm_exit", "stdout", "stderr", "output_sha256",
            "observation_symbols", "rows", "observations", "guards",
        ))
        if (
            type(native["version"]) is not int or native["version"] != 1
            or native["stage"] != stage.value or native["channel"] != channel
            or native["compiler"] != context.native_compiler or native["nm"] != context.native_nm
        ):
            raise ValueError("MEM05 native replay borrowed another source stage or tool")
        arguments = record_strings(native["arguments"])
        if len(arguments) != 3 or arguments[1:] != (stage.value, channel):
            raise ValueError("MEM05 native replay changed its execution stage/channel")
        executable = Path(arguments[0])
        directory = executable.parent
        if (
            not executable.is_absolute() or executable.name != "native"
            or not directory.name.startswith("micros-mem05-native-") or directory in directories
            or directory.parent.resolve() != Path(tempfile.gettempdir()).resolve()
        ):
            raise ValueError("MEM05 native replay names a borrowed or unowned execution")
        directories.add(directory)
        if (
            record_strings(native["command"]) != native_command(
                context.root, context.native_compiler, directory / "source", executable, stage, channel,
            )
            or record_strings(native["nm_command"]) != (context.native_nm["path"], "-a", str(executable))
        ):
            raise ValueError("MEM05 native replay changed its exact compile/link/symbol command")
        inputs, outputs = source_bundle(context.root, stage, channel)
        generated = {
            path: {"kind": "file", "mode": mode, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
            for path, (data, mode) in outputs.items()
        }
        if (
            native["source_inputs"] != inputs or native["generated_inputs"] != generated
            or native["fixture_inputs"] != {path: context.files[path] for path in NATIVE_FIXTURE_PATHS}
            or type(native["build_stdout"]) is not str
            or type(native["nm_output"]) is not str or not native["nm_output"]
            or type(native["nm_exit"]) is not int or native["nm_exit"] != 0
        ):
            raise ValueError("MEM05 native replay substituted a source/header/observation/fixture/symbol input")
        symbols = tuple(sorted({
            line.split()[-1].lstrip("_") for line in native["nm_output"].splitlines()
            if line.split() and "micros_test_work_" in line.split()[-1]
        }))
        wanted = () if channel == "ordinary" else (
            "micros_test_work_begin", "micros_test_work_finish", "micros_test_work_record",
        )
        if symbols != wanted or record_strings(native["observation_symbols"], empty=channel == "ordinary") != wanted:
            raise ValueError("MEM05 native replay changed its actual collector linkage")
        _validate_artifact_identity(native["executable"])
        if not native["executable"]["mode"] & 0o100:
            raise ValueError("MEM05 native replay artifact is not executable")
    return replacement_failures(measured)


def _validate_target_replay(context, targets, stage):
    from tests.host.diagnostic_history_authority import record_fields
    from tests.host.selected_release_handoff_source import fixed_source

    validate_targets(targets, stage)
    paths = (*SOURCE_ROLES["production_paths"], *SOURCE_ROLES["dependent_target_paths"])
    expected = {path: context.files[path] for path in paths}
    if stage is Stage.RED:
        for path in SOURCE_ROLES["production_paths"]:
            raw = fixed_source(str(context.root), path)
            expected[path] = {
                "kind": "file", "mode": context.files[path]["mode"], "size": len(raw),
                "sha256": hashlib.sha256(raw).hexdigest(),
            }
    for value in targets.values():
        record_fields(value, (
            "version", "stage", "command", "exit_code", "output", "source_inputs",
            "kernel_path", "kernel", "rows", "failures",
        ))
        if value["source_inputs"] != expected:
            raise ValueError("MEM05 target replay substituted its actual production/fixture source")
        _validate_artifact_identity(value["kernel"])
        if stage is Stage.GREEN and read_regular(context.root / value["kernel_path"])[1] != value["kernel"]:
            raise ValueError("MEM05 current target artifact changed after actual execution")


def validate_execution_log(context, replay, *, phase, attempt, reference=None):
    from tests.host.diagnostic_history_authority import record_fields
    from tests.host.selected_release_handoff_source import measure_audit_boundaries

    if phase not in ("red", "green", "recertify") or type(attempt) is not str or re.fullmatch(r"[a-f0-9]{32}", attempt) is None:
        raise ValueError("MEM05 replay has an unadmitted phase or attempt")
    record_fields(replay, ("version", "phase", "attempt", "native", "targets", "audits", "results", "oracle_output"))
    if (
        type(replay["version"]) is not int or replay["version"] != 1
        or replay["phase"] != phase or replay["attempt"] != attempt
        or type(replay["oracle_output"]) is not str or not replay["oracle_output"]
    ):
        raise ValueError("MEM05 replay belongs to a different phase or attempt")
    stage = Stage.RED if phase == "red" else Stage.GREEN
    _validate_native_replay(context, replay["native"], stage)
    _validate_target_replay(context, replay["targets"], stage)
    if stage is Stage.GREEN:
        if reference is None:
            raise ValueError("MEM05 current replay lacks its same-attempt fixed execution")
        _validate_native_replay(context, reference, Stage.RED)
    elif reference is not None:
        raise ValueError("MEM05 original replay cannot borrow a different reference")
    audits = measure_audit_boundaries(context.root, stage)
    if digest(replay["audits"]) != digest(audits):
        raise ValueError("MEM05 replay substituted its current retained/migrated/plan ledger")
    results, _ = run_oracles(context.root, replay["native"], replay["targets"], audits, reference)
    if replay["results"] != json.loads(json.dumps([asdict(result) for result in results])):
        raise ValueError("MEM05 replay outcomes do not follow the actual complete executions")
    return results


def check_native_products(root, executable_paths):
    from tests.host.selected_release_handoff_source import (
        CHANNELS, RECIPE_ADMISSION, declared_audit_boundaries, measure_audit_boundaries,
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
    audits = measure_audit_boundaries(root, Stage.GREEN)
    if (
        any(failures.values()) or audits["migrated"]
        or audits["retained"] != declared_audit_boundaries(str(root))["retained"]
    ):
        raise ValueError("MEM05 CTest failed semantics/work or retained diagnostics: " + json.dumps(failures))
    if (
        {path: read_regular(root / path)[1] for path in NATIVE_FIXTURE_PATHS} != fixtures
        or native_symbols_tool() != nm or any(read_regular(path)[1] != identities[str(path)] for path in expected)
    ):
        raise ValueError("MEM05 CTest source, fixture, tool or product changed during execution")
    for channel in CHANNELS:
        directory = root / RECIPE_ADMISSION["generator_output"] / channel
        current, _ = source_bundle(root, Stage.GREEN, channel)
        if current != inputs[channel] or {
            path: read_regular(directory / path)[1] for path in generated[channel]
        } != generated[channel]:
            raise ValueError("MEM05 CTest source or generated inputs changed during execution")
    return 0
