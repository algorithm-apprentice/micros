"""Closed MEM04 source, native binding, recipe and primitive declarations."""

import ast
from functools import lru_cache
import hashlib
from pathlib import Path
import re
import subprocess

from tests.host.c_source_records import function_records
from tests.host.evidence_authority_source import literal_value, regular_source, single_assignment
from tests.host.evidence_authority_types import AuthorityClass, Fact, digest
from tests.host.work_count_lexical import compound_layout, function_spans, positional_tokens
from tests.host.work_count_types import Stage


RUNTIME_REFERENCE = "f28377decc0d20533e05330f2f2211db3f230910"
FAMILY = "selected-address-space-baseline"
SOURCE_PATH = "tests/host/selected_address_space_source.py"
AUTHORITY_PATH = "tests/host/selected_address_space_authority.py"

RECIPE_ADMISSION = {
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
    ),
    "base_cmake_sha256": "9dfda2f310dfeb7cc9fd97f16b50935b56b2a802a83a89cfa3644a47ae811feb",
    "legacy_cmake_sha256": "528fd82f7df2fd8d49188db24362def4793e94d84f096e38bb30fe0d0dcc2746",
    "native_targets": (
        "selected_address_space_aggregate_test", "selected_address_space_path_test",
        "selected_address_space_context_test", "selected_address_space_ordinary_test",
    ),
    "native_case": "selected-address-space",
    "native_tiers": ("test-unit",),
    "native_label": "slow",
    "native_standard": "c17",
    "native_sanitizers": ("address", "undefined"),
    "source_pool": "mem03_source_proof",
    "generator": "tools/generate_selected_address_space_fixture.py",
    "generator_output": "build/host-debug/selected-address-space",
    "driver": "tools/check_selected_address_space.py",
    "fixture_header": "tests/host/selected_address_space_test.h",
    "fixture_sources": ("tests/host/selected_address_space_test.c", "tests/work/work_counts.c"),
    "baseline_headers": "fixed-runtime-reference",
    "current_headers": "actual-current-source",
    "ordinary_observations": "absent",
}
NATIVE_SOURCE_MEMBERS = RECIPE_ADMISSION["native_source_members"]
OBSERVED_HEADERS = ("kernel/boot/bootstrap_control_internal.h",)

SOURCE_ROLES = {
    "base_membership_sha256": "60cfae0e2a9a22cda02b6715cf5fd42a738ddf2da30409fa016f5028f3e0bd15",
    "production_paths": (
        "include/micros/user_address_space.h",
        "kernel/memory/user_address_space_activation.c",
        "kernel/memory/user_address_space_handoff.c",
        "kernel/memory/user_address_space_internal.h",
        "kernel/memory/user_address_space_mapping.c",
        "kernel/memory/user_address_space_mutation.c",
        "kernel/memory/user_address_space_runtime.c",
        "kernel/memory/user_address_space_tty.c",
        "kernel/memory/user_address_space_validation.c",
        "kernel/memory/user_address_space_vm_scratch.c",
        "kernel/scheduler/scheduler_context.c",
        "kernel/tty/tty_handoff_runtime.h",
        "kernel/tty/tty_handoff_runtime_validate.c",
        "kernel/vm/vm_handoff_core.c",
        "kernel/vm/vm_handoff_core.h",
        "kernel/vm/vm_handoff_runtime.c",
        "kernel/vm/vm_handoff_runtime.h",
    ),
    "dependent_production_paths": ("kernel/memory/user_address_space_handoff.c",),
    "stack_admission_inputs": (
        "tools/stack_bound_bindings.py",
        "tools/stack_bound_fatal.py",
        "tests/host/test_stack_bound_evidence.py",
    ),
    "fixed_adapters": (
        "kernel/tests/memory/address_space_handoff_test_runtime.c",
        "kernel/tests/memory/user_address_space_test_validation.c",
        "tests/host/kernel_layout_source.py",
        "tests/host/kernel_mapping_source.py",
        "tests/host/selected_ownership_source.py",
        "tests/host/test_kernel_vm_handoff_source_layout.py",
        "tests/host/test_scheduler_source_layout.py",
        "tests/host/test_tty_source_layout.py",
        "tools/generate_kernel_mapping_fixture.py",
        "tools/generate_selected_ownership_fixture.py",
    ),
    "unchanged_candidates": (
        "tests/host/memory_native_source_bindings.py",
        "tests/host/selected_ownership_native_source_bindings.py",
    ),
    "immutable_additions": (
        "tests/host/selected_address_space_test.c",
        "tests/host/selected_address_space_test.h",
        "tests/host/selected_address_space_source.py",
        "tests/host/selected_address_space_evidence.py",
        "tests/host/selected_address_space_authority.py",
        "tests/host/test_selected_address_space.py",
        "tools/generate_selected_address_space_fixture.py",
        "tools/check_selected_address_space.py",
    ),
}

STATE_WORK_CONTRACT = {
    "version": 1,
    "dimensions": (27, 14),
    "owner_mask": 11388,
    "channels": ("aggregate", "path", "context"),
    "producer_base": 5567941383739670528,
    "current_producer_offset": 256,
    "red_queries": 523,
    "green_queries": 529,
    "current_binding_guards": 30,
    "query_inputs_sha256": "2370689a52246a161868f3c78bbb8c276eb837186fcbca9073e71352d393dbae",
    "point_pte_reads": 3,
    "point_owner_reads": 4,
    "root_pte_reads": "1024+3*s",
    "root_owner_reads": "3+4*s",
    "controller_slots": 2,
    "binding_inspections": 1,
    "scratch_stores": 0,
    "complete_audits": 0,
    "owner_kinds": 8,
    "owner_geometry": "(proofs+1)*(8*g+2*k)",
    "owner_index_work": "(proofs+1)*(64*g+32*k)",
    "root_index_allowance": "512+3*s",
    "vm_header_work": 128,
    "tty_header_work": 64,
    "path_bytes": 128,
    "binding_bytes": 128,
    "rollback_cursor": 64,
    "first_free_frame": 128,
    "allocation_prefix_frames": (3, 2, 1),
    "target_workflows": ("test-qemu-user-address-space", "test-qemu-address-space-handoff"),
    "preservation": (
        "full-diagnostic-bodies-and-20-retained-caller-rows",
        "selected-owner-before-dereference-and-scalar-ancestry",
        "exact-allocator-ledger-object-pte-scratch-controller-and-output-state",
        "irq-restoration-and-full-same-root-hardware-commit",
        "VM-TTY-command-authority-and-prepared-address-space-seals",
        "actual-prior-memory-evidence-and-unchanged-collector-ABI",
    ),
}

