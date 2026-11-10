"""Native execution and independent OBJ01 work/semantic evidence."""

from dataclasses import asdict
import hashlib
import io
import json
from functools import lru_cache
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile
from types import MappingProxyType
import unittest

from tests.host.runtime_memory_evidence import json_pairs, native_compiler
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
FIXTURE_PATHS = (
    "tests/host/object_hart_validation_test.c",
    "tests/host/object_hart_validation_test.h",
    "tests/host/object_hart_validation_source.py",
    "tests/host/object_hart_validation_evidence.py",
    "tools/generate_object_hart_validation_fixture.py",
    "tests/host/selected_release_handoff_source.py",
    "tests/host/selected_address_space_source.py",
    "tests/host/selected_ownership_source.py",
    "tests/host/c_source_records.py",
    "tests/host/work_count_lexical.py",
    "tests/host/work_count_types.py",
    "tests/host/work_count_snapshots.py",
    "docs/architecture/selected-object-and-hart-validation.md",
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
        add("return-plan", variant, threads, 1, result=15, diagnostic=15)
    add("return-plan", "unselected-hart-queue", 2, 2, diagnostic=15, result=0 if stage == "current" else 15)
    for operation in SCHEDULER_OPERATIONS:
        for threads in (2, 64):
            add(operation, "valid", threads, 2, selected_thread=threads - 1,
                requests=threads if operation == "ipc-batch" else 0)
    return tuple(cases)


def check_semantics(records, stage, *, historical=False):
    from tests.host.common_scheduler_return_source import STATE_WORK_CONTRACT, _current_authority

    expected = expected_cases(stage)
    common_return = stage == "current" and not historical and _current_authority(Path(__file__).resolve().parents[2])
    if len(records) != len(expected):
        raise ValueError("OBJ01 semantic corpus is incomplete")
    readonly = {
        "hart-context", "is-current", "validate", "admit-preflight", "pick",
        "ipc-preflight", "ipc-wake", "return-plan",
    }
    for row, wanted in zip(records, expected):
        if common_return and wanted["operation"] == "return-plan" and wanted["variant"] in STATE_WORK_CONTRACT["legacy_scoped_cases"]:
            wanted = {**wanted, "result": 0}
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


def fixed_native_inputs(root):
    from tests.host.object_hart_validation_source import SOURCE_PROJECTION

    root = Path(root).resolve()
    contract = (root / "docs/architecture/selected-object-and-hart-validation.md").read_text()
    references = re.findall(r"^runtime:\s+([0-9a-f]{40})$", contract, re.M)
    trees = re.findall(r"^tree:\s+([0-9a-f]{40})$", contract, re.M)
    if references != [SOURCE_PROJECTION["source_reference"]] or trees != [SOURCE_PROJECTION["source_tree"]]:
        raise ValueError("OBJ01 native input reference differs from its fixed Accepted runtime")
    reference, tree = references[0], trees[0]
    return _fixed_native_inputs(str(root), reference, tree)


@lru_cache(maxsize=4)
def _fixed_native_inputs(root, reference, tree):
    actual = subprocess.check_output(
        ("git", "rev-parse", reference + "^{tree}"), cwd=root, text=True, timeout=30,
    ).strip()
    if actual != tree:
        raise ValueError("OBJ01 fixed native commit/tree differ from the Accepted contract")
    members = set((*NATIVE_SOURCES, *NATIVE_HEADERS))
    expected = {}
    for entry in subprocess.check_output(
        ("git", "ls-tree", "-r", "-z", reference, "--", *sorted(members)), cwd=root, timeout=30,
    ).split(b"\0"):
        if not entry:
            continue
        metadata, separator, raw_path = entry.partition(b"\t")
        fields = metadata.decode().split()
        path = raw_path.decode()
        if (
            not separator or len(fields) != 3 or fields[:2] != ["100644", "blob"]
            or path not in members or path in expected
        ):
            raise ValueError("OBJ01 fixed native Git inputs contain a nonordinary member")
        expected[path] = fields[2]
    if set(expected) != members:
        raise ValueError("OBJ01 fixed native Git inventory is incomplete")
    archive = subprocess.check_output(
        ("git", "-c", "tar.umask=022", "archive", reference, *sorted(members)), cwd=root, timeout=30,
    )
    result = {}
    with tarfile.open(fileobj=io.BytesIO(archive)) as entries:
        for member in entries:
            if member.isdir():
                continue
            if (
                not member.isfile() or member.name not in members or member.name in result
                or member.mode & 0o777 != 0o644
            ):
                raise ValueError("OBJ01 fixed native archive has a borrowed or invalid member")
            raw = entries.extractfile(member).read()
            if hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest() != expected[member.name]:
                raise ValueError("OBJ01 native archive does not contain its exact Git blob")
            result[member.name] = raw
    if set(result) != members or len(result) != 26:
        raise ValueError("OBJ01 fixed native source/header closure is incomplete")
    return MappingProxyType(result)


def parse_native_output(output, channel, stage="baseline", *, historical=False):
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
                historical or type(row) is not dict or row.get("case") != len(records)
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
                or not historical and len(plans) != len(records)
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
        if not historical and len(plans) != len(records):
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
    if not historical:
        validate_plan_observations(records, plans)
    return tuple(records), tuple(work), tuple(plans)


def native_command(root, compiler, generated, executable, stage, channel):
    return (
        compiler["path"], "-std=c17", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
        "-Wmissing-prototypes", "-Wstrict-prototypes", "-fno-builtin",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
        "-DMICROS_OBJ01_NATIVE_STAGE=" + str(int(stage == "current")),
        *((
            "-DMICROS_TEST_WORK_COUNTS=1",
            "-DMICROS_OBJ01_NATIVE_CHANNEL=" + str(("aggregate", "stack").index(channel)),
        ) if channel != "ordinary" else ()),
        "-I", str(generated), "-I", str(generated / "include"),
        "-I", str(root), "-I", str(root / "include"),
        *(str(generated / path) for path in NATIVE_SOURCES),
        str(root / FIXTURE_PATHS[0]),
        *((str(root / "tests/work/work_counts.c"),) if channel != "ordinary" else ()),
        "-o", str(executable),
    )


def observation_symbols(output):
    return frozenset(
        line.split()[-1].lstrip("_") for line in output.splitlines()
        if line.split() and "micros_test_work_" in line.split()[-1]
    )


def measure_native_channel(root, stage, channel):
    from tools.generate_object_hart_validation_fixture import prepare_sources

    if stage not in ("baseline", "current") or channel not in ("ordinary", "aggregate", "stack"):
        raise ValueError("OBJ01 native execution has an unknown source stage or channel")
    root = Path(root).resolve()
    compiler, nm = native_compiler(root), native_symbols_tool()
    fixtures = {path: read_regular(root / path)[1] for path in (
        *FIXTURE_PATHS, "tests/work/work_counts.c", "tests/work/work_counts.h",
        "include/micros/work_observation.h",
    )}
    with tempfile.TemporaryDirectory(prefix="micros-obj01-" + stage + "-" + channel + "-") as directory:
        directory = Path(directory).resolve()
        generated, executable = directory / "source", directory / "native"
        source_inputs, generated_inputs = prepare_sources(root, generated, stage, channel)
        command = native_command(root, compiler, generated, executable, stage, channel)
        build = subprocess.run(
            command, cwd=root, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=180,
        )
        if build.returncode or build.stderr:
            raise ValueError("OBJ01 fixed native compilation failed:\n" + build.stdout + build.stderr)
        executable_identity = read_regular(executable)[1]
        symbols = subprocess.check_output((nm["path"], "-a", str(executable)), cwd=root, text=True, timeout=30)
        observed_symbols = observation_symbols(symbols)
        expected_symbols = set() if channel == "ordinary" else {
            "micros_test_work_begin", "micros_test_work_finish", "micros_test_work_record",
        }
        if observed_symbols != expected_symbols:
            raise ValueError("OBJ01 fixture has the wrong observation linkage")
        result = subprocess.run(
            (str(executable), stage, channel), cwd=root, stdin=subprocess.DEVNULL,
            capture_output=True, text=True, timeout=30,
        )
        if result.returncode or result.stderr:
            raise ValueError("OBJ01 fixed native controls failed:\n" + result.stdout + result.stderr)
        if (
            read_regular(executable)[1] != executable_identity
            or {path: read_regular(generated / path)[1] for path in generated_inputs} != generated_inputs
            or stage == "current" and {path: read_regular(root / path)[1] for path in source_inputs} != source_inputs
        ):
            raise ValueError("OBJ01 source or native artifacts changed during execution")
        records, work, plans = parse_native_output(result.stdout, channel, stage)
        measured = {
            "version": 1, "stage": stage, "command": command, "compiler": compiler, "nm": nm,
            "fixtures": fixtures, "source_inputs": source_inputs, "generated_inputs": generated_inputs,
            "output": result.stdout, "records": records, "work": work, "plans": plans, "channel": channel,
            "executable": executable_identity,
            "build_stdout": build.stdout, "build_stderr": build.stderr, "build_exit": build.returncode,
            "execution_exit": result.returncode, "execution_stderr": result.stderr,
            "nm_command": (nm["path"], "-a", str(executable)), "nm_output": symbols,
            "arguments": (str(executable), stage, channel), "nm_exit": 0,
            "output_sha256": hashlib.sha256(result.stdout.encode()).hexdigest(),
        }
    if (
        {path: read_regular(root / path)[1] for path in fixtures} != fixtures
        or native_compiler(root) != compiler or native_symbols_tool() != nm
    ):
        raise ValueError("OBJ01 fixture or native tool changed during execution")
    return measured


def measure_fixed_fixture(root, channel="ordinary"):
    return measure_native_channel(root, "baseline", channel)


def measure_native(root, phase):
    if phase not in ("red", "green", "recertify"):
        raise ValueError("OBJ01 native measurement has an unknown author phase")
    result = {}
    for stage in (("baseline",) if phase == "red" else ("baseline", "current")):
        result[stage] = {}
        for channel in ("ordinary", "aggregate", "stack"):
            print(f"MICROS_OBJ01_NATIVE_BEGIN stage={stage} channel={channel}", flush=True)
            result[stage][channel] = measure_native_channel(root, stage, channel)
            print(f"MICROS_OBJ01_NATIVE_COMPLETE stage={stage} channel={channel} cases=171", flush=True)
    return result


def _require_discriminating_red(products, failures):
    rows = products["ordinary"]["records"]

    def witnessed(group, metric, predicate):
        return any(item[1] == metric and predicate(rows[item[0] - 1]) for item in failures[group])

    controls = (
        {rows[item[0] - 1]["operation"] for item in failures[WORK_ORACLES[0]] if item[1] == "complete-validators"}
        == set(SCHEDULER_OPERATIONS),
        witnessed(WORK_ORACLES[0], "selected-indices", lambda row: row["operation"] == "ipc-batch" and row["requests"] == 128),
        witnessed(WORK_ORACLES[1], "geometry-comparisons",
                  lambda row: row["variant"] == "regular" and row["threads"] == 128 and row["harts"] == 8),
        *(witnessed(WORK_ORACLES[2], "direct-records",
                    lambda row, operation=operation: row["operation"] == operation and row["harts"] == 8)
          for operation in ("hart-context", "is-current", "capture")),
        witnessed(WORK_ORACLES[2], "unsupported-current-diagnostic",
                  lambda row: row["operation"] == "validate" and row["variant"] == "unscheduled-current"),
    )
    if not all(controls):
        raise ValueError("OBJ01 actual fixed work did not expose every declared divergence")


def compare_source_stages(baseline, current):
    from tests.host.common_scheduler_return_evidence import compare_plan_observations

    old, new = baseline["ordinary"]["records"], current["ordinary"]["records"]
    check_semantics(old, "baseline")
    check_semantics(new, "current")
    compare_plan_observations(old, new, baseline["ordinary"]["plans"], current["ordinary"]["plans"])


class ObjectHartValidationOracles(unittest.TestCase):
    def test_local_validation_work(self):
        self.assertFalse(self.failures[WORK_ORACLES[0]], repr(self.failures[WORK_ORACLES[0]][:12]))
        self.assertFalse(self.audits["migrated"], repr(self.audits["migrated"]))

    def test_stack_geometry_work(self):
        self.assertFalse(self.failures[WORK_ORACLES[1]], repr(self.failures[WORK_ORACLES[1]][:12]))

    def test_direct_current_ownership(self):
        self.assertFalse(self.failures[WORK_ORACLES[2]], repr(self.failures[WORK_ORACLES[2]][:12]))


def run_oracles(root, measured, audits, phase, reference=None, *, current_sources=None):
    from tests.host.evidence_authority_types import ObservedResult, OracleResult, RunStatus, digest
    from tests.host.object_hart_validation_authority import ORACLE_RULES
    from tests.host.object_hart_validation_source import measure_audit_boundaries

    expected_stages = {"baseline"} if phase == "red" else {"baseline", "current"}
    if phase not in ("red", "green", "recertify") or type(measured) is not dict or set(measured) != expected_stages:
        raise ValueError("OBJ01 author measurement omitted a required actual native product")
    baseline_failures = work_failures(measured["baseline"], "baseline")
    _require_discriminating_red(measured["baseline"], baseline_failures)
    stage = "baseline" if phase == "red" else "current"
    if digest(audits) != digest(measure_audit_boundaries(root, stage, current_sources=current_sources)):
        raise ValueError("OBJ01 execution substituted its complete caller census")
    if phase == "red":
        if reference is not None:
            raise ValueError("OBJ01 baseline cannot borrow another attempt")
        failures = baseline_failures
    else:
        if type(reference) is not dict or set(reference) != {"baseline"}:
            raise ValueError("OBJ01 current execution lacks its same-attempt actual Red")
        prior = reference["baseline"]
        _require_discriminating_red(prior, work_failures(prior, "baseline"))
        for channel in ("ordinary", "aggregate", "stack"):
            if (
                digest(prior[channel]["records"]) != digest(measured["baseline"][channel]["records"])
                or digest(prior[channel]["work"]) != digest(measured["baseline"][channel]["work"])
                or digest(prior[channel]["plans"]) != digest(measured["baseline"][channel]["plans"])
            ):
                raise ValueError("OBJ01 fixed controls changed between Red and current execution")
        compare_source_stages(measured["baseline"], measured["current"])
        failures = work_failures(measured["current"], "current")
    outcomes, output = [], io.StringIO()
    for identifier, test, property_name, _, _, _ in ORACLE_RULES:
        case = ObjectHartValidationOracles(test.rsplit(".", 1)[1])
        case.failures, case.audits = failures, audits
        result = unittest.TextTestRunner(stream=output, verbosity=2).run(unittest.TestSuite((case,)))
        if result.testsRun != 1 or result.errors or result.skipped:
            raise ValueError("OBJ01 oracle was skipped or failed outside its measured assertion")
        if phase == "red":
            if result.wasSuccessful():
                raise ValueError("OBJ01 fixed runtime did not fail every replacement property")
            observed = ObservedResult.EXPECTED_FAILURE
        else:
            observed = ObservedResult.PASS if result.wasSuccessful() else ObservedResult.FAIL
        outcomes.append(OracleResult(identifier, RunStatus.RUN, observed, property_name))
    return tuple(outcomes), output.getvalue()


def execution_log(measured, audits, results, oracle_output, *, phase, attempt):
    return json.loads(json.dumps({
        "version": 1, "phase": phase, "attempt": attempt, "native": measured,
        "audits": audits, "results": [asdict(result) for result in results], "oracle_output": oracle_output,
    }, allow_nan=False))


def _replay_sources(context, paths):
    from types import MappingProxyType
    from tools.stack_bound_manifest import owned_artifact

    sources = {}
    for path in paths:
        if path not in context.files or path in sources:
            raise ValueError("OBJ01 replay source is outside its captured input namespace")
        raw, identity = read_regular(owned_artifact(context.root, path))
        if identity != context.files[path]:
            raise ValueError("OBJ01 replay source changed after author input capture")
        sources[path] = raw
    return MappingProxyType(sources)


def _validate_native_replay(context, products, stage, *, current_sources=None):
    from tests.host.diagnostic_history_authority import record_fields, record_strings
    from tests.host.evidence_authority_types import digest
    from tools.generate_object_hart_validation_fixture import source_bundle

    if type(products) is not dict or set(products) != {"ordinary", "aggregate", "stack"}:
        raise ValueError("OBJ01 native replay omitted a product")
    directories = set()
    fixture_paths = (*FIXTURE_PATHS, "tests/work/work_counts.c", "tests/work/work_counts.h", "include/micros/work_observation.h")
    for channel, native in products.items():
        record_fields(native, (
            "version", "stage", "command", "compiler", "nm", "fixtures", "source_inputs", "generated_inputs",
            "output", "records", "work", "plans", "channel", "executable", "build_stdout", "build_stderr", "build_exit",
            "execution_exit", "execution_stderr", "nm_command", "nm_output", "arguments", "nm_exit", "output_sha256",
        ))
        if (
            type(native["version"]) is not int or native["version"] != 1
            or native["stage"] != stage or native["channel"] != channel
            or native["compiler"] != context.native_compiler or native["nm"] != context.native_nm
        ):
            raise ValueError("OBJ01 native replay borrowed another source stage or tool")
        arguments = record_strings(native["arguments"])
        if len(arguments) != 3 or arguments[1:] != (stage, channel):
            raise ValueError("OBJ01 native replay changed its exact executable invocation")
        executable = Path(arguments[0])
        directory = executable.parent
        if (
            not executable.is_absolute() or executable.name != "native" or directory in directories
            or not directory.name.startswith("micros-obj01-" + stage + "-" + channel + "-")
            or directory.parent.resolve() != Path(tempfile.gettempdir()).resolve()
        ):
            raise ValueError("OBJ01 native replay names a borrowed or unowned execution")
        directories.add(directory)
        if (
            record_strings(native["command"]) != native_command(
                context.root, context.native_compiler, directory / "source", executable, stage, channel,
            )
            or record_strings(native["nm_command"]) != (context.native_nm["path"], "-a", str(executable))
        ):
            raise ValueError("OBJ01 native replay changed its compile/link/symbol command")
        inputs, outputs = source_bundle(context.root, stage, channel, current_sources=current_sources)
        generated = {
            path: {"kind": "file", "mode": 0o644, "size": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}
            for path, raw in outputs.items()
        }
        if (
            native["source_inputs"] != inputs or native["generated_inputs"] != generated
            or native["fixtures"] != {path: context.files[path] for path in fixture_paths}
            or type(native["build_stdout"]) is not str
            or any(type(native[name]) is not int or native[name] != 0 for name in ("build_exit", "execution_exit", "nm_exit"))
            or native["build_stderr"] != "" or native["execution_stderr"] != ""
            or type(native["nm_output"]) is not str or not native["nm_output"]
            or type(native["output"]) is not str
            or hashlib.sha256(native["output"].encode()).hexdigest() != native["output_sha256"]
        ):
            raise ValueError("OBJ01 replay substituted its inputs or lacks a complete successful execution")
        symbols = frozenset() if channel == "ordinary" else frozenset((
            "micros_test_work_begin", "micros_test_work_finish", "micros_test_work_record",
        ))
        if observation_symbols(native["nm_output"]) != symbols:
            raise ValueError("OBJ01 replay changed its actual observation linkage")
        record_fields(native["executable"], ("kind", "mode", "size", "sha256"))
        artifact = native["executable"]
        if (
            artifact["kind"] != "file" or type(artifact["mode"]) is not int or not artifact["mode"] & 0o100
            or not 0 < artifact["mode"] <= 0o777 or type(artifact["size"]) is not int or artifact["size"] <= 0
            or type(artifact["sha256"]) is not str or re.fullmatch(r"[a-f0-9]{64}", artifact["sha256"]) is None
        ):
            raise ValueError("OBJ01 replay lacks a complete executable artifact identity")
        records, work, plans = parse_native_output(native["output"], channel, stage)
        if (
            digest(records) != digest(native["records"]) or digest(work) != digest(native["work"])
            or digest(plans) != digest(native["plans"])
        ):
            raise ValueError("OBJ01 replay changed the observations from its actual output")
    return work_failures(products, stage)


def validate_execution_log(context, replay, *, phase, attempt, reference=None):
    from tests.host.diagnostic_history_authority import record_fields
    from tests.host.object_hart_validation_source import AUDIT_SOURCES, measure_audit_boundaries

    if phase not in ("red", "green", "recertify") or type(attempt) is not str or re.fullmatch(r"[a-f0-9]{32}", attempt) is None:
        raise ValueError("OBJ01 replay has an unadmitted phase or attempt")
    record_fields(replay, ("version", "phase", "attempt", "native", "audits", "results", "oracle_output"))
    stages = ("baseline",) if phase == "red" else ("baseline", "current")
    if (
        type(replay["version"]) is not int or replay["version"] != 1
        or replay["phase"] != phase or replay["attempt"] != attempt
        or type(replay["oracle_output"]) is not str or not replay["oracle_output"]
        or type(replay["native"]) is not dict or set(replay["native"]) != set(stages)
    ):
        raise ValueError("OBJ01 replay belongs to a different phase, attempt or source stage")
    native_paths = tuple(fixed_native_inputs(context.root))
    audit_paths = {*AUDIT_SOURCES, "kernel/scheduler/scheduler_core_validate.c"}
    current = None if phase == "red" else _replay_sources(context, sorted({*native_paths, *audit_paths}))
    for stage in stages:
        _validate_native_replay(
            context, replay["native"][stage], stage,
            current_sources=None if stage == "baseline" else {path: current[path] for path in native_paths},
        )
    if phase != "red":
        if type(reference) is not dict or set(reference) != {"baseline"}:
            raise ValueError("OBJ01 current replay lacks its same-attempt fixed execution")
        _validate_native_replay(context, reference["baseline"], "baseline")
    elif reference is not None:
        raise ValueError("OBJ01 original replay cannot borrow another attempt")
    audit_sources = None if current is None else {path: current[path] for path in audit_paths}
    audits = measure_audit_boundaries(context.root, stages[-1], current_sources=audit_sources)
    results, _ = run_oracles(
        context.root, replay["native"], replay["audits"], phase, reference,
        current_sources=audit_sources,
    )
    if (
        json.loads(json.dumps(audits)) != replay["audits"]
        or replay["results"] != json.loads(json.dumps([asdict(result) for result in results]))
    ):
        raise ValueError("OBJ01 replay outcomes do not follow the actual complete executions")
    return results


def check_native_products(root, executable_paths):
    from tests.host.object_hart_validation_source import RECIPE_ADMISSION, measure_audit_boundaries, native_products
    from tools.generate_object_hart_validation_fixture import source_bundle

    root = Path(root).resolve()
    build = root / "build/host-debug"
    expected = tuple(str(build / name) for name in RECIPE_ADMISSION["native_targets"])
    if tuple(executable_paths) != expected:
        raise ValueError("OBJ01 native orchestration requires its exact six owned executables")
    before = {path: read_regular(path)[1] for path in expected}
    nm = native_symbols_tool()
    measured = {"baseline": {}, "current": {}}
    generated_inputs = {}
    for (stage, channel), path in zip(native_products(), expected, strict=True):
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
        measured[stage][channel] = {"stage": stage, "channel": channel, "records": records, "work": work, "plans": plans}
        print(f"MICROS_OBJ01_NATIVE_ACCEPTED stage={stage} channel={channel} cases={len(records)}", flush=True)
    if (
        {path: read_regular(path)[1] for path in expected} != before
        or {path: read_regular(path)[1] for path in generated_inputs} != generated_inputs
        or native_symbols_tool() != nm
    ):
        raise ValueError("OBJ01 compiled products or native tools changed during execution")
    baseline_failures = work_failures(measured["baseline"], "baseline")
    _require_discriminating_red(measured["baseline"], baseline_failures)
    compare_source_stages(measured["baseline"], measured["current"])
    failures = work_failures(measured["current"], "current")
    if any(failures.values()) or measure_audit_boundaries(root, "current")["migrated"]:
        raise ValueError("OBJ01 current production does not satisfy its three independent oracles: " + repr(failures))
    return measured
