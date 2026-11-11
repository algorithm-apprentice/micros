"""Current scheduler observations and strict plan/capture protocols."""

import json
from pathlib import Path
import re
import subprocess

from tests.host.evidence_records import record_fields
from tests.host.runtime_memory_evidence import json_pairs
from tests.host.selected_address_space_evidence import native_symbols_tool
from tests.host.work_count_types import Capture, Kind, Owner, Snapshot, Status
from tests.host.work_count_snapshots import compose_snapshots
from tools.stack_artifact_files import read_regular


PLAN_OBSERVATION_FIELDS = (
    "version", "case", "bytes", "before", "after", "has_effect", "effect",
)
CASE_FIELDS = (
    "version", "case", "name", "operation", "threads", "exhausted", "priority",
    "mode", "nonpreemptible", "two_priorities", "fault", "result", "renewed",
    "current_slot", "current_generation", "plan_bytes", "workspace_bytes",
    "before_state", "after_state", "logical_state", "plan_preserved",
)
PRODUCER_BASE = 0x4f424a3032000001
TARGET_COUNT_FIELDS = (
    "comparison", "record", "index", "link", "bitmap", "pte", "queue",
    "validator", "copy", "copied", "clear", "cleared", "range", "hash",
)
TARGET_FIELDS = (
    "class", "version", "reserved0", "producer", "capture", "owner_mask", "reserved1",
    "mode", "outgoing_slot", "outgoing_generation", "selected_slot", "selected_generation",
)
TARGET_MARKERS = (
    "MICROS_IPC_RETURN completions=shared paths=start,user,captured,idle buffers=bounded",
    "MICROS_SCHEDULER_TEST_PASS queues=minix-priority current=reachable accounting=separate switches=alternating idle=resumed registers=preserved",
    "MICROS_TTY_TRAP_TEST_PASS user=cause9-scheduled supervisor=cause9-direct idle=cause9-selected",
)


def validate_plan_observations(records, plans):
    if (
        type(records) not in (tuple, list) or type(plans) not in (tuple, list)
        or not records or len(records) != len(plans)
    ):
        raise ValueError("return-plan observations do not cover the complete case set")
    size = None
    for index, (row, plan) in enumerate(zip(records, plans, strict=True), 1):
        record_fields(plan, PLAN_OBSERVATION_FIELDS)
        if (
            type(row) is not dict
            or not {
                "case", "operation", "result", "diagnostic", "outputs_preserved",
                "before_state", "after_state", "before_output", "after_output",
            } <= row.keys()
            or any(type(value) is not int or not 0 <= value < 1 << 64 for value in plan.values())
            or plan["version"] != 1
            or type(row["case"]) is not int or row["case"] != index or plan["case"] != index
            or plan["bytes"] == 0
            or any(type(row[name]) is not int or not 0 <= row[name] < 1 << 64
                   for name in ("result", "diagnostic", "before_state", "after_state", "before_output", "after_output"))
            or type(row["operation"]) is not str
        ):
            raise ValueError("return-plan observation has malformed fields or case identity")
        if size is None:
            size = plan["bytes"]
        if plan["bytes"] != size:
            raise ValueError("return-plan observations borrowed a different ABI")
        effect = row["operation"] == "return-plan" and row["result"] == 0
        if plan["has_effect"] != int(effect) or not effect and plan["effect"] != 0:
            raise ValueError("return-plan effect is absent or belongs to an unexecuted apply")
        if not effect and plan["before"] != plan["after"]:
            raise ValueError("return-plan rejection or unrelated operation changed plan bytes")
        if row["result"] != 0 and (
            type(row["outputs_preserved"]) is not int or row["outputs_preserved"] != 1
        ):
            raise ValueError("return-plan rejection changed an output")


def compare_plan_observations(before, after, before_plans, after_plans):
    validate_plan_observations(before, before_plans)
    validate_plan_observations(after, after_plans)
    if len(before) != len(after):
        raise ValueError("return-plan stages omitted or added cases")
    for old, new, left, right in zip(before, after, before_plans, after_plans, strict=True):
        if (
            old.keys() != new.keys()
            or any(old[name] != new[name] for name in ("case", "operation", "before_state"))
            or old["before_output"] ^ left["before"] != new["before_output"] ^ right["before"]
        ):
            raise ValueError("return-plan stages changed their independent case setup")
        if old["result"] != new["result"] or old["diagnostic"] != new["diagnostic"]:
            continue
        if (
            any(old[name] != new[name] for name in old if name not in ("before_output", "after_output"))
            or old["after_output"] ^ left["after"] != new["after_output"] ^ right["after"]
            or left["effect"] != right["effect"]
        ):
            raise ValueError("return-plan stages changed retained outputs or defined-state effects")