SOURCE_PROJECTION = {
    "source_reference": "f28377decc0d20533e05330f2f2211db3f230910",
    "adapter_reference": "60e8b8e160b1c063e448cd32b0418535dd63bbb7",
    "stack_admission_sha256": {
        "tools/stack_bound_bindings.py": "91ce65eb6cac697ce168ff0225ee863547ac96cd10d2082ef0784cbc8c90604d",
        "tools/stack_bound_fatal.py": "582941e9382b7edc56dfb4fc5a4ad42745485e7e229f818fe6cb6a17f0be67e0",
        "tests/host/test_stack_bound_evidence.py": "2c22c60f1ed016f531a7a15de785471b3b0b71ee411fce0223d76382bfe924f2",
    },
    "handoff_preflight_functions": (
        "micros_user_address_space_prepare_wired_handoff",
        "micros_user_address_space_complete_wired_handoff",
    ),
    "predecessor_facts_sha256": "2904605d09065ca72937f26aaf2b42861872ba09363438739c817b63450aeb37",
    "affected_facts_sha256": "6ab74a18b41e7441592cad398f6cd18d41e2b5aab48007c73a110682a012eaa5",
    "baseline_primitive_sha256": "49637027076eb1e11e0aa0b1bcb7bf00df7c370bedc9015717e22c592dd0d32c",
    "baseline_primitive_sites": 294,
    "audit_paths_sha256": "c16ed9079e302b8666c990bcc2c353573eafae42bd12e119ac724c07a5faa364",
    "baseline_audits_sha256": "9586970719d25a8bf8eb64a89368d7827a7bc7e5aa3170cb12094cbdc7484428",
    "retained_audits_sha256": "a6c3e7d070772195f82a0808db703ea9b17bba6dcc8cce52cf44fd832cc25462",
    "migrated_audits_sha256": "071b8e9870c4ce5b0d999d92761b6a179b94f19aa4c36e1c9832f1660ac39807",
    "migrated_callers": (
        "micros_user_address_space_lookup", "micros_user_address_space_translate",
        "micros_user_address_space_allocate_page", "micros_user_address_space_activate",
        "scheduler_runtime_validate_selected_thread",
    ),
    "target_adapter_sha256": {
        "kernel/tests/memory/user_address_space_test_validation.c": "0b4cb6429912cc5fa6d39fc28637fccaa72977aa5cec3de291af9bc582c19ffb",
        "kernel/tests/memory/address_space_handoff_test_runtime.c": "848cb29706a170d0558aa31142696e1d6f78bda442bb3196bf76e7b5b2ba2ab5",
    },
    "registration_paths": (
        "tests/host/evidence_authority_inventory.py",
        "tests/host/evidence_authority_scope.py",
        "tests/host/test_evidence_authority.py",
        "tests/host/test_evidence_authority_scope.py",
        "tests/host/test_python_host_runtime.py",
        "tools/check_selected_ownership.py",
        "tests/host/test_selected_ownership.py",
    ),
    "retirement_keys": (
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/address_space_handoff_test_runtime.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/scheduler_context.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/tty_handoff_runtime.h/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/tty_handoff_runtime_validate.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/user_address_space_activation.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/user_address_space_internal.h/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/user_address_space_mapping.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/user_address_space_mutation.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/user_address_space_runtime.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/user_address_space_test_validation.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/user_address_space_tty.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/vm_handoff_core.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/vm_handoff_core.h/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/vm_handoff_runtime.h/2",
        "tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/include/micros/user_address_space.h",
        "tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/memory/user_address_space_internal.h",
        "tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/tty/tty_handoff_runtime.h",
        "tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/vm/vm_handoff_core.h",
        "tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/vm/vm_handoff_runtime.h",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/definitions/kernel/memory/user_address_space_validation.c",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/definitions/kernel/vm/vm_handoff_runtime.c",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/user_address_space_vm_scratch.c/micros_user_address_space_prepare_vm_scratch_removal",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/user_address_space_vm_scratch.c/prepare_vm_scratch_mapping_leaf",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/user_address_space_vm_scratch.c/prepare_vm_scratch_removal_leaf",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/native-input/include/micros/user_address_space.h",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/native-input/kernel/memory/user_address_space_internal.h",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/native-input/kernel/tty/tty_handoff_runtime.h",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/native-input/kernel/vm/vm_handoff_core.c",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/native-input/kernel/vm/vm_handoff_core.h",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/native-input/kernel/vm/vm_handoff_runtime.h",
        "tests/host/test_kernel_vm_handoff_source_layout.py:data-entry:STABLE_HEADER_HASHES/kernel/vm_handoff_core.h",
        "tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler.c/validate_selected_thread",
        "tests/host/test_tty_source_layout.py:data-entry:STABLE_HASHES/kernel/tty_handoff_runtime.h",
    ),
    "adapter_sha256": {
        "kernel/tests/memory/address_space_handoff_test_runtime.c": "e0bc1316a956ac5dc890306ad07f664ca4d06b735ffe7dad4d28368cefb8d33d",
        "kernel/tests/memory/user_address_space_test_validation.c": "dc5d451e6b147e45bef7a58e799ef9b7cbe05d511c975479f8bdd2a6a86b6cb7",
        "tests/host/kernel_layout_source.py": "490cc2eacfed49c61588484e361a10df152ac22fab341debc2cd42c7f393ec03",
        "tests/host/kernel_mapping_source.py": "a398d734030302a9ed6ab504a1dc7f059dcd05269e34ca4b910b9ca25387b3dc",
        "tests/host/memory_native_source_bindings.py": "4a90c7ec808b4fa3edd4606b83b250b0e9ddba4cbde09de03df65dea6ec35202",
        "tests/host/selected_ownership_native_source_bindings.py": "6b67d60370e9ee30c480a730c400589660e11d5919ce4e728b3265d449629903",
        "tests/host/selected_ownership_source.py": "9ce015b77ba30291aac8c196e7631929b5c7de965209fea968824c4ee44a06cf",
        "tests/host/test_kernel_vm_handoff_source_layout.py": "aa7718c62a53a2745966849bf8850bf538f6c159f7d6381e9dd2f871c78d793e",
        "tests/host/test_scheduler_source_layout.py": "88c5e7fc9a1d61a203da570d881d97fdb720aae579a11442bcd1380effe0fabf",
        "tests/host/test_tty_source_layout.py": "ec10e6f6a4489a1986c75300fb98c1f8c2bf6e2725bbd4f0cfe2eb7c4edc3e85",
        "tools/generate_kernel_mapping_fixture.py": "e6bd1f2e5da119b9e6fce04545f19046b9ea2c5d8c108b5d4e1ead67049766cc",
        "tools/generate_selected_ownership_fixture.py": "758f50f92bcf8fcea79c5ef8592a2f7f79a9102450ed5c50e605fea044069f03",
    },
    "mutable_functions": {
        "kernel/memory/user_address_space_runtime.c": (),
        "kernel/memory/user_address_space_validation.c": (),
        "kernel/memory/user_address_space_mapping.c": (
            "micros_user_address_space_lookup_leaf_locked",
            "micros_user_address_space_locate_leaf_slot_locked",
            "micros_user_address_space_lookup", "micros_user_address_space_translate",
        ),
        "kernel/memory/user_address_space_activation.c": ("micros_user_address_space_activate",),
        "kernel/memory/user_address_space_mutation.c": (
            "inspect_page_allocation", "rollback_page_allocation", "micros_user_address_space_allocate_page",
            "inspect_page_release", "micros_user_address_space_release_page",
        ),
        "kernel/memory/user_address_space_tty.c": (
            "prepare_tty_uart_leaf", "micros_user_address_space_prepare_tty_uart_mapping",
            "micros_user_address_space_validate_tty_uart_mapping",
        ),
        "kernel/memory/user_address_space_vm_scratch.c": (
            "prepare_vm_scratch_mapping_leaf", "prepare_vm_scratch_removal_leaf",
            "micros_user_address_space_prepare_vm_scratch_mapping",
            "micros_user_address_space_prepare_vm_scratch_removal",
        ),
        "kernel/scheduler/scheduler_context.c": ("scheduler_runtime_validate_selected_thread",),
        "kernel/vm/vm_handoff_core.c": (),
        "kernel/vm/vm_handoff_runtime.c": (),
        "kernel/tty/tty_handoff_runtime_validate.c": (),
    },
    "removed_functions": {
        "kernel/memory/user_address_space_activation.c": ("acquire_scratch", "release_scratch"),
        "kernel/memory/user_address_space_mutation.c": ("decode_pte",),
    },
    "new_helpers": {
        "kernel/memory/user_address_space_runtime.c": ("micros_user_address_space_require_idle",),
        "kernel/memory/user_address_space_mapping.c": (
            "selected_slot_is_required", "selected_table_is_distinct",
            "micros_user_address_space_select_path", "prepare_selected_path", "read_selected_leaf",
        ),
        "kernel/memory/user_address_space_validation.c": (
            "micros_user_address_space_load_selected_bindings",
            "micros_user_address_space_check_selected_table",
            "micros_user_address_space_classify_selected_leaf",
            "selected_kernel_root_is_valid", "validate_selected_shared_entries",
            "validate_selected_private_descriptor", "validate_selected_controller_slots",
            "micros_user_address_space_validate_selected_root",
        ),
        "kernel/memory/user_address_space_activation.c": ("micros_user_address_space_prepare_activation",),
        "kernel/memory/user_address_space_mutation.c": ("preflight_selected_allocation",),
        "kernel/vm/vm_handoff_core.c": (
            "scratch_binding_record_is_valid", "scratch_binding_load_is_valid",
            "micros_vm_handoff_inspect_scratch_binding",
        ),
        "kernel/vm/vm_handoff_runtime.c": ("micros_vm_handoff_runtime_inspect_scratch_binding",),
        "kernel/tty/tty_handoff_runtime_validate.c": (
            "tty_handoff_selected_binding_is_valid",
            "micros_tty_handoff_runtime_inspect_device_binding",
        ),
    },
    "current_automatic_primitives": {
        "micros_user_address_space_select_path": (
            ("insert", "context", "INDEX_PROBE", "", (
                ("for ( depth = 0 ; depth < MICROS_SV39_LEVEL_COUNT ; ++ depth ) {", 0),
            )),
            ("wrap", "path", "PTE_VISIT", "table -> entries [ index ]", (
                ("for ( depth = 0 ; depth < MICROS_SV39_LEVEL_COUNT ; ++ depth ) {", 0),
            )),
        ),
        "validate_selected_shared_entries": (
            ("wrap", "path", "PTE_VISIT",
             "kernel_root -> entries [ MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX ]", ()),
            ("insert", "context", "INDEX_PROBE", "", (
                ("for ( root_index = 0 ; root_index < MICROS_SV39_TABLE_ENTRY_COUNT ; ++ root_index ) {", 0),
            )),
            ("wrap", "path", "PTE_VISIT", "root -> entries [ root_index ]", (
                ("for ( root_index = 0 ; root_index < MICROS_SV39_TABLE_ENTRY_COUNT ; ++ root_index ) {", 0),
            )),
            ("wrap", "path", "PTE_VISIT", "kernel_root -> entries [ root_index ]", (
                ("for ( root_index = 0 ; root_index < MICROS_SV39_TABLE_ENTRY_COUNT ; ++ root_index ) {", 0),
            )),
        ),
        "validate_selected_private_descriptor": (
            ("wrap", "path", "PTE_VISIT",
             "root -> entries [ MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX ]", ()),
        ),
        "validate_selected_controller_slots": (
            ("insert", "context", "INDEX_PROBE", "", (
                ("for ( slot = 0 ; slot < 2 ; ++ slot ) {", 0),
            )),
        ),
        "scratch_binding_record_is_valid": (
            ("wrap", "context", "INDEX_PROBE",
             "& state -> mappings [ state -> load . process . slot ]", ()),
        ),
    },
    "current_extra_primitives": {
        "selected_table_is_distinct": (
            ("mem04-ancestor-root", "compare", "path", "COMPARISON",
             "physical_address == path -> addresses [ 0 ]", ()),
            ("mem04-ancestor-middle", "compare", "path", "COMPARISON",
             "physical_address == path -> addresses [ 1 ]", ()),
        ),
        "validate_selected_shared_entries": (
            ("mem04-shared-compare", "compare", "context", "COMPARISON",
             "root_pte != kernel_pte", (
                 ("for ( root_index = 0 ; root_index < MICROS_SV39_TABLE_ENTRY_COUNT ; ++ root_index ) {", 0),
             )),
        ),
        "validate_selected_private_descriptor": (
            ("mem04-root-child-ancestor", "compare", "path", "COMPARISON",
             "entry . physical_address == runtime -> process_root", ()),
        ),
        "rollback_page_allocation": (
            ("mem04-cursor-compensate", "after", "context", "COPY_UNIT",
             "allocation -> allocator -> allocation_cursor = allocation -> original_cursor ;", ()),
        ),
    },
    "baseline_extra_primitives": {
        "validate_shared_kernel_entries": (
            ("mem04-original-shared-compare", "compare", "context", "COMPARISON",
             "root -> entries [ root_index ] != kernel_root -> entries [ root_index ]", (
                 ("for ( root_index = 0 ; root_index < MICROS_SV39_TABLE_ENTRY_COUNT ; ++ root_index ) {", 0),
             )),
        ),
    },
}

BOOTSTRAP_FUNCTIONS = (
    "micros_bootstrap_frame_allocator", "ownership_mutation_is_allowed",
    "owner_is_process_bound", "validate_process_owners",
    "micros_frame_ownership_runtime_initialize", "micros_frame_ownership_runtime_ledger",
    "micros_frame_ownership_runtime_allocate", "micros_frame_ownership_runtime_release",
    "micros_frame_ownership_runtime_release_process_set",
    "micros_frame_ownership_runtime_validate", "micros_frame_ownership_runtime_release_process",
    "micros_frame_ownership_runtime_complete_handoff",
    "micros_frame_ownership_runtime_validate_metadata", "validate_active_process_owners",
)

NATIVE_PREFIX = '#include "tests/host/selected_address_space_test.h"\n'
CHANNELS = ("aggregate", "path", "context", "ordinary")
OWNER_MASK = 11388
COMPLETE_FUNCTIONS = (
    "micros_user_address_space_validate_locked",
    "micros_user_address_space_validate_all_process_roots_locked",
    "micros_frame_ownership_validate",
    "micros_frame_ownership_runtime_validate",
    "micros_kernel_objects_validate",
    "micros_scheduler_core_validate",
    "micros_endpoint_registry_validate",
    "micros_endpoint_registry_validate_objects",
    "micros_vm_handoff_state_validate",
    "micros_vm_handoff_runtime_validate",
    "micros_tty_handoff_runtime_validate",
)
NATIVE_FUNCTION_SUBSETS = {
    "kernel/memory/bootstrap_memory.c": BOOTSTRAP_FUNCTIONS,
    "kernel/memory/address_space.c": ("micros_kernel_address_space_report",),
    "kernel/ipc/ipc_runtime.c": (
        "micros_ipc_runtime_registry", "micros_ipc_runtime_authoritative_registry",
    ),
    "kernel/vm/vm_handoff_runtime.c": (
        "micros_vm_handoff_runtime_state", "micros_vm_handoff_runtime_authoritative_state",
        "micros_vm_handoff_runtime_scratch_authority",
    ),
    "kernel/scheduler/scheduler_context.c": ("scheduler_runtime_validate_selected_thread",),
    "kernel/boot/bootstrap_control_core.c": ("micros_bootstrap_control_find_binding",),
}


@lru_cache(maxsize=512)
def fixed_source(root, path):
    scaffold = {
        "CMakeLists.txt", *SOURCE_ROLES["fixed_adapters"], *SOURCE_ROLES["unchanged_candidates"],
        *SOURCE_PROJECTION["registration_paths"], *SOURCE_ROLES["stack_admission_inputs"],
    }
    reference = SOURCE_PROJECTION["adapter_reference"] if path in scaffold else RUNTIME_REFERENCE
    return subprocess.check_output(
        ("git", "show", reference + ":" + path),
        cwd=root, timeout=30,
    )


def candidate_paths():
    return tuple(sorted((
        *SOURCE_ROLES["production_paths"], *SOURCE_ROLES["fixed_adapters"],
        *SOURCE_ROLES["unchanged_candidates"],
    )))


def header_additions(path):
    error = "enum micros_user_address_space_error "
    handle = "struct micros_process_handle process"
    view = "struct micros_user_address_space_runtime_view *runtime"
    bindings = "const struct micros_user_address_space_bindings *bindings"
    if path == "include/micros/user_address_space.h":
        return (
            error + "micros_user_address_space_prepare_activation("
            + handle + ", uint64_t *root_physical_address);\n"
        )
    if path == "kernel/vm/vm_handoff_core.h":
        return (
            "struct micros_user_address_space_vm_scratch_authority;\n"
            "enum micros_vm_handoff_error micros_vm_handoff_inspect_scratch_binding(\n"
            "    const struct micros_vm_handoff_state *state,\n"
            "    struct micros_user_address_space_vm_scratch_authority *binding);\n"
        )
    if path == "kernel/vm/vm_handoff_runtime.h":
        return (
            "enum micros_vm_scratch_authority_status\n"
            "micros_vm_handoff_runtime_inspect_scratch_binding(\n"
            "    struct micros_user_address_space_vm_scratch_authority *binding);\n"
        )
    if path == "kernel/tty/tty_handoff_runtime.h":
        return (
            "enum micros_tty_device_authority_status\n"
            "micros_tty_handoff_runtime_inspect_device_binding(\n"
            "    const struct micros_kernel_objects *objects,\n"
            "    struct micros_tty_device_authority *binding);\n"
        )
    if path != "kernel/memory/user_address_space_internal.h":
        raise ValueError("MEM04 has no header extension in this owner")
    return (
        "struct micros_user_address_space_bindings {\n"
        "    struct micros_user_address_space_vm_scratch_authority vm;\n"
        "    struct micros_tty_device_authority tty;\n"
        "    bool tty_active;\n"
        "};\n\n"
        "struct micros_user_address_space_path {\n"
        "    struct micros_user_address_space_page_table *tables[MICROS_SV39_LEVEL_COUNT];\n"
        "    uint64_t addresses[MICROS_SV39_LEVEL_COUNT];\n"
        "    struct micros_user_address_space_page_indices indices;\n"
        "    uint16_t table_count;\n"
        "    struct micros_sv39_decoded_pte leaf;\n"
        "    struct micros_process_handle process;\n"
        "    bool exceptional;\n"
        "};\n\n"
        "_Static_assert(sizeof(struct micros_user_address_space_path) <= 128,\n"
        '               "selected paths must remain bounded automatic state");\n'
        "_Static_assert(sizeof(struct micros_user_address_space_bindings) <= 128,\n"
        '               "selected bindings must remain bounded automatic state");\n\n'
        + error + "micros_user_address_space_require_idle(\n"
        "    const struct micros_user_address_space_runtime_view *view);\n"
        + error + "micros_user_address_space_load_selected_bindings(\n"
        "    const struct micros_user_address_space_runtime_view *runtime,\n"
        "    struct micros_user_address_space_bindings *bindings);\n"
        + error + "micros_user_address_space_check_selected_table(\n"
        "    const struct micros_frame_ownership *ledger, " + handle + ", uint64_t physical_address);\n"
        + error + "micros_user_address_space_classify_selected_leaf(\n"
        "    const struct micros_user_address_space_runtime_view *runtime, " + handle + ",\n"
        "    uint64_t virtual_address, const struct micros_sv39_decoded_pte *leaf,\n"
        "    " + bindings + ", bool *exceptional);\n"
        + error + "micros_user_address_space_select_path(\n"
        "    " + view + ", " + handle + ", uint64_t virtual_address,\n"
        "    " + bindings + ", struct micros_user_address_space_path *path);\n"
        + error + "micros_user_address_space_validate_selected_root(\n"
        "    " + view + ", " + handle + ", " + bindings + ");\n"
    )


