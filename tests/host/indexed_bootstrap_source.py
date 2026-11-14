"""Current bootstrap primitive coverage, separate from independent work limits."""

from pathlib import Path
import re

from tests.host.c_source_records import c_lexical_text, function_records, tokens
from tests.host.native_observations import bind_sites, declarations
from tests.host.selected_ipc_validation_source import validate_native_sites as validate_ipc_sites
from tests.host.source_snapshot import source_bytes
from tests.host.work_count_lexical import (
    compound_layout, enclosing_regions, matching_tokens, positional_tokens, region_header,
)


INDEX_FIELDS = frozenset((
    "entries", "bindings", "ordered_service_ids", "ordered_manifest_indices",
    "processes", "threads", "harts", "endpoints", "profiles", "service_slots", "binding_slots", "grants",
))
MOVEMENT_HELPERS = {
    "kernel/boot/bootstrap_manifest_common.c": {
        "bootstrap_manifest_copy_bytes": ("COPY_UNIT", "COPIED_BYTE"),
        "bootstrap_manifest_clear_bytes": ("CLEAR_UNIT", "CLEARED_BYTE"),
    },
    "kernel/boot/bootstrap_control_core.c": {
        "copy_bytes": ("COPY_UNIT", "COPIED_BYTE"), "clear_bytes": ("CLEAR_UNIT", "CLEARED_BYTE"),
    },
    "kernel/boot/bootstrap_runtime_state.c": {
        "bootstrap_runtime_copy_bytes": ("COPY_UNIT", "COPIED_BYTE"),
        "bootstrap_runtime_clear_bytes": ("CLEAR_UNIT", "CLEARED_BYTE"),
    },
    "kernel/boot/bootstrap_image.c": {"clear_bytes": ("CLEAR_UNIT", "CLEARED_BYTE")},
}
RETAINED_AUDITS = (
    ("kernel/boot/bootstrap_control_core.c", "micros_bootstrap_control_publish_controller", "micros_bootstrap_control_validate", 1),
    ("kernel/boot/bootstrap_runtime.c", "micros_bootstrap_runtime_publish_controller", "micros_bootstrap_control_validate", 1),
    ("kernel/boot/bootstrap_runtime_validation.c", "micros_bootstrap_runtime_validate", "micros_bootstrap_control_validate", 1),
    ("kernel/diagnostic/diagnostic_control.c", "micros_diagnostic_control_dispatch", "micros_bootstrap_runtime_validate", 1),
    ("kernel/pm/pm_control_syscall.c", "runtime_state_is_valid", "micros_bootstrap_runtime_validate", 1),
    ("kernel/tty/tty_control_syscall.c", "common_invariants_hold", "micros_bootstrap_runtime_validate", 1),
    ("kernel/tty/tty_interrupt.c", "micros_tty_interrupt_dispatch", "micros_bootstrap_runtime_validate", 1),
    ("kernel/vm/vm_handoff_syscall_boot.c", "micros_vm_handoff_syscall_handle_ready", "micros_bootstrap_runtime_validate", 1),
    ("kernel/vm/vm_handoff_syscall_boot.c", "require_tty_mapping_runtime", "micros_bootstrap_runtime_validate", 1),
)
SELECTED_MOVEMENT = {
    "initialize_transition_plan": (
        "bootstrap_manifest_clear_bytes(plan, sizeof(*plan))",
        "bootstrap_manifest_copy_bytes(&plan->entry, &runtime->entries[index], sizeof(plan->entry))",
    ),
    "bootstrap_runtime_prepare_release": ("bootstrap_manifest_copy_bytes(plan, &candidate, sizeof(*plan))",),
    "bootstrap_runtime_prepare_ready": ("bootstrap_manifest_copy_bytes(plan, &candidate, sizeof(*plan))",),
    "bootstrap_runtime_prepare_complete": ("bootstrap_manifest_copy_bytes(plan, &candidate, sizeof(*plan))",),
    "bootstrap_runtime_commit_prevalidated": (
        "bootstrap_manifest_copy_bytes(&runtime->entries[plan->entry_index], &plan->entry, sizeof(plan->entry))",
    ),
    "micros_bootstrap_control_prepare_ready": (
        "clear_bytes(&candidate, sizeof(candidate))", "copy_bytes(plan, &candidate, sizeof(*plan))",
    ),
    "micros_bootstrap_control_prepare_complete": (
        "clear_bytes(&candidate, sizeof(candidate))", "copy_bytes(plan, &candidate, sizeof(*plan))",
    ),
    "micros_bootstrap_control_commit_ready_prevalidated": ("clear_bytes(plan, sizeof(*plan))",),
    "micros_bootstrap_control_commit_complete_prevalidated": ("clear_bytes(plan, sizeof(*plan))",),
}


