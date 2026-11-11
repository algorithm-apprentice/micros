"""Native execution and independent OBJ01 work/semantic evidence."""

import json
from pathlib import Path
import re
import subprocess

from tests.host.runtime_memory_evidence import json_pairs
from tests.host.selected_address_space_evidence import native_symbols_tool
from tests.host.work_count_evidence import Capture, Kind, Owner, Snapshot, Status, compose_snapshots
from tools.stack_artifact_files import read_regular


NATIVE_SOURCES = tuple("kernel/objects/" + name + ".c" for name in (
    "kernel_objects", "kernel_object_state", "kernel_object_identity",
    "kernel_object_stack", "kernel_object_process", "kernel_object_thread",
    "kernel_object_context", "kernel_object_hart", "kernel_object_validation",
)) + tuple("kernel/scheduler/" + name + ".c" for name in (
    "scheduler_core", "scheduler_core_queue", "scheduler_core_validate",
    "scheduler_core_policy", "scheduler_core_ipc", "scheduler_core_ipc_transition",
    "scheduler_core_return_plan", "scheduler_core_return", "scheduler_core_accounting",
))
NATIVE_HEADERS = (
    "include/micros/ipc.h", "include/micros/kernel_objects.h",
    "include/micros/scheduler_core.h", "include/micros/stack_observation.h",
    "include/micros/user_context.h", "kernel/objects/kernel_object_private.h",
    "kernel/scheduler/scheduler_core_internal.h", "kernel/scheduler/scheduler_core_private.h",
)
CASE_FIELDS = frozenset((
    "version", "case", "operation", "variant", "threads", "harts", "attached", "requests",
    "priority",
    "exceptions", "selected_thread", "selected_hart", "result", "diagnostic",
    "preserved", "outputs_preserved", "before_state", "after_state",
    "before_output", "after_output", "value",
))
SCHEDULER_OPERATIONS = (
    "admit-preflight", "admit", "hold", "remove", "flags-set", "flags-unset",
    "policy", "pick", "account-initialize", "account-thread", "account-idle",
    "ipc-preflight", "ipc-begin", "ipc-rollback", "ipc-commit", "ipc-batch",
    "ipc-wake", "return-plan",
)
GEOMETRY_CORRUPTIONS = (
    "thread-thread-overlap", "thread-idle-overlap", "thread-emergency-overlap",
    "idle-idle-overlap", "emergency-idle-overlap", "late-exception-overlap",
    "zero-stack", "misaligned-stack", "undersized-stack", "wrapped-stack",
    "emergency-emergency-overlap", "exception-pair-overlap",
)
QUEUE_CORRUPTIONS = (
    "stale-current-head", "stale-current-tail", "current-self-successor",
    "empty-head-live-tail", "current-primary-mismatch",
)
METADATA_CORRUPTIONS = (
    "invalid-thread-policy", "invalid-hart-policy", "invalid-live-count",
    "stale-owner", "empty-owner-count", "unknown-flags", "invalid-priority",
    "zero-quantum", "excess-remaining", "stale-assigned-hart", "ready-state-mismatch",
    "invalid-selected-stack", "invalid-account-owner", "stray-accounted-thread",
)
WORK_ORACLES = (
    "obj01-local-validation-work",
    "obj01-stack-geometry-work",
    "obj01-direct-current-ownership",
)
PRODUCER_BASE = 0x4f424a3031000000


def producer_label(stage, channel):
    if stage not in ("baseline", "current") or channel not in ("aggregate", "stack"):
        raise ValueError("OBJ01 producer has an unknown source stage or channel")
    return PRODUCER_BASE + 256 * int(stage == "current") + ("aggregate", "stack").index(channel) + 1