def native_case_specs(stage):
    if stage not in ("baseline", "current"):
        raise ValueError("OBJ02 native corpus has an unknown source stage")
    specs = []
    for priority in (0, 15):
        specs.append(("empty", 0, 0, priority, 0, 128, 0, 0, 0))
        for count in (1, 2, 128):
            for mode in (0, 1):
                for exhausted in (0, count):
                    specs.append((
                        "positive" if exhausted == 0 else "all-zero",
                        count, exhausted, priority, mode, 128, 0, 0, 0,
                    ))
    for count in (1, 2, 128):
        for mode in (2, 3):
            for exhausted in (0, count):
                specs.append(("repair", count, exhausted, 7, mode, 128, 0, 0, 0))
    specs.extend((
        ("mixed-prefix", 8, 3, 7, 0, 128, 0, 0, 0),
        ("mixed-current", 128, 127, 7, 1, 128, 0, 0, 0),
        ("nonpreemptible-head", 8, 8, 7, 0, 0, 0, 0, 0),
        ("nonpreemptible-prefix", 8, 8, 7, 0, 3, 0, 0, 0),
        ("nonpreemptible-current", 8, 8, 7, 1, 0, 1, 0, 0),
        ("nonpreemptible-repaired", 8, 8, 7, 2, 0, 0, 0, 0),
        ("different-priorities", 8, 8, 7, 1, 128, 1, 0, 0),
        ("repair-different-priorities", 8, 8, 7, 2, 128, 1, 0, 0),
        ("repair-head-different-priorities", 8, 0, 7, 2, 128, 1, 0, 0),
        ("permitted-null", 8, 0, 7, 1, 128, 0, 16, 0),
        ("permitted-null-rotation", 8, 8, 7, 0, 128, 0, 16, 0),
        ("unselected-hart", 8, 3, 7, 0, 128, 0, 12, 0),
    ))
    for name, count, exhausted, mode, fault, error in (
        ("head-cycle", 8, 8, 0, 1, 15),
        ("prefix-cycle", 8, 8, 0, 2, 15),
        ("current-backedge", 8, 8, 1, 3, 15),
        ("stale-successor", 8, 8, 0, 4, 15),
        ("outside-successor", 8, 8, 0, 5, 15),
        ("wrong-tail", 8, 8, 0, 6, 15),
        ("duplicate-priority", 8, 8, 0, 7, 15),
        ("stale-hart", 8, 0, 0, 8, 8),
        ("thread-accounting", 8, 0, 1, 9, 9),
        ("idle-accounting", 0, 0, 0, 10, 9),
        ("noncanonical-tail", 0, 0, 0, 11, 15),
        ("unused-assignment", 0, 0, 0, 13, 15),
        ("empty-reschedule", 0, 0, 0, 14, 15),
        ("unreachable-ready", 2, 0, 0, 15, 15),
    ):
        specs.append((name, count, exhausted, 7, mode, 128, 0, fault,
                      0 if stage == "current" and fault in (13, 14, 15) else error))
    rows = []
    fields = ("name", "threads", "exhausted", "priority", "mode", "nonpreemptible",
              "two_priorities", "fault", "result")
    for values in specs:
        spec = dict(zip(fields, values, strict=True))
        for operation in (("construct", "checked", "apply") if spec["result"] == 0 else ("construct",)):
            rows.append({**spec, "operation": operation})
    for name in (
        "stale-tail", "tamper-selected", "tamper-outgoing", "tamper-action", "tamper-repair",
        "tamper-renew", "tamper-queue-count", "tamper-patch-count", "tamper-renew-count", "plan-padding",
    ):
        rows.append(dict(zip(fields, (
            name, 2, 0 if name == "stale-tail" else 2, 7, 0, 128, 0, 0,
            0 if name == "plan-padding" else 9,
        ), strict=True), operation="checked"))
    return tuple({**row, "case": index, "version": 1} for index, row in enumerate(rows, 1))


