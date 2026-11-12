"""Current IPC observations and independent selected-operation work bounds."""

import json
from pathlib import Path
import re
import subprocess

from tests.host.evidence_records import record_fields
from tests.host.object_hart_validation_evidence import (
    NATIVE_HEADERS as OBJECT_HEADERS, NATIVE_SOURCES as OBJECT_SOURCES,
    observation_symbols,
)
from tests.host.runtime_memory_evidence import json_pairs
from tests.host.selected_address_space_evidence import native_symbols_tool
from tests.host.work_count_snapshots import compose_snapshots
from tests.host.work_count_types import Capture, Kind, Owner, Snapshot, Status
from tools.stack_artifact_files import read_regular


NATIVE_SOURCES = OBJECT_SOURCES + tuple("kernel/endpoint/" + name + ".c" for name in (
    "endpoint", "endpoint_close", "endpoint_codec", "endpoint_completion_validation",
    "endpoint_identity", "endpoint_object_validation", "endpoint_privilege",
    "endpoint_publication", "endpoint_queue_validation", "endpoint_registry",
)) + tuple("kernel/ipc/" + name + ".c" for name in (
    "ipc_common", "ipc_queue", "ipc_completion", "ipc_tokens", "ipc_notifications",
    "ipc_deadlock", "ipc_rendezvous", "ipc_close", "ipc_reply", "ipc",
)) + ("kernel/ipc/ipc_buffer.c", "kernel/ipc/ipc_abi.c", "kernel/scheduler/scheduler_completion.c")
NATIVE_HEADERS = OBJECT_HEADERS + (
    "include/micros/endpoint.h", "include/micros/bootstrap_control.h",
    "include/micros/bootstrap.h", "include/micros/user_address_space.h",
    "include/micros/syscall_abi.h", "include/micros/grant.h", "include/micros/tty.h",
    "include/micros/ipc_core.h", "kernel/ipc/ipc_internal.h",
    "include/micros/ipc_buffer.h", "include/micros/ipc_abi.h", "include/micros/ipc_runtime.h",
    "include/micros/sv39.h", "include/micros/scheduler.h", "include/micros/work_observation.h",
    "kernel/ipc/ipc_buffer_internal.h", "kernel/scheduler/scheduler_runtime_internal.h",
    "tests/qemu/selected_ipc_work.h",
    "kernel/endpoint/endpoint_private.h", "kernel/endpoint/endpoint_internal.h",
)
TARGETS = ("selected_ipc_validation_ordinary", "selected_ipc_validation_test")
PRODUCER = 0x4950433031000001
OWNER_MASK = sum(1 << owner for owner in (
    Owner.KERNEL_OBJECTS, Owner.KERNEL_SCHEDULER, Owner.KERNEL_ENDPOINT, Owner.KERNEL_IPC,
))
CASE_FIELDS = (
    "version", "case", "name", "result", "value", "preserved", "before_state", "after_state",
    "lookups", "translations", "plan_bytes",
)
WORK_FIELDS = ("version", "reserved0", "producer", "case", "owner_mask", "reserved1", "counts")
QUERY_CASES = (
    ("profile-first", 1, 1), ("profile-last", 31, 1),
    ("profile-operation", 0, 1), ("profile-target", 0, 2),
    ("profile-kernel", 0, 1), ("endpoint-first", 0x1000, 0),
    ("endpoint-last", 0x103f, 0), ("endpoint-operation", 0, 1),
    ("endpoint-target", 0, 2), ("endpoint-kernel", 0, 1),
)
# Four identity probes; policy rereads one process and probes two profile rows.
QUERY_INDEX_LIMITS = (2, 2, 2, 4, 2, 4, 4, 7, 14, 7)
IPC_CASES = (
    ("send-wait", 0, 2, 1, 0), ("send-delivery", 0, 2, 1, 0),
    ("send-last", 0, 2, 1, 0), ("send-unmatched", 0, 2, 1, 0),
    ("receive-wait", 0, 1, 1, 0), ("receive-delivery", 0, 1, 1, 0),
    ("receive-last", 0, 1, 1, 0), ("call-wait", 1, 2, 1, 128),
    ("call-delivery", 1, 2, 1, 128), ("reply", 1, 1, 0, 256),
    ("reply-receive-wait", 1, 1, 1, 256), ("reply-receive-sender", 1, 1, 1, 256),
    ("notify-coalesce", 0, 2, 1, 0), ("notify-delivery", 0, 2, 1, 0),
    ("kernel-coalesce", 0, 0, 1, 0), ("kernel-delivery", 0, 0, 1, 0),
    ("kernel-last", 0, 0, 1, 0), ("stage-no-message", 0, 0, 0, 0),
    ("cancel-close", 0, 0, 0, 0),
    ("kernel-backedge", 0, 0, 1, 0, 11),
    ("receive-pending-corrupt", 0, 1, 0, 0, 11),
    ("reply-pending-corrupt", 1, 1, 0, 256, 11),
    ("call-token-collision", 0, 2, 0, 128, 11),
)
LIFECYCLE_CASES = (
    ("reserve", 0x103e, 0, 0, 0),
    ("install-profile", 1, 1, 0, 0),
    ("activate", 2, 0, 0, 0),
    ("preflight-publish", 0, 1, 0, 1),
    ("preflight-source-only", 0, 0, 0, 1),
    ("substrate-close", 0, 0, 0, 0),
    ("publish-bad-current", 0, 1, 12, 1),
    ("reserve-dirty-record", 0, 0, 12, 1),
)