def extended_header(path, original):
    if not original.endswith("\n#endif\n"):
        raise ValueError("MEM04 fixed header lacks its exact outer include guard")
    if path == "kernel/memory/user_address_space_internal.h":
        anchor = '#include "kernel/vm/vm_handoff_core.h"\n'
        if original.count(anchor) != 1:
            raise ValueError("MEM04 private header lost its include boundary")
        original = original.replace(anchor, '#include "kernel/tty/tty_handoff_core.h"\n' + anchor, 1)
        for name in ("lookup_leaf_locked", "locate_leaf_slot_locked"):
            before = "micros_user_address_space_" + name + "(\n    const struct micros_process *process,\n"
            after = (
                "micros_user_address_space_" + name + "(\n"
                "    struct micros_user_address_space_runtime_view *runtime,\n"
                "    struct micros_process_handle process,\n"
            )
            if original.count(before) != 1:
                raise ValueError("MEM04 private walker lacks its fixed signature")
            original = original.replace(before, after, 1)
    return original[:-len("\n#endif\n")] + "\n" + header_additions(path) + "\n#endif\n"


def read_declarations(root):
    values = {}
    for path, names in (
        (AUTHORITY_PATH, ("DESIGN_PINS", "ORACLE_RULES")),
        (SOURCE_PATH, ("SOURCE_ROLES", "RECIPE_ADMISSION", "STATE_WORK_CONTRACT", "SOURCE_PROJECTION")),
    ):
        tree = ast.parse(regular_source(Path(root), path).read_text(encoding="utf-8"))
        values.update((name, literal_value(single_assignment(tree, name).value)) for name in names)
    validate_declarations(values)
    return values


def _paths(value, count):
    return (
        type(value) is tuple and len(value) == count and len(set(value)) == count
        and all(type(path) is str and path and not Path(path).is_absolute()
                and "\\" not in path and ".." not in Path(path).parts
                and Path(path).as_posix() == path for path in value)
    )


def validate_declarations(values):
    if type(values) is not dict or set(values) != {
        "DESIGN_PINS", "ORACLE_RULES", "SOURCE_ROLES", "RECIPE_ADMISSION",
        "STATE_WORK_CONTRACT", "SOURCE_PROJECTION",
    } or any(type(values[name]) is not dict for name in values if name != "ORACLE_RULES"):
        raise ValueError("MEM04 has incomplete literal authority declarations")
    pins = values["DESIGN_PINS"]
    documents = (
        "design_document", "adr_document", "research_document",
        "reporting_adr_document", "reporting_contract_document", "handoff_adr_document",
        "stack_adr_document", "stack_contract_document", "fatal_adr_document",
    )
    revisions = (
        "design_commit", "design_tree", "implementation_base_commit", "implementation_base_tree",
        "runtime_reference", "runtime_tree", "minix_reference", "minix_tree",
    )
    if (
        set(pins) != set((*documents, *revisions))
        or any(type(pins[name]) is not str or re.fullmatch(r"[0-9a-f]{40}", pins[name]) is None
               for name in revisions)
        or not _paths(tuple(pins[name] for name in documents), 9)
    ):
        raise ValueError("MEM04 design and reporting prerequisite pins are incomplete")
    roles, projection = values["SOURCE_ROLES"], values["SOURCE_PROJECTION"]
    if (
        set(roles) != {
            "base_membership_sha256", "production_paths", "fixed_adapters",
            "unchanged_candidates", "immutable_additions", "dependent_production_paths",
            "stack_admission_inputs",
        }
        or not _paths(roles["production_paths"], 17) or not _paths(roles["fixed_adapters"], 10)
        or not _paths(roles["unchanged_candidates"], 2) or not _paths(roles["immutable_additions"], 8)
        or roles["dependent_production_paths"] != ("kernel/memory/user_address_space_handoff.c",)
        or roles["stack_admission_inputs"] != (
            "tools/stack_bound_bindings.py", "tools/stack_bound_fatal.py",
            "tests/host/test_stack_bound_evidence.py",
        )
        or not set(roles["dependent_production_paths"]) <= set(roles["production_paths"])
        or len(set((*roles["production_paths"], *roles["fixed_adapters"], *roles["unchanged_candidates"]))) != 29
        or set(roles["immutable_additions"]) & set((*roles["production_paths"], *roles["fixed_adapters"], *roles["unchanged_candidates"]))
        or not _paths(projection.get("retirement_keys"), 33)
        or set(projection.get("adapter_sha256", ())) != set((*roles["fixed_adapters"], *roles["unchanged_candidates"]))
        or projection.get("source_reference") != values["DESIGN_PINS"].get("runtime_reference")
        or projection.get("adapter_reference") != pins["implementation_base_commit"]
        or type(projection.get("stack_admission_sha256")) is not dict
        or set(projection["stack_admission_sha256"]) != set(roles["stack_admission_inputs"])
        or any(type(value) is not str or re.fullmatch(r"[0-9a-f]{64}", value) is None
               for value in projection["stack_admission_sha256"].values())
        or projection.get("handoff_preflight_functions") != (
            "micros_user_address_space_prepare_wired_handoff",
            "micros_user_address_space_complete_wired_handoff",
        )
    ):
        raise ValueError("MEM04 source membership or reference differs from its closed design")
    rules = values["ORACLE_RULES"]
    if type(rules) is not tuple or len(rules) != 3:
        raise ValueError("MEM04 requires three independently owned replacement oracles")
    ids, tests, positions = set(), set(), set()
    for row in rules:
        if (
            type(row) is not tuple or len(row) != 6
            or any(type(item) is not str or not item for item in row[:5])
            or row[0] in ids or row[1] in tests or row[3] != "ADR-0107"
            or type(row[5]) is not tuple or not row[5] or len(set(row[5])) != len(row[5])
            or any(type(index) is not int or index < 0 or index >= 33 for index in row[5])
            or positions & set(row[5])
        ):
            raise ValueError("MEM04 oracle scope is malformed or duplicated")
        ids.add(row[0])
        tests.add(row[1])
        positions.update(row[5])
    if positions != set(range(33)) or tuple(len(row[5]) for row in rules) != (26, 6, 1):
        raise ValueError("MEM04 replacement oracles omit their exact retirement partition")
    recipe = values["RECIPE_ADMISSION"]
    if recipe["native_tiers"] != ("test-unit",) or recipe["native_label"] != "slow" or len(recipe["native_targets"]) != 4:
        raise ValueError("MEM04 requires four products in one full-only slow CTest")


def declaration_facts(root, owner):
    if owner not in (AUTHORITY_PATH, SOURCE_PATH):
        raise ValueError("MEM04 has no declaration in this source owner")
    values = read_declarations(root)
    pins, roles = values["DESIGN_PINS"], values["SOURCE_ROLES"]
    recipe = values["RECIPE_ADMISSION"]
    references = tuple(pins[key] for key in (
        "design_document", "adr_document", "research_document",
        "reporting_adr_document", "reporting_contract_document", "handoff_adr_document",
        "stack_adr_document", "stack_contract_document", "fatal_adr_document",
    ))
    consumers = tuple(f"B{index:02d}" for index in range(1, 13)) + ("B14",)
    candidates = (*roles["production_paths"], *roles["fixed_adapters"], *roles["unchanged_candidates"])

    def fact(kind, scope, classification, payloads, value):
        return Fact(
            owner + ":" + kind + ":" + scope, FAMILY, owner, kind, scope, classification,
            "B02", consumers, tuple(sorted({owner, *references, *payloads})), digest(value),
        )

    if owner == AUTHORITY_PATH:
        return (
            fact("design-anchor", "MEM04", AuthorityClass.TRANSPORT_IDENTITY, (), pins),
            *(fact("baseline-oracle", row[0], AuthorityClass.BASELINE_ORACLE,
                   roles["immutable_additions"], row) for row in values["ORACLE_RULES"]),
        )
    return (
        fact("source-membership", "MEM04", AuthorityClass.PERMANENT_INVARIANT,
             (*candidates, *roles["immutable_additions"], *roles["stack_admission_inputs"]), roles),
        fact("artifact-contract", "MEM04-recipes", AuthorityClass.ARTIFACT_CONTRACT,
             ("CMakeLists.txt", "CMakePresets.json", "include/micros/work_observation.h",
              "tests/work/work_counts.h", *recipe["fixture_sources"], *recipe["native_source_members"],
              recipe["generator"], recipe["driver"]), recipe),
        fact("state-work-contract", "MEM04", AuthorityClass.PERMANENT_INVARIANT,
             (*roles["production_paths"], *roles["immutable_additions"][:2],
              "tests/host/selected_address_space_evidence.py", "include/micros/work_observation.h",
              "tests/work/work_counts.h"), values["STATE_WORK_CONTRACT"]),
        fact("source-projection", "MEM04", AuthorityClass.TRANSPORT_IDENTITY,
             (*candidates, "tests/host/c_source_records.py", "tests/host/work_count_evidence.py",
              recipe["generator"], *roles["stack_admission_inputs"]), values["SOURCE_PROJECTION"]),
    )


def _function_calls(body):
    from tests.host.selected_ownership_source import _function_calls as calls

    return calls(body)