def expected_cases(stage):
    if stage not in ("baseline", "current"):
        raise ValueError("OBJ01 semantic corpus has an unknown source stage")
    cases = []

    def add(operation, variant, threads, harts, **fields):
        cases.append({
            "case": len(cases) + 1, "operation": operation, "variant": variant,
            "threads": threads, "harts": harts, "attached": threads, "requests": 0,
            "exceptions": 0, "priority": 3, "selected_thread": 0, "selected_hart": 0,
            "result": 0, "diagnostic": 0, **fields,
        })

    for threads in (0, 1, 2, 16, 64, 128):
        for harts in (0, 1, 2, 8):
            add("validate", "regular", threads, harts)
    for exceptions in (1, 2, 8):
        add("validate", "oversize-exceptions", 128, 8, exceptions=exceptions)
    add("validate", "offset-underflow-exception", 128, 1, exceptions=1)
    for harts in (1, 2, 8):
        for operation, variant, result in (
            ("hart-context", "last-hart", 0), ("hart-context", "misaligned-hart", 8),
            ("hart-context", "outside-hart", 8), ("is-current", "assigned-current", 0),
            ("capture", "assigned-current", 0),
        ):
            add(operation, variant, 128, harts, selected_thread=127, selected_hart=harts - 1, result=result)
    add("hart-context", "live-zero-generation", 1, 1, diagnostic=15, result=15 if stage == "current" else 0)
    add("validate", "unscheduled-current", 1, 1, result=15 if stage == "current" else 0,
        diagnostic=15 if stage == "current" else 0)
    add("capture", "unscheduled-current", 1, 1, result=9 if stage == "current" else 0,
        diagnostic=15 if stage == "current" else 0)
    add("validate", "regular-gap-owner", 3, 1, attached=2)
    add("validate", "zero-cohort-base", 3, 1, attached=2)
    add("validate", "permuted-portable-ranges", 4, 1, exceptions=3)
    add("validate", "current-primary-aliases", 2, 2)
    add("validate", "wrong-primary-alias", 2, 2, result=15, diagnostic=15)
    add("validate", "interleaved-touching-cohorts", 5, 2, attached=2)
    add("validate", "hart-cohort-gap", 1, 7)
    for variant in GEOMETRY_CORRUPTIONS:
        add("validate", variant, 8, 2, result=15, diagnostic=15,
            exceptions=2 if variant == "exception-pair-overlap" else
            int(variant in ("thread-thread-overlap", "late-exception-overlap")))
    add("capture", "foreign-current-duplicate", 2, 2, diagnostic=15, result=0 if stage == "current" else 15)
    add("capture", "stale-caller", 2, 1, result=8)
    add("is-current", "stale-caller", 2, 1)
    for variant in ("stale-selected-owner", "stale-selected-assignment"):
        add("capture", variant, 2, 1, diagnostic=15, result=15 if stage == "current" else 0)
    add("capture", "noncurrent-thread", 2, 1, selected_thread=1, result=9)
    for variant in ("first-hart", "below-hart-array", "after-hart-array", "nonlive-hart", "null-hart"):
        add("hart-context", variant, 1, 1, result=0 if variant == "first-hart" else 8)
    for index, variant in enumerate(("head-removal", "middle-removal", "tail-removal")):
        add("hold", variant, 3, 1, selected_thread=index)
    add("hold", "singleton-removal", 1, 1)
    add("policy", "same-priority-head", 3, 1, priority=7)
    add("policy", "same-priority-tail", 3, 1, priority=7, selected_thread=2)
    for index, variant in enumerate(("preempt-after-middle", "preempt-after-tail"), 1):
        add("policy", variant, 3, 1, selected_thread=index)
    for variant in QUEUE_CORRUPTIONS:
        add("admit", variant, 3, 1, selected_thread=2, result=15, diagnostic=15)
    add("hold", "consumed-prefix-backedge", 4, 1, selected_thread=2, result=15, diagnostic=15)
    add("ipc-rollback", "rollback-self-successor", 3, 1, result=15, diagnostic=15)
    add("ipc-commit", "commit-stale-tail", 3, 1, result=15, diagnostic=15)
    add("admit", "unselected-process", 3, 1, selected_thread=2, diagnostic=15,
        result=0 if stage == "current" else 15)
    for variant, requests, result in (
        ("duplicate-handle", 2, 1), ("different-generation", 2, 8),
        ("generation-repeat", 3, 1), ("outside-slot", 2, 8),
        ("outside-before-duplicate", 3, 1), ("zero-generation", 2, 8),
        ("duplicate-zero-generation", 2, 1),
    ):
        add("ipc-batch", variant, 3, 1, requests=requests, result=result)
    for threads in (1, 4, 128):
        add("ipc-batch", "valid", threads, 2, requests=threads, selected_thread=threads - 1)
    for threads in (4, 128):
        add("ipc-batch", "shared-queues-priority-preemption", threads, 2, requests=threads - 1)
    for variant in METADATA_CORRUPTIONS:
        add("hold", variant, 2, 1, selected_thread=1, result=15, diagnostic=15)
    add("hold", "stale-caller-thread", 2, 1, result=8)
    add("pick", "stale-caller-hart", 2, 1, result=8)
    add("admit", "detached-candidate", 2, 1, selected_thread=1, attached=1, result=9)
    add("flags-unset", "generation-only-null-next", 2, 1, selected_thread=1)
    add("admit", "generation-only-null-head", 2, 1, selected_thread=1)
    add("admit", "noncanonical-empty-tail", 2, 1, selected_thread=1, result=15, diagnostic=15)
    add("pick", "empty-queues", 0, 1, result=13)
    add("policy", "preemption-under-thread-accounting", 2, 1, selected_thread=1, result=9)
    for operation in ("account-thread", "account-idle"):
        add(operation, "kernel-accounting-overflow", 2, 1, result=14)
    for variant, threads in (
        ("duplicate-queue-membership", 3), ("wrong-final-tail", 3), ("nonlive-assigned-record", 1),
        ("reschedule-without-assignment", 0), ("unreachable-assigned-ready", 2),
    ):
        scoped = stage == "current" and variant in (
            "nonlive-assigned-record", "reschedule-without-assignment", "unreachable-assigned-ready",
        )
        add("return-plan", variant, threads, 1, result=0 if scoped else 15, diagnostic=15)
    add("return-plan", "unselected-hart-queue", 2, 2, diagnostic=15, result=0 if stage == "current" else 15)
    for operation in SCHEDULER_OPERATIONS:
        for threads in (2, 64):
            add(operation, "valid", threads, 2, selected_thread=threads - 1,
                requests=threads if operation == "ipc-batch" else 0)
    return tuple(cases)