def completion_cases():
    rows = []
    for kind, chunks in (
        ("empty", 0), ("success", 0), ("error", 0), ("message", 1),
        ("cross", 2), ("token", 1), ("kernel", 1), ("source-only", 1),
    ):
        for phase in ("plan", "commit"):
            rows.append({
                "name": f"completion-{kind}-{phase}", "result": 0, "value": chunks,
                "preserved": int(phase == "plan" or kind == "empty"),
                "lookups": int(phase == "plan" and kind != "empty"),
                "translations": chunks if phase == "plan" else 0,
            })
    for name, lookups, translations in (
        ("malformed", 0, 0), ("buffer-fault", 1, 2), ("duplicate-token", 1, 0),
    ):
        rows.append({
            "name": "completion-" + name, "result": 1, "value": 0, "preserved": 1,
            "lookups": lookups, "translations": translations,
        })
    return tuple(rows)


def expected_cases():
    queries = tuple({
        "version": 1, "case": index, "name": name, "result": 0,
        "value": value, "preserved": 1,
    } for index, (name, value, _) in enumerate(QUERY_CASES, 1))
    operations = tuple({
        "version": 1, "case": index, "name": spec[0], "result": spec[5] if len(spec) == 6 else 0,
        "value": spec[1], "preserved": int(len(spec) == 6),
    } for index, spec in enumerate(IPC_CASES, len(queries) + 1))
    lifecycles = tuple({
        "version": 1, "case": index, "name": spec[0], "result": spec[3],
        "value": spec[1], "preserved": spec[4],
    } for index, spec in enumerate(LIFECYCLE_CASES, len(queries) + len(operations) + 1))
    ordinary = tuple({**row, "lookups": 0, "translations": 0} for row in queries + operations + lifecycles)
    return ordinary + tuple(
        {"version": 1, "case": index, **row}
        for index, row in enumerate(completion_cases(), len(ordinary) + 1)
    )


def case_identities(records):
    return tuple({key: value for key, value in row.items()
                  if key not in ("before_state", "after_state", "plan_bytes")} for row in records)


def work_totals(row):
    record_fields(row, WORK_FIELDS)
    if (
        any(type(row[key]) is not int for key in WORK_FIELDS if key != "counts")
        or type(row["counts"]) is not list
        or any(type(values) is not list for values in row["counts"])
    ):
        raise ValueError("IPC01 work fields have an invalid type")
    snapshot = Snapshot(
        row["producer"], row["case"], row["owner_mask"],
        tuple(tuple(values) for values in row["counts"]),
        row["version"], row["reserved0"], row["reserved1"],
    )
    return compose_snapshots(
        (Capture(Status.OK, snapshot),), capture_label=row["case"],
        expected_producers={PRODUCER: OWNER_MASK},
    )


