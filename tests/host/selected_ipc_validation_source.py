"""Finite current IPC primitive and target-capture placement contracts."""

from pathlib import Path
import re

from tests.host.c_source_records import c_lexical_text, file_scope_objects, function_records, tokens
from tests.host.native_observations import bind_sites, declarations, direct_calls, validate_source
from tests.host.source_snapshot import source_bytes
from tests.host.work_count_lexical import (
    compound_layout, enclosing_regions, matching_tokens, positional_tokens, region_header,
)


ROOT = Path(__file__).resolve().parents[2]
INDEX_FIELDS = frozenset((
    "processes", "threads", "harts", "endpoints", "profiles", "pending_events",
    "ready_head", "ready_tail", "transitions",
))
AUDIT_SITES = {
    "kernel/objects/kernel_objects.c": ("micros_kernel_objects_validate", "objects", "KERNEL_OBJECTS"),
    "kernel/objects/kernel_object_validation.c": ("micros_kernel_objects_validate_base", "object-base", "KERNEL_OBJECTS"),
    "kernel/scheduler/scheduler_core_validate.c": ("micros_scheduler_core_validate", "scheduler", "KERNEL_SCHEDULER"),
    "kernel/endpoint/endpoint_registry.c": ("micros_endpoint_registry_validate", "registry", "KERNEL_ENDPOINT"),
    "kernel/endpoint/endpoint_object_validation.c": ("micros_endpoint_registry_validate_objects", "endpoints", "KERNEL_ENDPOINT"),
    "kernel/ipc/ipc_runtime.c": ("micros_ipc_runtime_validate", "runtime", "KERNEL_IPC"),
}
TARGET_PATHS = (*AUDIT_SITES, "kernel/ipc/ipc_buffer.c", "kernel/ipc/ipc_syscall.c",
                "kernel/scheduler/scheduler_completion.c", "tests/qemu/selected_ipc_work.c",
                "kernel/tests/ipc/ipc_syscall_test_report.c")
AUDITS = frozenset(row[0] for row in AUDIT_SITES.values())
RETAINED_AUDITS = (
    ("kernel/boot/bootstrap_control_core.c", "micros_bootstrap_control_validate", "micros_endpoint_registry_validate_objects", 1),
    ("kernel/boot/bootstrap_preparation_rollback.c", "bootstrap_preparation_rollback_preparation", "micros_ipc_runtime_validate", 1),
    ("kernel/endpoint/endpoint_object_validation.c", "micros_endpoint_registry_validate_objects", "micros_endpoint_registry_validate", 1),
    ("kernel/grant/grant.c", "micros_grant_registry_validate", "micros_endpoint_registry_validate_objects", 1),
    ("kernel/grant/grant_runtime.c", "micros_grant_runtime_initialize", "micros_ipc_runtime_validate", 1),
    ("kernel/ipc/ipc_runtime.c", "micros_ipc_runtime_initialize", "micros_endpoint_registry_validate_objects", 1),
    ("kernel/ipc/ipc_runtime.c", "micros_ipc_runtime_validate", "micros_endpoint_registry_validate_objects", 1),
    ("kernel/pm/pm_control_execution.c", "prepare_environment_is_valid", "micros_endpoint_registry_validate_objects", 1),
    ("kernel/pm/pm_control_exit.c", "micros_pm_control_exit_begin_preflight", "micros_endpoint_registry_validate_objects", 1),
    ("kernel/pm/pm_control_exit.c", "validate_stopped_exit", "micros_endpoint_registry_validate_objects", 1),
    ("kernel/pm/pm_control_validation.c", "micros_pm_control_validate_prepared_shape", "micros_endpoint_registry_validate_objects", 1),
    ("kernel/tty/tty_handoff_runtime_validate.c", "micros_tty_handoff_runtime_validate", "micros_endpoint_registry_validate_objects", 1),
)


def validate_audit_boundaries(root=ROOT, subjects=None):
    root = Path(root)
    if subjects is None:
        subjects = {
            str(path.relative_to(root)): source_bytes(root, path).decode()
            for path in (root / "kernel").rglob("*.c")
            if "tests" not in path.relative_to(root / "kernel").parts
        }
    rows = []
    for path, text in subjects.items():
        for name, record in function_records(text).items():
            body = c_lexical_text(record.body)
            for callee in {row[2] for row in RETAINED_AUDITS}:
                calls = len(re.findall(r"\b" + callee + r"\s*\(", body))
                if calls != len(re.findall(r"\b" + callee + r"\b", body)):
                    raise ValueError("IPC01 complete audit escaped its direct diagnostic owner")
                if calls:
                    rows.append((path, name, callee, calls))
    if tuple(sorted(rows)) != RETAINED_AUDITS:
        raise ValueError("IPC01 added an ordinary audit or lost a complete diagnostic boundary")
    return tuple(rows)

