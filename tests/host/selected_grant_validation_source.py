"""Finite current grant primitive coverage, independent of work limits."""

from pathlib import Path
import re

from tests.host.c_source_records import c_lexical_text, function_records, tokens
from tests.host.native_observations import bind_sites, declarations
from tests.host.runtime_memory_evidence import MEMORY_PATH, validate_green_source
from tests.host.source_snapshot import source_bytes
from tests.host.work_count_lexical import (
    compound_layout, enclosing_regions, matching_tokens, positional_tokens, region_header,
)


INDEX_FIELDS = frozenset((
    "processes", "threads", "harts", "endpoints", "profiles", "grants", "expected_generation",
    "pending_events", "ready_head", "ready_tail", "transitions", "entries", "bindings", "binding_slots", "chunks", "participants",
))
AUDITS = frozenset((
    "micros_kernel_objects_validate", "micros_kernel_objects_validate_base",
    "micros_scheduler_core_validate", "micros_endpoint_registry_validate",
    "micros_endpoint_registry_validate_objects", "micros_grant_registry_validate",
    "validate_registry_storage", "validate_registry_indexes", "micros_grant_runtime_validate", "micros_ipc_runtime_validate",
    "micros_tty_handoff_runtime_validate",
))

RETAINED_AUDITS = (
    ("kernel/boot/bootstrap_runtime_validation.c", "micros_bootstrap_runtime_validate", "micros_grant_runtime_validate", 1),
    ("kernel/grant/grant.c", "micros_grant_registry_validate", "validate_registry_indexes", 1),
    ("kernel/grant/grant.c", "micros_grant_registry_validate", "validate_registry_storage", 1),
    ("kernel/grant/grant_runtime.c", "micros_grant_runtime_initialize", "micros_grant_registry_validate", 1),
    ("kernel/grant/grant_runtime.c", "micros_grant_runtime_validate", "micros_grant_registry_validate", 1),
    ("kernel/pm/pm_control_syscall.c", "runtime_state_is_valid", "micros_grant_runtime_validate", 1),
)
DEVICE_READS = (
    ("kernel/grant/grant_copy.c", "grant_copy_execute"),
    ("kernel/grant/grant_copy.c", "grant_copy_prepare_remote_plan"),
    ("kernel/grant/grant_syscall.c", "grant_syscall_create_request"),
)


def audit_subjects(root):
    root = Path(root)
    return {
        str(path.relative_to(root)): source_bytes(root, path).decode()
        for path in (root / "kernel").rglob("*.c")
        if "tests" not in path.relative_to(root / "kernel").parts
    }


def validate_sparse_plan_storage(record):
    body = c_lexical_text(record.body)
    for declaration in re.finditer(r"\bstruct\s+micros_grant_cancel_plan\s+(\w+)\s*([^;]*);", body):
        name, initializer = declaration.groups()
        if initializer.strip() or re.search(r"\b" + name + r"\s*=", body):
            raise ValueError("GRANT02 cancellation storage regained a whole-plan initializer or assignment")
        if re.search(r"\bsizeof\s*(?:\(\s*" + name + r"\s*\)|" + name + r"\b)", body):
            raise ValueError("GRANT02 cancellation lifetime regained capacity-sized plan work")
        if re.search(r"\b(?:memcpy|memset|copy_bytes|clear_bytes)\s*\(\s*&?\s*" + name + r"\b", body):
            raise ValueError("GRANT02 cancellation candidate is moved as bulk storage")
    if record.name == "publish_cancel_plan":
        values = tokens(record.body)
        calls = [
            values[index:matching_tokens(values, index + 1) + 1]
            for index, value in enumerate(values[:-1]) if value == "copy_bytes" and values[index + 1] == "("
        ]
        if calls != [tokens("copy_bytes(&plan->expected_generation[slot], &candidate->expected_generation[slot], "
                            "sizeof(plan->expected_generation[slot]))")]:
            raise ValueError("GRANT02 plan publication no longer copies only a selected generation")
        if re.search(r"\*\s*plan\s*=|\b(?:memcpy|memset|clear_bytes)\s*\(", body):
            raise ValueError("GRANT02 plan publication regained whole-plan movement")


def validate_audit_boundaries(root, subjects=None):
    subjects = audit_subjects(root) if subjects is None else subjects
    rows, devices = [], []
    for path, text in subjects.items():
        for name, record in function_records(text).items():
            validate_sparse_plan_storage(record)
            body = c_lexical_text(record.body)
            for callee in {row[2] for row in RETAINED_AUDITS}:
                if callee in ("validate_registry_storage", "validate_registry_indexes") and path != "kernel/grant/grant.c":
                    continue
                calls = len(re.findall(r"\b" + callee + r"\s*\(", body))
                if calls != len(re.findall(r"\b" + callee + r"\b", body)):
                    raise ValueError("GRANT01 complete audit escaped its direct diagnostic owner")
                if calls:
                    rows.append((path, name, callee, calls))
            if path.startswith("kernel/grant/"):
                if "micros_tty_handoff_runtime_device_authority" in body:
                    raise ValueError("GRANT01 ordinary device read regained complete command authority")
                calls = len(re.findall(r"\bmicros_tty_handoff_runtime_inspect_device_binding\s*\(", body))
                devices.extend(((path, name),) * calls)
    if tuple(sorted(rows)) != RETAINED_AUDITS or tuple(sorted(devices)) != DEVICE_READS:
        raise ValueError("GRANT01 changed a retained diagnostic or the finite device consumer set")
    return tuple(rows)


def validate_native_sites(root, path, text):
    family = declarations(root)["families"]["GRANT01"]
    declared = family["sites"].get(path, {})
    records = function_records(text)
    if path in family["subsets"]:
        records = {name: records[name] for name in family["subsets"][path]}
    if declared.keys() != records.keys():
        raise ValueError("GRANT01 declarations omit a compiled function: " + path)
    if path == MEMORY_PATH:
        if any(declared.values()):
            raise ValueError("GRANT03 cannot duplicate the existing COPY01 observations")
        validate_green_source(text)
        return
    for name, record in records.items():
        if any(tuple(channels) != ("work",) for channels, _ in declared[name]):
            raise ValueError("GRANT01 primitive belongs to another channel")
        sites = tuple(site for _, site in declared[name])
        bound = bind_sites(record.body, sites)
        positions = positional_tokens(record.body)
        values = tuple(item.value for item in positions)
        stacks, closings = compound_layout(values)
        if name in AUDITS and sum(
            site[1] == "insert" and site[3] == "COMPLETE_VALIDATOR_CALL" and site[4] == 1
            and site[5] == "entry" and not site[7] for site in sites
        ) != 1:
            raise ValueError("GRANT01 lost an actual complete-validator invocation: " + name)
        for index, value in enumerate(values):
            if value == "{":
                first = region_header(values, index)
                if values[first] in ("for", "while"):
                    scope = enclosing_regions(values, stacks, closings, (*stacks[index], index))
                    if not any(site[1] == "insert" and site[5] == "entry" and site[7] == scope for site in sites):
                        raise ValueError("GRANT01 loop lost its executing primitive: " + name)
            if value != "[" or index < 3 or values[index - 1] not in INDEX_FIELDS:
                continue
            if values[index - 2] not in (".", "->"):
                continue
            end = matching_tokens(values, index)
            extent = (positions[index + 1].start, positions[end - 1].end)
            if sum(
                site[1] == "wrap" and site[3] == "INDEX_PROBE" and observation[5:7] == extent
                for site, observation in zip(sites, bound, strict=True)
            ) != 1:
                raise ValueError("GRANT01 indexed record lost its primitive: " + name)