def parse_native_output(output, channel):
    if channel not in ("ordinary", "work") or type(output) is not str or not output.endswith("\n"):
        raise ValueError("IPC01 native transcript is incomplete or has an unknown channel")
    records, work = [], []
    lines = output.splitlines()
    for line in lines[:-1]:
        if line.startswith("MICROS_IPC01_CASE "):
            if channel == "work" and len(work) != len(records):
                raise ValueError("IPC01 native case omitted its complete work snapshot")
            row = json.loads(line.removeprefix("MICROS_IPC01_CASE "), object_pairs_hook=json_pairs)
            record_fields(row, CASE_FIELDS)
            if (
                any(type(row[key]) is not int or not 0 <= row[key] < 1 << 64
                    for key in CASE_FIELDS if key != "name")
                or type(row["name"]) is not str
                or re.fullmatch(r"[a-z-]+", row["name"]) is None
                or row["case"] != len(records) + 1
            ):
                raise ValueError("IPC01 native case fields or order are invalid")
            if (not 0 < row["plan_bytes"] <= 256 if row["name"].startswith("completion-")
                    else row["plan_bytes"] != 0):
                raise ValueError("IPC01 completion plan exceeds its accepted storage boundary")
            records.append(row)
        elif line.startswith("MICROS_IPC01_WORK "):
            row = json.loads(line.removeprefix("MICROS_IPC01_WORK "), object_pairs_hook=json_pairs)
            work_totals(row)
            if channel != "work" or row["case"] != len(records) or len(work) + 1 != len(records):
                raise ValueError("IPC01 work belongs to another or duplicate case")
            work.append(row)
        else:
            raise ValueError("IPC01 native transcript contains an unknown record")
    if (
        case_identities(records) != expected_cases()
        or lines[-1:] != ["MICROS_IPC01_NATIVE_PASS cases=" + str(len(records))]
        or channel == "work" and len(work) != len(records)
    ):
        raise ValueError("IPC01 native evidence omits or changes an independent semantic case")
    return tuple(records), tuple(work)


def work_failures(records, work):
    if case_identities(records) != expected_cases() or len(records) != len(work):
        raise ValueError("IPC01 work oracle requires the complete independent case set")
    failures = []
    specs = (tuple((*spec, 0, 0) for spec in QUERY_CASES) + IPC_CASES
             + tuple((*spec[:3], 0, 0) for spec in LIFECYCLE_CASES)
             + tuple((row["name"], row["value"], 0, 0, 2) for row in completion_cases()))
    lifecycle = {spec[0] for spec in LIFECYCLE_CASES}
    for row, snapshot, spec in zip(records, work, specs, strict=True):
        if row["case"] != snapshot["case"]:
            raise ValueError("IPC01 work oracle borrowed a different case")
        totals = work_totals(snapshot)
        audits = totals.per_kind[Kind.COMPLETE_VALIDATOR_CALL]
        profiles = totals.per_owner[Owner.KERNEL_ENDPOINT][Kind.RANGE_VISIT]
        if audits:
            failures.append((row["name"], "complete-audits", audits, 0))
        if profiles > spec[2]:
            failures.append((row["name"], "profile-resolutions", profiles, spec[2]))
        if row["case"] <= len(QUERY_CASES):
            visits = totals.per_kind[Kind.RECORD_VISIT]
            probes = totals.per_kind[Kind.INDEX_PROBE]
            limit = QUERY_INDEX_LIMITS[row["case"] - 1]
            if visits:
                failures.append((row["name"], "query-table-visits", visits, 0))
            if probes > limit:
                failures.append((row["name"], "query-index-probes", probes, limit))
        endpoint_rows = totals.per_owner[Owner.KERNEL_ENDPOINT][Kind.RECORD_VISIT]
        endpoint_bound = (
            4 * 64 + 3 * 128 + 8 if row["name"] in lifecycle else
            128 if row["name"] in ("completion-token-plan", "completion-duplicate-token") else
            2 * 64 if row["name"] == "cancel-close" else 0
        )
        if endpoint_rows > endpoint_bound:
            failures.append((row["name"], "endpoint-table-visits", endpoint_rows, endpoint_bound))
        searches = totals.per_owner[Owner.KERNEL_IPC][Kind.RANGE_VISIT]
        if searches > spec[3]:
            failures.append((row["name"], "matching-searches", searches, spec[3]))
        prefix = {"send-last": 16, "receive-last": 16, "kernel-last": 128, "kernel-backedge": 2}.get(
            row["name"], int(row["name"] in {case[0] for case in IPC_CASES})
        )
        links = totals.per_owner[Owner.KERNEL_IPC][Kind.LINK_TRAVERSE]
        link_bound = 2 * 64 + 3 if row["name"] == "cancel-close" else 2 * prefix
        if links > link_bound:
            failures.append((row["name"], "queue-prefix-and-replay", links, link_bound))
        token_rows = totals.per_owner[Owner.KERNEL_IPC][Kind.RECORD_VISIT]
        token_bound = 4 * 128 + 5 * 64 if row["name"] == "cancel-close" else spec[4]
        if token_rows > token_bound:
            failures.append((row["name"], "token-or-close-visits", token_rows, token_bound))
        bits = totals.per_owner[Owner.KERNEL_IPC][Kind.BITMAP_WORD_LOAD]
        bit_bound = 1 if row["name"] in ("receive-last", "receive-pending-corrupt") else 64
        if bits > bit_bound:
            failures.append((row["name"], "notification-selection", bits, bit_bound))
        if row["name"].startswith("completion-") and row["name"].endswith("-commit"):
            copied = totals.per_owner[Owner.KERNEL_IPC][Kind.COPIED_BYTE]
            wanted = 64 if row["value"] else 0
            if copied != wanted:
                failures.append((row["name"], "retained-message-bytes", copied, wanted))
    return tuple(failures)