def _helper_contracts():
    error = "enum micros_user_address_space_error "
    view = "struct micros_user_address_space_runtime_view *runtime"
    handle = "struct micros_process_handle process"
    bindings = "const struct micros_user_address_space_bindings *bindings"
    path = "struct micros_user_address_space_path *path"
    return {
        "micros_user_address_space_require_idle": (
            error + "micros_user_address_space_require_idle(const struct micros_user_address_space_runtime_view *view)",
            {"riscv_irq_is_enabled"},
        ),
        "micros_user_address_space_load_selected_bindings": (
            error + "micros_user_address_space_load_selected_bindings(const " + view
            + ", struct micros_user_address_space_bindings *bindings)",
            {"micros_vm_handoff_runtime_inspect_scratch_binding",
             "micros_tty_handoff_runtime_inspect_device_binding",
             "micros_frame_ownership_runtime_validate_metadata"},
        ),
        "micros_user_address_space_check_selected_table": (
            error + "micros_user_address_space_check_selected_table(const struct micros_frame_ownership *ledger, "
            + handle + ", uint64_t physical_address)",
            {"micros_frame_ownership_lookup", "micros_user_address_space_validate_frame_owner"},
        ),
        "micros_user_address_space_classify_selected_leaf": (
            error + "micros_user_address_space_classify_selected_leaf(const " + view + ", " + handle
            + ", uint64_t virtual_address, const struct micros_sv39_decoded_pte *leaf, "
            + bindings + ", bool *exceptional)",
            {"micros_user_address_space_classify_tty_device_leaf", "micros_frame_ownership_lookup",
             "micros_user_address_space_classify_managed_leaf"},
        ),
        "selected_kernel_root_is_valid": (
            "static bool selected_kernel_root_is_valid(const " + view + ")",
            {"micros_frame_ownership_lookup"},
        ),
        "validate_selected_shared_entries": (
            "static " + error + "validate_selected_shared_entries(const " + view + ")",
            {"table_at"},
        ),
        "validate_selected_private_descriptor": (
            "static " + error + "validate_selected_private_descriptor(const " + view + ", " + handle + ")",
            {"table_at", "decode_pte", "micros_user_address_space_check_selected_table"},
        ),
        "validate_selected_controller_slots": (
            "static " + error + "validate_selected_controller_slots(" + view + ", " + handle + ", " + bindings + ")",
            {"micros_user_address_space_process_handles_equal", "micros_user_address_space_select_path"},
        ),
        "micros_user_address_space_validate_selected_root": (
            error + "micros_user_address_space_validate_selected_root(" + view + ", " + handle + ", " + bindings + ")",
            {"micros_user_address_space_check_selected_table", "selected_kernel_root_is_valid",
             "validate_selected_shared_entries", "validate_selected_private_descriptor",
             "validate_selected_controller_slots"},
        ),
        "selected_slot_is_required": (
            "static bool selected_slot_is_required(" + bindings + ", " + handle
            + ", uint64_t root, uint64_t virtual_address)",
            {"micros_user_address_space_process_handles_equal", "micros_tty_device_leaf_required"},
        ),
        "selected_table_is_distinct": (
            "static bool selected_table_is_distinct(const " + path + ", uint64_t physical_address, unsigned int depth)",
            set(),
        ),
        "micros_user_address_space_select_path": (
            error + "micros_user_address_space_select_path(" + view + ", " + handle
            + ", uint64_t virtual_address, " + bindings + ", " + path + ")",
            {"micros_user_address_space_runtime_resolve_process", "micros_user_address_space_read_page_indices",
             "micros_user_address_space_check_selected_table", "selected_table_is_distinct",
             "table_at", "decode_pte", "selected_slot_is_required",
             "micros_user_address_space_classify_selected_leaf"},
        ),
        "prepare_selected_path": (
            "static " + error + "prepare_selected_path(" + view + ", " + handle
            + ", uint64_t virtual_address, " + path + ")",
            {"micros_user_address_space_runtime_resolve_process",
             "micros_user_address_space_load_selected_bindings", "micros_user_address_space_select_path"},
        ),
        "read_selected_leaf": (
            "static " + error + "read_selected_leaf(" + view + ", " + handle
            + ", uint64_t virtual_address, struct micros_sv39_decoded_pte *leaf)",
            {"prepare_selected_path"},
        ),
        "micros_user_address_space_prepare_activation": (
            error + "micros_user_address_space_prepare_activation(" + handle + ", uint64_t *root_physical_address)",
            {"riscv_irq_is_enabled", "require_read_authority", "resolve_process_with_root",
             "micros_user_address_space_require_idle", "micros_user_address_space_load_selected_bindings",
             "micros_user_address_space_validate_selected_root"},
        ),
        "preflight_selected_allocation": (
            "static " + error + "preflight_selected_allocation(" + view + ", struct user_page_allocation *allocation)",
            {"micros_user_address_space_require_idle",
             "micros_user_address_space_load_selected_bindings", "micros_user_address_space_select_path",
             "active_root", "inspect_page_allocation"},
        ),
        "scratch_binding_record_is_valid": (
            "static bool scratch_binding_record_is_valid(const struct micros_vm_handoff_state *state)",
            {"micros_vm_handoff_process_handles_equal", "micros_vm_handoff_bytes_are_zero"},
        ),
        "scratch_binding_load_is_valid": (
            "static bool scratch_binding_load_is_valid(const struct micros_vm_handoff_state *state)",
            {"micros_vm_handoff_bytes_are_zero", "active_load_identity_is_valid",
             "scratch_binding_record_is_valid", "micros_vm_handoff_mapping_entry_is_valid"},
        ),
        "micros_vm_handoff_inspect_scratch_binding": (
            "enum micros_vm_handoff_error micros_vm_handoff_inspect_scratch_binding("
            "const struct micros_vm_handoff_state *state, "
            "struct micros_user_address_space_vm_scratch_authority *binding)",
            {"micros_vm_handoff_pointer_is_aligned", "vm_handoff_identity_is_valid", "scratch_binding_load_is_valid"},
        ),
        "micros_vm_handoff_runtime_inspect_scratch_binding": (
            "enum micros_vm_scratch_authority_status micros_vm_handoff_runtime_inspect_scratch_binding("
            "struct micros_user_address_space_vm_scratch_authority *binding)",
            {"micros_vm_handoff_inspect_scratch_binding"},
        ),
        "tty_handoff_selected_binding_is_valid": (
            "static bool tty_handoff_selected_binding_is_valid("
            "const struct micros_tty_handoff_runtime_state *state, const struct micros_kernel_objects *objects)",
            {"micros_process_resolve", "micros_thread_resolve", "tty_handoff_process_handles_equal"},
        ),
        "micros_tty_handoff_runtime_inspect_device_binding": (
            "enum micros_tty_device_authority_status micros_tty_handoff_runtime_inspect_device_binding("
            "const struct micros_kernel_objects *objects, struct micros_tty_device_authority *binding)",
            {"tty_handoff_context", "tty_handoff_identity_is_valid", "tty_handoff_selected_binding_is_valid"},
        ),
    }


def _current_signature(name, signature):
    from tests.host.c_source_records import compact

    value = compact(signature)
    if name in (
        "micros_user_address_space_lookup_leaf_locked", "micros_user_address_space_locate_leaf_slot_locked",
        "prepare_vm_scratch_mapping_leaf", "prepare_vm_scratch_removal_leaf",
    ):
        value = value.replace(
            compact("const struct micros_process *process"),
            compact("struct micros_user_address_space_runtime_view *runtime, struct micros_process_handle process"),
        )
    elif name == "prepare_tty_uart_leaf":
        value = value.replace(
            compact("const struct micros_frame_ownership *ledger, const struct micros_process *process"),
            compact("struct micros_user_address_space_runtime_view *runtime, "
                    "const struct micros_frame_ownership *ledger, struct micros_process_handle process"),
        )
    elif name == "inspect_page_allocation":
        value = value.replace(
            compact("struct user_page_allocation *allocation"),
            compact("struct user_page_allocation *allocation, const struct micros_user_address_space_path *path"),
        )
    elif name == "inspect_page_release":
        value = value.replace(
            compact("struct user_page_release *release"),
            compact("struct micros_user_address_space_runtime_view *runtime, struct user_page_release *release"),
        )
    return value


def _current_calls(name, body):
    calls = _function_calls(body)
    if name in ("micros_user_address_space_lookup", "micros_user_address_space_translate"):
        return (calls - {
            "acquire_scratch", "release_scratch", "micros_user_address_space_validate_locked",
            "micros_user_address_space_lookup_leaf_locked", "micros_user_address_space_reject_exceptional_leaf",
        }) | {"micros_user_address_space_require_idle", "read_selected_leaf"}
    if name in ("micros_user_address_space_lookup_leaf_locked", "micros_user_address_space_locate_leaf_slot_locked"):
        return {"prepare_selected_path"}
    if name == "micros_user_address_space_activate":
        return {
            "micros_user_address_space_runtime_lock", "micros_user_address_space_runtime_unlock",
            "micros_user_address_space_prepare_activation", "micros_riscv_activate_sv39", "expected_satp",
        }
    if name == "inspect_page_allocation":
        return set()
    if name == "micros_user_address_space_allocate_page":
        return (calls - {
            "acquire_scratch", "release_scratch", "micros_user_address_space_validate_locked",
            "active_root", "inspect_page_allocation",
        }) | {"preflight_selected_allocation"}
    if name == "scheduler_runtime_validate_selected_thread":
        return (calls - {"micros_user_address_space_validate"}) | {"micros_user_address_space_prepare_activation"}
    return calls


def _dependent_body(path, name, body):
    replacements = {
        "inspect_page_release": (
            "micros_user_address_space_lookup_leaf_locked(release->resolved,",
            "micros_user_address_space_lookup_leaf_locked(runtime, release->process,",
        ),
        "micros_user_address_space_release_page": (
            "inspect_page_release(&release, virtual_address)", "inspect_page_release(&runtime, &release, virtual_address)",
        ),
        "prepare_tty_uart_leaf": (
            "micros_user_address_space_locate_leaf_slot_locked(process,",
            "micros_user_address_space_locate_leaf_slot_locked(runtime, process,",
        ),
        "micros_user_address_space_prepare_tty_uart_mapping": (
            "prepare_tty_uart_leaf(ledger, resolved, &candidate)",
            "prepare_tty_uart_leaf(&runtime, ledger, process, &candidate)",
        ),
        "micros_user_address_space_validate_tty_uart_mapping": (
            "micros_user_address_space_lookup_leaf_locked(resolved,",
            "micros_user_address_space_lookup_leaf_locked(&runtime, process,",
        ),
        "prepare_vm_scratch_mapping_leaf": (
            "micros_user_address_space_locate_leaf_slot_locked(process,",
            "micros_user_address_space_locate_leaf_slot_locked(runtime, process,",
        ),
        "prepare_vm_scratch_removal_leaf": (
            "micros_user_address_space_locate_leaf_slot_locked(process,",
            "micros_user_address_space_locate_leaf_slot_locked(runtime, process,",
        ),
        "micros_user_address_space_prepare_vm_scratch_mapping": (
            "prepare_vm_scratch_mapping_leaf(context.resolved,",
            "prepare_vm_scratch_mapping_leaf(&runtime, process,",
        ),
        "micros_user_address_space_prepare_vm_scratch_removal": (
            "prepare_vm_scratch_removal_leaf(context.resolved,",
            "prepare_vm_scratch_removal_leaf(&runtime, process,",
        ),
    }
    if name not in replacements:
        return None
    from tests.host.c_source_records import compact

    before, after = map(compact, replacements[name])
    original = compact(body)
    if original.count(before) != 1:
        raise ValueError("MEM04 fixed dependent caller lost its exact argument boundary: " + path)
    return original.replace(before, after, 1)


def _source_skeleton(text):
    from tests.host.c_source_records import compact

    for record in function_records(text).values():
        fragment = record.signature + "{" + record.body + "}"
        if text.count(fragment) != 1:
            raise ValueError("MEM04 source has ambiguous function boundaries")
        text = text.replace(fragment, "", 1)
    return compact(text)


def _extended_skeleton(path, original):
    if path == "kernel/memory/user_address_space_mutation.c":
        anchor = "    uint64_t user_pte;\n"
        if original.count(anchor) != 1:
            raise ValueError("MEM04 allocation plan lost its fixed cursor-save boundary")
        original = original.replace(
            anchor, anchor + "    struct micros_frame_allocator *allocator;\n    uint64_t original_cursor;\n", 1,
        )
    for owner, anchor, addition in (
        ("kernel/memory/user_address_space_activation.c",
         '#include "arch/riscv64/mmu.h"\n', '#include "arch/riscv64/interrupt.h"\n'),
        ("kernel/vm/vm_handoff_core.c",
         '#include "kernel/vm/vm_handoff_internal.h"\n', '#include "micros/user_address_space_core.h"\n'),
    ):
        if path == owner:
            if original.count(anchor) != 1:
                raise ValueError("MEM04 source lost its fixed include boundary")
            original = original.replace(
                anchor, anchor + addition if path == "kernel/vm/vm_handoff_core.c" else addition + anchor, 1,
            )
    return _source_skeleton(original)


def extend_handoff_preflight(original):
    records = function_records(original)
    before = (
        "        error = micros_user_address_space_validate_locked(\n"
        "            &runtime,\n            &scratch,\n            process,\n            NULL\n"
        "        );\n"
        "        if (error != MICROS_USER_ADDRESS_SPACE_OK) {\n"
        "            goto done_with_scratch;\n"
        "        }\n"
    )
    after = before.replace(
        "            goto done_with_scratch;\n",
        "            error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;\n"
        "            goto done_with_scratch;\n",
        1,
    )
    for name in SOURCE_PROJECTION["handoff_preflight_functions"]:
        record = records[name]
        current = record.signature + "{" + record.body + "}"
        if record.body.count(before) != 1 or original.count(current) != 1:
            raise ValueError("MEM04 handoff dependency lost its exact complete-check guard")
        original = original.replace(current, current.replace(before, after, 1), 1)
    return original


def validate_retained_source(root, path, text, stage):
    from tests.host.c_source_records import compact

    if stage not in (Stage.RED, Stage.GREEN) or type(text) is not str:
        raise ValueError("MEM04 source admission has an unknown stage or representation")
    original = fixed_source(str(root), path).decode()
    if stage is Stage.RED or path not in SOURCE_ROLES["production_paths"]:
        if text != original:
            raise ValueError("MEM04 fixed or noncandidate native source changed: " + path)
        return
    if path in SOURCE_ROLES["dependent_production_paths"]:
        if text != extend_handoff_preflight(original):
            raise ValueError("MEM04 handoff dependency changed beyond its two error assignments")
        return
    if path.endswith(".h"):
        if text != extended_header(path, original):
            raise ValueError("MEM04 header differs from its exact byte-level extension: " + path)
        return
    before, after = function_records(original), function_records(text)
    additions = SOURCE_PROJECTION["new_helpers"].get(path, ())
    removed = SOURCE_PROJECTION["removed_functions"].get(path, ())
    if set(after) != set(before).difference(removed) | set(additions):
        raise ValueError("MEM04 current source has missing or unadmitted definitions: " + path)
    mutable = SOURCE_PROJECTION["mutable_functions"][path]
    for name, old in before.items():
        if name in removed:
            continue
        current = after[name]
        signature = _current_signature(name, old.signature) if name in mutable else compact(old.signature)
        if compact(current.signature) != signature or current.guards != old.guards:
            raise ValueError("MEM04 changed an original signature or CPP owner: " + name)
        if name not in mutable and current.body != old.body:
            raise ValueError("MEM04 changed a retained diagnostic or unrelated body: " + name)
        if name in mutable:
            if _function_calls(current.body) != _current_calls(name, old.body):
                raise ValueError("MEM04 operation escaped its exact admitted callees: " + name)
            dependent = _dependent_body(path, name, old.body)
            if dependent is not None and compact(current.body) != dependent:
                raise ValueError("MEM04 changed a retained command/release operation beyond its selected handle: " + name)
    contracts = _helper_contracts()
    for name in additions:
        current = after[name]
        signature, calls = contracts[name]
        if compact(current.signature) != compact(signature) or current.guards or _function_calls(current.body) != calls:
            raise ValueError("MEM04 helper escaped its exact signature/call admission: " + name)
    if _source_skeleton(text) != _extended_skeleton(path, original):
        raise ValueError("MEM04 changed unrelated includes, types or storage: " + path)

