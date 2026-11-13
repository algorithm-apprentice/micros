"""Independent same-guest bootstrap command membership and work limits."""

import re

from tests.host.evidence_records import record_fields
from tests.host.runtime_memory_evidence import json_pairs
from tests.host.work_count_snapshots import compose_snapshots
from tests.host.work_count_types import Capture, Kind, Owner, Snapshot, Status


PRODUCERS = {"bootstrap-command": 0x424f4f5430325401, "tty-bootstrap-command": 0x424f4f5430325402}
OWNER_MASK = sum(1 << owner for owner in (
    Owner.KERNEL_MEMORY, Owner.KERNEL_OBJECTS, Owner.KERNEL_SCHEDULER, Owner.KERNEL_ENDPOINT,
    Owner.KERNEL_IPC, Owner.KERNEL_GRANT, Owner.KERNEL_EXECUTION, Owner.KERNEL_BOOT,
    Owner.KERNEL_VM, Owner.KERNEL_TTY,
))
FIELDS = (
    "index", "version", "reserved0", "producer", "capture", "owner_mask", "reserved1",
    "command", "service", "role", "thread_slot", "thread_generation", "return_kind", "result",
)
DIMENSIONS = (
    "comparison", "record", "index", "link", "bitmap", "pte", "queue",
    "validator", "copy", "copied", "clear", "cleared", "range", "hash",
)
COMMANDS = {
    "bootstrap-command": ((1, 2, 0), (2, 2, 0), (1, 3, 0), (2, 3, 0), (4, 0, 0)),
    "tty-bootstrap-command": (
        (1, 2, 2), (2, 2, 2), (1, 3, 8), (2, 3, 8), (5, 4, 4),
        (1, 4, 4), (2, 4, 4), (1, 5, 0), (2, 5, 0), (4, 0, 0),
    ),
}
PASS_LINES = {
    "bootstrap-command": (
        "MICROS_BOOTSTRAP_TEST_PASS manifest=immutable order=topological profiles=exact "
        "endpoints=staged readiness=acknowledged authority=revoked"
    ),
    "tty-bootstrap-command": (
        "MICROS_TTY_TEST_PASS handoff=two-phase mapping=exact irq=deferred "
        "input=canonical output=interrupt-driven grants=checked"
    ),
}