def validate_selected_movement(path, text):
    if path not in ("kernel/boot/bootstrap_transitions.c", "kernel/boot/bootstrap_control_core.c"):
        return
    cold = {
        "micros_bootstrap_runtime_initialize", "micros_bootstrap_control_state_prepare",
        "micros_bootstrap_control_publish_controller",
    }
    movers = {"copy_bytes", "clear_bytes", "bootstrap_manifest_copy_bytes", "bootstrap_manifest_clear_bytes"}
    for name, record in function_records(text).items():
        if name in cold:
            continue
        body = c_lexical_text(record.body)
        if (
            re.search(r"\b(?:memcpy|memmove|memset)\s*\(", body)
            or (name not in MOVEMENT_HELPERS.get(path, {})
                and re.search(r"\bmicros_runtime_mem(?:cpy|set)\s*\(", body))
            or re.search(r"\bstruct\s+micros_bootstrap_runtime\s+(?!\*)\w+", body)
            or re.search(r"\*\s*runtime\s*=|\bruntime\s*->\s*(?:entries|ordered_service_ids|service_slots)\s*=", body)
        ):
            raise ValueError("BOOT01 ordinary transition regained unobserved whole-runtime movement")
        values = tokens(record.body)
        calls = tuple(
            values[index:matching_tokens(values, index + 1) + 1]
            for index, value in enumerate(values[:-1]) if value in movers and values[index + 1] == "("
        )
        if calls != tuple(tokens(call) for call in SELECTED_MOVEMENT.get(name, ())):
            raise ValueError("BOOT01 movement left its selected record/plan owner: " + name)


def audit_subjects(root):
    root = Path(root)
    return {
        str(path.relative_to(root)): source_bytes(root, path).decode()
        for path in (root / "kernel").rglob("*.c") if "tests" not in path.relative_to(root / "kernel").parts
    }


def validate_audit_boundaries(root, subjects=None):
    subjects = audit_subjects(root) if subjects is None else subjects
    rows = []
    for path, text in subjects.items():
        for name, record in function_records(text).items():
            body = c_lexical_text(record.body)
            for callee in {row[2] for row in RETAINED_AUDITS}:
                calls = len(re.findall(r"\b" + callee + r"\s*\(", body))
                if calls != len(re.findall(r"\b" + callee + r"\b", body)):
                    raise ValueError("BOOT01 retained audit escaped its direct owner")
                if calls:
                    rows.append((path, name, callee, calls))
    if tuple(sorted(rows)) != RETAINED_AUDITS:
        raise ValueError("BOOT01 changed a retained whole-control/runtime audit boundary")
    return tuple(rows)


def validate_native_sites(root, path, text):
    from tests.host.indexed_bootstrap_evidence import NATIVE_SUBSETS

    declared = declarations(root)
    family = declared["families"]["BOOT01"]
    sites_by_name = family["sites"].get(path, {})
    records = function_records(text)
    validate_selected_movement(path, text)
    if path in NATIVE_SUBSETS:
        records = {name: records[name] for name in NATIVE_SUBSETS[path]}
    if sites_by_name.keys() != records.keys():
        raise ValueError("BOOT01 declarations omit a compiled function: " + path)
    if path == "lib/runtime/memory.c":
        from tests.host.runtime_memory_evidence import validate_green_source

        if any(sites_by_name.values()):
            raise ValueError("BOOT03 cannot double-count the existing COPY01 provider")
        validate_green_source(text)
        return
    if path in declared["families"]["IPC01"]["sites"]:
        original = declared["families"]["IPC01"]["sites"][path]
        for name, rows in original.items():
            expected = tuple((channels, ("boot01-shared/" + site[0], *site[1:])) for channels, site in rows)
            if tuple(sites_by_name[name]) != expected:
                raise ValueError("BOOT01 shared native primitive differs from its existing owner")
        validate_ipc_sites(root, path, text)
        return
    if path in ("kernel/tty/tty_handoff_core.c", "kernel/tty/tty_handoff_runtime_state.c"):
        original = declared["families"]["GRANT01"]["sites"][path]
        for name, rows in original.items():
            expected = tuple((channels, ("boot01-shared/" + site[0], *site[1:])) for channels, site in rows)
            if tuple(sites_by_name[name]) != expected:
                raise ValueError("BOOT01 shared TTY primitive differs from its existing owner")
        return
    for name, record in records.items():
        if any(tuple(channels) != ("work",) for channels, _ in sites_by_name[name]):
            raise ValueError("BOOT01 primitive belongs to another observation channel")
        sites = tuple(site for _, site in sites_by_name[name])
        bound = bind_sites(record.body, sites)
        positions = positional_tokens(record.body)
        values = tuple(item.value for item in positions)
        stacks, closings = compound_layout(values)
        if name in ("micros_bootstrap_control_validate", "bootstrap_runtime_validate_index") and sum(
            site[1] == "insert" and site[3] == "COMPLETE_VALIDATOR_CALL"
            and site[4] == 1 and site[5] == "entry" and not site[7] for site in sites
        ) != 1:
            raise ValueError("BOOT01 lost a retained control audit invocation")
        for index, value in enumerate(values):
            if value == "{":
                first = region_header(values, index)
                if values[first] in ("for", "while"):
                    scope = enclosing_regions(values, stacks, closings, (*stacks[index], index))
                    if not any(site[1] == "insert" and site[5] == "entry" and site[7] == scope for site in sites):
                        raise ValueError("BOOT01 loop lost its executing primitive: " + name)
                    for kind in MOVEMENT_HELPERS.get(path, {}).get(name, ()):
                        if sum(site[1] == "insert" and site[3] == kind and site[4] == 1
                               and site[5] == "entry" and site[7] == scope for site in sites) != 1:
                            raise ValueError("BOOT03 byte primitive lost its actual unit/byte observation")
            if value != "[" or index < 3 or values[index - 1] not in INDEX_FIELDS:
                continue
            if values[index - 2] not in (".", "->"):
                continue
            end = matching_tokens(values, index)
            extent = (positions[index + 1].start, positions[end - 1].end)
            if sum(site[1] == "wrap" and site[3] == "INDEX_PROBE" and observation[5:7] == extent
                   for site, observation in zip(sites, bound, strict=True)) != 1:
                raise ValueError("BOOT01 indexed record lost its primitive: " + name)