def check_native_products(root, executable_paths):
    from tools.generate_selected_ipc_validation_fixture import source_bundle

    root = Path(root).resolve()
    build = root / "build/host-debug"
    expected = tuple(str(build / name) for name in TARGETS)
    if tuple(executable_paths) != expected:
        raise ValueError("IPC01 native check requires its two owned executables")
    products = {path: read_regular(path)[1] for path in expected}
    generated_inputs, measured = {}, {}
    nm = native_symbols_tool()
    for channel, path in zip(("ordinary", "work"), expected, strict=True):
        outputs = source_bundle(root, channel)
        for relative, raw in outputs.items():
            generated = build / "selected-ipc-validation" / channel / relative
            actual, identity = read_regular(generated)
            if actual != raw or identity["mode"] != 0o644:
                raise ValueError("IPC01 compiled input is stale or belongs to another product")
            generated_inputs[str(generated)] = identity
        symbols = subprocess.check_output((nm["path"], "-a", path), cwd=root, text=True, timeout=30)
        wanted = frozenset() if channel == "ordinary" else frozenset((
            "micros_test_work_begin", "micros_test_work_finish", "micros_test_work_record",
        ))
        if observation_symbols(symbols) != wanted:
            raise ValueError("IPC01 native product has the wrong collector linkage")
        completed = subprocess.run(
            (path, "--run", channel), cwd=root, stdin=subprocess.DEVNULL,
            capture_output=True, text=True, timeout=30,
        )
        if completed.returncode or completed.stderr:
            raise ValueError("IPC01 compiled native product failed:\n" + completed.stdout + completed.stderr)
        measured[channel] = parse_native_output(completed.stdout, channel)
        print(f"MICROS_IPC01_NATIVE_ACCEPTED channel={channel} cases={len(measured[channel][0])}", flush=True)
    if (
        {path: read_regular(path)[1] for path in expected} != products
        or {path: read_regular(path)[1] for path in generated_inputs} != generated_inputs
        or native_symbols_tool() != nm
    ):
        raise ValueError("IPC01 native products or tools changed during execution")
    if measured["ordinary"][0] != measured["work"][0] or measured["ordinary"][1]:
        raise ValueError("IPC01 passive observations changed native semantics")
    failures = work_failures(*measured["work"])
    if failures:
        raise ValueError("IPC01 independent work bounds failed: " + repr(failures))
    return measured


TARGET_PRODUCER = 0x4950433031540001
TARGET_COUNT_FIELDS = (
    "comparison", "record", "index", "link", "bitmap", "pte", "queue",
    "validator", "copy", "copied", "clear", "cleared", "range", "hash",
)
TARGET_FIELDS = (
    "class", "version", "reserved0", "producer", "capture", "owner_mask", "reserved1",
    "kind", "operation", "thread_slot", "thread_generation", "result", "chunks", "token",
)
TARGET_PASS = (
    "MICROS_IPC_SYSCALL_TEST_PASS spaces=three operations=six blocking=validated "
    "calls=tokenized notifications=coalesced errors=stable completion=deferred "
    "registers=preserved cleanup=complete"
)