def _matching(values, start, opening, closing):
    depth = 1
    for index in range(start + 1, len(values)):
        if values[index] == opening:
            depth += 1
        elif values[index] == closing:
            depth -= 1
            if depth == 0:
                return index
    raise ValueError("MEM04 fixed primitive has an unbalanced delimiter")


def _member_start(values, member):
    index = member
    while index >= 2 and values[index - 1] in (".", "->"):
        index -= 2
        if values[index] == "]":
            depth = 1
            while index > 0 and depth:
                index -= 1
                depth += (values[index] == "]") - (values[index] == "[")
            if depth or index == 0:
                raise ValueError("MEM04 primitive has an ambiguous base subscript")
            index -= 1
    return index


def _owner(path):
    owners = {
        "memory": "KERNEL_MEMORY", "objects": "KERNEL_OBJECTS",
        "scheduler": "KERNEL_SCHEDULER", "endpoint": "KERNEL_ENDPOINT",
        "ipc": "KERNEL_IPC", "boot": "KERNEL_BOOT", "vm": "KERNEL_VM", "tty": "KERNEL_TTY",
    }
    return owners[path.split("/")[1]]


def _body_sites(path, name, body):
    from tests.host.selected_ownership_source import _original_regions

    positions = positional_tokens(body)
    values = tuple(token.value for token in positions)
    stacks, closings = compound_layout(values)
    sites = []
    owner = _owner(path)

    def add(mode, channel, kind, start, end):
        begin = positions[start].start if start < len(positions) else len(body)
        finish = positions[end - 1].end if end > start else begin
        identifier = "mem04-" + hashlib.sha256(
            repr((path, name, mode, kind, start, end)).encode()
        ).hexdigest()[:20]
        regions = _original_regions(values, stacks, closings, stacks[start]) if start < len(stacks) else ()
        sites.append((identifier, mode, channel, owner, kind, begin, finish, body[begin:finish], regions))

    if name in COMPLETE_FUNCTIONS:
        sites.append(("mem04-audit-" + hashlib.sha256(name.encode()).hexdigest()[:20],
                      "insert", "context", owner, "COMPLETE_VALIDATOR_CALL", 0, 0, "", ()))
    for index, value in enumerate(values):
        if value in ("for", "while") and index + 1 < len(values) and values[index + 1] == "(":
            closing = _matching(values, index + 1, "(", ")")
            if closing + 1 >= len(values) or values[closing + 1] != "{":
                raise ValueError("MEM04 observed loop requires its fixed compound")
            add("insert", "context", "INDEX_PROBE", closing + 2, closing + 2)
        if (
            value == "initialization_magic" and "/vm/" in path
            and index + 1 < len(values) and values[index + 1] != "="
        ):
            add("wrap", "context", "RECORD_VISIT", _member_start(values, index), index + 1)
        if value == "service_id" and name == "tty_handoff_identity_is_valid":
            add("wrap", "context", "RECORD_VISIT", _member_start(values, index), index + 1)
        if index + 1 >= len(values) or values[index + 1] != "[":
            continue
        kind = None
        channel = "context"
        if value == "entries" and "/memory/user_address_space" in path:
            kind, channel = "PTE_VISIT", "path"
        elif value == "owners" and "/memory/" in path:
            kind, channel = "RECORD_VISIT", "path"
        elif value == "managed_ranges" and "/memory/" in path:
            kind = "RANGE_VISIT"
        elif value == "allocated_bitmap" and "/memory/" in path:
            kind = "BITMAP_WORD_LOAD"
        elif value == "mappings" and "/vm/" in path:
            kind = "INDEX_PROBE"
        if kind is not None and (index == 0 or values[index - 1] not in (".", "->")):
            kind = None
        if kind is not None:
            end = _matching(values, index + 1, "[", "]") + 1
            following = end
            while following + 1 < len(values) and values[following] in (".", "->"):
                following += 2
            start = _member_start(values, index)
            addressed = start > 0 and values[start - 1] == "&"
            written = following < len(values) and values[following] in (
                "=", "|=", "&=", "^=", "+=", "-=", "++", "--",
            )
            if not addressed and not written and value != "mappings":
                add("wrap", channel, kind, start, end)
            elif addressed and kind in ("RANGE_VISIT", "INDEX_PROBE"):
                add("wrap", channel, kind, start - 1, end)
            elif written and kind == "PTE_VISIT":
                semicolon = values.index(";", following)
                add("insert", "path", "COPY_UNIT", semicolon + 1, semicolon + 1)
        if value == "bitmap" and name in (
            "clear_scratch_bitmap", "micros_user_address_space_clear_bitmap",
        ):
            closing = _matching(values, index + 1, "[", "]")
            if values[closing + 1:closing + 4] != ("=", "0", ";"):
                raise ValueError("MEM04 scratch primitive differs from its fixed store")
            add("insert", "context", "CLEAR_UNIT", closing + 4, closing + 4)
    if len({site[0] for site in sites}) != len(sites):
        raise ValueError("MEM04 primitive identifiers are not unique")
    return tuple(sites)


@lru_cache(maxsize=128)
def baseline_observations(root, path):
    text = fixed_source(root, path).decode()
    spans = function_spans(text)
    selected = NATIVE_FUNCTION_SUBSETS.get(path, tuple(spans))
    return {
        name: _body_sites(path, name, text[spans[name].body_start:spans[name].body_end])
        for name in selected
    }


def _explicit_sites(path, name, body, declarations):
    from tests.host.c_source_records import tokens
    from tests.host.work_count_lexical import matches_at
    from tests.host.work_count_projection import resolve_regions, validate_comparison
    from tests.host.work_count_types import Region

    positions = positional_tokens(body)
    values = tuple(item.value for item in positions)
    stacks, closings = compound_layout(values)
    result = []
    for identifier, mode, channel, kind, fragment, regions in declarations:
        start, end, enclosing = resolve_regions(
            values, stacks, closings, tuple(Region(*region) for region in regions),
        )
        matches = [index for index in matches_at(values, tokens(fragment), start, end) if stacks[index] == enclosing]
        if len(matches) != 1:
            raise ValueError("MEM04 declared primitive is missing or ambiguous: " + name + "/" + identifier)
        first = matches[0]
        last = first + len(tokens(fragment))
        begin, finish = positions[first].start, positions[last - 1].end
        if mode == "compare":
            validate_comparison(fragment)
            if kind != "COMPARISON":
                raise ValueError("MEM04 comparison changed its work dimension")
        elif mode == "after" and tokens(fragment)[-1] == ";":
            begin = finish
        else:
            raise ValueError("MEM04 explicit primitive has an unknown placement")
        result.append((identifier, mode, channel, _owner(path), kind,
                       begin, finish, body[begin:finish], regions))
    return tuple(result)


def observation_sites(root, path, text, stage):
    from tests.host.c_source_records import tokens

    records = function_records(text)
    original = baseline_observations(str(root), path)
    selected = NATIVE_FUNCTION_SUBSETS.get(path, tuple(records))
    if stage is Stage.GREEN and path == "kernel/vm/vm_handoff_runtime.c":
        selected = (*selected, "micros_vm_handoff_runtime_inspect_scratch_binding")
    additions = set(SOURCE_PROJECTION["new_helpers"].get(path, ()))
    mutable = set(SOURCE_PROJECTION["mutable_functions"].get(path, ()))
    result = {}
    for name in selected:
        if name not in records:
            raise ValueError("MEM04 observed native closure omitted a declared definition")
        body = records[name].body
        if stage is Stage.GREEN and name in additions | mutable:
            automatic = _body_sites(path, name, body)
            observed = tuple(
                (mode, channel, kind, " ".join(tokens(fragment)), regions)
                for _, mode, channel, _, kind, _, _, fragment, regions in automatic
            )
            expected = SOURCE_PROJECTION["current_automatic_primitives"].get(name, ())
            if observed != expected:
                raise ValueError("MEM04 current source changed its frozen primitive/compound inventory: " + name)
            automatic = tuple(
                ("mem04-current-" + hashlib.sha256((name + "/" + str(index)).encode()).hexdigest()[:20], *row[1:])
                for index, row in enumerate(automatic)
            )
            declarations = SOURCE_PROJECTION["current_extra_primitives"].get(name, ())
        else:
            automatic = original[name]
            declarations = SOURCE_PROJECTION["baseline_extra_primitives"].get(name, ())
        result[name] = (*automatic, *_explicit_sites(path, name, body, declarations))
    return result


def _observation_call(site, fragment=None):
    identifier, mode, _, owner, kind, _, _, original, _ = site
    if fragment is None:
        fragment = original
    if mode == "wrap":
        return (
            f'MICROS_AS_OBSERVE_VALUE("{identifier}", MICROS_WORK_OWNER_{owner}, '
            f"MICROS_WORK_KIND_{kind}, ({fragment}))"
        )
    if mode == "compare":
        return f'MICROS_TEST_WORK_COMPARE("{identifier}", MICROS_WORK_OWNER_{owner}, ({fragment}))'
    return f'MICROS_TEST_WORK_HIT("{identifier}", MICROS_WORK_OWNER_{owner}, MICROS_WORK_KIND_{kind}, 1);'


def observe_source(root, path, text, channel, stage=Stage.RED):
    if channel not in CHANNELS:
        raise ValueError("MEM04 observation channel is unknown")
    validate_retained_source(root, path, text, stage)
    if channel == "ordinary":
        return text
    spans = function_spans(text)
    edits, comparisons = [], []
    for name, sites in observation_sites(root, path, text, stage).items():
        span = spans[name]
        for site in sites:
            _, mode, category, _, _, start, end, _, _ = site
            if channel != "aggregate" and category != channel:
                continue
            if mode == "compare":
                comparisons.append((name, site, span.body_start + start, span.body_start + end))
            else:
                edits.append((span.body_start + start, span.body_start + end, _observation_call(site)))
    for start, end, replacement in sorted(edits, reverse=True):
        text = text[:start] + replacement + text[end:]
    for name, site, begin, end in comparisons:
        fragment = site[7]
        for start, finish, replacement in sorted(edits, reverse=True):
            if begin <= start < end and finish <= end:
                fragment = fragment[:start - begin] + replacement + fragment[finish - begin:]
        span = function_spans(text)[name]
        body = text[span.body_start:span.body_end]
        if body.count(fragment) != 1:
            raise ValueError("MEM04 comparison lost its exact observed primitive")
        body = body.replace(fragment, _observation_call(site, fragment), 1)
        text = text[:span.body_start] + body + text[span.body_end:]
    return NATIVE_PREFIX + text


def project_observations(root, path, text, channel, stage=Stage.RED):
    if channel not in CHANNELS:
        raise ValueError("MEM04 observation channel is unknown")
    original = source_bytes(root, path, stage).decode()
    if channel == "ordinary":
        if text != original:
            raise ValueError("MEM04 ordinary source contains an undeclared transformation")
        return text
    if not text.startswith(NATIVE_PREFIX):
        raise ValueError("MEM04 observation prefix is missing")
    restored = text[len(NATIVE_PREFIX):]
    sites = tuple(site for rows in observation_sites(root, path, original, stage).values() for site in rows)
    for site in sorted(sites, key=lambda row: row[1] == "compare"):
        _, mode, category, _, _, _, _, fragment, _ = site
        if channel != "aggregate" and category != channel:
            continue
        call = _observation_call(site)
        replacement = fragment if mode in ("wrap", "compare") else ""
        if restored.count(call) != 1:
            raise ValueError("MEM04 observation is missing, changed or duplicated")
        restored = restored.replace(call, replacement, 1)
    if restored != original or observe_source(root, path, restored, channel, stage) != text:
        raise ValueError("MEM04 observation changed source or moved away from its primitive")
    return restored


def definitions(text, names):
    records = function_records(text)
    if len(set(names)) != len(names) or not set(names) <= set(records):
        raise ValueError("MEM04 native closure is missing a real source definition")
    return (
        "".join(records[name].signature + ";\n" for name in names)
        + "".join(records[name].signature + "{" + records[name].body + "}\n" for name in names)
    )