def parse_target_output(output, exit_code, scope):
    if scope not in COMMANDS or type(output) is not str or not output.endswith("\n") or type(exit_code) is not int or exit_code:
        raise ValueError("BOOT02 target did not complete its declared guest")
    captures, summary = [], None
    expected = COMMANDS[scope]
    for line in output.splitlines():
        if not line.startswith("# MICROS_BOOT02_"):
            continue
        match = re.fullmatch(r"# (MICROS_BOOT02_CAPTURE(?:_COUNTS|_END)?) (.*)", line)
        if match is None or summary is not None:
            raise ValueError("BOOT02 target has unknown, duplicate or late evidence")
        pairs = [re.fullmatch(r"([a-z0-9_]+)=(0x[0-9a-f]{16})", item) for item in match[2].split(" ")]
        if any(pair is None for pair in pairs):
            raise ValueError("BOOT02 target has malformed work fields")
        row = json_pairs((pair[1], int(pair[2], 16)) for pair in pairs)
        if match[1] == "MICROS_BOOT02_CAPTURE":
            record_fields(row, FIELDS)
            if (
                len(captures) >= len(expected) or row["index"] != len(captures)
                or captures and len(captures[-1]["counts"]) != len(Owner)
                or row["capture"] == 0 or captures and row["capture"] <= captures[-1]["capture"]
                or row["thread_slot"] >= 128 or not 0 < row["thread_generation"] < 1 << 32
                or (row["command"], row["service"], row["role"]) != expected[len(captures)]
            ):
                raise ValueError("BOOT02 target omitted, reordered or borrowed a command/role")
            captured = row["command"] in (2, 4)
            if (row["return_kind"], row["result"]) != (int(captured), row["command"] if captured else 0):
                raise ValueError("BOOT02 target changed captured or normal return behavior")
            if captures and (row["thread_slot"], row["thread_generation"]) != (
                captures[0]["thread_slot"], captures[0]["thread_generation"],
            ):
                raise ValueError("BOOT02 target borrowed another controller")
            captures.append({**row, "counts": []})
        elif match[1] == "MICROS_BOOT02_CAPTURE_COUNTS":
            record_fields(row, ("capture", "owner", *DIMENSIONS))
            if (
                not captures or row["capture"] != captures[-1]["capture"]
                or row["owner"] != len(captures[-1]["counts"]) or row["owner"] >= len(Owner)
            ):
                raise ValueError("BOOT02 target omitted, duplicated or borrowed an owner")
            captures[-1]["counts"].append(tuple(row[name] for name in DIMENSIONS))
        else:
            record_fields(row, ("complete", "captures", "attempts", "rejected", "audits"))
            summary = row
    if (
        summary is None or len(captures) != len(expected) or summary["complete"] != 1
        or summary["captures"] != len(captures)
        or summary["attempts"] != len(captures) + summary["rejected"]
        or summary["attempts"] < captures[-1]["capture"]
        or len(re.findall(r"^# " + re.escape(PASS_LINES[scope]) + r"\r?$", output, re.M)) != 1
        or re.search(r"^# MICROS_(?:PANIC|TEST_FAILURE)\b", output, re.M)
    ):
        raise ValueError("BOOT02 target lacks complete original behavior and capture coverage")
    audits = 0
    for row in captures:
        snapshot = Snapshot(
            row["producer"], row["capture"], row["owner_mask"], tuple(row["counts"]),
            row["version"], row["reserved0"], row["reserved1"],
        )
        totals = compose_snapshots(
            (Capture(Status.OK, snapshot),), capture_label=row["capture"],
            expected_producers={PRODUCERS[scope]: OWNER_MASK},
        )
        for owner in Owner:
            for kind in Kind:
                if totals.per_owner[owner][kind] and not (
                    kind == Kind.COMPLETE_VALIDATOR_CALL
                    or owner == Owner.KERNEL_EXECUTION and kind == Kind.INDEX_PROBE
                    or owner == Owner.KERNEL_MEMORY and kind == Kind.INDEX_PROBE
                    or owner == Owner.KERNEL_MEMORY and kind == Kind.RANGE_VISIT
                ):
                    raise ValueError("BOOT02 target contains undeclared observations")
        audits += totals.per_kind[Kind.COMPLETE_VALIDATOR_CALL]
    if audits > summary["audits"] or audits >= 1 << 64:
        raise ValueError("BOOT02 target summary lost or overflowed actual audits")
    return tuple(captures), summary


def target_failures(measured, scope):
    captures, summary = measured
    failures = []
    if summary["audits"]:
        failures.append(("all-commands", "complete-audits", summary["audits"], 0))
    for row in captures:
        contexts = row["counts"][Owner.KERNEL_EXECUTION][Kind.INDEX_PROBE]
        wanted_contexts = int(row["command"] in (1, 2))
        if contexts != wanted_contexts:
            failures.append((row["index"], "selected-execution-contexts", contexts, wanted_contexts))
        points = row["counts"][Owner.KERNEL_MEMORY][Kind.RANGE_VISIT]
        wanted_points = int(scope == "tty-bootstrap-command" and (row["service"] == 4 or row["command"] == 4))
        if points != wanted_points:
            failures.append((row["index"], "uart-point-queries", points, wanted_points))
        metadata = row["counts"][Owner.KERNEL_MEMORY][Kind.INDEX_PROBE]
        # Each PC/SP lookup and UART point retains MEM03's scalar/geometry admission.
        wanted_metadata = 2 * wanted_contexts + wanted_points
        if metadata != wanted_metadata:
            failures.append((row["index"], "selected-metadata-admissions", metadata, wanted_metadata))
    return tuple(failures)


def checked_terminal_prefix(output, exit_code, scope):
    """Consume only a complete owned tail, preserving preceding evidence bytes."""
    lines = output.splitlines(keepends=True)
    reported = "".join("# " + line for line in lines)
    failures = target_failures(parse_target_output(reported, exit_code, scope), scope)
    if failures:
        raise ValueError("BOOT02 terminal semantic/work witnesses failed: " + repr(failures))
    start = next(index for index, line in enumerate(lines) if line.startswith("MICROS_BOOT02_"))
    if any(line.strip() and not line.startswith("MICROS_BOOT02_") for line in lines[start:]):
        raise ValueError("BOOT02 command records split the terminal evidence boundary")
    return "".join(lines[:start])