def parse_target_output(output, exit_code):
    if type(output) is not str or not output.endswith("\n") or type(exit_code) is not int or exit_code != 0:
        raise ValueError("IPC01 target did not complete successfully")
    captures, summary = [], None
    for line in output.splitlines():
        if not line.startswith("# MICROS_IPC01_"):
            continue
        match = re.fullmatch(r"# (MICROS_IPC01_CAPTURE(?:_COUNTS|_END)?) (.*)", line)
        if match is None or summary is not None:
            raise ValueError("IPC01 target has an unknown, repeated or late witness")
        pairs = [
            re.fullmatch(r"([a-z0-9_]+)=(0x(?:0|[1-9a-f][0-9a-f]{0,15}))", field)
            for field in match[2].split(" ")
        ]
        if any(pair is None for pair in pairs):
            raise ValueError("IPC01 target has malformed witness fields")
        row = json_pairs((pair[1], int(pair[2], 16)) for pair in pairs)
        if match[1] == "MICROS_IPC01_CAPTURE":
            record_fields(row, TARGET_FIELDS)
            if (
                len(captures) >= 10 or row["class"] != len(captures)
                or captures and len(captures[-1]["counts"]) != len(Owner)
                or row["capture"] == 0 or row["capture"] in {item["capture"] for item in captures}
                or row["thread_slot"] >= 128 or not 0 < row["thread_generation"] < 1 << 32
            ):
                raise ValueError("IPC01 target has incomplete membership or a borrowed thread/capture")
            index = row["class"]
            if index < 6:
                valid = (row["kind"], row["operation"], row["result"], row["chunks"], row["token"]) == (
                    0, index + 1, 0, 0, 0,
                )
            else:
                valid = (
                    row["kind"] == 1 and row["operation"] == 0
                    and row["result"] == ((1 << 64) - 2 if index == 7 else 0)
                    and row["chunks"] == (0, 0, 1, 2)[index - 6]
                    and (index >= 8 or row["token"] == 0)
                )
            if not valid:
                raise ValueError("IPC01 target misattributes its operation or completion class")
            captures.append({**row, "counts": []})
        elif match[1] == "MICROS_IPC01_CAPTURE_COUNTS":
            record_fields(row, ("capture", "owner", *TARGET_COUNT_FIELDS))
            if (
                not captures or row["capture"] != captures[-1]["capture"]
                or row["owner"] != len(captures[-1]["counts"]) or row["owner"] >= len(Owner)
            ):
                raise ValueError("IPC01 target snapshot omitted, duplicated or borrowed an owner")
            captures[-1]["counts"].append(tuple(row[name] for name in TARGET_COUNT_FIELDS))
        else:
            record_fields(row, ("complete", "syscalls", "completions", "captures", "audits", "tokens"))
            summary = row
    if (
        summary is None or len(captures) != 10 or summary["complete"] != 1
        or summary["syscalls"] != 63 or summary["completions"] != 15
        or not 1 <= summary["tokens"] <= summary["captures"]
        or summary["captures"] < max(row["capture"] for row in captures)
        or len(re.findall(r"^# " + re.escape(TARGET_PASS) + r"$", output, re.M)) != 1
        or re.search(r"^# MICROS_(?:PANIC|TEST_FAILURE)\b", output, re.M)
    ):
        raise ValueError("IPC01 target lacks complete original behavior and owned capture coverage")
    captured_audits = 0
    for row in captures:
        snapshot = Snapshot(
            row["producer"], row["capture"], row["owner_mask"], tuple(row["counts"]),
            row["version"], row["reserved0"], row["reserved1"],
        )
        totals = compose_snapshots(
            (Capture(Status.OK, snapshot),), capture_label=row["capture"],
            expected_producers={TARGET_PRODUCER: OWNER_MASK},
        )
        for owner in Owner:
            for kind in Kind:
                if totals.per_owner[owner][kind] and not (
                    kind == Kind.COMPLETE_VALIDATOR_CALL
                    or owner == Owner.KERNEL_IPC and kind == Kind.INDEX_PROBE
                ):
                    raise ValueError("IPC01 target snapshot contains undeclared work")
        captured_audits += totals.per_kind[Kind.COMPLETE_VALIDATOR_CALL]
    if captured_audits > summary["audits"]:
        raise ValueError("IPC01 target summary dropped actual audit invocations")
    return tuple(captures), summary


def target_failures(measured):
    captures, summary = measured
    failures = []
    if summary["audits"]:
        failures.append(("all-scopes", "complete-audits", summary["audits"], 0))
    for row in captures:
        lookups = row["counts"][Owner.KERNEL_IPC][Kind.INDEX_PROBE]
        if row["kind"] == 0:
            low, high = (0, 0) if row["operation"] == 6 else (2, 4) if row["operation"] == 5 else (1, 2)
        else:
            low = high = row["chunks"]
        if not low <= lookups <= high:
            failures.append((row["class"], "buffer-translations", lookups, (low, high)))
    return tuple(failures)