def validate_dependency_bitmap(values, sites):
    scope = (("for ( steps = 0 ; steps < MICROS_THREAD_CAPACITY ; ++ steps ) {", 0),)
    membership = tokens(
        "visited = thread_handle.slot < 64 ? &visited_low : &visited_high;"
        "bit = UINT64_C(1) << (thread_handle.slot % 64);"
        "if ((*visited & bit) != 0) { return MICROS_IPC_ERROR_INVARIANT; }"
        "*visited |= bit;"
    )
    if (
        "[" in values
        or any(values.count(name) != count for name, count in (
            ("visited_low", 2), ("visited_high", 2), ("visited", 4), ("bit", 4),
        ))
        or sum(values[index:index + len(membership)] == membership for index in range(len(values))) != 1
    ):
        raise ValueError("IPC02 visited storage or exact slot/word membership changed")
    initializers = tuple(site for site in sites if site[3] == "CLEARED_BYTE")
    if len(initializers) != 2 or {
        (site[1], site[4], site[5], site[6], site[7]) for site in initializers
    } != {
        ("insert", 8, "after", "uint64_t visited_low = 0 ;", ()),
        ("insert", 8, "after", "uint64_t visited_high = 0 ;", ()),
    }:
        raise ValueError("IPC02 lost an actual visited-word initialization")
    if sum(
        site[1] == "wrap" and site[3] == "BITMAP_WORD_LOAD" and site[4] == 1
        and site[5] == "value" and site[6] == "* visited" and site[7] == scope for site in sites
    ) != 1 or sum(
        site[1] == "insert" and site[3] == "LINK_TRAVERSE" and site[4] == 1
        and site[5] == "entry" and site[7] == scope for site in sites
    ) != 1:
        raise ValueError("IPC02 lost its executing hop or visited-word observation")


def validate_native_sites(root, path, text):
    family = declarations(root)["families"]["IPC01"]
    records = function_records(text)
    declared = family["sites"].get(path, {})
    if declared.keys() != records.keys():
        raise ValueError("IPC01 native declarations omit a compiled function: " + path)
    for name, record in records.items():
        sites = tuple(site for channels, site in declared[name] if "work" in channels)
        if any(tuple(channels) != ("work",) for channels, _ in declared[name]):
            raise ValueError("IPC01 primitive belongs to another observation channel")
        bound = bind_sites(record.body, sites)
        positions = positional_tokens(record.body)
        values = tuple(item.value for item in positions)
        stacks, closings = compound_layout(values)
        if path == "kernel/ipc/ipc_deadlock.c" and name == "ipc_preflight_deadlock":
            validate_dependency_bitmap(values, sites)
        required_kind = "COMPLETE_VALIDATOR_CALL" if name in AUDITS else (
            "RANGE_VISIT" if name in ("micros_privilege_profile_resolve", "ipc_select_queue") else None
        )
        if required_kind is not None and sum(
            site[1] == "insert" and site[3] == required_kind and site[4] == 1
            and site[5] == "entry" and not site[7] for site in sites
        ) != 1:
            raise ValueError("IPC01 source lost an actual invocation observation: " + name)
        for index, value in enumerate(values):
            if value == "{":
                first = region_header(values, index)
                if values[first] not in ("for", "while"):
                    continue
                scope = enclosing_regions(values, stacks, closings, (*stacks[index], index))
                covered = any(site[1] == "insert" and site[5] == "entry" and site[7] == scope for site in sites)
                if not covered and name == "ipc_find_pending_notification":
                    covered = any(
                        site[1] == "wrap" and site[3] == "BITMAP_WORD_LOAD"
                        and site[6] == "destination -> pending_notification_sources" and site[7] == scope
                        and tokens(record.body[positions[index].end:observation[5]]) == ("if", "(", "(")
                        for site, observation in zip(sites, bound, strict=True)
                    )
                if not covered:
                    raise ValueError("IPC01 loop lost its executing primitive observation: " + name)
            if value != "[" or index < 3 or values[index - 1] not in INDEX_FIELDS:
                continue
            if values[index - 2] not in (".", "->"):
                continue
            end = matching_tokens(values, index)
            extent = (positions[index + 1].start, positions[end - 1].end)
            if sum(site[1] == "wrap" and site[3] == "INDEX_PROBE"
                   and observation[5:7] == extent for site, observation in zip(sites, bound, strict=True)) != 1:
                raise ValueError("IPC01 indexed record lost its primitive observation: " + name)