def check_semantics(records, stage, *, historical=False):
    expected = expected_cases(stage)
    if len(records) != len(expected):
        raise ValueError("OBJ01 semantic corpus is incomplete")
    readonly = {
        "hart-context", "is-current", "validate", "admit-preflight", "pick",
        "ipc-preflight", "ipc-wake", "return-plan",
    }
    for row, wanted in zip(records, expected):
        if {key: row[key] for key in wanted} != wanted:
            raise ValueError("OBJ01 case differs from the independent " + stage + " contract: " + repr(row))
        if row["operation"] in readonly or row["result"] != 0:
            if row["preserved"] != 1 or row["before_state"] != row["after_state"]:
                raise ValueError("OBJ01 read or rejection changed registry state")
        if row["result"] != 0 and (
            row["outputs_preserved"] != 1 or row["before_output"] != row["after_output"]
        ):
            raise ValueError("OBJ01 rejection changed an output")
        if row["variant"] == "last-hart" and row["value"] != row["harts"] - 1:
            raise ValueError("OBJ01 routed hart differs from the exact selected identity")
        if row["operation"] == "is-current" and row["value"] != int(row["variant"] == "assigned-current"):
            raise ValueError("OBJ01 current membership differs from the exact assignment")


def work_totals(row, channel, stage):
    if channel not in ("aggregate", "stack"):
        raise ValueError("OBJ01 work totals require one declared observed product")
    snapshot = Snapshot(
        row["producer"], row["case"], row["owner_mask"],
        tuple(tuple(values) for values in row["counts"]),
        row["version"], row["reserved0"], row["reserved1"],
    )
    return compose_snapshots(
        (Capture(Status.OK, snapshot),), capture_label=row["case"],
        expected_producers={producer_label(stage, channel): 24 if channel == "aggregate" else 8},
    ).per_kind