def replace_body(text, name, body):
    record = function_records(text)[name]
    original = record.signature + "{" + record.body + "}"
    if text.count(original) != 1:
        raise ValueError("MEM04 native binding has ambiguous source boundaries")
    return text.replace(original, record.signature + "{\n" + body + "\n}", 1)


def bind_native(path, text, stage=Stage.RED):
    prefix = NATIVE_PREFIX
    if path == "arch/riscv64/interrupt.h":
        for name, body in (
            ("riscv_irq_save", "    return micros_as_native_irq_save();"),
            ("riscv_irq_restore", "    micros_as_native_irq_restore(status);"),
            ("riscv_irq_is_enabled", "    return micros_as_native_irq_enabled();"),
        ):
            text = replace_body(text, name, body)
        return prefix + text
    if path == "kernel/memory/bootstrap_memory.c":
        return (
            prefix + '#include "micros/kernel_object_runtime.h"\n#include <string.h>\n'
            "#define riscv_irq_is_enabled micros_as_native_irq_enabled\n"
            "static struct micros_frame_allocator bootstrap_allocator;\n"
            "static struct micros_frame_ownership bootstrap_ownership;\n"
            "static bool ownership_runtime_ready;\n"
            + definitions(text, BOOTSTRAP_FUNCTIONS)
            + "\nstruct micros_frame_allocator *micros_as_native_allocator(void)\n"
            "{ return &bootstrap_allocator; }\n"
            "struct micros_frame_ownership *micros_as_native_ownership(void)\n"
            "{ return &bootstrap_ownership; }\n"
            "void micros_as_native_bootstrap_reset(void)\n{\n"
            "    memset(&bootstrap_allocator, 0, sizeof(bootstrap_allocator));\n"
            "    memset(&bootstrap_ownership, 0, sizeof(bootstrap_ownership));\n"
            "    ownership_runtime_ready = false;\n}\n"
        )
    if path == "kernel/objects/kernel_object_runtime.c":
        return prefix + "#include <string.h>\n" + text + (
            "\nvoid micros_as_native_objects_reset(void)\n{\n"
            "    memset(&kernel_objects, 0, sizeof(kernel_objects));\n"
            "    memset(&boot_hart_handle, 0, sizeof(boot_hart_handle));\n"
            "    runtime_ready = false;\n}\n"
        )
    if path == "kernel/memory/address_space.c":
        return (
            prefix + '#include "kernel/memory/address_space_internal.h"\n#include <string.h>\n'
            "static struct micros_kernel_address_space_private_state kernel_address_space;\n"
            + definitions(text, ("micros_kernel_address_space_report",))
            + "\nvoid micros_as_native_kernel_reset(void)\n"
            "{ memset(&kernel_address_space, 0, sizeof(kernel_address_space)); }\n"
            "void micros_as_native_kernel_bind(const struct micros_kernel_address_space_report *report)\n"
            "{ kernel_address_space.report = *report; kernel_address_space.initialized = true; }\n"
        )
    if path == "kernel/ipc/ipc_runtime.c":
        return (
            prefix + '#include "micros/ipc_runtime.h"\n#include "kernel/ipc/ipc_runtime_internal.h"\n'
            "#include <string.h>\n"
            "static struct micros_endpoint_registry authoritative_registry;\n"
            "static bool runtime_ready;\n"
            + definitions(text, ("micros_ipc_runtime_registry", "micros_ipc_runtime_authoritative_registry"))
            + "\nvoid micros_as_native_ipc_reset(void)\n"
            "{ memset(&authoritative_registry, 0, sizeof(authoritative_registry)); runtime_ready = false; }\n"
            "struct micros_endpoint_registry *micros_as_native_ipc_input(void)\n"
            "{ return &authoritative_registry; }\n"
            "void micros_as_native_ipc_publish(void)\n"
            "{ runtime_ready = true; }\n"
        )
    if path == "kernel/vm/vm_handoff_runtime.c":
        return (
            prefix + '#include "kernel/vm/vm_handoff_runtime.h"\n#include <string.h>\n'
            "static struct micros_vm_handoff_state handoff_state;\n"
            + definitions(text, (
                "micros_vm_handoff_runtime_state", "micros_vm_handoff_runtime_authoritative_state",
                "micros_vm_handoff_runtime_scratch_authority",
                *(("micros_vm_handoff_runtime_inspect_scratch_binding",) if stage is Stage.GREEN else ()),
            ))
            + "\nvoid micros_as_native_vm_reset(void)\n"
            "{ memset(&handoff_state, 0, sizeof(handoff_state)); }\n"
            "struct micros_vm_handoff_state *micros_as_native_vm_input(void)\n"
            "{ return &handoff_state; }\n"
        )
    if path == "kernel/scheduler/scheduler_context.c":
        return (
            prefix + '#include "kernel/scheduler/scheduler_runtime_internal.h"\n'
            '#include "micros/user_address_space.h"\n'
            + definitions(text, ("scheduler_runtime_validate_selected_thread",))
        )
    if path == "kernel/boot/bootstrap_control_core.c":
        return (
            prefix + '#include "kernel/boot/bootstrap_control_internal.h"\n'
            + definitions(text, ("micros_bootstrap_control_find_binding",))
        )
    if path == "kernel/memory/user_address_space_runtime.c":
        return prefix + replace_body(text, "read_satp", "    return micros_as_native_read_satp();") + (
            "\nconst uint64_t *micros_as_native_scratch_bitmap(bool release)\n"
            "{ return release ? release_bitmap : reachable_bitmap; }\n"
            "bool micros_as_native_scratch_busy(void)\n"
            "{ return scratch_busy; }\n"
        )
    return text


def source_bytes(root, path, stage):
    original = fixed_source(root, path)
    if stage is Stage.RED:
        return original
    if stage is not Stage.GREEN:
        raise ValueError("MEM04 source stage is unknown")
    from tools.stack_artifact_files import read_regular

    current = read_regular(Path(root) / path)[0]
    validate_retained_source(root, path, current.decode(), stage)
    return current


def _native_target_recipe(index, channel, target):
    observed = channel != "ordinary"
    root = "${MEM04_NATIVE_ROOT}/" + channel
    result = (
        "    add_executable(" + target + " ${MEM04_SOURCES_" + str(index) + "}\n"
        "        tests/host/selected_address_space_test.c\n"
        + ("        tests/work/work_counts.c\n" if observed else "")
        + "    )\n"
        "    target_include_directories(" + target + " PRIVATE\n"
        '        "' + root + '" "' + root + '/include"\n'
        '        "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/include")\n'
        "    target_compile_features(" + target + " PRIVATE c_std_17)\n"
        "    target_compile_options(" + target
        + " PRIVATE -Wall -Wextra -Werror -Wpedantic -Wmissing-prototypes -Wstrict-prototypes -fno-builtin)\n"
        "    target_compile_definitions(" + target + " PRIVATE MICROS_BUILD_USER_ADDRESS_SPACE_TEST=1)\n"
        '    if(MICROS_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")\n'
        "        target_compile_options(" + target + " PRIVATE -fno-omit-frame-pointer -fsanitize=address,undefined)\n"
        "        target_link_options(" + target + " PRIVATE -fsanitize=address,undefined)\n"
        "    endif()\n"
    )
    if observed:
        return result + (
            "    target_compile_definitions(" + target + " PRIVATE MICROS_TEST_WORK_COUNTS=1 "
            "MICROS_AS_NATIVE_CHANNEL=" + str(index) + ")\n"
        )
    return result + (
        "    target_compile_definitions(" + target + " PRIVATE\n"
        "        MICROS_SELECTED_ADDRESS_SPACE_LAUNCHER=1\n"
        '        "MICROS_SELECTED_ADDRESS_SPACE_PYTHON=\\"${MICROS_PYTHON}\\""\n'
        '        "MICROS_SELECTED_ADDRESS_SPACE_DRIVER=\\"${CMAKE_CURRENT_SOURCE_DIR}/tools/check_selected_address_space.py\\""\n'
        '        "MICROS_SELECTED_ADDRESS_SPACE_ROOT=\\"${CMAKE_CURRENT_SOURCE_DIR}\\""\n'
        "    )\n"
    )


def native_recipe():
    result = "".join(
        "    add_custom_command(\n"
        "        OUTPUT ${" + owner + "_NATIVE_OUTPUTS} APPEND\n"
        "        DEPENDS tests/host/selected_address_space_source.py\n"
        "            tests/host/selected_address_space_authority.py\n"
        "            tests/host/evidence_authority_scope.py\n"
        "            tools/validation_context.py\n"
        "            tools/validation_inputs.py\n"
        "    )\n"
        for owner in ("MEM01", "MEM02", "MEM03")
    )
    result += (
        '    set(MEM04_NATIVE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/selected-address-space")\n'
        "    set(MEM04_NATIVE_INPUTS\n"
        + "".join("        " + path + "\n" for path in NATIVE_SOURCE_MEMBERS)
        + "    )\n"
        "    file(GLOB_RECURSE MEM04_NATIVE_HEADERS\n"
        '        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"\n'
        '        "${CMAKE_CURRENT_SOURCE_DIR}/arch/*.h"\n'
        '        "${CMAKE_CURRENT_SOURCE_DIR}/kernel/*.h"\n'
        "    )\n"
        "    set(MEM04_NATIVE_OUTPUTS)\n"
    )
    for index, channel in enumerate(CHANNELS):
        root = "${MEM04_NATIVE_ROOT}/" + channel
        result += (
            "    set(MEM04_SOURCES_" + str(index) + ")\n"
            "    foreach(mem04_source IN LISTS MEM04_NATIVE_INPUTS)\n"
            "        list(APPEND MEM04_SOURCES_" + str(index) + ' "' + root + '/${mem04_source}")\n'
            "    endforeach()\n"
            "    list(APPEND MEM04_NATIVE_OUTPUTS ${MEM04_SOURCES_" + str(index) + "})\n"
            "    foreach(mem04_header IN LISTS MEM04_NATIVE_HEADERS)\n"
            '        file(RELATIVE_PATH mem04_relative_header "${CMAKE_CURRENT_SOURCE_DIR}" "${mem04_header}")\n'
            '        list(APPEND MEM04_NATIVE_OUTPUTS "' + root + '/${mem04_relative_header}")\n'
            "    endforeach()\n"
        )
    result += (
        "    add_custom_command(\n"
        "        OUTPUT ${MEM04_NATIVE_OUTPUTS}\n"
        "        JOB_POOL mem03_source_proof\n"
        '        COMMAND "${MICROS_PYTHON}" ${MICROS_PYTHON_ARGS}\n'
        '            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_selected_address_space_fixture.py"\n'
        "        DEPENDS ${MEM04_NATIVE_INPUTS} ${MEM04_NATIVE_HEADERS}\n"
        + "".join("            " + path + "\n" for path in SOURCE_ROLES["immutable_additions"])
        + "            tests/host/evidence_authority_scope.py\n"
        "            tests/host/selected_ownership_source.py\n"
        "            tests/host/c_source_records.py\n"
        "            tests/host/work_count_lexical.py\n"
        "            tests/host/work_count_projection.py\n"
        "            tools/validation_context.py\n"
        "            tools/validation_inputs.py\n"
        '        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"\n'
        "        VERBATIM\n"
        "    )\n"
    )
    for index, (channel, target) in enumerate(zip(CHANNELS, RECIPE_ADMISSION["native_targets"], strict=True)):
        result += _native_target_recipe(index, channel, target)
    return result + (
        "    add_test(NAME selected-address-space COMMAND selected_address_space_ordinary_test\n"
        + "".join('        "$<TARGET_FILE:' + target + '>"\n' for target in RECIPE_ADMISSION["native_targets"][:-1])
        + "    )\n"
        "    set_tests_properties(selected-address-space PROPERTIES LABELS slow)\n"
        "    add_dependencies(test-unit " + " ".join(RECIPE_ADMISSION["native_targets"]) + ")\n\n"
    )


def extend_cmake(text):
    anchor = '    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/stack/StackEvidence.cmake")\n'
    if hashlib.sha256(text.encode()).hexdigest() != RECIPE_ADMISSION["base_cmake_sha256"] or text.count(anchor) != 1:
        raise ValueError("MEM04 native recipe lacks its complete reviewed base")
    if "    set_property(GLOBAL APPEND PROPERTY JOB_POOLS mem03_source_proof=1)\n" not in text:
        raise ValueError("MEM04 requires the existing depth-one source-proof pool")
    return text.replace(anchor, native_recipe() + anchor, 1)


