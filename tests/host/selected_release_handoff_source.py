"""Closed MEM05 source, interface and observation declarations."""

from pathlib import Path
import re

from tests.host.c_source_records import function_records
from tests.host.work_count_types import Stage


RECIPE_ADMISSION = {
    "base_cmake_sha256": "45c4a122c949f59564ba7b552257fcd805981d1097016707701a4d7e04fbe59c",
    "legacy_cmake_sha256": "f657d8b24c20ec1a2447939feff5660fe3f7f10fe4ffdea25acd08a3ec5ee085",
    "native_source_members": (
        "kernel/memory/frame_allocator.c",
        "kernel/memory/bootstrap_memory.c",
        "kernel/memory/frame_ownership.c",
        "kernel/memory/frame_ownership_validation.c",
        "kernel/memory/frame_ownership_bootstrap.c",
        "kernel/memory/frame_ownership_handoff.c",
        "kernel/memory/frame_ownership_vm_claim.c",
        "kernel/memory/frame_ownership_vm_freeze.c",
        "kernel/memory/frame_ownership_vm_release.c",
        "kernel/objects/kernel_objects.c",
        "kernel/objects/kernel_object_state.c",
        "kernel/objects/kernel_object_identity.c",
        "kernel/objects/kernel_object_stack.c",
        "kernel/objects/kernel_object_process.c",
        "kernel/objects/kernel_object_thread.c",
        "kernel/objects/kernel_object_context.c",
        "kernel/objects/kernel_object_hart.c",
        "kernel/objects/kernel_object_validation.c",
        "kernel/objects/kernel_object_runtime.c",
        "kernel/scheduler/scheduler_core.c",
        "kernel/scheduler/scheduler_core_queue.c",
        "kernel/scheduler/scheduler_core_validate.c",
        "kernel/scheduler/scheduler_core_policy.c",
        "kernel/scheduler/scheduler_core_ipc.c",
        "kernel/scheduler/scheduler_core_ipc_transition.c",
        "kernel/scheduler/scheduler_core_return_plan.c",
        "kernel/scheduler/scheduler_core_return.c",
        "kernel/scheduler/scheduler_core_accounting.c",
        "kernel/scheduler/scheduler_context.c",
        "kernel/endpoint/endpoint.c",
        "kernel/endpoint/endpoint_codec.c",
        "kernel/endpoint/endpoint_identity.c",
        "kernel/endpoint/endpoint_registry.c",
        "kernel/endpoint/endpoint_publication.c",
        "kernel/endpoint/endpoint_privilege.c",
        "kernel/endpoint/endpoint_completion_validation.c",
        "kernel/endpoint/endpoint_queue_validation.c",
        "kernel/endpoint/endpoint_object_validation.c",
        "kernel/ipc/ipc_runtime.c",
        "kernel/memory/sv39.c",
        "kernel/memory/address_space.c",
        "kernel/memory/user_address_space.c",
        "kernel/memory/user_address_space_core.c",
        "kernel/memory/user_address_space_table.c",
        "kernel/memory/user_address_space_runtime.c",
        "kernel/memory/user_address_space_mapping.c",
        "kernel/memory/user_address_space_validation.c",
        "kernel/memory/user_address_space_mutation.c",
        "kernel/memory/user_address_space_activation.c",
        "kernel/vm/vm_handoff_core.c",
        "kernel/vm/vm_handoff_boot.c",
        "kernel/vm/vm_handoff_runtime.c",
        "kernel/tty/tty_handoff_core.c",
        "kernel/tty/tty_handoff_runtime_state.c",
        "kernel/tty/tty_handoff_runtime_validate.c",
        "kernel/boot/bootstrap_control_core.c",
        "kernel/memory/user_address_space_handoff.c",
        "kernel/execution/user_execution_validation.c",
    ),
    "native_header_count": 156,
    "native_header_membership_sha256": "f6d999c1bb6b9f5fc57e097211f88ac72beb51577b964e6e4a24a777f057e3c2",
    "native_targets": (
        "selected_release_handoff_aggregate_test",
        "selected_release_handoff_frames_test",
        "selected_release_handoff_plans_test",
        "selected_release_handoff_context_test",
        "selected_release_handoff_ordinary_test",
    ),
    "native_case": "selected-release-handoff",
    "native_tiers": ("test-unit",),
    "native_label": "slow",
    "native_standard": "c17",
    "native_sanitizers": ("address", "undefined"),
    "source_pool": "mem03_source_proof",
    "generator": "tools/generate_selected_release_handoff_fixture.py",
    "generator_output": "build/host-debug/selected-release-handoff",
    "driver": "tools/check_selected_release_handoff.py",
    "fixture_header": "tests/host/selected_release_handoff_test.h",
    "fixture_sources": ("tests/host/selected_release_handoff_test.c", "tests/work/work_counts.c"),
    "baseline_headers": "fixed-runtime-reference",
    "current_headers": "actual-current-source",
    "ordinary_observations": "absent",
}

STATE_WORK_CONTRACT = {
    "version": 1,
    "dimensions": (27, 14),
    "owner_mask": 11900,
    "channels": ("aggregate", "frames", "plans", "context"),
    "producer_base": 0x4d454d3035000000,
    "current_producer_offset": 256,
    "common_cases": 128,
    "case_inputs_sha256": "fe9e35bbc0521d251eebf129ae5ec594ffb0276f70ac1bb787226c81ffac3065",
    "frame_capacity": 262144,
    "bitmap_words": 4096,
    "process_capacity": 64,
    "thread_capacity": 128,
    "frame_links_bytes": 8,
    "process_index_bytes": 8,
    "selection_bytes": 49168,
    "handoff_summary_bytes": 1024,
    "target_workflows": ("test-qemu-user-address-space", "test-qemu-address-space-handoff"),
    "preservation": (
        "complete-typed-owner-and-availability-authority",
        "mixed-portable-generations-and-all-index-transitions",
        "same-call-complete-index-proof-and-original-all-free-diagnostic-counts",
        "selected-versus-complete-corruption-scope-and-byte-exact-failure-state",
        "two-process-pass-handoff-error-order-and-nonfailing-commit",
        "all-prior-native-cases-bounds-and-complete-target-stack-coverage",
        "sixteen-retained-root-audits-and-four-owned-replacement-boundaries",
    ),
}