def _local_work_limits(row):
    operation = row["operation"]
    prefix = 0
    priorities = 0
    if operation in ("hold", "flags-set", "policy", "ipc-begin", "ipc-commit"):
        prefix = row["threads"]
    if operation == "pick":
        priorities = 16
    if operation == "ipc-batch":
        return 128 * row["requests"] + 32, 64 * row["requests"] + 32, 8 * row["requests"] + 8
    if operation == "return-plan":
        return 128 + 8 * 128 + 8 * 16 + 8 * row["threads"], None, 2 * row["threads"] + 8
    return 128 + 8 * prefix + 8 * priorities, 264 + 4 * prefix + 4 * priorities, 2 * prefix + 8


def check_current_reference(root, records, plans):
    from tests.host.source_snapshot import source_bytes

    root = Path(root).resolve()
    path = root / "tests/host/object_hart_reference.json"
    reference = json.loads(source_bytes(root, path), object_pairs_hook=json_pairs)
    fields = (
        "case", "before_state", "after_state", "before_nonplan", "after_nonplan",
        "value", "has_effect", "effect",
    )
    if (
        type(reference) is not dict
        or set(reference) != {"version", "reference", "source_tree", "transcript_sha256", "fields", "records"}
        or type(reference["version"]) is not int or reference["version"] != 1
        or reference["fields"] != list(fields)
        or type(reference["records"]) is not list
        or len(reference["records"]) != len(expected_cases("current"))
        or len(records) != len(reference["records"]) or len(plans) != len(records)
    ):
        raise ValueError("current object reference is incomplete or has an unknown schema")
    for index, (row, plan, expected) in enumerate(zip(records, plans, reference["records"], strict=True), 1):
        if (
            type(expected) is not list or len(expected) != len(fields)
            or any(type(value) is not int or not 0 <= value < 1 << 64 for value in expected)
            or expected[0] != index or row["case"] != index or plan["case"] != index
        ):
            raise ValueError("current object reference omitted or borrowed a case")
        observed = [
            row["case"], row["before_state"], row["after_state"],
            row["before_output"] ^ plan["before"], row["after_output"] ^ plan["after"],
            row["value"], plan["has_effect"], plan["effect"],
        ]
        if observed != expected:
            raise ValueError("current object case changed input, nonplan output or apply effect: " + str(index))


def work_failures(products, stage):
    from tests.host.common_scheduler_return_evidence import validate_plan_observations

    if type(products) is not dict or set(products) != {"ordinary", "aggregate", "stack"}:
        raise ValueError("OBJ01 work evidence lacks its exact three-product source stage")
    records = products["ordinary"]["records"]
    check_semantics(records, stage)
    if any(product["stage"] != stage or product["channel"] != channel for channel, product in products.items()):
        raise ValueError("OBJ01 product substituted its source stage or channel")
    if products["ordinary"]["work"]:
        raise ValueError("OBJ01 ordinary product contains observations")
    for channel in ("aggregate", "stack"):
        if (
            products[channel]["records"] != records or len(products[channel]["work"]) != len(records)
            or products[channel]["plans"] != products["ordinary"]["plans"]
        ):
            raise ValueError("OBJ01 observation changed behavior or omitted a capture")
    validate_plan_observations(records, products["ordinary"]["plans"])
    failures = {name: [] for name in WORK_ORACLES}
    for index, row in enumerate(records):
        counts = {
            channel: work_totals(products[channel]["work"][index], channel, stage)
            for channel in ("aggregate", "stack")
        }
        if any(
            products[channel]["work"][index]["case"] != row["case"]
            for channel in ("aggregate", "stack")
        ):
            raise ValueError("OBJ01 work belongs to another semantic case")
        if any(
            part > total
            for stack_row, aggregate_row in zip(
                products["stack"]["work"][index]["counts"], products["aggregate"]["work"][index]["counts"],
            )
            for part, total in zip(stack_row, aggregate_row)
        ):
            raise ValueError("OBJ01 stack-only work is not a slice of its inclusive aggregate")
        operation = row["operation"]

        def maximum(group, name, actual, bound):
            if actual > bound:
                failures[group].append((row["case"], name, actual, bound))

        if operation in SCHEDULER_OPERATIONS:
            maximum(WORK_ORACLES[0], "complete-validators", counts["aggregate"][Kind.COMPLETE_VALIDATOR_CALL], 0)
            records_limit, indices_limit, links_limit = _local_work_limits(row)
            maximum(WORK_ORACLES[0], "selected-records", counts["aggregate"][Kind.RECORD_VISIT], records_limit)
            maximum(WORK_ORACLES[0], "consumed-links", counts["aggregate"][Kind.LINK_TRAVERSE], links_limit)
            if indices_limit is not None:
                maximum(WORK_ORACLES[0], "selected-indices", counts["aggregate"][Kind.INDEX_PROBE], indices_limit)
        if operation == "validate" and row["variant"] not in ("unscheduled-current", "wrong-primary-alias", *GEOMETRY_CORRUPTIONS):
            owned, exceptions, capacity = row["attached"] + 2 * row["harts"], row["exceptions"], 128 + 2 * 8
            maximum(
                WORK_ORACLES[1], "geometry-comparisons", counts["stack"][Kind.COMPARISON],
                16 * (owned + 1) + 2 * exceptions * max(owned - 1, 0),
            )
            maximum(
                WORK_ORACLES[1], "geometry-probes",
                counts["stack"][Kind.RECORD_VISIT] + counts["stack"][Kind.INDEX_PROBE],
                4 * capacity + 2 * exceptions * capacity + 32,
            )
        if operation in ("hart-context", "is-current", "capture"):
            maximum(
                WORK_ORACLES[2], "direct-records", counts["aggregate"][Kind.RECORD_VISIT],
                {"hart-context": 1, "is-current": 2, "capture": 3}[operation],
            )
            copied = 264 if operation == "capture" and row["result"] == 0 else 0
            if counts["aggregate"][Kind.COPIED_BYTE] != copied:
                raise ValueError("OBJ01 direct capture lost its exact fixed context copy")
        if operation == "validate" and row["variant"] == "unscheduled-current" and row["result"] != 15:
            failures[WORK_ORACLES[2]].append((row["case"], "unsupported-current-diagnostic", row["result"], 15))
    return {name: tuple(rows) for name, rows in failures.items()}