def project_cmake(text):
    if not any(marker in text for marker in ("MEM04_NATIVE", "selected_address_space", "selected-address-space")):
        return text
    addition = native_recipe()
    anchor = '    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/stack/StackEvidence.cmake")\n'
    if text.count(addition + anchor) != 1:
        raise ValueError("MEM04 native recipe is changed, partial or relocated")
    original = text.replace(addition, "", 1)
    if hashlib.sha256(original.encode()).hexdigest() not in (
        RECIPE_ADMISSION["base_cmake_sha256"], RECIPE_ADMISSION["legacy_cmake_sha256"],
    ):
        raise ValueError("MEM04 native recipe changed unrelated predecessor bytes")
    return original


def project_include_transports(root, logical, raw, transports, required):
    if type(required) is not bool:
        raise AssertionError("MEM04 include transport requires checked source authority")
    if logical != "kernel/user_address_space_internal.h" or required:
        return transports
    path = "kernel/memory/user_address_space_internal.h"
    if raw == fixed_source(str(root), path):
        return transports
    validate_retained_source(root, path, raw.decode(), Stage.GREEN)
    if transports != ((
        8, "kernel/vm_handoff_core.h",
        '#include "kernel/vm_handoff_core.h"\n',
        '#include "kernel/vm/vm_handoff_core.h"\n',
    ),):
        raise AssertionError("MEM04 private-header predecessor transport changed")
    return (
        (8, "kernel/tty_handoff_core.h", '#include "kernel/tty_handoff_core.h"\n',
         '#include "kernel/tty/tty_handoff_core.h"\n'),
        *((line + 1, target, old, new) for line, target, old, new in transports),
    )


def legacy_tty_additions(root, logical):
    from tests.host.evidence_authority_scope import identity_required

    if logical != "kernel/tty_handoff_runtime_validate.c":
        return frozenset()
    key = "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/" + logical + "/2"
    if identity_required(root, key):
        return frozenset()
    path = "kernel/tty/tty_handoff_runtime_validate.c"
    raw = read_regular_source(root, path)
    if raw == fixed_source(str(root), path):
        return frozenset()
    validate_retained_source(root, path, raw.decode(), Stage.GREEN)
    return frozenset(SOURCE_PROJECTION["new_helpers"][path])


def native_assertion_admission(root, family, path, text, stage):
    from tests.host.evidence_authority_scope import identity_required

    if stage not in (Stage.RED, Stage.GREEN):
        raise ValueError("MEM04 legacy admission has an unknown source stage")
    if stage is Stage.RED or family == "MEM01":
        return frozenset()
    owners = {
        "MEM02": "tests/host/memory_native_source_bindings.py",
        "MEM03": "tests/host/selected_ownership_native_source_bindings.py",
    }
    if family not in owners:
        raise ValueError("MEM04 legacy admission has an unknown family")
    prefix = owners[family] + ":native-source-admission:"
    scopes = tuple(
        (key, key.removeprefix(prefix)) for key in SOURCE_PROJECTION["retirement_keys"]
        if key == prefix + family + "/native-input/" + path
        or key == prefix + family + "/definitions/" + path
        or key.startswith(prefix + family + "/function/" + path + "/")
    )
    selected = frozenset(scope for key, scope in scopes if not identity_required(root, key))
    if selected:
        validate_retained_source(root, path, text, Stage.GREEN)
    return selected


def _legacy_changes(path):
    if path == "tests/host/selected_ownership_source.py":
        return (
            (
                "def native_assertion_admission(root, family, path, text, stage):\n"
                "    from tests.host.evidence_authority_scope import identity_required\n",
                "def native_assertion_admission(root, family, path, text, stage):\n"
                "    from tests.host.evidence_authority_scope import identity_required\n"
                "    from tests.host.selected_address_space_source import native_assertion_admission as next_admission\n\n"
                "    inherited = next_admission(root, family, path, text, stage)\n",
            ),
            (
                "    if stage is Stage.RED or not scopes:\n        return frozenset()\n",
                "    if stage is Stage.RED or not scopes:\n        return inherited\n",
            ),
            (
                "    if selected:\n        validate_retained_source(root, path, text, Stage.GREEN)\n    return selected\n",
                "    if selected:\n        validate_retained_source(root, path, text, Stage.GREEN)\n    return selected | inherited\n",
            ),
            (
                "def validate_retained_source(root, path, text, stage):\n"
                "    from tests.host.c_source_records import compact, function_records\n",
                "def validate_retained_source(root, path, text, stage):\n"
                "    from tests.host.c_source_records import compact, function_records\n"
                "    from tests.host.selected_address_space_source import native_assertion_admission as next_admission\n\n"
                '    retired = next_admission(root, "MEM03", path, text, stage)\n',
            ),
            (
                '            raise ValueError("MEM03 fixed or noncandidate source changed: " + path)\n',
                '            if "MEM03/native-input/" + path in retired:\n'
                "                return\n"
                '            raise ValueError("MEM03 fixed or noncandidate source changed: " + path)\n',
            ),
            (
                "    if set(after) != set(before) | set(additions):\n",
                '    if "MEM03/definitions/" + path not in retired and set(after) != set(before) | set(additions):\n',
            ),
            (
                "    for name, old in before.items():\n        current = after[name]\n",
                "    for name, old in before.items():\n"
                '        if "MEM03/function/" + path + "/" + name in retired:\n'
                "            continue\n"
                "        current = after[name]\n",
            ),
            (
                "def extend_cmake(text):\n",
                "def extend_cmake(text):\n"
                "    from tests.host.selected_address_space_source import native_recipe as selected_address_space_recipe\n\n",
            ),
            (
                "    return text.replace(anchor, native_recipe() + anchor, 1)\n",
                "    return text.replace(anchor, native_recipe() + selected_address_space_recipe() + anchor, 1)\n",
            ),
            (
                "def project_cmake(text):\n",
                "def project_cmake(text):\n"
                "    from tests.host.selected_address_space_source import project_cmake as project_selected_address_space\n\n"
                "    text = project_selected_address_space(text)\n",
            ),
            (
                "def project_legacy_adapter(path, text):\n",
                "def project_legacy_adapter(path, text):\n"
                "    from tests.host.selected_address_space_source import project_legacy_adapter as project_next\n\n"
                '    if "tests.host.selected_address_space_source" in text:\n'
                "        text = project_next(Path(__file__).resolve().parents[2], path, text)\n",
            ),
            (
                "def validate_scaffold(root):\n"
                "    from tests.host.work_count_recipe import project_host_test_commands\n",
                "def validate_scaffold(root):\n"
                "    from tests.host.work_count_recipe import project_host_test_commands\n"
                "    from tests.host.selected_address_space_source import project_legacy_adapter as project_next\n",
            ),
            (
                "    for path in SOURCE_ROLES[\"fixed_adapters\"]:\n"
                "        actual = regular_source(root, path).read_text()\n",
                "    for path in SOURCE_ROLES[\"fixed_adapters\"]:\n"
                "        actual = project_next(root, path, regular_source(root, path).read_text())\n",
            ),
        )
    if path == "tools/generate_selected_ownership_fixture.py":
        original = '                raise ValueError("MEM03 changed an unadmitted native dependency: " + name)\n'
        return ((original, (
            "                from tests.host.selected_address_space_source import native_assertion_admission\n"
            "                try:\n"
            '                    admitted = "MEM03/native-input/" + name in native_assertion_admission(\n'
            '                        root, "MEM03", name, data.decode("utf-8"), stage,\n'
            "                    )\n"
            "                except ValueError as error:\n"
            '                    raise ValueError("MEM03 changed an unadmitted native dependency: " + name) from error\n'
            "                if not admitted:\n" + "    " + original
        )),)
    if path == "tests/host/kernel_layout_source.py":
        return (
            (
                "    return _restored_include_bytes(logical, raw, expected_sha256, transports, required=required)\n",
                "    from tests.host.selected_address_space_source import project_include_transports\n\n"
                "    transports = project_include_transports(root, logical, raw, transports, required)\n"
                "    return _restored_include_bytes(logical, raw, expected_sha256, transports, required=required)\n",
            ),
            (
                "def restored_gate_ast_sha256(root: Path, relative: str) -> str:\n"
                '    source = (root / relative).read_text(encoding="utf-8")\n',
                "def restored_gate_ast_sha256(root: Path, relative: str) -> str:\n"
                "    from tests.host.selected_address_space_source import project_legacy_adapter\n\n"
                '    source = (root / relative).read_text(encoding="utf-8")\n'
                "    source = project_legacy_adapter(root, relative, source)\n",
            ),
        )
    if path == "tests/host/test_tty_source_layout.py":
        return (
            (
                "def original_locations(family):\n",
                "def original_locations(family):\n"
                "    from tests.host.selected_address_space_source import legacy_tty_additions\n\n",
            ),
            (
                "    for path in FAMILY_PATHS[family]:\n"
                "        for spelling, record in function_records(source(path)).items():\n",
                "    for path in FAMILY_PATHS[family]:\n"
                "        additions = legacy_tty_additions(ROOT, path) if family == 0 else frozenset()\n"
                "        for spelling, record in function_records(source(path)).items():\n"
                "            if spelling in additions:\n"
                "                continue\n",
            ),
            (
                "        allowed = {0: {1, 2, 3}, 1: set(), 2: {1}, 3: {1}}\n"
                "        for name, (owner, record) in records.items():\n"
                "            destinations = {\n"
                "                records[word][0] for word in tokens(record.body)\n"
                "                if word in records and records[word][0] != owner\n"
                "            }\n",
                "        allowed = {0: {1, 2, 3}, 1: set(), 2: {1}, 3: {1}}\n"
                "        for name, (owner, record) in records.items():\n"
                "            body_tokens = tokens(record.body)\n"
                "            destinations = {\n"
                "                records[word][0] for index, word in enumerate(body_tokens)\n"
                "                if word in records and records[word][0] != owner\n"
                '                and (index == 0 or body_tokens[index - 1] not in ("struct", "union", "enum"))\n'
                "            }\n",
            ),
        )
    if path == "tests/host/test_kernel_vm_handoff_source_layout.py":
        original = (
            "    def test_stable_headers_are_byte_exact(self) -> None:\n"
            "        observed = {\n"
            "            path: hashlib.sha256(\n"
            "                kernel_layout_source.read_bytes_path(REPOSITORY_ROOT / path)\n"
            "            ).hexdigest()\n"
            "            for path in STABLE_HEADER_HASHES\n"
            "        }\n"
            "        self.assertEqual(STABLE_HEADER_HASHES, observed)\n"
        )
        return ((original, (
            "    def test_stable_headers_are_byte_exact(self) -> None:\n"
            "        from tests.host.evidence_authority_scope import identity_required\n\n"
            "        expected = {\n"
            "            path: value for path, value in STABLE_HEADER_HASHES.items()\n"
            "            if identity_required(REPOSITORY_ROOT,\n"
            '                "tests/host/test_kernel_vm_handoff_source_layout.py:data-entry:STABLE_HEADER_HASHES/" + path)\n'
            "        }\n"
            "        observed = {\n"
            "            path: hashlib.sha256(\n"
            "                kernel_layout_source.read_bytes_path(REPOSITORY_ROOT / path)\n"
            "            ).hexdigest()\n"
            "            for path in expected\n"
            "        }\n"
            "        self.assertEqual(expected, observed)\n"
        )),)
    if path == "tests/host/test_scheduler_source_layout.py":
        return (
            (
                "    def test_all_original_owners_and_pending_bodies_are_frozen(self) -> None:\n",
                "    def test_all_original_owners_and_pending_bodies_are_frozen(self) -> None:\n"
                "        from tests.host.evidence_authority_scope import identity_required\n\n",
            ),
            (
                "                        if is_pending or not is_phase_root:\n",
                "                        if (is_pending or not is_phase_root) and identity_required(\n"
                '                            REPOSITORY_ROOT, "tests/host/test_scheduler_source_layout.py:function-body:"\n'
                '                            + "FROZEN_FUNCTIONS/" + original + "/" + name,\n'
                "                        ):\n",
            ),
        )
    if path in SOURCE_ROLES["fixed_adapters"] and not path.startswith("kernel/tests/"):
        return ()
    raise ValueError("MEM04 has no token adapter in this owner")


def extend_legacy_adapter(path, text):
    if hashlib.sha256(text.encode()).hexdigest() != SOURCE_PROJECTION["adapter_sha256"].get(path):
        raise ValueError("MEM04 adapter lacks its complete fixed predecessor")
    for before, after in _legacy_changes(path):
        if text.count(before) != 1 or after in text:
            raise ValueError("MEM04 adapter lacks its exact original clause: " + path)
        text = text.replace(before, after, 1)
    return text


def project_legacy_adapter(root, path, text):
    if path not in SOURCE_ROLES["fixed_adapters"] or path.startswith("kernel/tests/"):
        return text
    original = fixed_source(str(root), path).decode()
    if text == original:
        return text
    if text != extend_legacy_adapter(path, original):
        raise ValueError("MEM04 legacy adapter is changed, partial or alters unrelated authority")
    return original