CHANNELS = ("aggregate", "frames", "plans", "context", "ordinary")
OBSERVED_HEADERS = ("kernel/boot/bootstrap_control_internal.h",)


def _call_rows(path, text, callees):
    from tests.host.work_count_lexical import c_lexical_text

    rows = []
    for name, record in function_records(text).items():
        body = c_lexical_text(record.body)
        for callee in callees:
            references = len(re.findall(r"\b" + callee + r"\b", body))
            calls = len(re.findall(r"\b" + callee + r"\s*\(", body))
            if references != calls:
                raise ValueError("MEM05 complete-audit or private-plan authority escaped its direct caller")
            if calls:
                rows.append((path, name, callee, calls))
    return tuple(rows)


AUDIT_CALLEES = (
    "micros_user_address_space_validate", "micros_user_address_space_validate_locked",
    "micros_user_address_space_validate_all_process_roots_locked",
)
CURRENT_AUDIT_BOUNDARIES = (
    ("kernel/boot/bootstrap_preparation.c", "validate_prepared_services", "micros_user_address_space_validate", 1),
    ("kernel/boot/bootstrap_runtime_validation.c", "micros_bootstrap_runtime_validate", "micros_user_address_space_validate", 1),
    ("kernel/execution/user_execution_validation.c", "validate_address_space_context", "micros_user_address_space_validate", 1),
    ("kernel/memory/user_address_space.c", "micros_user_address_space_create", "micros_user_address_space_validate_locked", 1),
    ("kernel/memory/user_address_space_mapping.c", "micros_user_address_space_inventory", "micros_user_address_space_validate_locked", 1),
    ("kernel/memory/user_address_space_tty.c", "micros_user_address_space_prepare_tty_uart_mapping", "micros_user_address_space_validate_locked", 1),
    ("kernel/memory/user_address_space_tty.c", "micros_user_address_space_validate_tty_uart_mapping", "micros_user_address_space_validate_locked", 1),
    ("kernel/memory/user_address_space_validation.c", "micros_user_address_space_validate", "micros_user_address_space_validate_locked", 1),
    ("kernel/memory/user_address_space_validation.c", "micros_user_address_space_validate_all_process_roots_locked", "micros_user_address_space_validate_locked", 1),
    ("kernel/memory/user_address_space_vm_freeze.c", "micros_user_address_space_install_vm_freeze_prevalidated", "micros_user_address_space_validate", 1),
    ("kernel/memory/user_address_space_vm_freeze.c", "validate_vm_freeze_address_spaces", "micros_user_address_space_validate_all_process_roots_locked", 1),
    ("kernel/memory/user_address_space_vm_scratch.c", "micros_user_address_space_prepare_vm_scratch_mapping", "micros_user_address_space_validate_all_process_roots_locked", 1),
    ("kernel/memory/user_address_space_vm_scratch.c", "micros_user_address_space_prepare_vm_scratch_removal", "micros_user_address_space_validate_all_process_roots_locked", 1),
    ("kernel/memory/user_address_space_vm_teardown.c", "micros_user_address_space_prepare_vm_teardown", "micros_user_address_space_validate_all_process_roots_locked", 1),
    ("kernel/vm/vm_handoff_syscall.c", "require_post_handoff_runtime", "micros_user_address_space_validate", 1),
    ("kernel/vm/vm_handoff_syscall_boot.c", "require_tty_mapping_runtime", "micros_user_address_space_validate", 1),
)
CURRENT_PLAN_BOUNDARIES = (
    ("kernel/memory/user_address_space.c", "micros_user_address_space_destroy", "micros_user_address_space_collect_bootstrap_release_locked", 1),
    ("kernel/memory/user_address_space_handoff.c", "micros_user_address_space_complete_wired_handoff", "micros_user_address_space_collect_bootstrap_handoff_locked", 1),
    ("kernel/memory/user_address_space_handoff.c", "micros_user_address_space_prepare_wired_handoff", "micros_user_address_space_collect_bootstrap_handoff_locked", 1),
    ("kernel/memory/user_address_space_mutation.c", "micros_user_address_space_release_page", "inspect_page_release", 1),
)


def measure_audit_boundaries(root, stage):
    from tests.host.source_snapshot import source_bytes

    if stage is not Stage.GREEN:
        raise ValueError("MEM05 audit scope requires current source")
    root = Path(root).resolve()
    subjects = {
        str(path.relative_to(root)): source_bytes(root, path).decode()
        for path in (root / "kernel").rglob("*.c")
        if "tests" not in path.relative_to(root / "kernel").parts
    }
    rows = tuple(sorted(row for path, text in subjects.items() for row in _call_rows(path, text, AUDIT_CALLEES)))
    planned_calls = tuple(sorted({row[2] for row in CURRENT_PLAN_BOUNDARIES}))
    plans = tuple(sorted(row for path, text in subjects.items() for row in _call_rows(path, text, planned_calls)))
    if rows != CURRENT_AUDIT_BOUNDARIES or plans != CURRENT_PLAN_BOUNDARIES:
        raise ValueError("MEM05 removed, added or retained an unowned complete audit/private-plan caller")
    return {"retained": rows, "plans": plans}