def parse_native_output(output, channel, stage="current"):
    from tests.host.common_scheduler_return_evidence import validate_plan_observations

    if type(output) is not str or not output.endswith("\n"):
        raise ValueError("OBJ01 native transcript is not complete text")
    if stage not in ("baseline", "current") or channel not in ("ordinary", "aggregate", "stack"):
        raise ValueError("OBJ01 native transcript has an unknown source stage or channel")
    records, work, plans = [], [], []
    lines = output.splitlines()
    for line in lines[:-1]:
        if line.startswith("MICROS_OBJ01_PLAN "):
            row = json.loads(line.removeprefix("MICROS_OBJ01_PLAN "), object_pairs_hook=json_pairs)
            if (
                type(row) is not dict or row.get("case") != len(records)
                or len(plans) + 1 != len(records) or len(work) >= len(records) and channel != "ordinary"
            ):
                raise ValueError("OBJ01 plan observation has a borrowed or duplicate case")
            plans.append(row)
            continue
        if line.startswith("MICROS_OBJ01_WORK "):
            row = json.loads(line.removeprefix("MICROS_OBJ01_WORK "), object_pairs_hook=json_pairs)
            if (
                channel == "ordinary" or type(row) is not dict
                or set(row) != {"version", "reserved0", "producer", "case", "owner_mask", "reserved1", "counts"}
                or row["version"] != 1 or row["reserved0"] != 0 or row["reserved1"] != 0
                or row["producer"] != producer_label(stage, channel)
                or row["case"] != len(records) or len(work) + 1 != len(records)
                or len(plans) != len(records)
                or row["owner_mask"] != (24 if channel == "aggregate" else 8)
                or any(type(row[key]) is not int for key in row if key != "counts")
                or type(row["counts"]) is not list or len(row["counts"]) != len(Owner)
                or any(
                    type(counts) is not list or len(counts) != len(Kind)
                    or any(type(value) is not int or not 0 <= value <= (1 << 64) - 1 for value in counts)
                    for counts in row["counts"]
                )
                or any(
                    any(counts) for owner, counts in enumerate(row["counts"])
                    if not row["owner_mask"] & (1 << owner)
                )
            ):
                raise ValueError("OBJ01 work record has malformed dimensions or capture ownership")
            work.append(row)
            continue
        if not line.startswith("MICROS_OBJ01_CASE "):
            raise ValueError("OBJ01 ordinary transcript contains an unknown record")
        if len(plans) != len(records):
            raise ValueError("OBJ01 native transcript omitted a plan observation")
        row = json.loads(line.removeprefix("MICROS_OBJ01_CASE "), object_pairs_hook=json_pairs)
        if (
            type(row) is not dict or set(row) != CASE_FIELDS
            or any(
                type(value) is not int or not 0 <= value <= (1 << 64) - 1
                for key, value in row.items() if key not in ("operation", "variant")
            )
            or any(type(row[key]) is not str or re.fullmatch(r"[a-z-]+", row[key]) is None
                   for key in ("operation", "variant"))
            or row["version"] != 1 or row["case"] != len(records) + 1
            or row["preserved"] not in (0, 1) or row["outputs_preserved"] not in (0, 1)
        ):
            raise ValueError("OBJ01 native case has malformed fields or order")
        records.append(row)
    if (
        not records or lines[-1] != "MICROS_OBJ01_NATIVE_PASS cases=" + str(len(records))
        or channel != "ordinary" and len(work) != len(records)
    ):
        raise ValueError("OBJ01 native transcript lacks its exact complete case count")
    validate_plan_observations(records, plans)
    return tuple(records), tuple(work), tuple(plans)