def check_native_semantics(records, stage):
    expected = native_case_specs(stage)
    if len(records) != len(expected):
        raise ValueError("OBJ02 native corpus omitted or added cases")
    for row, wanted in zip(records, expected, strict=True):
        if {name: row[name] for name in wanted} != wanted:
            raise ValueError("OBJ02 case differs from the independent " + stage + " corpus: " + repr(row))
        if row["operation"] == "construct" or row["result"] != 0:
            if row["before_state"] != row["after_state"]:
                raise ValueError("OBJ02 planning or rejection changed registry state")
        if (row["operation"] != "construct" or row["result"] != 0) and row["plan_preserved"] != 1:
            raise ValueError("OBJ02 rejection or commit changed the caller plan")
        if row["renewed"] > row["threads"]:
            raise ValueError("OBJ02 model renewed a record more than once")
        if stage == "baseline" and (row["plan_bytes"], row["workspace_bytes"]) != (1344, 4224):
            raise ValueError("OBJ02 fixed source lost its actual legacy storage witness")


def native_work_failures(measured):
    if measured["channel"] != "work" or len(measured["records"]) != len(measured["work"]):
        raise ValueError("OBJ02 work oracle lacks a complete observed native product")
    check_native_semantics(measured["records"], measured["stage"])
    failures = []
    for row, work in zip(measured["records"], measured["work"], strict=True):
        if work["case"] != row["case"]:
            raise ValueError("OBJ02 work oracle borrowed a different case")
        counts = work_totals(work, measured["stage"])

        def maximum(name, actual, bound):
            if actual > bound:
                failures.append((row["case"], name, actual, bound))

        maximum("workspace", row["workspace_bytes"], 0)
        maximum("plan-size", row["plan_bytes"], 320)
        maximum("queue-shifts", counts[Kind.QUEUE_RECORD_MOVE], 0)
        if row["operation"] in ("construct", "checked"):
            maximum("copied-bytes", counts[Kind.COPIED_BYTE], 320)
            maximum("zeroed-bytes", counts[Kind.CLEARED_BYTE], 640)
            if row["result"] == 0:
                priorities = counts[Kind.COMPARISON]
                maximum("priority-prefix", priorities, 16 * (row["renewed"] + 1))
                maximum("selected-probes", counts[Kind.INDEX_PROBE] + counts[Kind.RECORD_VISIT],
                        128 + 64 * row["renewed"] + 16 * priorities)
        if row["operation"] == "apply":
            maximum("apply-stores", counts[Kind.COPY_UNIT], 48 + 4 * row["renewed"])
    return tuple(failures)


def work_totals(row, stage):
    producer = PRODUCER_BASE + 256 * int(stage == "current")
    snapshot = Snapshot(
        row["producer"], row["case"], row["owner_mask"],
        tuple(tuple(values) for values in row["counts"]),
        row["version"], row["reserved0"], row["reserved1"],
    )
    return compose_snapshots(
        (Capture(Status.OK, snapshot),), capture_label=row["case"],
        expected_producers={producer: (1 << Owner.KERNEL_OBJECTS) | (1 << Owner.KERNEL_SCHEDULER)},
    ).per_kind


def parse_native_output(output, channel, stage):
    if stage not in ("baseline", "current") or channel not in ("ordinary", "work"):
        raise ValueError("OBJ02 native transcript has an unknown stage or channel")
    if type(output) is not str or not output.endswith("\n"):
        raise ValueError("OBJ02 native transcript is not complete text")
    records, work = [], []
    lines = output.splitlines()
    for line in lines[:-1]:
        if line.startswith("MICROS_OBJ02_WORK "):
            row = json.loads(line.removeprefix("MICROS_OBJ02_WORK "), object_pairs_hook=json_pairs)
            record_fields(row, ("version", "reserved0", "producer", "case", "owner_mask", "reserved1", "counts"))
            if (
                channel != "work" or row["case"] != len(records) or len(work) + 1 != len(records)
                or any(type(row[key]) is not int for key in row if key != "counts")
                or type(row["counts"]) is not list
                or any(type(counts) is not list for counts in row["counts"])
            ):
                raise ValueError("OBJ02 work has malformed capture identity")
            work_totals(row, stage)
            work.append(row)
            continue
        if not line.startswith("MICROS_OBJ02_CASE "):
            raise ValueError("OBJ02 native transcript has an unknown record")
        row = json.loads(line.removeprefix("MICROS_OBJ02_CASE "), object_pairs_hook=json_pairs)
        record_fields(row, CASE_FIELDS)
        if (
            any(type(value) is not int or not 0 <= value < 1 << 64
                for key, value in row.items() if key not in ("name", "operation"))
            or any(type(row[key]) is not str or re.fullmatch(r"[a-z-]+", row[key]) is None
                   for key in ("name", "operation"))
            or row["version"] != 1 or row["case"] != len(records) + 1
            or row["plan_preserved"] not in (0, 1)
            or channel == "work" and len(work) != len(records)
        ):
            raise ValueError("OBJ02 native case has malformed fields or order")
        records.append(row)
    if (
        not records or lines[-1] != "MICROS_OBJ02_NATIVE_PASS cases=" + str(len(records))
        or channel == "work" and len(work) != len(records)
    ):
        raise ValueError("OBJ02 native transcript lacks complete coverage")
    check_native_semantics(records, stage)
    return tuple(records), tuple(work)