def validate_target_source(path, text):
    records = function_records(text)
    if path in AUDIT_SITES:
        name, label, owner = AUDIT_SITES[path]
        body = records[name].body
        pattern = (
            r'#if\s+defined\(MICROS_BUILD_IPC_SYSCALL_TEST\)\s*\|\|\s*defined\(MICROS_TEST_BOOTSTRAP_COMMAND_COUNTS\)\s+'
            r'MICROS_TEST_WORK_HIT\("ipc01-target-' + re.escape(label) + r'",\s*'
            r'MICROS_WORK_OWNER_' + owner + r',\s*MICROS_WORK_KIND_COMPLETE_VALIDATOR_CALL,\s*1\);\s*#endif'
        )
        matches = tuple(re.finditer(pattern, body))
        if len(matches) != 1 or tokens(body).count("MICROS_TEST_WORK_HIT") != 1:
            raise ValueError("IPC01 target audit invocation lost its exact passive observation")
        prefix = body[:matches[0].start()]
        if direct_calls(prefix) or set(tokens(prefix)) & {"if", "for", "while", "return", "goto"}:
            raise ValueError("IPC01 audit observation moved after executed work")
    elif path == "kernel/ipc/ipc_syscall.c":
        body = tokens(records["micros_ipc_handle_captured_user_ecall"].body)
        if (body.count("MICROS_IPC01_SYSCALL_BEGIN") != 1
            or body.count("MICROS_IPC01_SYSCALL_END") != 4
            or body.index("MICROS_IPC01_SYSCALL_BEGIN") > body.index("resolve_current_runtime")):
            raise ValueError("IPC01 syscall scope lost its admission boundary")
        for index, value in enumerate(body):
            if value == "return" and body[index - 5:index] not in (
                ("MICROS_IPC01_SYSCALL_END", "(", "false", ")", ";"),
                ("MICROS_IPC01_SYSCALL_END", "(", "true", ")", ";"),
            ):
                raise ValueError("IPC01 syscall return escapes its scope")
    elif path == "kernel/scheduler/scheduler_completion.c":
        body = tokens(records["preflight_selected_completion"].body)
        begin = body.index("MICROS_IPC01_COMPLETION_BEGIN")
        if (body.count("MICROS_IPC01_COMPLETION_BEGIN") != 1
            or body.count("MICROS_IPC01_COMPLETION_END") != 4
            or begin > body.index("micros_ipc_runtime_registry")
            or begin < body.index("ipc_delivery_pending")
            or body.count("return") != 6):
            raise ValueError("IPC01 completion preflight lost its pending-only scope")
        closed_returns = {
            matching_tokens(body, index + 1) + 2
            for index, value in enumerate(body) if value == "MICROS_IPC01_COMPLETION_END"
        }
        if closed_returns != {index for index, value in enumerate(body) if value == "return" and index > begin}:
            raise ValueError("IPC01 pending completion return escapes its scope")
        commit = tokens(records["commit_selected_completion"].body)
        if any(value.startswith("MICROS_IPC01_") for value in commit):
            raise ValueError("IPC01 reporting entered the post-timer completion commit")
    elif path == "kernel/ipc/ipc_buffer.c":
        body = tokens(records["plan_buffer"].body)
        expected = tokens(
            '#ifdef MICROS_BUILD_IPC_SYSCALL_TEST\n'
            'MICROS_TEST_WORK_HIT("ipc01-target-buffer-lookup", MICROS_WORK_OWNER_KERNEL_IPC,'
            'MICROS_WORK_KIND_INDEX_PROBE, 1);\n#endif\n'
            'address_error = micros_user_address_space_translate('
        )
        matches = sum(body[index:index + len(expected)] == expected for index in range(len(body)))
        if matches != 1 or body.count("MICROS_TEST_WORK_HIT") != 1:
            raise ValueError("IPC01 buffer observation detached from its actual lookup")
    elif path == "tests/qemu/selected_ipc_work.c":
        if tuple(" ".join(tokens(row)) for row in file_scope_objects(text)) != (
            "static struct ipc01_observer observer ;",
        ):
            raise ValueError("IPC01 target collection no longer has one test-owned state")
    elif path == "kernel/tests/ipc/ipc_syscall_test_report.c":
        body = records["micros_ipc_syscall_test_finish"].body
        if not body.index("cleanup_syscall_fixture_processes") < body.index("micros_ipc01_work_report") < body.index(
            "MICROS_IPC_SYSCALL_TEST_PASS"
        ):
            raise ValueError("IPC01 reporting precedes semantic cleanup or follows success")


def validate_target_observations(root=ROOT):
    root = Path(root)
    for path in TARGET_PATHS:
        text = source_bytes(root, root / path).decode()
        validate_source(root, path, text)
        validate_target_source(path, text)