def observation_symbols(output):
    return frozenset(
        line.split()[-1].lstrip("_") for line in output.splitlines()
        if line.split() and "micros_test_work_" in line.split()[-1]
    )


def check_native_products(root, executable_paths):
    from tests.host.object_hart_validation_source import CHANNELS, measure_audit_boundaries
    from tools.generate_object_hart_validation_fixture import source_bundle

    root = Path(root).resolve()
    build = root / "build/host-debug"
    expected = tuple(str(build / ("object_hart_current_" + channel + "_test")) for channel in CHANNELS)
    if tuple(executable_paths) != expected:
        raise ValueError("OBJ01 native orchestration requires its exact three current executables")
    before = {path: read_regular(path)[1] for path in expected}
    nm = native_symbols_tool()
    measured = {}
    generated_inputs = {}
    stage = "current"
    for channel, path in zip(CHANNELS, expected, strict=True):
        _, outputs = source_bundle(root, stage, channel)
        directory = build / "object-hart-validation" / stage / channel
        for relative, raw in outputs.items():
            generated = directory / relative
            actual, identity = read_regular(generated)
            if actual != raw or identity["mode"] != 0o644:
                raise ValueError("OBJ01 compiled native input is stale or belongs to another product")
            generated_inputs[str(generated)] = identity
        symbols = subprocess.check_output((nm["path"], "-a", path), cwd=root, text=True, timeout=30)
        wanted = frozenset() if channel == "ordinary" else frozenset((
            "micros_test_work_begin", "micros_test_work_finish", "micros_test_work_record",
        ))
        if observation_symbols(symbols) != wanted:
            raise ValueError("OBJ01 compiled native product has the wrong collector linkage")
        completed = subprocess.run(
            (path, stage, channel), cwd=root, stdin=subprocess.DEVNULL,
            capture_output=True, text=True, timeout=30,
        )
        if completed.returncode or completed.stderr:
            raise ValueError("OBJ01 actual native product failed:\n" + completed.stdout + completed.stderr)
        records, work, plans = parse_native_output(completed.stdout, channel, stage)
        measured[channel] = {"stage": stage, "channel": channel, "records": records, "work": work, "plans": plans}
        print(f"MICROS_OBJ01_NATIVE_ACCEPTED stage={stage} channel={channel} cases={len(records)}", flush=True)
    if (
        {path: read_regular(path)[1] for path in expected} != before
        or {path: read_regular(path)[1] for path in generated_inputs} != generated_inputs
        or native_symbols_tool() != nm
    ):
        raise ValueError("OBJ01 compiled products or native tools changed during execution")
    failures = work_failures(measured, "current")
    check_current_reference(root, measured["ordinary"]["records"], measured["ordinary"]["plans"])
    if any(failures.values()) or measure_audit_boundaries(root, "current")["migrated"]:
        raise ValueError("OBJ01 current production does not satisfy its three independent oracles: " + repr(failures))
    return measured