def target_failures(captures):
    return tuple(
        (row["class"], row["counts"][Owner.KERNEL_EXECUTION][Kind.COMPLETE_VALIDATOR_CALL], wanted)
        for row, wanted in zip(captures, (1, 2, 1), strict=True)
        if row["counts"][Owner.KERNEL_EXECUTION][Kind.COMPLETE_VALIDATOR_CALL] != wanted
    )


def parse_target_output(output, stage, exit_code):
    if stage not in ("baseline", "current") or type(exit_code) is not int or type(output) is not str:
        raise ValueError("OBJ02 target has an unknown stage or malformed execution result")
    captures, ended, summary = [], False, None
    for line in output.splitlines():
        if not line.startswith("# MICROS_OBJ02_"):
            continue
        match = re.fullmatch(r"# (MICROS_OBJ02_CONTEXT(?:_COUNTS|_END)?) (.*)", line)
        if match is None or ended:
            raise ValueError("OBJ02 target has an unknown, repeated or late witness")
        pairs = [re.fullmatch(r"([a-z0-9_]+)=(0x[0-9a-f]{16})", field) for field in match[2].split(" ")]
        if any(pair is None for pair in pairs):
            raise ValueError("OBJ02 target witness has malformed fields")
        row = json_pairs((pair[1], int(pair[2], 16)) for pair in pairs)
        if match[1] == "MICROS_OBJ02_CONTEXT":
            record_fields(row, TARGET_FIELDS)
            if (
                len(captures) >= 3 or row["class"] != len(captures)
                or captures and len(captures[-1]["counts"]) != len(Owner)
                or row["mode"] != (2 if row["class"] == 2 else 1)
                or row["selected_slot"] >= 128 or not 0 < row["selected_generation"] < 1 << 32
                or row["outgoing_slot"] >= 128 or not 0 <= row["outgoing_generation"] < 1 << 32
                or row["capture"] in {capture["capture"] for capture in captures}
            ):
                raise ValueError("OBJ02 target capture has incomplete membership or borrowed identity")
            outgoing = row["outgoing_slot"], row["outgoing_generation"]
            selected = row["selected_slot"], row["selected_generation"]
            if (
                row["class"] == 0 and outgoing != selected
                or row["class"] == 1 and (outgoing == selected or outgoing[1] == 0)
                or row["class"] == 2 and outgoing != (0, 0)
            ):
                raise ValueError("OBJ02 target capture misattributes outgoing context authority")
            captures.append({**row, "counts": []})
        elif match[1] == "MICROS_OBJ02_CONTEXT_COUNTS":
            record_fields(row, ("capture", "owner", *TARGET_COUNT_FIELDS))
            if (
                not captures or row["capture"] != captures[-1]["capture"]
                or row["owner"] != len(captures[-1]["counts"]) or row["owner"] >= len(Owner)
            ):
                raise ValueError("OBJ02 target snapshot omitted, duplicated or borrowed an owner")
            captures[-1]["counts"].append(tuple(row[name] for name in TARGET_COUNT_FIELDS))
        else:
            record_fields(row, ("valid", "modes", "captures"))
            summary, ended = row, True
    if (
        not ended or len(captures) != 3 or summary["modes"] != 15
        or summary["valid"] != int(stage == "current")
        or summary["captures"] < max(row["capture"] for row in captures)
    ):
        raise ValueError("OBJ02 target lacks its complete successful scope inventory")
    for row in captures:
        snapshot = Snapshot(
            row["producer"], row["capture"], row["owner_mask"], tuple(row["counts"]),
            row["version"], row["reserved0"], row["reserved1"],
        )
        totals = compose_snapshots(
            (Capture(Status.OK, snapshot),), capture_label=row["capture"],
            expected_producers={0x4f424a3032540001: 1 << Owner.KERNEL_EXECUTION},
        ).per_kind
        if any(value for kind, value in enumerate(totals) if kind != Kind.COMPLETE_VALIDATOR_CALL):
            raise ValueError("OBJ02 target context snapshot contains undeclared work")
    failures = target_failures(captures)
    if failures != (((0, 2, 1),) if stage == "baseline" else ()):
        raise ValueError("OBJ02 target lacks its exact fixed/current context-validation witness")
    if re.search(r"^# MICROS_PANIC\b", output, re.M):
        raise ValueError("OBJ02 target panic is not an accepted work divergence")
    failures_in_output = re.findall(r"^# MICROS_TEST_FAILURE (.*)$", output, re.M)
    markers = tuple(len(re.findall(r"^# " + re.escape(marker) + r"$", output, re.M)) for marker in TARGET_MARKERS)
    if stage == "baseline":
        if exit_code not in (1, 2) or failures_in_output != ["scheduler-test"] or markers != (0, 0, 0):
            raise ValueError("OBJ02 target Red lacks its prescribed post-cleanup failure")
    elif exit_code != 0 or failures_in_output or markers != (1, 1, 1):
        raise ValueError("OBJ02 target lost an original scheduler, IPC-return or TTY-trap acceptance marker")
    return tuple(captures)