@lru_cache(maxsize=4)
def original_audit_paths(root):
    names = subprocess.check_output(
        ("git", "ls-tree", "-r", "--name-only", RUNTIME_REFERENCE), cwd=root, timeout=30,
    ).decode().splitlines()
    paths = tuple(name for name in names if name.startswith("kernel/")
                  and not name.startswith("kernel/tests/") and name.endswith(".c"))
    if len(paths) != 183 or digest(paths) != SOURCE_PROJECTION["audit_paths_sha256"]:
        raise ValueError("MEM04 original audit namespace changed")
    return paths


def _audit_rows(path, text):
    from tests.host.work_count_lexical import c_lexical_text

    callees = (
        "micros_user_address_space_validate", "micros_user_address_space_validate_locked",
        "micros_user_address_space_validate_all_process_roots_locked",
    )
    rows = []
    for name, record in function_records(text).items():
        for callee in callees:
            count = len(re.findall(r"\b" + callee + r"\s*\(", c_lexical_text(record.body)))
            if count:
                rows.append((path, name, callee, count))
    return tuple(rows)


@lru_cache(maxsize=4)
def declared_audit_boundaries(root):
    root = str(Path(root).resolve())
    rows = tuple(sorted(
        row for path in original_audit_paths(root)
        for row in _audit_rows(path, fixed_source(root, path).decode())
    ))
    retained = tuple(row for row in rows if row[1] not in SOURCE_PROJECTION["migrated_callers"])
    migrated = tuple(row for row in rows if row[1] in SOURCE_PROJECTION["migrated_callers"])
    if any((
        len(value) != count or digest(value) != SOURCE_PROJECTION[key]
    ) for value, count, key in (
        (rows, 25, "baseline_audits_sha256"),
        (retained, 20, "retained_audits_sha256"),
        (migrated, 5, "migrated_audits_sha256"),
    )):
        raise ValueError("MEM04 diagnostic ledger differs from its fixed-source declaration")
    return {"original": rows, "retained": retained, "migrated": migrated}


def measure_audit_boundaries(root, stage):
    root = Path(root).resolve()
    if stage not in (Stage.RED, Stage.GREEN):
        raise ValueError("MEM04 audit scope has an unknown stage")
    paths = original_audit_paths(str(root))
    current = tuple(
        name for name in subprocess.check_output(("git", "ls-files"), cwd=root, timeout=30).decode().splitlines()
        if name.startswith("kernel/") and not name.startswith("kernel/tests/") and name.endswith(".c")
    )
    if current != paths:
        raise ValueError("MEM04 complete current audit namespace changed")
    expected = declared_audit_boundaries(root)
    rows = tuple(sorted(
        row for path in paths for row in _audit_rows(
            path, fixed_source(str(root), path).decode() if stage is Stage.RED else regular_source(root, path).read_text(),
        )
    ))
    retained = tuple(row for row in rows if row not in expected["migrated"])
    migrated = tuple(row for row in rows if row in expected["migrated"])
    if retained != expected["retained"] or any(row not in expected["original"] for row in rows):
        raise ValueError("MEM04 removed, moved or added an unowned complete audit")
    return {"retained": retained, "migrated": migrated}


@lru_cache(maxsize=4)
def validate_primitive_declarations(root):
    root = str(Path(root).resolve())
    catalog = {
        path: observation_sites(root, path, fixed_source(root, path).decode(), Stage.RED)
        for path in (*NATIVE_SOURCE_MEMBERS, *OBSERVED_HEADERS)
    }
    if (
        digest(catalog) != SOURCE_PROJECTION["baseline_primitive_sha256"]
        or sum(len(sites) for rows in catalog.values() for sites in rows.values())
        != SOURCE_PROJECTION["baseline_primitive_sites"]
    ):
        raise ValueError("MEM04 fixed primitive catalogue changed after its independent declaration")
    return True


def _registration_changes(root, path):
    if path == "tests/host/evidence_authority_inventory.py":
        from tests.host.evidence_authority_types import fact_set_digest

        fingerprint = fact_set_digest((
            *declaration_facts(root, AUTHORITY_PATH), *declaration_facts(root, SOURCE_PATH),
        ))
        owners = (
            '    "selected-ownership-native-admission": (\n'
            '        "tests/host/selected_ownership_native_source_bindings.py",\n'
            "    ),\n"
        )
        prior_digest = (
            '    "selected-ownership-native-admission":\n'
            '        "4678706adb8ef75162ed57b16821af9262b06ed642a721f482778dbba381d3ff",\n'
        )
        return (
            (
                "    panic_trace_source, selected_ownership_source, terminal_failure_source,\n",
                "    panic_trace_source, selected_address_space_source, selected_ownership_source, terminal_failure_source,\n",
            ),
            (
                owners,
                owners + '    "selected-address-space-baseline": (\n'
                '        "tests/host/selected_address_space_authority.py",\n'
                '        "tests/host/selected_address_space_source.py",\n'
                "    ),\n",
            ),
            (
                '    "tests/host/test_selected_ownership.py",\n',
                '    "tests/host/test_selected_ownership.py",\n    "tests/host/test_selected_address_space.py",\n',
            ),
            (
                prior_digest,
                prior_digest + '    "selected-address-space-baseline":\n        "' + fingerprint + '",\n',
            ),
            (
                "    if family == selected_ownership_source.FAMILY:\n"
                "        return selected_ownership_source.declaration_facts(root, relative)\n",
                "    if family == selected_ownership_source.FAMILY:\n"
                "        return selected_ownership_source.declaration_facts(root, relative)\n"
                "    if family == selected_address_space_source.FAMILY:\n"
                "        return selected_address_space_source.declaration_facts(root, relative)\n",
            ),
        )
    if path == "tests/host/evidence_authority_scope.py":
        return (
            (
                "        runtime_memory_authority,\n        selected_ownership_authority,\n    )\n",
                "        runtime_memory_authority,\n        selected_ownership_authority,\n"
                "        selected_address_space_authority,\n    )\n",
            ),
            (
                "        selected_ownership_authority,\n    )\n\n\ndef _keys(provider):\n",
                "        selected_ownership_authority, selected_address_space_authority,\n    )\n\n\ndef _keys(provider):\n",
            ),
            (
                '    if provider.__name__ == "tests.host.selected_ownership_authority":\n',
                '    if provider.__name__ in (\n'
                '        "tests.host.selected_ownership_authority", "tests.host.selected_address_space_authority",\n'
                "    ):\n",
            ),
        )
    if path == "tests/host/test_evidence_authority.py":
        return ((
            "        self.assertEqual(19, len(inventory.families))\n"
            "        self.assertEqual(4774, len(inventory.facts))\n",
            "        self.assertEqual(20, len(inventory.families))\n"
            "        self.assertEqual(4782, len(inventory.facts))\n",
        ),)
    if path == "tests/host/test_python_host_runtime.py":
        return (
            (
                '            "tools/check_selected_ownership.py",\n',
                '            "tools/check_selected_ownership.py",\n            "tools/check_selected_address_space.py",\n',
            ),
            (
                '            "tools/generate_selected_ownership_fixture.py",\n',
                '            "tools/generate_selected_ownership_fixture.py",\n            "tools/generate_selected_address_space_fixture.py",\n',
            ),
            (
                '            "tests/host/selected_ownership_authority.py",\n',
                '            "tests/host/selected_ownership_authority.py",\n            "tests/host/selected_address_space_authority.py",\n',
            ),
        )
    if path == "tools/check_selected_ownership.py":
        return ((
            "        check_native_products(ROOT, arguments.executable)\n",
            "        from tests.host.evidence_authority_scope import checked_source_view\n\n"
            "        with checked_source_view(ROOT):\n"
            "            check_native_products(ROOT, arguments.executable)\n",
        ),)
    if path == "tests/host/test_selected_ownership.py":
        return (
            (
                "    def test_native_generators_serialize_their_shared_index_metadata_only(self):\n"
                '        recipe = (ROOT / "CMakeLists.txt").read_text()\n',
                "    def test_native_generators_serialize_their_shared_index_metadata_only(self):\n"
                "        from tests.host.selected_address_space_source import project_cmake\n\n"
                '        recipe = project_cmake((ROOT / "CMakeLists.txt").read_text())\n',
            ),
            (
                "    def test_scaffold_restores_exact_legacy_adapters_and_the_complete_previous_recipe(self):\n",
                "    def test_scaffold_restores_exact_legacy_adapters_and_the_complete_previous_recipe(self):\n"
                "        from tests.host.selected_address_space_source import project_legacy_adapter as project_next\n\n",
            ),
            (
                '        for path in source.SOURCE_ROLES["fixed_adapters"]:\n'
                "            original = source.baseline_adapter_source(str(ROOT), path)\n"
                '            actual = (ROOT / path).read_text()\n',
                '        for path in source.SOURCE_ROLES["fixed_adapters"]:\n'
                "            original = source.baseline_adapter_source(str(ROOT), path)\n"
                '            actual = project_next(ROOT, path, (ROOT / path).read_text())\n',
            ),
        )
    if path == "tests/host/test_evidence_authority_scope.py":
        start = "    def test_selected_ownership_view_binds_its_distinct_native_symbols_tool(self):\n"
        original = fixed_source(str(root), path).decode()
        begin = original.index(start)
        end = original.index("\n    def ", begin + len(start))
        before = original[begin:end]
        body = before[len(start):]
        name = '        self.provider.__name__ = "tests.host.selected_ownership_authority"\n'
        if body.count(name) != 1:
            raise ValueError("MEM04 scope regression lost its exact native-tool fixture")
        body = body.replace(name, "        self.provider.__name__ = provider_name\n", 1)
        after = (
            start
            + "        for provider_name in (\n"
            '            "tests.host.selected_ownership_authority",\n'
            '            "tests.host.selected_address_space_authority",\n'
            "        ):\n"
            "            self.provider.read_verified_journal.reset_mock()\n"
            + "".join("    " + line if line.strip() else line for line in body.splitlines(keepends=True))
        )
        return ((before, after),)
    raise ValueError("MEM04 has no closed registration extension in this owner")


def extend_registration(root, path, text):
    if path not in SOURCE_PROJECTION["registration_paths"] or text != fixed_source(str(root), path).decode():
        raise ValueError("MEM04 registration lacks its complete fixed predecessor")
    for before, after in _registration_changes(root, path):
        if text.count(before) != 1 or after in text:
            raise ValueError("MEM04 registration lost its exact original boundary: " + path)
        text = text.replace(before, after, 1)
    return text


def validate_scaffold(root):
    from tests.host.selected_address_space_evidence import expected_queries

    root = Path(root).resolve()
    read_declarations(root)
    actual = regular_source(root, "CMakeLists.txt").read_text()
    if actual != extend_cmake(fixed_source(str(root), "CMakeLists.txt").decode()):
        raise ValueError("MEM04 four-product/one-CTest recipe is absent or altered")
    for path in SOURCE_ROLES["fixed_adapters"]:
        actual = read_regular_source(root, path)
        if path.startswith("kernel/tests/"):
            if hashlib.sha256(actual).hexdigest() != SOURCE_PROJECTION["target_adapter_sha256"][path]:
                raise ValueError("MEM04 frozen target observation/expectation adaptation changed")
        elif actual.decode() != extend_legacy_adapter(path, fixed_source(str(root), path).decode()):
            raise ValueError("MEM04 frozen identity adapter is absent or changed: " + path)
    for path in SOURCE_ROLES["unchanged_candidates"]:
        if read_regular_source(root, path) != fixed_source(str(root), path):
            raise ValueError("MEM04 changed an independently declared predecessor literal module")
    for path in SOURCE_PROJECTION["registration_paths"]:
        expected = extend_registration(root, path, fixed_source(str(root), path).decode())
        if read_regular_source(root, path).decode() != expected:
            raise ValueError("MEM04 closed provider/owner registration is absent or changed: " + path)
    for path in SOURCE_ROLES["stack_admission_inputs"]:
        if hashlib.sha256(read_regular_source(root, path)).hexdigest() != SOURCE_PROJECTION["stack_admission_sha256"][path]:
            raise ValueError("MEM04 frozen stack admission scaffold changed: " + path)
    for path in SOURCE_ROLES["immutable_additions"]:
        regular_source(root, path)
    for path in SOURCE_ROLES["dependent_production_paths"]:
        actual = read_regular_source(root, path)
        stage = Stage.RED if actual == fixed_source(str(root), path) else Stage.GREEN
        validate_retained_source(root, path, actual.decode(), stage)
    if digest({stage.value: expected_queries(stage) for stage in (Stage.RED, Stage.GREEN)}) != STATE_WORK_CONTRACT["query_inputs_sha256"]:
        raise ValueError("MEM04 native case inputs changed after independent declaration")
    validate_primitive_declarations(root)


def read_regular_source(root, path):
    from tools.stack_artifact_files import read_regular

    return read_regular(regular_source(root, path))[0]