def check_native_stage(products, stage):
    if type(products) is not dict or set(products) != {"ordinary", "work"}:
        raise ValueError("OBJ02 native stage lacks its exact ordinary/observed pair")
    for channel, product in products.items():
        if product["stage"] != stage or product["channel"] != channel:
            raise ValueError("OBJ02 native stage borrowed a different product")
        check_native_semantics(product["records"], stage)
    if products["ordinary"]["work"] or products["ordinary"]["records"] != products["work"]["records"]:
        raise ValueError("OBJ02 passive native observation changed semantic state")
    failures = native_work_failures(products["work"])
    if stage == "baseline":
        required = {"workspace", "plan-size", "copied-bytes", "zeroed-bytes", "queue-shifts", "apply-stores"}
        if not required <= {row[1] for row in failures}:
            raise ValueError("OBJ02 baseline lacks all independent projection/work divergences")
        cases = {row["case"]: row for row in products["work"]["records"]}
        if not any(
            metric == "queue-shifts" and cases[case]["threads"] == 128 and cases[case]["name"] == "all-zero"
            for case, metric, *_ in failures
        ):
            raise ValueError("OBJ02 baseline omitted maximum-queue repeated-rotation work")
    return failures


def check_native_products(root, executable_paths):
    from tests.host.common_scheduler_return_source import RECIPE_ADMISSION, native_products
    from tests.host.object_hart_validation_evidence import observation_symbols
    from tools.generate_common_scheduler_return_fixture import source_bundle

    root = Path(root).resolve()
    build = root / "build/host-debug"
    expected = tuple(str(build / name) for name in RECIPE_ADMISSION["native_targets"])
    if tuple(executable_paths) != expected:
        raise ValueError("current return orchestration requires its two owned executables")
    before = {path: read_regular(path)[1] for path in expected}
    nm = native_symbols_tool()
    measured = {}
    generated_inputs = {}
    for (stage, channel), path in zip(native_products(), expected, strict=True):
        _, outputs = source_bundle(root, stage, channel)
        directory = build / "common-scheduler-return" / stage / channel
        for relative, raw in outputs.items():
            generated = directory / relative
            actual, identity = read_regular(generated)
            if actual != raw or identity["mode"] != 0o644:
                raise ValueError("OBJ02 compiled input is stale or belongs to another product")
            generated_inputs[str(generated)] = identity
        symbols = subprocess.check_output((nm["path"], "-a", path), cwd=root, text=True, timeout=30)
        wanted = frozenset() if channel == "ordinary" else frozenset((
            "micros_test_work_begin", "micros_test_work_finish", "micros_test_work_record",
        ))
        if observation_symbols(symbols) != wanted:
            raise ValueError("OBJ02 compiled product has the wrong collector linkage")
        completed = subprocess.run(
            (path, stage, channel), cwd=root, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30,
        )
        if completed.returncode or completed.stderr:
            raise ValueError("OBJ02 compiled native product failed:\n" + completed.stdout + completed.stderr)
        records, work = parse_native_output(completed.stdout, channel, stage)
        measured[channel] = {"stage": stage, "channel": channel, "records": records, "work": work}
        print(f"MICROS_OBJ02_NATIVE_ACCEPTED stage={stage} channel={channel} cases={len(records)}", flush=True)
    if (
        {path: read_regular(path)[1] for path in expected} != before
        or {path: read_regular(path)[1] for path in generated_inputs} != generated_inputs
        or native_symbols_tool() != nm
    ):
        raise ValueError("OBJ02 compiled products or native tools changed during execution")
    failures = check_native_stage(measured, "current")
    if failures:
        raise ValueError("OBJ02 current native code exceeds the independent work/storage bounds: " + repr(failures[:16]))
    return measured
