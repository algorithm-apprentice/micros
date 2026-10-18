"""Bounded source-contract checks for Accepted ADR-0068."""

import ast
import hashlib
import json
from pathlib import Path
import re
import unittest

from tests.host.ast_evidence import ast_dump
from tests.host.c_source_records import function_records, matching_lexical
from tests.host.test_grant_data_plane_source_layout import grant_data_plane_cmake_projection
from tests.host.test_frame_ownership_source_layout import c_function_definitions, c_lexical_text, tokens
from tests.host.test_pm_service_source_layout import cmake_commands
from tests.host.test_vm_service_source_layout import file_scope_objects
from tools.check_docs import topological_order
from tools import validation_context, validation_plan
from tests.host import kernel_layout_source


ROOT = Path(__file__).resolve().parents[2]
REVIEWED_BASE = "4eb1c4d68de3830b888a0d239a2874bd79792244"
BASE_CMAKE_DIGEST = "50cac89bbc8dfc0246b97d36be3585ff53f7e86c3dbc772539a6f8352e05db7d"
BASE_CORE_DIGEST = "453c3cfbfb02d1c014226a315106a157676a7b195be06f561bb3abbda042db4a"
STRUCTURAL_TEST_PATH = "tests/host/test_kernel_object_source_layout.py"
SHARED_INSPECTION_PATH = "tests/host/c_source_records.py"
HOST_MACROS_DIGEST = "cef5a866a045d5de47e43507e2dd21dc9c609dd30f096224ae36301633b972a1"
MODEL_TYPES_DIGEST = "d8e00a14a902b448209ed4f27d866534d4ef32a46baeb2d5407c83fff825a444"
HOST_CASE_TABLE_DIGEST = "b3566dbd412d23b759fc2fd7c3efaf6f083bb0dfd9617fce9fa35c84a164bf90"
CORRUPTION_MACRO_DIGEST = "84ac83b95d4899eafe2ef63f372e85a5e6257eb742ce778d3472aeb300bda545"
REGISTRY_FIELDS = (
    "initialization_magic", "max_threads_per_process", "max_harts", "live_process_count",
    "live_thread_count", "registered_hart_count", "processes", "threads", "harts",
)
MODEL_PROJECTION_PHASES = (
    ("kernel_object_test_model_processes_match", ("fixture", "model"), "MICROS_PROCESS_CAPACITY"),
    ("kernel_object_test_model_threads_match", ("fixture", "model"), "MICROS_THREAD_CAPACITY"),
    ("kernel_object_test_model_harts_match", ("fixture",), "MICROS_HART_CAPACITY"),
)
MODEL_OPERATION_TASKS = (
    "kernel_object_test_model_process_create",
    "kernel_object_test_model_process_release",
    "kernel_object_test_model_thread_create",
    "kernel_object_test_model_thread_release",
    "kernel_object_test_model_address_space_attach",
    "kernel_object_test_model_address_space_detach",
    "kernel_object_test_model_context_attach",
    "kernel_object_test_model_context_detach",
)
MODEL_ITERATION_TAG = "kernel_object_test_model_iteration"
MODEL_ITERATION_FIELDS = (
    ("uint32_t value;", "value"),
    ("size_t slot;", None),
    ("enum micros_kernel_object_error expected;", None),
    ("enum micros_kernel_object_error actual;", None),
)
MODEL_OPERATION_PARAMETERS = (
    "struct micros_kernel_objects *fixture, struct lifecycle_model *model, "
    "size_t step, struct kernel_object_test_model_iteration *iteration"
)
TARGET_WORK_TAG = "kernel_object_model_test_work"
TARGET_WORK_FIELDS = (
    ("struct micros_kernel_objects *objects;", None),
    ("struct micros_hart *hart;", None),
    ("struct micros_hart_handle hart_handle;", None),
    ("struct micros_process_handle first_process;", None),
    ("struct micros_process_handle replacement_process;", None),
    ("struct micros_thread_handle thread;", None),
    ("struct micros_thread_handle unchanged_thread;", "aggregate"),
    ("struct micros_thread_handle current;", None),
    ("struct micros_scheduler_return_plan plan;", None),
    ("const struct micros_process *resolved_process;", "value"),
    ("enum micros_kernel_object_error error;", None),
)
TARGET_TASKS = (
    "kernel_object_model_test_create_process_and_thread",
    "kernel_object_model_test_attach_and_select_thread",
    "kernel_object_model_test_retire_thread_and_process",
    "kernel_object_model_test_verify_process_reuse",
)
TARGET_PARAMETERS = {
    name: "struct " + TARGET_WORK_TAG + " *work" + (
        ", uintptr_t stack_bottom, uintptr_t stack_top, const struct micros_user_context *context"
        if name == TARGET_TASKS[1] else ""
    ) for name in TARGET_TASKS
}
HART_STACK_PHASES = (
    (
        "kernel_object_hart_stacks_avoid_harts",
        ("objects", "hart", "primary_stack_bottom", "primary_stack_top", "emergency_stack_bottom", "emergency_stack_top"),
        "MICROS_HART_CAPACITY",
    ),
    (
        "kernel_object_hart_stacks_avoid_threads",
        ("objects", "primary_stack_bottom", "primary_stack_top", "emergency_stack_bottom", "emergency_stack_top"),
        "MICROS_THREAD_CAPACITY",
    ),
)
CONTEXT_STACK_PHASES = (
    (
        "kernel_object_context_stack_avoids_threads",
        ("objects", "thread", "kernel_stack_bottom", "kernel_stack_top"),
        "MICROS_THREAD_CAPACITY",
    ),
    (
        "kernel_object_context_stack_avoids_harts",
        ("objects", "kernel_stack_bottom", "kernel_stack_top"),
        "MICROS_HART_CAPACITY",
    ),
)
STACK_CONFLICT_TASKS = {
    "micros_hart_install_trap_stacks": (7, "hart", HART_STACK_PHASES),
    "micros_thread_attach_execution_context": (6, "thread", CONTEXT_STACK_PHASES),
}
BASE_VALIDATION_SIGNATURES = {
    "kernel_object_validation_process_slots_match": (
        "const struct micros_kernel_objects *objects, size_t *observed_process_count"
    ),
    "kernel_object_validation_thread_context_matches": (
        "const struct micros_kernel_objects *objects, const struct micros_thread *thread, size_t index"
    ),
    "kernel_object_validation_thread_slots_match": (
        "const struct micros_kernel_objects *objects, "
        "size_t process_thread_counts[MICROS_PROCESS_CAPACITY], size_t *observed_thread_count"
    ),
    "kernel_object_validation_process_counts_match": (
        "const struct micros_kernel_objects *objects, "
        "const size_t process_thread_counts[MICROS_PROCESS_CAPACITY]"
    ),
    "kernel_object_validation_free_hart_is_clear": "const struct micros_hart *hart",
    "kernel_object_validation_hart_stacks_avoid_peers": (
        "const struct micros_kernel_objects *objects, const struct micros_hart *hart, size_t index"
    ),
    "kernel_object_validation_hart_stack_state_matches": (
        "const struct micros_kernel_objects *objects, const struct micros_hart *hart, size_t index"
    ),
    "kernel_object_validation_current_binding_matches": (
        "const struct micros_kernel_objects *objects, const struct micros_hart *hart, "
        "bool current_threads[MICROS_THREAD_CAPACITY]"
    ),
    "kernel_object_validation_hart_slots_match": (
        "const struct micros_kernel_objects *objects, "
        "bool current_threads[MICROS_THREAD_CAPACITY], size_t *observed_hart_count"
    ),
}
BASE_VALIDATION_PASSES = (
    (
        "kernel_object_validation_process_slots_match", ("objects", "&observed_process_count"),
        "MICROS_PROCESS_CAPACITY", "observed_process_count",
    ),
    (
        "kernel_object_validation_thread_slots_match",
        ("objects", "process_thread_counts", "&observed_thread_count"),
        "MICROS_THREAD_CAPACITY", "observed_thread_count",
    ),
    (
        "kernel_object_validation_process_counts_match", ("objects", "process_thread_counts"),
        "MICROS_PROCESS_CAPACITY", None,
    ),
    (
        "kernel_object_validation_hart_slots_match",
        ("objects", "current_threads", "&observed_hart_count"),
        "MICROS_HART_CAPACITY", "observed_hart_count",
    ),
)
NATIVE_CASE_TASKS = {
    "test_uninitialized_registry_rejects_every_operation": {
        "owner": 2,
        "work": "kernel_object_test_uninitialized_work",
        "fields": (
            ("struct micros_process_handle process;", "aggregate"),
            ("struct micros_process_handle process_output;", "aggregate"),
            ("struct micros_thread_handle thread;", "aggregate"),
            ("struct micros_thread_handle thread_output;", "aggregate"),
            ("struct micros_hart_handle hart;", "aggregate"),
            ("struct micros_hart_handle hart_output;", "aggregate"),
            ("const struct micros_process *resolved_process;", "value"),
            ("const struct micros_thread *resolved_thread;", "value"),
            ("const struct micros_hart *resolved_hart;", "value"),
            ("struct micros_user_context context;", "value"),
            ("struct micros_user_context context_output;", "value"),
            ("struct micros_user_context context_before;", "value"),
            ("uintptr_t stack_bottom;", "value"),
            ("uintptr_t stack_top;", "value"),
        ),
        "phases": (
            "kernel_object_test_uninitialized_process_operations",
            "kernel_object_test_uninitialized_thread_context_operations",
            "kernel_object_test_uninitialized_hart_operations",
        ),
    },
    "test_fixed_capacities_report_exhaustion_atomically": {
        "owner": 2,
        "work": "kernel_object_test_capacity_work",
        "fields": (
            ("struct micros_process_handle processes[MICROS_PROCESS_CAPACITY];", None),
            ("struct micros_thread_handle threads[MICROS_THREAD_CAPACITY];", None),
            ("struct micros_hart_handle harts[MICROS_HART_CAPACITY];", None),
            ("struct micros_process_handle process_output;", "aggregate"),
            ("struct micros_thread_handle thread_output;", "aggregate"),
            ("struct micros_hart_handle hart_output;", "aggregate"),
            ("size_t index;", None),
        ),
        "phases": (
            "kernel_object_test_process_capacity_refusal",
            "kernel_object_test_thread_capacity_refusal",
            "kernel_object_test_hart_capacity_refusal",
        ),
    },
    "test_generation_boundaries_and_quarantine": {
        "owner": 6,
        "work": "kernel_object_test_generation_work",
        "fields": (
            ("uint32_t next;", "value"),
            ("struct micros_process_handle process;", "aggregate"),
            ("struct micros_process_handle stale_process;", "aggregate"),
            ("struct micros_process_handle replacement;", "aggregate"),
            ("struct micros_thread_handle thread;", "aggregate"),
            ("struct micros_thread_handle stale_thread;", "aggregate"),
            ("struct micros_thread_handle final_thread;", "aggregate"),
            ("const struct micros_process *resolved_process;", "value"),
            ("const struct micros_thread *resolved_thread;", "value"),
        ),
        "phases": (
            "kernel_object_test_generation_value_boundaries",
            "kernel_object_test_thread_generation_quarantine",
            "kernel_object_test_process_generation_quarantine",
        ),
        "without_fixture": ("kernel_object_test_generation_value_boundaries",),
    },
    "test_process_address_space_lifecycle_is_exact": {
        "owner": 3,
        "work": "kernel_object_test_address_space_work",
        "fields": (
            ("struct micros_process_handle process;", "aggregate"),
            ("struct micros_process_handle stale;", None),
            ("struct micros_thread_handle thread;", "aggregate"),
            ("struct micros_kernel_objects snapshot;", None),
        ),
        "constants": ("const uintptr_t root = UINT64_C(0x0000000081000000);",),
        "phases": (
            "kernel_object_test_prepare_address_space_rejections",
            "kernel_object_test_attach_address_space_and_reject_mutations",
            "kernel_object_test_detach_address_space_after_thread_release",
            "kernel_object_test_reuse_process_rejects_stale_root_handles",
        ),
        "parameters": {
            "kernel_object_test_prepare_address_space_rejections": (("uintptr_t", "root"),),
            "kernel_object_test_attach_address_space_and_reject_mutations": (("uintptr_t", "root"),),
            "kernel_object_test_detach_address_space_after_thread_release": (("uintptr_t", "root"),),
            "kernel_object_test_reuse_process_rejects_stale_root_handles": (("uintptr_t", "root"),),
        },
    },
    "test_thread_execution_context_lifecycle_is_exact": {
        "owner": 4,
        "work": "kernel_object_test_context_work",
        "fields": (
            ("struct micros_process_handle process;", "aggregate"),
            ("struct micros_thread_handle first;", "aggregate"),
            ("struct micros_thread_handle second;", "aggregate"),
            ("struct micros_thread_handle stale;", None),
            ("struct micros_hart_handle hart;", "aggregate"),
            ("struct micros_hart_handle second_hart;", "aggregate"),
            ("struct micros_user_context initial;", "value"),
            ("struct micros_user_context captured;", "value"),
            ("struct micros_user_context observed;", "value"),
            ("struct micros_user_context unchanged;", "value"),
            ("uintptr_t observed_bottom;", "value"),
            ("uintptr_t observed_top;", "value"),
            ("struct micros_kernel_objects snapshot;", None),
        ),
        "constants": (
            "const uintptr_t idle_bottom = UINT64_C(0x0000000000100000);",
            "const uintptr_t idle_top = idle_bottom + MICROS_PRIMARY_TRAP_STACK_MIN_SIZE;",
            "const uintptr_t emergency_bottom = UINT64_C(0x0000000000200000);",
            "const uintptr_t emergency_top = emergency_bottom + MICROS_EMERGENCY_TRAP_STACK_MIN_SIZE;",
            "const uintptr_t first_stack_bottom = UINT64_C(0x0000000000300000);",
            "const uintptr_t first_stack_top = first_stack_bottom + MICROS_THREAD_KERNEL_STACK_SIZE;",
            "const uintptr_t second_stack_bottom = UINT64_C(0x0000000000400000);",
            "const uintptr_t second_stack_top = second_stack_bottom + (2 * MICROS_THREAD_KERNEL_STACK_SIZE);",
        ),
        "phases": (
            "kernel_object_test_prepare_context_peers",
            "kernel_object_test_first_context_attach_and_inspect",
            "kernel_object_test_context_peer_stack_conflicts",
            "kernel_object_test_context_current_capture",
            "kernel_object_test_context_idle_detach_and_stale",
            "kernel_object_test_context_lifecycle_cleanup",
        ),
        "parameters": {
            "kernel_object_test_prepare_context_peers": (
                ("uintptr_t", "idle_bottom"), ("uintptr_t", "idle_top"),
                ("uintptr_t", "emergency_bottom"), ("uintptr_t", "emergency_top"),
            ),
            "kernel_object_test_first_context_attach_and_inspect": (
                ("uintptr_t", "first_stack_bottom"), ("uintptr_t", "first_stack_top"),
            ),
            "kernel_object_test_context_peer_stack_conflicts": (
                ("uintptr_t", "first_stack_bottom"), ("uintptr_t", "first_stack_top"),
                ("uintptr_t", "second_stack_bottom"), ("uintptr_t", "second_stack_top"),
                ("uintptr_t", "idle_bottom"), ("uintptr_t", "idle_top"), ("uintptr_t", "emergency_bottom"),
            ),
            "kernel_object_test_context_current_capture": (
                ("uintptr_t", "first_stack_bottom"), ("uintptr_t", "first_stack_top"),
                ("uintptr_t", "idle_bottom"), ("uintptr_t", "idle_top"),
            ),
            "kernel_object_test_context_idle_detach_and_stale": (
                ("uintptr_t", "idle_bottom"), ("uintptr_t", "idle_top"),
                ("uintptr_t", "first_stack_bottom"), ("uintptr_t", "first_stack_top"),
            ),
        },
    },
    "test_validator_rejects_corrupt_relationships": {
        "owner": 7,
        "work": "kernel_object_test_corruption_work",
        "fields": (
            ("struct micros_process_handle owner;", "aggregate"),
            ("struct micros_thread_handle first;", "aggregate"),
            ("struct micros_thread_handle second;", "aggregate"),
            ("struct micros_hart_handle hart_zero;", "aggregate"),
            ("struct micros_hart_handle hart_one;", "aggregate"),
            ("struct micros_kernel_objects snapshot;", None),
        ),
        "phases": (
            "kernel_object_test_prepare_corruption_snapshot",
            "kernel_object_test_corrupt_relationships_and_counts",
            "kernel_object_test_corrupt_generations_and_context",
            "kernel_object_test_corrupt_released_payload_and_hart",
        ),
    },
}
CORE_SOURCES = (
    "kernel/kernel_objects.c",
    "kernel/kernel_object_state.c",
    "kernel/kernel_object_identity.c",
    "kernel/kernel_object_stack.c",
    "kernel/kernel_object_process.c",
    "kernel/kernel_object_thread.c",
    "kernel/kernel_object_context.c",
    "kernel/kernel_object_hart.c",
    "kernel/kernel_object_validation.c",
)
SOURCE_SETS = {
    "MICROS_KERNEL_OBJECT_CORE_SOURCES": CORE_SOURCES,
    "MICROS_KERNEL_OBJECT_HOST_TEST_SOURCES": (
        "tests/host/kernel_objects_test.c",
        "tests/host/kernel_objects_test_fixture.c",
        "tests/host/kernel_objects_initialization_test.c",
        "tests/host/kernel_objects_process_test.c",
        "tests/host/kernel_objects_thread_test.c",
        "tests/host/kernel_objects_hart_test.c",
        "tests/host/kernel_objects_identity_test.c",
        "tests/host/kernel_objects_validation_test.c",
        "tests/host/kernel_objects_model_test.c",
        "tests/host/kernel_objects_model_reference.c",
        "tests/host/kernel_objects_model_projection.c",
        "tests/host/kernel_objects_model_operations.c",
    ),
    "MICROS_KERNEL_OBJECT_QEMU_TEST_SOURCES": (
        "kernel/object_model_test.c",
        "kernel/object_model_test_scenarios.c",
    ),
}
PRIVATE_HEADERS = (
    "kernel/kernel_object_private.h",
    "tests/host/kernel_objects_test_internal.h",
    "kernel/object_model_test_internal.h",
)
ORIGINAL_FUNCTIONS = {
    0: {
        0: "micros_kernel_objects_initialize micros_kernel_objects_validate".split(),
        1: (
            "bytes_are_zero clear_bytes storage_is_zero thread_scheduler_metadata_is_zero "
            "clear_thread_scheduler_metadata micros_thread_ipc_state_is_clear clear_thread_ipc_state "
            "copy_user_context clear_user_context user_context_is_zero trap_anchor_is_zero timer_state_is_initial"
        ).split(),
        2: (
            "process_handle_is_valid thread_handle_is_valid hart_handle_is_valid thread_handles_equal "
            "thread_handle_is_null hart_handle_is_null require_initialized resolve_process_mutable "
            "resolve_thread_mutable resolve_hart_mutable micros_process_next_generation micros_thread_next_generation"
        ).split(),
        3: "stack_range_is_valid ranges_overlap trap_anchor_stacks_are_valid".split(),
        4: (
            "micros_process_resolve micros_process_create micros_process_create_at micros_process_release "
            "micros_process_attach_address_space micros_process_detach_address_space"
        ).split(),
        5: "micros_thread_resolve micros_thread_create micros_thread_release".split(),
        6: (
            "micros_thread_attach_execution_context micros_thread_capture_execution_context "
            "micros_thread_inspect_execution_context micros_thread_detach_execution_context"
        ).split(),
        7: (
            "micros_hart_resolve micros_hart_register micros_hart_resolve_context "
            "micros_hart_install_trap_stacks micros_hart_current_thread"
        ).split(),
        8: ["micros_kernel_objects_validate_base"],
    },
    1: {
        0: ["main"],
        1: "reset_objects context_pattern contexts_equal".split(),
        2: (
            "test_initialization_is_checked_and_one_shot test_uninitialized_registry_rejects_every_operation "
            "test_fixed_capacities_report_exhaustion_atomically"
        ).split(),
        3: (
            "test_process_slots_reuse_with_new_generations test_process_exact_slot_creation_is_isolated "
            "test_process_address_space_lifecycle_is_exact"
        ).split(),
        4: (
            "test_thread_execution_context_lifecycle_is_exact test_thread_slots_reuse_with_new_generations "
            "test_thread_ownership_and_production_limit test_model_policy_supports_multiple_threads"
        ).split(),
        5: ["test_hart_policy_and_stack_validation_are_atomic"],
        6: "test_generation_boundaries_and_quarantine test_invalid_arguments_and_stale_handles_preserve_outputs".split(),
        7: ["test_validator_rejects_corrupt_relationships"],
        8: ["test_seeded_lifecycle_model"],
        9: (
            "process_handles_equal model_lowest_free_process model_lowest_free_thread "
            "model_process_handle model_thread_handle model_reports_expected_error"
        ).split(),
        10: ["lifecycle_model_matches_registry"],
        11: ["model_random"],
    },
    2: {0: ["micros_kernel_object_runtime_run_self_test"], 1: []},
}
CORE_PRIVATE_EXPORTS = (
    "storage_is_zero process_handle_is_valid thread_handle_is_valid hart_handle_is_valid thread_handles_equal "
    "thread_handle_is_null hart_handle_is_null thread_scheduler_metadata_is_zero clear_thread_scheduler_metadata "
    "clear_thread_ipc_state stack_range_is_valid ranges_overlap copy_user_context clear_user_context "
    "user_context_is_zero trap_anchor_is_zero trap_anchor_stacks_are_valid require_initialized "
    "resolve_process_mutable resolve_thread_mutable resolve_hart_mutable timer_state_is_initial"
).split()
LONG_FUNCTIONS = (
    (0, "micros_hart_install_trap_stacks"),
    (0, "micros_kernel_objects_validate_base"),
    (1, "test_uninitialized_registry_rejects_every_operation"),
    (1, "test_thread_execution_context_lifecycle_is_exact"),
    (1, "test_generation_boundaries_and_quarantine"),
    (1, "lifecycle_model_matches_registry"),
    (1, "test_seeded_lifecycle_model"),
    (2, "micros_kernel_object_runtime_run_self_test"),
)
SHORTER_TASKS = (
    (0, "micros_thread_attach_execution_context"),
    (1, "test_fixed_capacities_report_exhaustion_atomically"),
    (1, "test_process_address_space_lifecycle_is_exact"),
    (1, "test_validator_rejects_corrupt_relationships"),
)
REQUIRED_TASKS = (*LONG_FUNCTIONS, *SHORTER_TASKS)
INITIAL_SYMBOL_OWNER_MIGRATION = tuple(
    (family, name) for family, owners in ORIGINAL_FUNCTIONS.items()
    for owner, names in owners.items() if owner != 0 for name in names
)
SYMBOL_OWNER_MIGRATION = ()
FUNCTION_TASK_MIGRATION = ()
CORE_DAG = {
    0: {1}, 1: {2}, 2: set(), 3: set(), 4: {2},
    5: {1, 2}, 6: {1, 2, 3, 5}, 7: {2, 3, 5}, 8: {1, 2, 3},
}
BASELINE_SIGNATURES = (
    "9010731d332ba9e936802a3e43feab1bae99af4bb80bd61e2e282d6ee2025db1",
    "b39fe6c7b6086a51bfcf3a8816438334d26de32fd12b07efea880e720ee40968",
    "7286deed76b4e7245c39433585d2d17a33e6919eab722c3f34166d05adeb2bcc",
)
BASELINE_BODIES = {
    0: {
        "bytes_are_zero": "6a6327e7774d4d0381673fcee5103704058d8b7004c210a70a93a6c0ace932f8",
        "clear_bytes": "1bfe896da396160656d79ce0e67c19f9a3ed382a4d894e12c0abdce0c797e1a0",
        "storage_is_zero": "d7a54408fe91b6861d2b937db0855867770e8a753345d65a042e10d89fff3f16",
        "process_handle_is_valid": "2bc62468bf177fa1f353c4ce344cbe58bcd3f7f89c3174202d21a957d9defa5f",
        "thread_handle_is_valid": "6d1ec96b91e22e394ce1c53d8078a267dfcfcd34e3293e6d573c2d8eaf9b827e",
        "hart_handle_is_valid": "74bbd2919a91715fdd6ded843c506b9ab52a149ce03ffee509cc99dcf5281cfc",
        "thread_handles_equal": "1eb9ca3d14ed288168beacc0916c15eb03aa6ecf2e283e022b4d3ccb7e92f678",
        "thread_handle_is_null": "2f4d5857a78d658059c3e3faa75d3ff69a56c09a4d573a89d695b633d7299d5e",
        "hart_handle_is_null": "2f4d5857a78d658059c3e3faa75d3ff69a56c09a4d573a89d695b633d7299d5e",
        "thread_scheduler_metadata_is_zero": "abc4ca92735c2f8aea744b0928256114cfbcdd5eb6214ee637fa2856a1f678ab",
        "clear_thread_scheduler_metadata": "2a1bc8a1b9bd6a27c8d6b3a1797de9deb2a1162f4338a8a8b1983113721d1de3",
        "micros_thread_ipc_state_is_clear": "7bcf82008f555152f6857df8771cbaf562ab7a511dd4cfc5e4f0a515c83f48c4",
        "clear_thread_ipc_state": "fe88b17b377077ac564b6b8bdd6f6528eef4bbf09836652c10ae54eb293dce99",
        "stack_range_is_valid": "d314e5a01cdf87c7cfc611b096912b1b61c3da78353ba85a42ce9d3a0f13bb05",
        "ranges_overlap": "67e80565d3bf4bfae293bbee016dfb6737738e44f817e33b801628afe5eb1d79",
        "copy_user_context": "2afe82178b061416299109a458ca503581360aef82fd57ab5c3c100b01cd8851",
        "clear_user_context": "e4a54f8a45d63685840a15073e83fd158071fecdf30cce5c8049ea74c62561cf",
        "user_context_is_zero": "67bb620d312715fa872be97103a06e0f54e8e5d201b8612ee69999e5e7b2ad45",
        "trap_anchor_is_zero": "be63065e19f73835854e19b9a3fa681f8a25593f1ff94d16d09f8e39ce428926",
        "trap_anchor_stacks_are_valid": "fb26862314688b37fbd165a94d42d1d923081251b2dff56fe855c2d5684d539e",
        "require_initialized": "23677722aea4e59474529dd7dbb1bfe327c75e0e0a546b7b91ca6e2be5676605",
        "resolve_process_mutable": "68825299ca5388fcf31eb60da190d33287fd8eeeb81aaf16520ec4bb341e845b",
        "resolve_thread_mutable": "3642aac7f900241b76f281669d71d51de16b77fd4ba52bdbbd66cc899ee05390",
        "resolve_hart_mutable": "f4f2f334bda00332040907ede742cd6ad1f3aa4a26732525bc194d8bf8325aba",
        "timer_state_is_initial": "be2904c1524fd362d34433da3305b35726a3d8ef997818956a6ce44f03b25e33",
        "micros_process_next_generation": "5d0cedb28266082ea79f53141b9e5e1234022de87ad81729c57fe77670531689",
        "micros_thread_next_generation": "6326dac0fd48837c1b403f4a6ca1e0e0a290d3f2ce54dabf4cb36413271b139e",
        "micros_kernel_objects_initialize": "77d09c42954a57293c4de8a79e7205833299d1813e233bd85de3789e099ba811",
        "micros_process_resolve": "8fe940a1d0f1ad3c0cc4ae345c50e0806c4fcfb90d01b0e877d5b64f30fb15bc",
        "micros_process_create": "5c1b3d6c60d0ee8f627cbccbf7e1530e7f6442ffeb00e98592b1c834c6e71ed8",
        "micros_process_create_at": "adc49cdbacd3c361f10a6c879c33fa1ec8e8befe47d7fda90f6afc98909f7edb",
        "micros_process_release": "afb1c120485b5b508e1bb6522f5b72e0bb1112743e6c5f63886147c02a022dc6",
        "micros_thread_resolve": "eb58dbc97be2deaf41313bcb8020884d7d6e53f1eb749ff97c5ffe0e7d1226c4",
        "micros_thread_attach_execution_context": "ce1ea34128a4c5d01865941bf66299836d6880979f9b160002d490a2983cc353",
        "micros_thread_capture_execution_context": "b629a2900321ff0b711322fc594eab9406760483751684ad4b0cfaaebc8be5f7",
        "micros_thread_inspect_execution_context": "89f556ca29b2292ec8015935c033c26e5a85f88ac846ac96150dd1eb0073c3ba",
        "micros_thread_detach_execution_context": "b8abc59908db98f7c21fe2a57b3d8c0148072708aacfe3b30124d330e8da1fd5",
        "micros_process_attach_address_space": "affa5ed93a38ef93cde624712e3e89903ed14119c557310d0ef7bd287207437b",
        "micros_process_detach_address_space": "ebf772fea15a08ece81e702dfcf7ccdf9021c47205f7a175011698bdd62d59a2",
        "micros_thread_create": "3cd94ad146d2854325c0867748771f4c6f5861771835122db0b30952c7c29d97",
        "micros_thread_release": "3f062646fcbdee574a23979c8fe942d12c9a347de7404ee89c1db7aa8bf019f0",
        "micros_hart_resolve": "768dd87c2e34c8a4fa3c1348e022b151d15b4834eea99c84a986acca6a6256a5",
        "micros_hart_register": "ea2e3d1a274dd04372d8236bd4d73ca2b6500026b7b940d109283d3386d53c3a",
        "micros_hart_resolve_context": "d8d14687170a56d35f12d5c254d5d318f4d3a9119890c2acb65cc7654238d46c",
        "micros_hart_install_trap_stacks": "fa317d8c197e950cb6e46867110e8936fa58c36a9fa43502535dbaba8acd7f2b",
        "micros_hart_current_thread": "ff46b0e0ff7c4f7bca4a908c7364aa914a14dffc83f0387ebe1176b9ce7327f4",
        "micros_kernel_objects_validate": "8198b41338088f95d960097d7e1c662da347e2c4c92750b5670696fd738e6079",
        "micros_kernel_objects_validate_base": "fe2cf26c2cf20f464ddb38aff6a29907b4db0fc755c4fcd343d520a555fc2eab",
    },
    1: {
        "reset_objects": "bcf98945593ede83ead0ccc8605cdb7fe8979a38e1e9a02f69b3b9e26da4c4d9",
        "process_handles_equal": "1eb9ca3d14ed288168beacc0916c15eb03aa6ecf2e283e022b4d3ccb7e92f678",
        "context_pattern": "27beff55faa7c13139dcbfb8ca672501f612ff42033bf579e2651fabbb46d06c",
        "contexts_equal": "edae7519639b5ac9d8623bb0d1491d55b1de8fb2ada7ef2729797d0410773115",
        "test_initialization_is_checked_and_one_shot": "dc6778f882120773227bf775d8723fd8a0abf63ac818ffeb09047803e0be9e1c",
        "test_uninitialized_registry_rejects_every_operation": "75227aacab44588a750f0b9590396b56219ba75fe0e08b095dff0d65aa2b8460",
        "test_fixed_capacities_report_exhaustion_atomically": "ac9f4fa6aa689838322470424703f83c709afce18544f13837ddbbe6459b3766",
        "test_process_slots_reuse_with_new_generations": "edcaac9e67c32b1a724ba785802fb50b5b7c2dac907ddb437c590ef600451ba3",
        "test_process_exact_slot_creation_is_isolated": "07d36d96ec7d5b3e8ffb84a15047adfd32fb733f84f5371e5961d75fa84b4f2c",
        "test_process_address_space_lifecycle_is_exact": "e3b323123c27ff60f9bb3345d81ade81745d2308c6d061376716dbba73af9097",
        "test_thread_execution_context_lifecycle_is_exact": "5ee589b0bc6ebfd5535e97a4949c32ed4286bdf9a50649ee15615e6483c2ef3f",
        "test_thread_slots_reuse_with_new_generations": "2a4fd406187a51fe7414d6b2a2ec4d9d9abeb699232cf7520c410458f8af4e03",
        "test_thread_ownership_and_production_limit": "a96475b300a3f87ecac73ea3d1eaa85f829542a546196e30ec00d023c11731c2",
        "test_model_policy_supports_multiple_threads": "2f1f2298077e2edb46b4e50efb4221f1c6f4a5277b561c77c9a702360a311702",
        "test_hart_policy_and_stack_validation_are_atomic": "f8e5650419eea59e653fb76cda940818d0a88ea26fae6517ae44ad4a2dbe5332",
        "test_generation_boundaries_and_quarantine": "0584f888942a8478b40593d31a512d9a4cf868bba75bea4beac21f7f49bf2ca0",
        "test_invalid_arguments_and_stale_handles_preserve_outputs": "e91d7f008e751cc9eb8c897ebabe9595685745626cb32d824185ead382f93502",
        "model_random": "7dcaf7a92f2e711f3c5895cc3c4c2254d1f1104a83fa875186db6f3da9489b92",
        "model_lowest_free_process": "4b2eeac086f57a6cf5c4140ec3583b23e13744538ba631d2d74f5b23c43d3d10",
        "model_lowest_free_thread": "a935acf6aedc965cf12b7d8def34a756887f8d4a78934c1b8ed0526617e60c77",
        "model_process_handle": "5e86f7b2395923d5ccd7b3d4fd1ec08ac7a02915d4d63fa9d11ea8d3603ffa3f",
        "model_thread_handle": "f55006c7793fa04383bab5bfe8f4666546255ab3f05d1ecf7f5b0cea757b85dc",
        "model_reports_expected_error": "1335362cc0b94845b3a90a7e74f21971043aa9ac525e32fdcb8eb79cb8ebf505",
        "lifecycle_model_matches_registry": "afcf0a20496858b95ea471d306df0fa0df43a96fdd4dc373d07d7806bd6a515b",
        "test_seeded_lifecycle_model": "96800ce8d488fe60684653f33b7cf79fae758aa2b7bd3bcf38e26e372d16767c",
        "test_validator_rejects_corrupt_relationships": "49d1f7a4b7ce6c5837d31cd7ff081f6af7205b37341c473eea0d9462d31c783e",
        "main": "685f3b5446ace3b45e224122fef19a20452a3c4bb3cf64da9644acc59dfa182b",
    },
    2: {
        "micros_kernel_object_runtime_run_self_test": "f5df9f3de5263baf8b1dcbb61e989982d03ec62e4c6f41320ded9de205d81579",
    },
}
STABLE_HASHES = {
    "include/micros/kernel_objects.h": "106fe5aaa03b06b1746c73aa24588749d4cfe02c6b1e4a11efc1f5317f7523e3",
    "include/micros/kernel_object_runtime.h": "46af15cfa0d00649ea2e481e6f529bf8aae1e63b89d23ffef602b98b0da3fd11",
    "kernel/kernel_object_runtime.c": "22925d14e6000b26a78403cc2dfbacc023bca6a4e239ec1bdcd1570b91a85a43",
    "kernel/kernel_object_runtime_internal.h": "23917cbd674d926ec7ffe8700747fc4626f3a31dc4620ed4f7d4ee8147f57c72",
    "include/micros/user_context.h": "92a1034064b570385d720849e0e42a975581fd595fd6ad41130cc0e10f82ca90",
    "include/micros/scheduler_core.h": "62976539f042a654003034157beefcefe25f90a5e92247461fb1fe3f704044f1",
    "kernel/scheduler_core_internal.h": "c9378e2e926b28400c474a35f6eb102aeed017a01fe1d367bb1f33687974bfb6",
    "kernel/scheduler_core_validate.c": "73bd02fdac06a05c0bdc33130258132e3fe90cfe8c2204f5502e054d5021f38c",
    "include/micros/endpoint.h": "57e8c5b5536ddc270015be7e2ccef07008e6d00ae514e27fb3c7ebc55c1f871c",
    "kernel/endpoint_internal.h": "fa8bc084ed5b6777a805abb4def2d8d20f747a3b193cbc9fa46a9b6c1145caac",
    "arch/riscv64/trap_context.h": "584a45235e8eea9a973ccaebb0ace4b84f22778f360625a68feecf1dc276e5d3",
    "arch/riscv64/scheduler_context.h": "3b854415cf846f8c940afbed956a975651b44a6630fcd77eed7470150beb81ae",
    "arch/riscv64/trap.S": "e541ebbc7c1ab6e68eb366ec9ea353bc381db55364b999c7516e30a56c96666d",
    "arch/riscv64/user_entry.S": "7dca2e414e36db2851f7f7ded760fd78ca8da0a74b9fee677aee8699b7f6d9d2",
    "arch/riscv64/scheduler.S": "4c75823725dc68cc8e9b5e5d7051946e2418b322014a6e3e5fe1d72df5bc85ea",
    "kernel/boot_component_tests.c": "6a22c42e953c6af39be80baee6334a583658e150f0dde2944d637692b4a53cc3",
    "CMakePresets.json": "eae6c86ce5ac8f0e1b39605ebbd6816135558f0b4f8ea293b3680d7ecb79df86",
    "tools/run_qemu_smoke.py": "9e54f744074d077d05b52880495b73f7c74ae1db0a6ad751f8b7024688bf79e3",
}
SHARED_INSPECTION_ASTS = {
    "compact": "b1c6f8d02e33ae3835898a1cc1983f029d1da7d0b30aae32cb42069a06a7138c",
    "matching": "582aff5ac86e8d1746f0328fbe519d5439c40393e3713fd48cce71bf5b4d3837",
    "matching_lexical": "060470315942e9444843ca5aafd193340085c98981a0a5292c5e9f6ee009be15",
    "cpp_guards": "4065926ac0c263d65cf4943286b94cf45d8e57c8335c116ce2e39a5895ca74c8",
    "Function": "312fdebc83fa138ed1822691b7a540be4d0f3330a2be8de5164fa74f9ec86eef",
    "function_records": "068cc6a2b9f64e99a0d937ac5c64a60291c23608ef1f637f170c54e851f5cf72",
}


def digest_bytes(value):
    return hashlib.sha256(value).hexdigest()


def digest_tokens(value):
    return digest_bytes(json.dumps(tuple(value), separators=(",", ":")).encode())


def family_paths(family):
    return tuple(SOURCE_SETS.values())[family]


def source(path):
    return kernel_layout_source.read_path(ROOT / path)


def private_name(name):
    return "kernel_object_" + name


def host_name(name):
    if name == "main":
        return name
    return "kernel_object_test_" + (name[5:] if name.startswith("test_") else name)


def normal_names(family):
    if family == 0:
        return {private_name(name): name for name in CORE_PRIVATE_EXPORTS}
    if family == 1:
        return {host_name(name): name for name in BASELINE_BODIES[1] if name != "main"}
    return {}


def owned_records(family, texts=None):
    texts = texts if texts is not None else {
        path: source(path) for path in family_paths(family) if kernel_layout_source.is_file_path(ROOT / path)
    }
    return {
        (path, name): record for path, text in texts.items()
        for name, record in function_records(text).items()
    }


def original_locations(family, texts=None):
    names = normal_names(family)
    locations = {}
    stack_owners = {
        phase[0]: CORE_SOURCES[owner] for owner, _, phases in STACK_CONFLICT_TASKS.values() for phase in phases
    }
    stack_owners.update({name: CORE_SOURCES[8] for name in BASE_VALIDATION_SIGNATURES})
    case_owners = {
        name: family_paths(1)[spec["owner"]] for spec in NATIVE_CASE_TASKS.values() for name in spec["phases"]
    }
    for (path, spelling), record in owned_records(family, texts).items():
        if family == 0 and spelling in stack_owners:
            if path != stack_owners[spelling]:
                raise AssertionError("stack conflict passes have their exact file-local owner")
            continue
        if family == 1 and spelling in case_owners:
            if path != case_owners[spelling]:
                raise AssertionError("native case tasks have their exact file-local owner")
            continue
        if family == 1 and spelling in MODEL_OPERATION_TASKS:
            if path != family_paths(1)[11]:
                raise AssertionError("the eight model operations have their exact separate owner")
            continue
        if family == 2 and spelling in TARGET_TASKS:
            if path != family_paths(2)[1]:
                raise AssertionError("target scenarios have their exact separate owner")
            continue
        if family == 1 and spelling in {phase[0] for phase in MODEL_PROJECTION_PHASES}:
            if path != family_paths(1)[10]:
                raise AssertionError("model projection passes have their exact file-local owner")
            continue
        if family == 1 and spelling == "kernel_object_test_registry":
            if path != family_paths(1)[1]:
                raise AssertionError("the sole native registry getter has one exact fixture owner")
            continue
        name = names.get(spelling, spelling)
        if name not in BASELINE_BODIES[family] or name in locations:
            raise AssertionError(f"unknown/duplicate original or unreviewed phase: {family}/{spelling}")
        locations[name] = (path, record)
    if set(locations) != set(BASELINE_BODIES[family]):
        raise AssertionError("missing original definitions")
    return locations


def original_signature_tokens(family, original, record):
    names = normal_names(family)
    signature = tuple(names.get(token, token) for token in tokens(record.signature))
    moved_private = (
        family == 0 and original in CORE_PRIVATE_EXPORTS and record.name == private_name(original)
    ) or (family == 1 and original != "main" and record.name == host_name(original))
    if moved_private:
        if signature[:1] == ("static",):
            raise AssertionError("cross-owner private entries have their exact declared external scope")
        signature = ("static", *signature)
        if family == 1 and original == "reset_objects":
            if signature != tokens(
                "static void reset_objects(struct micros_kernel_objects *fixture)"
            ):
                raise AssertionError("reset receives exactly its original registry, never another fixture")
            signature = tokens("static void reset_objects(void)")
        elif family == 1 and original == "lifecycle_model_matches_registry":
            prefix = tokens(
                "static bool lifecycle_model_matches_registry(const struct micros_kernel_objects *fixture,"
            )
            if signature[:len(prefix)] != prefix:
                raise AssertionError("model projection receives the exact readonly borrowed registry first")
            signature = (*tokens("static bool lifecycle_model_matches_registry("), *signature[len(prefix):])
    return signature


def restore_registry_tokens(body):
    body = list(body)
    result = []
    index = 0
    while index < len(body):
        if body[index:index + 2] == ["fixture", "->"]:
            if index and body[index - 1] in (".", "->"):
                raise AssertionError("registry transport cannot qualify an existing member as a global root")
            field = body[index + 2:index + 3]
            if not field or field[0] not in REGISTRY_FIELDS:
                raise AssertionError("registry transport admits only the original public registry fields")
            result.extend(("objects", ".", field[0]))
            index += 3
        elif body[index:index + 2] == ["*", "fixture"] and body[index + 2:index + 3] != ["->"]:
            if index and body[index - 1] in (".", "->"):
                raise AssertionError("registry dereference cannot be an existing member's new authority")
            result.append("objects")
            index += 2
        else:
            if body[index] == "fixture":
                if index and body[index - 1] in (".", "->", "&"):
                    raise AssertionError("only the explicitly borrowed registry pointer may be transported")
                result.extend(("&", "objects"))
            else:
                result.append(body[index])
            index += 1
    return tuple(result)


def native_registry_body_tokens(original, body):
    body = list(body)
    borrow = list(tokens(
        "struct micros_kernel_objects *fixture = kernel_object_test_registry();"
    ))
    if original.startswith("test_"):
        if body[:1 + len(borrow)] != ["{"] + borrow:
            raise AssertionError("each native case borrows its one original registry once without a reset")
        del body[1:1 + len(borrow)]
    restored = []
    index = 0
    while index < len(body):
        token = body[index]
        if token in ("reset_objects", "lifecycle_model_matches_registry") and body[index + 1:index + 2] == ["("]:
            delimiter = ")" if token == "reset_objects" else ","
            if body[index + 2:index + 4] != ["fixture", delimiter]:
                raise AssertionError("registry helpers bind the exact caller-owned fixture as first argument")
            restored.extend((token, "("))
            index += 3 if delimiter == ")" else 4
        else:
            restored.append(token)
            index += 1
    return restore_registry_tokens(restored)


def model_projection_body_tokens(caller, phases):
    body = list(tokens(caller))
    declaration = list(tokens("size_t index;"))
    if body[:1] != ["{"] or "index" in body:
        raise AssertionError("model root retains no duplicate or hidden loop counter")
    guard = list(tokens(
        "if (!" + " || !".join(
            name + "(" + ", ".join(arguments) + ")"
            for name, arguments, _ in MODEL_PROJECTION_PHASES
        ) + ") { goto mismatch; }"
    ))
    positions = [index for index in range(len(body)) if body[index:index + len(guard)] == guard]
    if len(positions) != 1 or len(phases) != 3:
        raise AssertionError("three model passes retain exact caller arguments, order and mismatch exit")
    expanded = []
    for (_, _, bound), phase in zip(MODEL_PROJECTION_PHASES, phases):
        phase = list(tokens(phase))
        success = list(tokens("return true;"))
        if phase[:1 + len(declaration)] != ["{"] + declaration:
            raise AssertionError("model passes own only their original size_t counter")
        if phase[-len(success) - 1:] != success + ["}"]:
            raise AssertionError("model passes retain one terminal helper success")
        inner = phase[1 + len(declaration):-len(success) - 1]
        head = list(tokens("for (index = 0; index < " + bound + "; ++index) {"))
        if inner[:len(head)] != head or "goto" in inner:
            raise AssertionError("each pass retains its original bound and cannot borrow a caller label")
        index = 0
        while index < len(inner):
            if inner[index] == "return":
                if inner[index:index + 3] != ["return", "false", ";"]:
                    raise AssertionError("pass success cannot bypass remaining slots or caller validation")
                expanded.extend(tokens("goto mismatch;"))
                index += 3
            else:
                expanded.append(inner[index])
                index += 1
    start = positions[0]
    body[start:start + len(guard)] = expanded
    body[1:1] = declaration
    return tuple(body)


def stack_conflict_call_guard(original):
    calls = []
    for name, arguments, _ in STACK_CONFLICT_TASKS[original][2]:
        calls.append(name + "(\n" + ",\n".join("            " + arg for arg in arguments) + "\n        )")
    return (
        "    if (\n        !" + "\n        || !".join(calls)
        + "\n    ) {\n        return MICROS_KERNEL_OBJECT_ERROR_STACK;\n    }"
    )


def phase_failure_text(body, error):
    edits = []
    for match in re.finditer(r"\breturn\b[^;]*;", c_lexical_text(body)):
        if match.group() != "return false;":
            raise AssertionError("phase success cannot bypass the remaining original checks")
        edits.append((match.start(), match.end()))
    for start, end in reversed(edits):
        body = body[:start] + "return " + error + ";" + body[end:]
    return body


def stack_conflict_body_text(original, caller, phases):
    _, pointer, specification = STACK_CONFLICT_TASKS[original]
    prefix = "\n    struct micros_" + pointer + " *" + pointer + ";\n    enum micros_kernel_object_error error;\n"
    declaration = "    size_t index;\n"
    phase_prefix = "\n" + declaration + "\n"
    success = "\n    return true;\n"
    guard = stack_conflict_call_guard(original)
    if not caller.startswith(prefix) or "index" in tokens(caller):
        raise AssertionError("stack root retains its original locals without a duplicate counter")
    if len(phases) != 2 or caller.count(guard) != 1:
        raise AssertionError("both conflict passes retain ordered checked calls with exact caller arguments")
    loops = []
    for (_, _, bound), phase in zip(specification, phases):
        if not phase.startswith(phase_prefix) or not phase.endswith(success):
            raise AssertionError("conflict passes own only the original counter and terminal success")
        loop = phase[len(phase_prefix):-len(success)]
        head = "    for (index = 0; index < " + bound + "; ++index) {"
        lexical = c_lexical_text(loop)
        if (
            not loop.startswith(head)
            or matching_lexical(lexical, lexical.index("{"), "{", "}") != len(loop) - 1
            or "goto" in tokens(loop)
        ):
            raise AssertionError("one complete original loop retains its bound and independent exit scope")
        loops.append(phase_failure_text(loop, "MICROS_KERNEL_OBJECT_ERROR_STACK"))
    caller = caller.replace(guard, "\n".join(loops), 1)
    return prefix + phases[0][1:len(declaration) + 1] + caller[len(prefix):]


def validation_call(name, arguments, indent, error):
    pad = " " * indent
    return (
        pad + "if (!" + name + "(" + ", ".join(arguments) + ")) {\n"
        + pad + "    return " + error + ";\n" + pad + "}"
    )


def validation_phase_body(body, counter=None, bound=None):
    prefix = "\n" + ("    size_t " + counter + ";\n\n" if counter else "")
    success = "\n    return true;\n"
    if not body.startswith(prefix) or not body.endswith(success):
        raise AssertionError("validation phase owns only its exact typed counter and terminal success")
    body = body[len(prefix):-len(success)]
    if "goto" in tokens(body):
        raise AssertionError("base validation phases cannot borrow a caller exit label")
    phase_failure_text(body, "false")
    if bound is not None:
        head = "    for (index = 0; index < " + bound + "; ++index) {"
        lexical = c_lexical_text(body)
        if (
            not body.startswith(head)
            or matching_lexical(lexical, lexical.index("{"), "{", "}") != len(body) - 1
        ):
            raise AssertionError("validation pass retains one complete loop with its exact original bound")
    return body


def expand_validation_call(body, name, arguments, indent, fragment, error="false"):
    guard = validation_call(name, arguments, indent, error)
    if body.count(guard) != 1:
        raise AssertionError("validation phases bind original witnesses, counters, order and checked failure")
    shifted = "".join(" " * (indent - 4) + line if line.strip() else line for line in fragment.splitlines(True))
    return body.replace(guard, shifted, 1)


def restore_counter_declaration(body, predecessor, counter, actual):
    declaration = "    size_t " + counter + ";\n"
    if not actual.startswith("\n" + declaration + "\n") or body.count(predecessor) != 1:
        raise AssertionError("original nested counter is restored only from its verified actual typed declaration")
    return body.replace(predecessor, predecessor + "    " + actual[1:len(declaration) + 1], 1)


def base_validation_body_text(caller, phases):
    if set(phases) != set(BASE_VALIDATION_SIGNATURES):
        raise AssertionError("base validation admits exactly its nine named file-local tasks")
    fragments = {
        name: validation_phase_body(body, counter) for name, body, counter in (
            (
                "kernel_object_validation_thread_context_matches",
                phases["kernel_object_validation_thread_context_matches"], "other_index",
            ),
            ("kernel_object_validation_free_hart_is_clear", phases["kernel_object_validation_free_hart_is_clear"], "priority"),
            (
                "kernel_object_validation_hart_stacks_avoid_peers",
                phases["kernel_object_validation_hart_stacks_avoid_peers"], "other_index",
            ),
            (
                "kernel_object_validation_hart_stack_state_matches",
                phases["kernel_object_validation_hart_stack_state_matches"], None,
            ),
            (
                "kernel_object_validation_current_binding_matches",
                phases["kernel_object_validation_current_binding_matches"], None,
            ),
        )
    }
    stack = "kernel_object_validation_hart_stack_state_matches"
    peers = "kernel_object_validation_hart_stacks_avoid_peers"
    fragments[stack] = expand_validation_call(
        fragments[stack], peers, ("objects", "hart", "index"), 8, fragments[peers],
    )
    expanded = []
    for name, arguments, bound, count in BASE_VALIDATION_PASSES:
        body = validation_phase_body(phases[name], "index", bound)
        if name == "kernel_object_validation_thread_slots_match":
            context = "kernel_object_validation_thread_context_matches"
            body = expand_validation_call(body, context, ("objects", "thread", "index"), 12, fragments[context])
            body = restore_counter_declaration(
                body, "        uint32_t unused_generation;\n", "other_index", phases[context],
            )
        elif name == "kernel_object_validation_hart_slots_match":
            free = "kernel_object_validation_free_hart_is_clear"
            current = "kernel_object_validation_current_binding_matches"
            body = expand_validation_call(body, free, ("hart",), 12, fragments[free])
            body = restore_counter_declaration(body, "        size_t other_index;\n", "priority", phases[free])
            body = expand_validation_call(body, stack, ("objects", "hart", "index"), 8, fragments[stack])
            body = expand_validation_call(body, current, ("objects", "hart", "current_threads"), 8, fragments[current])
        if count is not None:
            transported = "++(*" + count + ");"
            if body.count(transported) != 1 or tokens(body).count(count) != 1:
                raise AssertionError("each observed count is the exact caller-owned original scalar, incremented once")
            body = body.replace(transported, "++" + count + ";", 1)
        expanded.append(phase_failure_text(body, "MICROS_KERNEL_OBJECT_ERROR_INVARIANT"))
    for (name, arguments, _, _), body in zip(BASE_VALIDATION_PASSES, expanded):
        caller = expand_validation_call(caller, name, arguments, 4, body, "MICROS_KERNEL_OBJECT_ERROR_INVARIANT")
    return caller


def checked_phase_bodies(records, parameters):
    for name, arguments in parameters.items():
        record = records[name]
        if record.guards or tokens(record.signature) != tokens("static bool " + name + "(" + arguments + ")"):
            raise AssertionError("actual file-local phases retain their complete admitted parameter types and guards")
    return {name: records[name].body for name in parameters}


def case_work_declarations(owner, tag, fields):
    lexical = c_lexical_text(owner)
    starts = tuple(re.finditer(r"\bstruct " + tag + r"\s*\{", lexical))
    if len(starts) != 1:
        raise AssertionError("each native case has one exact file-local automatic-work type")
    opening = lexical.index("{", starts[0].start())
    closing = matching_lexical(lexical, opening, "{", "}")
    actual = tuple(tokens(part + ";") for part in lexical[opening + 1:closing].split(";") if part.strip())
    if actual != tuple(tokens(declaration) for declaration, _ in fields):
        raise AssertionError("native work contains exactly the original fields, order, complete types and extents")
    return actual


def outline_boolean_case_tokens(caller, phases, calls):
    body = list(tokens(caller))
    positions = []
    expansions = []
    if set(phases) != {name for name, _ in calls}:
        raise AssertionError("case outlines admit only their exact named tasks")
    for name, arguments in calls:
        guard = list(tokens("if (!" + name + "(" + ", ".join(arguments) + ")) { return false; }"))
        starts = [index for index in range(len(body)) if body[index:index + len(guard)] == guard]
        if len(starts) != 1:
            raise AssertionError("case tasks retain exact borrowed registry/work bindings and checked first failure")
        phase = list(tokens(phases[name]))
        if phase[:1] != ["{"] or phase[-4:] != ["return", "true", ";", "}"] or "return" in phase[1:-4]:
            raise AssertionError("case task success replaces only its sole terminal success, never a partial attempt")
        positions.append(starts[0])
        expansions.append((starts[0], len(guard), phase[1:-4]))
    if positions != sorted(positions) or len(set(positions)) != len(positions):
        raise AssertionError("native case tasks retain their exact original statement order")
    for start, length, replacement in reversed(expansions):
        body[start:start + length] = replacement
    return tuple(body)


def case_phase_contract(spec, name):
    arguments = (() if name in spec.get("without_fixture", ()) else ("fixture",)) + ("&work",)
    parameters = (
        (() if name in spec.get("without_fixture", ()) else ("struct micros_kernel_objects *fixture",))
        + ("struct " + spec["work"] + " *work",)
    )
    for kind, variable in spec.get("parameters", {}).get(name, ()):
        parameters += (kind + " " + variable,)
        arguments += (variable,)
    return ", ".join(parameters), arguments


def restore_work_field_tokens(body, binding, names):
    restored = []
    index = 0
    while index < len(body):
        if body[index] == binding:
            if (
                body[index + 1:index + 2] not in (["."], ["->"])
                or body[index + 2:index + 3] not in ([name] for name in names)
                or (index and body[index - 1] in (".", "->"))
            ):
                raise AssertionError("work transport admits only exact original fields, never a copy, reset or borrowed pointer")
            restored.append(body[index + 2])
            index += 3
        else:
            restored.append(body[index])
            index += 1
    return tuple(restored)


def restore_automatic_work_tokens(caller, tag, fields, declarations, prefix_statements):
    body = list(caller)
    prefix_statements = list(prefix_statements)
    work = list(tokens("struct " + tag + " work;"))
    prefix = ["{"] + prefix_statements + work
    if body[:len(prefix)] != prefix:
        raise AssertionError("case work is one uninitialized automatic root object after the sole registry borrow")
    del body[1 + len(prefix_statements):len(prefix)]
    names = tuple(
        declaration[declaration.index("[") - 1] if "[" in declaration else declaration[-2]
        for declaration in declarations
    )
    restored = list(restore_work_field_tokens(body, "work", names))
    start = 1 + len(prefix_statements)
    initializers = []
    for (declaration, kind), actual, name in zip(fields, declarations, names):
        if tuple(actual) != tokens(declaration):
            raise AssertionError("each original declaration comes from its verified actual work field")
        if kind is None:
            initializers.extend(actual)
            continue
        if restored[start:start + 2] != [name, "="]:
            raise AssertionError("only original explicit initializers run, in their original declaration order")
        end = restored.index(";", start)
        value = restored[start + 2:end]
        if kind == "aggregate":
            cast = ["(", *actual[:-2], ")"]
            if value[:len(cast) + 1] != cast + ["{"]:
                raise AssertionError("aggregate assignment uses the exact actual original field type")
            value = value[len(cast):]
        elif kind != "value":
            raise AssertionError("unreviewed case initializer transport")
        initializers.extend((*actual[:-1], "=", *value, ";"))
        del restored[start:end + 1]
    restored[start:start] = initializers
    return tuple(restored)


def restore_case_work_tokens(caller, tag, fields, declarations, constants=()):
    prefix = (
        *tokens("struct micros_kernel_objects *fixture = kernel_object_test_registry();"),
        *(token for declaration in constants for token in tokens(declaration)),
    )
    return restore_automatic_work_tokens(caller, tag, fields, declarations, prefix)


def model_operation_body_tokens(phase):
    body = list(tokens(phase))
    if body[:1] != ["{"] or body[-4:] != ["return", "true", ";", "}"]:
        raise AssertionError("each model operation has only its original terminal case-success exit")
    inner = body[1:-4]
    if {"break", "continue", "goto"} & set(inner):
        raise AssertionError("model operations cannot borrow loop, switch or caller label exits")
    for index, token in enumerate(inner):
        if token == "return" and inner[index:index + 3] != ["return", "false", ";"]:
            raise AssertionError("a model attempt cannot succeed before its original status/output/reference checks")
    restored = []
    index = 0
    while index < len(inner):
        if inner[index] == "model":
            if index and inner[index - 1] in (".", "->"):
                raise AssertionError("independent model transport cannot borrow another member's authority")
            if inner[index + 1:index + 2] == ["->"]:
                if inner[index + 2:index + 3] not in (
                    ["processes"], ["threads"], ["process_count"], ["thread_count"],
                ):
                    raise AssertionError("model transport admits only the four original reference fields")
                restored.extend(("model", ".", inner[index + 2]))
                index += 3
                continue
            if (index and inner[index - 1] in ("&", "*")) or inner[index + 1:index + 2] == ["."]:
                raise AssertionError("only the original borrowed model pointer, not a copy or address of a pointer, is admitted")
            restored.extend(("&", "model"))
        else:
            restored.append(inner[index])
        index += 1
    return (*restored, *tokens("break;"))


def model_runner_body_tokens(caller, phases, declarations):
    if set(phases) != set(MODEL_OPERATION_TASKS) or tuple(declarations) != tuple(
        tokens(declaration) for declaration, _ in MODEL_ITERATION_FIELDS
    ):
        raise AssertionError("model dispatch retains exactly eight operations and four original typed iteration fields")
    body = list(tokens(caller))
    head = list(tokens("for (step = 0; step < MODEL_STEPS; ++step) {"))
    positions = [index for index in range(len(body)) if body[index:index + len(head)] == head]
    if len(positions) != 1:
        raise AssertionError("one original model loop owns the complete draw/dispatch/comparison lifetime")
    start = positions[0] + len(head)
    prefix = list(tokens("struct " + MODEL_ITERATION_TAG + " iteration; iteration.value ="))
    if body[start:start + len(prefix)] != prefix:
        raise AssertionError("iteration work is uninitialized except for its original one draw at loop entry")
    end = body.index(";", start + len(prefix))
    value = body[start + len(prefix):end]
    restored_declarations = [*declarations[0][:-1], "=", *value, ";"]
    restored_declarations.extend(token for declaration in declarations[1:] for token in declaration)
    body[start:end + 1] = restored_declarations
    if body.count("case") != 8 or "default" in body:
        raise AssertionError("the original numeric cases remain complete and gain no default")
    branches = []
    for number, name in enumerate(MODEL_OPERATION_TASKS):
        guard = list(tokens(
            "case " + str(number) + ": if (!" + name
            + "(fixture, &model, step, &iteration)) { return false; } break;"
        ))
        starts = [index for index in range(len(body)) if body[index:index + len(guard)] == guard]
        if len(starts) != 1:
            raise AssertionError("each numeric model case binds its actual registry/model/step/iteration with checked failure")
        fragment = model_operation_body_tokens(phases[name])
        branches.append((starts[0], len(guard), [*tokens("case " + str(number) + ": {"), *fragment, "}"]))
    if [start for start, _, _ in branches] != sorted(start for start, _, _ in branches):
        raise AssertionError("the eight original numeric operations retain exact order")
    for start, length, replacement in reversed(branches):
        body[start:start + length] = replacement
    return restore_work_field_tokens(body, "iteration", ("value", "slot", "expected", "actual"))


def native_model_body_tokens(record, operations=None, header=None):
    operations = source(family_paths(1)[11]) if operations is None else operations
    header = source(PRIVATE_HEADERS[1]) if header is None else header
    declarations = case_work_declarations(header, MODEL_ITERATION_TAG, MODEL_ITERATION_FIELDS)
    records = function_records(operations)
    phases = {}
    for name in MODEL_OPERATION_TASKS:
        phase = records[name]
        if phase.guards or tokens(phase.signature) != tokens("bool " + name + "(" + MODEL_OPERATION_PARAMETERS + ")"):
            raise AssertionError("cross-owner model tasks preserve their exact complete private parameter types")
        phases[name] = "{" + phase.body + "}"
    return model_runner_body_tokens("{" + record.body + "}", phases, declarations)


def target_phase_tokens(phase):
    body = list(tokens(phase))
    if body[:1] != ["{"] or body[-4:] != ["return", "true", ";", "}"]:
        raise AssertionError("target scenario success follows all original checks")
    body = body[1:-4]
    if {"goto", "break", "continue"} & set(body):
        raise AssertionError("target scenarios cannot borrow root IRQ or loop exit scope")
    for index, token in enumerate(body):
        if token == "return" and body[index:index + 3] != ["return", "false", ";"]:
            raise AssertionError("target scenario success cannot bypass its remaining original operations")
    return tuple(body)


def target_predicate_tokens(phase):
    body = target_phase_tokens(phase)
    if body[:2] != ("if", "(") or body[-6:] != (")", "{", "return", "false", ";", "}"):
        raise AssertionError("each retirement/reuse task is one complete checked original predicate group")
    condition = body[2:-6]
    if {"{", "}", ";"} & set(condition):
        raise AssertionError("retirement/reuse groups cannot hide another statement or success boundary")
    return condition


def target_body_tokens(caller, phases, declarations):
    if set(phases) != set(TARGET_TASKS) or tuple(declarations) != tuple(
        tokens(declaration) for declaration, _ in TARGET_WORK_FIELDS
    ):
        raise AssertionError("target scenarios retain exactly the original eleven typed mutable objects")
    body = list(tokens(caller))
    calls = (
        (TARGET_TASKS[0], ("&work",)),
        (
            TARGET_TASKS[1],
            ("&work", "(uintptr_t)thread_stack", "(uintptr_t)thread_stack + sizeof(thread_stack)", "&context"),
        ),
    )
    expansions = []
    for name, arguments in calls:
        guard = list(tokens("if (!" + name + "(" + ", ".join(arguments) + ")) { goto done; }"))
        starts = [index for index in range(len(body)) if body[index:index + len(guard)] == guard]
        if len(starts) != 1:
            raise AssertionError("target checked calls bind actual root work, static context and complete array bounds")
        phase = list(target_phase_tokens(phases[name]))
        if name == TARGET_TASKS[1]:
            bindings = {
                "stack_bottom": tokens(arguments[1]), "stack_top": tokens(arguments[2]), "context": tokens(arguments[3]),
            }
            expanded = []
            for index, token in enumerate(phase):
                if token in bindings:
                    if index and phase[index - 1] in (".", "->"):
                        raise AssertionError("explicit stack/context parameters cannot replace a nested member")
                    expanded.extend(bindings[token])
                else:
                    expanded.append(token)
            phase = expanded
        restored = []
        index = 0
        while index < len(phase):
            if phase[index:index + 3] == ["return", "false", ";"]:
                restored.extend(tokens("goto done;"))
                index += 3
            else:
                restored.append(phase[index])
                index += 1
        expansions.append((starts[0], len(guard), restored))
    combined = list(tokens(
        "if (!" + TARGET_TASKS[2] + "(&work) || !" + TARGET_TASKS[3] + "(&work)) { goto done; }"
    ))
    starts = [index for index in range(len(body)) if body[index:index + len(combined)] == combined]
    if len(starts) != 1:
        raise AssertionError("success-only retirement and reuse retain one ordered short-circuit root failure boundary")
    predicate = (
        *tokens("if ("), *target_predicate_tokens(phases[TARGET_TASKS[2]]), "||",
        *target_predicate_tokens(phases[TARGET_TASKS[3]]), *tokens(") { goto done; }"),
    )
    expansions.append((starts[0], len(combined), predicate))
    if [start for start, _, _ in expansions] != sorted(start for start, _, _ in expansions):
        raise AssertionError("target create/select/retire/reuse tasks retain original order")
    for start, length, replacement in reversed(expansions):
        body[start:start + length] = replacement
    return restore_automatic_work_tokens(body, TARGET_WORK_TAG, TARGET_WORK_FIELDS, declarations, ())


def target_actual_body_tokens(record, scenarios=None, header=None):
    scenarios = source(family_paths(2)[1]) if scenarios is None else scenarios
    header = source(PRIVATE_HEADERS[2]) if header is None else header
    declarations = case_work_declarations(header, TARGET_WORK_TAG, TARGET_WORK_FIELDS)
    records = function_records(scenarios)
    if set(records) != set(TARGET_TASKS):
        raise AssertionError("target scenario owner contains only its four necessary actual tasks")
    phases = {}
    for name, parameters in TARGET_PARAMETERS.items():
        phase = records[name]
        if phase.guards or tokens(phase.signature) != tokens("bool " + name + "(" + parameters + ")"):
            raise AssertionError("target cross-owner tasks retain exact complete private types")
        phases[name] = "{" + phase.body + "}"
    return target_body_tokens("{" + record.body + "}", phases, declarations)


def native_corruption_macro(owner, records, phases):
    definitions = tuple(re.finditer(
        r"(?m)^#define EXPECT_CORRUPTION\(statement\) \\\n(?:[^\n]*\\\n)*[^\n]*", owner,
    ))
    undefs = tuple(re.finditer(r"(?m)^#undef EXPECT_CORRUPTION$", owner))
    if len(definitions) != 1 or len(undefs) != 1:
        raise AssertionError("one actual corruption macro has one exact definition and undef lifetime")
    definition, undef = definitions[0], undefs[0]
    value = definition.group()
    if value.count("work->snapshot") != 1:
        raise AssertionError("corruption restores only its single original automatic snapshot")
    restored = value.replace("work->snapshot", "snapshot", 1)
    if digest_bytes(restored.encode()) != CORRUPTION_MACRO_DIGEST:
        raise AssertionError("actual corruption macro bytes retain restore, mutation and combined validation order")
    for name in phases:
        record = records[name]
        start = owner.index(record.signature)
        end = start + len(record.signature) + 1 + len(record.body) + 1
        if not definition.end() < start < end < undef.start():
            raise AssertionError("the file-local corruption macro encloses each actual scenario definition")
    root = records[host_name("test_validator_rejects_corrupt_relationships")]
    if undef.end() >= owner.index(root.signature):
        raise AssertionError("corruption macro is undefined before the root and cannot leak beyond its scenario owner")
    return restored, undef.group()


def native_case_body_tokens(original, record, owner=None):
    spec = NATIVE_CASE_TASKS[original]
    owner = source(family_paths(1)[spec["owner"]]) if owner is None else owner
    declarations = case_work_declarations(owner, spec["work"], spec["fields"])
    parameters = {name: case_phase_contract(spec, name)[0] for name in spec["phases"]}
    records = function_records(owner)
    phases = checked_phase_bodies(records, parameters)
    body = outline_boolean_case_tokens(
        "{" + record.body + "}", {name: "{" + phase + "}" for name, phase in phases.items()},
        tuple((name, case_phase_contract(spec, name)[1]) for name in spec["phases"]),
    )
    body = restore_case_work_tokens(body, spec["work"], spec["fields"], declarations, spec.get("constants", ()))
    if original == "test_validator_rejects_corrupt_relationships":
        macro, undef = native_corruption_macro(owner, records, spec["phases"])
        reset = tokens("kernel_object_test_reset_objects(fixture);")
        positions = [index for index in range(len(body)) if body[index:index + len(reset)] == reset]
        if len(positions) != 1 or body[-4:] != ("return", "true", ";", "}"):
            raise AssertionError("corruption retains its original sole setup, macro scope and final registry state")
        body = (*body[:positions[0]], *tokens(macro), *body[positions[0]:-4], *tokens(undef), *body[-4:])
    return body


def core_body_text(original, record, texts=None):
    if original in STACK_CONFLICT_TASKS and (0, original) not in FUNCTION_TASK_MIGRATION:
        index, pointer, phases = STACK_CONFLICT_TASKS[original]
        owner = function_records(source(CORE_SOURCES[index]) if texts is None else texts[CORE_SOURCES[index]])
        parameters = {
            name: "const struct micros_kernel_objects *objects" + "".join(
                ", const struct micros_" + pointer + " *" + arg if arg == pointer else ", uintptr_t " + arg
                for arg in arguments[1:]
            ) for name, arguments, _ in phases
        }
        bodies = checked_phase_bodies(owner, parameters)
        return stack_conflict_body_text(original, record.body, [bodies[name] for name, _, _ in phases])
    if original == "micros_kernel_objects_validate_base" and (0, original) not in FUNCTION_TASK_MIGRATION:
        owner = function_records(source(CORE_SOURCES[8]) if texts is None else texts[CORE_SOURCES[8]])
        return base_validation_body_text(record.body, checked_phase_bodies(owner, BASE_VALIDATION_SIGNATURES))
    return record.body


def original_body_digest(family, original, record):
    names = normal_names(family)
    body = tokens("{" + (core_body_text(original, record) if family == 0 else record.body) + "}")
    if family == 1 and original == "lifecycle_model_matches_registry" and (1, original) not in FUNCTION_TASK_MIGRATION:
        owner = function_records(source(family_paths(1)[10]))
        body = model_projection_body_tokens(
            "{" + record.body + "}",
            ["{" + owner[name].body + "}" for name, _, _ in MODEL_PROJECTION_PHASES],
        )
    if family == 1 and original in NATIVE_CASE_TASKS and (1, original) not in FUNCTION_TASK_MIGRATION:
        body = native_case_body_tokens(original, record)
    if family == 1 and original == "test_seeded_lifecycle_model" and (1, original) not in FUNCTION_TASK_MIGRATION:
        body = native_model_body_tokens(record)
    if family == 2 and (2, original) not in FUNCTION_TASK_MIGRATION:
        body = target_actual_body_tokens(record)
    body = tuple(names.get(token, token) for token in body)
    if family == 1 and any(name == "kernel_object_test_registry" for _, name in owned_records(1)):
        body = native_registry_body_tokens(original, body)
    return digest_tokens(body)


def rename_core_identifiers(text):
    lexical = c_lexical_text(text)
    aliases = normal_names(0)
    edits = []
    for match in re.finditer(r"\b[A-Za-z_]\w*\b", lexical):
        if match.group() in aliases and not lexical[:match.start()].rstrip().endswith((".", "->")):
            edits.append((match.start(), match.end(), aliases[match.group()]))
    for start, end, replacement in reversed(edits):
        text = text[:start] + replacement + text[end:]
    return text


def restored_kernel_objects_source(texts=None, private_header=None):
    texts = texts if texts is not None else {
        path: source(path) for path in CORE_SOURCES if kernel_layout_source.is_file_path(ROOT / path)
    }
    locations = original_locations(0, texts)
    root = texts[CORE_SOURCES[0]]
    first = next(iter(function_records(root).values()))
    prefix = root[:root.index(first.signature)]
    include = '#include "kernel_object_private.h"'
    if include in prefix:
        if prefix.count(include) != 1:
            raise AssertionError("one exact portable private include replaces the old magic declaration")
        header = source(PRIVATE_HEADERS[0]) if private_header is None else private_header
        definitions = re.findall(
            r"(?m)^#define MICROS_KERNEL_OBJECTS_MAGIC UINT64_C\(0x4d4943524f534f42\)[ \t]*$", header,
        )
        if len(definitions) != 1:
            raise AssertionError("original object magic comes from its verified actual private declaration")
        prefix = prefix.replace(include, definitions[0], 1)
    definitions = []
    for original in BASELINE_BODIES[0]:
        _, record = locations[original]
        signature = rename_core_identifiers(record.signature)
        if original in CORE_PRIVATE_EXPORTS and record.name == private_name(original):
            if tokens(signature)[:1] == ("static",):
                raise AssertionError("unexpected static extraction-only definition")
            signature = "static " + signature
        definitions.append(signature + "{" + rename_core_identifiers(core_body_text(original, record, texts)) + "}")
    restored = prefix + "\n\n".join(definitions) + "\n"
    if digest_bytes(restored.encode()) != BASE_CORE_DIGEST:
        raise AssertionError("complete current-source inverse differs from immutable object source bytes")
    return restored


def source_set_block(name, paths):
    return "set(\n    " + name + "\n" + "".join("    " + path + "\n" for path in paths) + ")"


def alias_rows():
    return tuple(
        (paths[0], (*paths[1:], PRIVATE_HEADERS[family]))
        for family, paths in enumerate(SOURCE_SETS.values())
    )


def kernel_object_cmake_projection(text):
    text = grant_data_plane_cmake_projection(text)
    blocks = [(name, paths, cmake_commands(text, "set", name)) for name, paths in SOURCE_SETS.items()]
    if not any(found for _, _, found in blocks):
        if any("${" + name + "}" in text for name in SOURCE_SETS):
            raise AssertionError("object variables require their complete ordered declarations")
        return text
    for family, (name, paths, found) in enumerate(blocks):
        expected = source_set_block(name, paths)
        if len(found) != 1 or found[0] != expected or text.count(expected + "\n\n") != 1:
            raise AssertionError("wrong complete object source-set insertion")
        text = text.replace(expected + "\n\n", "", 1)
        variable = "${" + name + "}"
        if text.count(variable) != (8, 1, 1)[family]:
            raise AssertionError("wrong exact object source-set consumer count")
        text = text.replace(variable, paths[0])
    return text


def restored_cmake(text):
    restored = kernel_object_cmake_projection(text)
    if digest_bytes(restored.encode()) != BASE_CMAKE_DIGEST:
        raise AssertionError("recipe changed outside three exact sets and ten operands")
    return restored


class KernelObjectSourceLayoutTest(unittest.TestCase):
    def test_portable_registry_storage_has_its_reviewed_state_owner(self):
        spellings = {"storage_is_zero", "kernel_object_storage_is_zero"}
        owners = [
            path for path in CORE_SOURCES
            if kernel_layout_source.is_file_path(ROOT / path)
            and spellings & set(c_function_definitions(kernel_layout_source.read_path(ROOT / path)))
        ]
        self.assertEqual(["kernel/kernel_object_state.c"], owners)

    def test_shared_records_preserve_complete_reviewed_asts_and_reexports(self):
        path = ROOT / "tests/host/c_source_records.py"
        self.assertTrue(kernel_layout_source.is_file_path(path))
        shared = ast.parse(kernel_layout_source.read_path(path))
        definitions = {
            node.name: node for node in shared.body
            if isinstance(node, (ast.FunctionDef, ast.ClassDef))
        }
        self.assertEqual(set(SHARED_INSPECTION_ASTS), set(definitions))
        for name, expected in SHARED_INSPECTION_ASTS.items():
            with self.subTest(name=name):
                current = ast_dump(definitions[name])
                self.assertEqual(expected, hashlib.sha256(current.encode()).hexdigest())
        scheduler = ast.parse(kernel_layout_source.read_path(ROOT / "tests/host/test_scheduler_source_layout.py"))
        self.assertFalse(any(
            isinstance(node, (ast.FunctionDef, ast.ClassDef)) and node.name in SHARED_INSPECTION_ASTS
            for node in scheduler.body
        ))
        imported = {
            alias.name for node in scheduler.body if isinstance(node, ast.ImportFrom)
            and node.module == "tests.host.c_source_records" for alias in node.names
        }
        self.assertEqual(set(SHARED_INSPECTION_ASTS), imported)

    def test_immutable_inventories_and_explicit_pending_work_are_exact(self):
        self.assertEqual((48, 27, 1), tuple(len(items) for items in BASELINE_BODIES.values()))
        self.assertEqual(72, len(INITIAL_SYMBOL_OWNER_MIGRATION))
        self.assertEqual(8, len(LONG_FUNCTIONS))
        self.assertEqual(4, len(SHORTER_TASKS))
        self.assertEqual(12, len(REQUIRED_TASKS))
        self.assertEqual(22, len(CORE_PRIVATE_EXPORTS))
        for active, initial in (
            (SYMBOL_OWNER_MIGRATION, INITIAL_SYMBOL_OWNER_MIGRATION),
            (FUNCTION_TASK_MIGRATION, REQUIRED_TASKS),
        ):
            self.assertIsInstance(active, tuple)
            self.assertEqual(len(active), len(set(active)))
            self.assertLessEqual(set(active), set(initial))
        for family, groups in ORIGINAL_FUNCTIONS.items():
            flattened = tuple(name for names in groups.values() for name in names)
            self.assertEqual(len(flattened), len(set(flattened)))
            self.assertEqual(set(BASELINE_BODIES[family]), set(flattened))

    def test_every_original_has_its_exact_current_and_final_owner(self):
        for family, groups in ORIGINAL_FUNCTIONS.items():
            locations = original_locations(family)
            for owner, names in groups.items():
                for name in names:
                    with self.subTest(family=family, name=name):
                        current = 0 if (family, name) in SYMBOL_OWNER_MIGRATION else owner
                        self.assertEqual(family_paths(family)[current], locations[name][0])

    def test_complete_original_bodies_and_all_parameter_types_remain_exact(self):
        for family in ORIGINAL_FUNCTIONS:
            locations = original_locations(family)
            signatures = []
            for name, expected in BASELINE_BODIES[family].items():
                with self.subTest(family=family, name=name):
                    _, record = locations[name]
                    self.assertEqual(expected, original_body_digest(family, name, record), REVIEWED_BASE)
                    signatures.append((name, original_signature_tokens(family, name, record)))
            self.assertEqual(BASELINE_SIGNATURES[family], digest_tokens(signatures))

    def test_only_original_long_task_migrations_exceed_the_signal(self):
        actual = {
            (family, normal_names(family).get(name, name))
            for family in ORIGINAL_FUNCTIONS for (_, name), record in owned_records(family).items()
            if record.lines > 120
        }
        self.assertEqual(set(LONG_FUNCTIONS) & set(FUNCTION_TASK_MIGRATION), actual)

    def test_portable_complete_byte_inverse_is_independent_and_rejects_changed_code(self):
        current = {path: source(path) for path in CORE_SOURCES if kernel_layout_source.is_file_path(ROOT / path)}
        restored = restored_kernel_objects_source(current)
        self.assertEqual(BASE_CORE_DIGEST, digest_bytes(restored.encode()))
        path, record = original_locations(0, current)["require_initialized"]
        changed = current[path].replace(
            "objects->initialization_magic != MICROS_KERNEL_OBJECTS_MAGIC",
            "objects->initialization_magic == MICROS_KERNEL_OBJECTS_MAGIC",
        )
        self.assertNotEqual(current[path], changed)
        with self.assertRaises(AssertionError):
            restored_kernel_objects_source({**current, path: changed})
        path, _ = original_locations(0, current)["micros_process_create_at"]
        changed = current[path].replace("handle->generation = generation;", "handle->generation = 0;", 1)
        self.assertNotEqual(current[path], changed)
        with self.assertRaises(AssertionError):
            restored_kernel_objects_source({**current, path: changed})
        if '#include "kernel_object_private.h"' in current[CORE_SOURCES[0]]:
            with self.assertRaises(AssertionError):
                restored_kernel_objects_source(current, source(PRIVATE_HEADERS[0]).replace("0x4d4943524f534f42", "0x4d4943524f534f43"))

    def test_literal_aware_namespace_inverse_does_not_rewrite_strings_comments_or_members(self):
        current = (
            'kernel_object_require_initialized(objects); "kernel_object_require_initialized"; '
            "/* kernel_object_require_initialized */ other.kernel_object_require_initialized;"
        )
        expected = (
            'require_initialized(objects); "kernel_object_require_initialized"; '
            "/* kernel_object_require_initialized */ other.kernel_object_require_initialized;"
        )
        self.assertEqual(expected, rename_core_identifiers(current))

    def test_all_real_sources_headers_consumers_and_complete_recipe_are_exact(self):
        self.assertEqual(23, sum(map(len, SOURCE_SETS.values())))
        self.assertEqual(23, len({path for paths in SOURCE_SETS.values() for path in paths}))
        cmake = source("CMakeLists.txt")
        for family, (name, paths) in enumerate(SOURCE_SETS.items()):
            self.assertEqual((source_set_block(name, paths),), cmake_commands(cmake, "set", name))
            for path in (*paths, PRIVATE_HEADERS[family]):
                self.assertTrue(kernel_layout_source.is_file_path(ROOT / path), path)
            self.assertEqual((8, 1, 1)[family], cmake.count("${" + name + "}"))
        core, host, target = ("${" + name + "}" for name in SOURCE_SETS)
        for consumer in (
            "kernel_objects_test", "frame_ownership_test", "scheduler_test", "endpoint_test",
            "ipc_model_test", "bootstrap_control_test", "pm_test", "micros",
        ):
            blocks = cmake_commands(cmake, "add_executable", consumer)
            self.assertEqual(1, len(blocks))
            self.assertEqual(1, blocks[0].count(core))
            self.assertEqual(int(consumer == "kernel_objects_test"), blocks[0].count(host))
        guards = re.findall(r"if\(MICROS_BUILD_OBJECT_MODEL_TEST\)([\s\S]*?)\nendif\(\)", cmake)
        self.assertEqual(1, sum(target in guard for guard in guards))
        self.assertEqual(BASE_CMAKE_DIGEST, digest_bytes(restored_cmake(cmake).encode()))
        self.assertEqual(
            digest_bytes(cmake.encode()),
            kernel_layout_source.original_cmake_recipe_digest(
                validation_context.RECIPE_DIGESTS["CMakeLists.txt"]
            ),
        )

    def test_portable_private_helpers_are_necessary_and_dependencies_stay_acyclic(self):
        current = owned_records(0)
        owners = {name: path for path, name in current}
        aliases = normal_names(0)
        final = {
            name: CORE_SOURCES[owner] for owner, names in ORIGINAL_FUNCTIONS[0].items() for name in names
        }
        final.update({
            name: CORE_SOURCES[owner] for owner, _, phases in STACK_CONFLICT_TASKS.values() for name, _, _ in phases
        })
        final.update({name: CORE_SOURCES[8] for name in BASE_VALIDATION_SIGNATURES})
        edges = set()
        projected = set()
        exports = set()
        for (path, spelling), record in current.items():
            caller = aliases.get(spelling, spelling)
            referenced = set(re.findall(r"\b[A-Za-z_]\w*\b", c_lexical_text(record.body)))
            for callee in referenced & set(owners):
                dependency = owners[callee]
                normalized = aliases.get(callee, callee)
                if dependency != path:
                    edges.add((dependency, path))
                    if not callee.startswith("micros_"):
                        exports.add(callee)
                source_owner = CORE_SOURCES.index(final[caller])
                dependency_owner = CORE_SOURCES.index(final[normalized])
                if source_owner != dependency_owner:
                    self.assertIn(dependency_owner, CORE_DAG[source_owner], (caller, normalized))
                    projected.add((final[normalized], final[caller]))
        self.assertEqual(set(CORE_SOURCES), set(topological_order(set(CORE_SOURCES), edges)))
        self.assertEqual(set(CORE_SOURCES), set(topological_order(set(CORE_SOURCES), projected)))
        header = source(PRIVATE_HEADERS[0])
        declarations = re.findall(r"\b(kernel_object_\w+)\s*\([^;{}]*\)\s*;", c_lexical_text(header))
        self.assertEqual(len(declarations), len(set(declarations)))
        self.assertEqual(exports, set(declarations))
        self.assertLessEqual(exports, {private_name(name) for name in CORE_PRIVATE_EXPORTS})
        self.assertEqual({}, c_function_definitions(header))
        for path in CORE_SOURCES:
            self.assertEqual((), file_scope_objects(source(path)))

    def test_public_runtime_architecture_driver_and_neighbor_artifacts_are_byte_exact(self):
        self.assertEqual(18, len(STABLE_HASHES))
        for path, expected in STABLE_HASHES.items():
            with self.subTest(path=path):
                from tests.host.test_qemu_runner_source_layout import restored_runner_source

                value = restored_runner_source().encode() if path == "tools/run_qemu_smoke.py" else kernel_layout_source.read_bytes_path(ROOT / path)
                self.assertEqual(expected, digest_bytes(value))

    def test_all_four_classification_dimensions_and_two_host_only_inputs_are_exact(self):
        qemu = [
            "test-qemu-object-model", "test-qemu-endpoint", "test-qemu-grant-syscall",
            "test-qemu-user-runtime", "test-qemu-bootstrap-launcher", "test-qemu-vm-handoff",
            "test-qemu-pm-service", "test-qemu-vm-scratch-preemption", "test-qemu-tty",
            "test-qemu-ramfs", "test-qemu-vfs", "test-qemu-elf-spawn", "test-qemu-vm-ready-early",
            "test-qemu-vm-self-fault", "test-qemu-vm-self-fault-sealed",
            "test-qemu-bootstrap-ready-timeout", "test-qemu-bootstrap-manifest-panic",
            "test-qemu-ipc", "test-qemu-ipc-syscall", "test-qemu-ipc-syscall-panic",
            "test-qemu-frame-ownership", "test-qemu-address-space-handoff", "test-qemu-scheduler",
        ]
        expected = (
            (False, qemu, ["test-ipc-model"], []),
            (True, [], [], []),
            (False, ["test-qemu-object-model"], [], []),
        )
        for family, row in enumerate(alias_rows()):
            self.assertEqual(1, kernel_layout_source.original_path_classification_equivalents(validation_plan.PATH_CLASSIFICATION_EQUIVALENTS).count(row))
            for path in (row[0], *row[1]):
                with self.subTest(path=path):
                    self.assertEqual(expected[family], (
                        validation_plan.requires_full_tier([path]),
                        validation_plan.affected_workflows([path]),
                        validation_plan.affected_slow_models([path]),
                        validation_plan.affected_image_workflows([path]),
                    ))
        for path in (STRUCTURAL_TEST_PATH, SHARED_INSPECTION_PATH):
            self.assertEqual(1, kernel_layout_source.original_host_unit_only_inputs(validation_plan.HOST_UNIT_ONLY_INPUTS).count(path))
            self.assertEqual(
                [validation_plan.UNIT_FAST, *validation_plan.diff_commands("main")],
                validation_plan.plan([path], "fast", "main"),
            )

    def test_complete_recipe_inverse_rejects_unknown_sets_members_operands_and_other_bytes(self):
        text = source("CMakeLists.txt")
        restored = restored_cmake(text)
        self.assertEqual(BASE_CMAKE_DIGEST, digest_bytes(restored.encode()))
        for changed in (
            text.replace("kernel/kernel_object_state.c", "kernel/unknown_object_state.c", 1),
            text.replace("${MICROS_KERNEL_OBJECT_CORE_SOURCES}", "kernel/kernel_objects.c", 1),
            text + "\nconsumer(${MICROS_KERNEL_OBJECT_HOST_TEST_SOURCES})\n",
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                kernel_object_cmake_projection(changed)
        with self.assertRaises(AssertionError):
            restored_cmake(text + "\n# unreviewed recipe byte\n")

    def test_host_fixture_owns_one_original_registry_and_a_nonresetting_getter(self):
        path = family_paths(1)[1]
        text = source(path)
        records = function_records(text)
        self.assertIn("kernel_object_test_registry", records)
        self.assertEqual(1, text.count("static struct micros_kernel_objects objects;"))
        self.assertEqual(
            tokens("{ return &objects; }"),
            tokens("{" + records["kernel_object_test_registry"].body + "}"),
        )
        self.assertEqual(
            tokens("struct micros_kernel_objects *kernel_object_test_registry(void)"),
            tokens(records["kernel_object_test_registry"].signature),
        )
        for owner in family_paths(1):
            expected = ("static struct micros_kernel_objects objects;",) if owner == path else ()
            self.assertEqual(tuple(map(tokens, expected)), tuple(map(tokens, file_scope_objects(source(owner)))))
            self.assertNotRegex(source(owner), r'#include\s*"[^"]*kernel_object_private\.h"')
        completed = (1, "lifecycle_model_matches_registry") not in FUNCTION_TASK_MIGRATION
        case_phases = sum(
            len(spec["phases"]) for name, spec in NATIVE_CASE_TASKS.items() if (1, name) not in FUNCTION_TASK_MIGRATION
        )
        model_operations = 8 * ((1, "test_seeded_lifecycle_model") not in FUNCTION_TASK_MIGRATION)
        self.assertEqual(28 + 3 * completed + case_phases + model_operations, len(owned_records(1)))

    def test_registry_transport_retains_whole_snapshot_reset_extent_and_member_scope(self):
        current = (
            "{ struct micros_kernel_objects *fixture = kernel_object_test_registry(); "
            "struct micros_kernel_objects snapshot = *fixture; reset_objects(fixture); "
            "candidate = &fixture->processes[index]; compare(fixture, sizeof(*fixture)); "
            "if (!lifecycle_model_matches_registry(fixture, &model, step)) { return false; } return true; }"
        )
        expected = (
            "{ struct micros_kernel_objects snapshot = objects; reset_objects(); "
            "candidate = &objects.processes[index]; compare(&objects, sizeof(objects)); "
            "if (!lifecycle_model_matches_registry(&model, step)) { return false; } return true; }"
        )
        self.assertEqual(tokens(expected), native_registry_body_tokens("test_example", tokens(current)))
        for changed in (
            current.replace("fixture->processes", "fixture->unknown"),
            current.replace("fixture->processes", "other.fixture->processes"),
            current.replace("reset_objects(fixture)", "reset_objects(other)"),
            current.replace("lifecycle_model_matches_registry(fixture,", "lifecycle_model_matches_registry(other,"),
            current.replace("struct micros_kernel_objects *fixture", "static struct micros_kernel_objects *fixture"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                native_registry_body_tokens("test_example", tokens(changed))
        for changed in (
            current.replace("sizeof(*fixture)", "sizeof(fixture)"),
            current.replace("return false;", "return true;"),
            current.replace("candidate =", "reset_objects(fixture); candidate ="),
        ):
            with self.subTest(changed=changed):
                self.assertNotEqual(tokens(expected), native_registry_body_tokens("test_example", tokens(changed)))

    def test_original_registry_reset_and_macro_snapshot_are_not_new_bundle_copies(self):
        current = (
            "{ struct micros_kernel_objects expected_objects = *fixture; "
            "enum error actual_error = (expression); "
            "compare(fixture, &expected_objects, sizeof(*fixture)); return false; }"
        )
        expected = (
            "{ struct micros_kernel_objects expected_objects = objects; "
            "enum error actual_error = (expression); "
            "compare(&objects, &expected_objects, sizeof(objects)); return false; }"
        )
        self.assertEqual(tokens(expected), restore_registry_tokens(tokens(current)))
        self.assertEqual(
            tokens("{ memset(&objects, 0, sizeof(objects)); }"),
            restore_registry_tokens(tokens("{ memset(fixture, 0, sizeof(*fixture)); }")),
        )
        for changed in (
            current.replace("*fixture", "*other.fixture"),
            current.replace("compare(fixture,", "compare(&fixture,"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                restore_registry_tokens(tokens(changed))

    def test_host_macros_model_types_and_automatic_case_table_are_actual_original_data(self):
        header = source(PRIVATE_HEADERS[1])
        start = header.index("#define EXPECT_TRUE")
        model_start = header.index("struct lifecycle_model_process {")
        macro = restore_registry_tokens(tokens(header[start:model_start]))
        self.assertEqual(HOST_MACROS_DIGEST, digest_tokens(macro))
        end = header.index("\n};", header.index("struct lifecycle_model {")) + len("\n};")
        self.assertEqual(MODEL_TYPES_DIGEST, digest_tokens(tokens(header[model_start:end])))
        main = function_records(source(family_paths(1)[0]))["main"].body
        table = main[:main.index("    size_t index;")].strip()
        self.assertNotIn("static", tokens(table))
        aliases = normal_names(1)
        self.assertEqual(
            HOST_CASE_TABLE_DIGEST,
            digest_tokens(aliases.get(token, token) for token in tokens(table)),
        )
        self.assertEqual(15, len(re.findall(r'\{\s*"[^"\n]+"', table)))
        for original, (path, record) in original_locations(1).items():
            count = record.body.count("kernel_object_test_registry()")
            self.assertEqual(int(original.startswith("test_")), count, original)
        self.assertEqual({}, c_function_definitions(header))

    def test_host_private_edges_include_callbacks_and_keep_reference_independent(self):
        paths = family_paths(1)
        records = owned_records(1)
        owners = {name: path for path, name in records}
        self.assertEqual(len(records), len(owners))
        allowed = {
            0: {2, 3, 4, 5, 6, 7, 8}, 1: set(), 2: {1}, 3: {1}, 4: {1},
            5: {1}, 6: {1}, 7: {1}, 8: {1, 9, 10, 11}, 9: set(), 10: {1, 9}, 11: {1, 9},
        }
        edges = set()
        exports = set()
        for (path, name), record in records.items():
            referenced = set(re.findall(r"\b[A-Za-z_]\w*\b", c_lexical_text(record.body)))
            for callee in referenced & set(owners):
                dependency = owners[callee]
                if dependency != path:
                    self.assertIn(paths.index(dependency), allowed[paths.index(path)], (name, callee))
                    edges.add((dependency, path))
                    exports.add(callee)
        self.assertEqual(set(paths), set(topological_order(set(paths), edges)))
        declarations = re.findall(
            r"\b(kernel_object_test_\w+)\s*\([^;{}]*\)\s*;", c_lexical_text(source(PRIVATE_HEADERS[1])),
        )
        self.assertEqual(len(declarations), len(set(declarations)))
        self.assertEqual(exports, set(declarations))

    def test_model_projection_exposes_three_bounded_typed_passes(self):
        records = function_records(source(family_paths(1)[10]))
        for name, arguments, _ in MODEL_PROJECTION_PHASES:
            with self.subTest(phase=name):
                self.assertIn(name, records)
                parameters = "const struct micros_kernel_objects *fixture"
                if arguments == ("fixture", "model"):
                    parameters += ", const struct lifecycle_model *model"
                self.assertEqual(
                    tokens("static bool " + name + "(" + parameters + ")"),
                    tokens(records[name].signature),
                )
                self.assertLessEqual(records[name].lines, 120)
        self.assertLessEqual(records[host_name("lifecycle_model_matches_registry")].lines, 120)

    def test_model_projection_preserves_loop_bounds_first_mismatch_and_final_validation(self):
        calls = " || !".join(
            name + "(" + ", ".join(arguments) + ")" for name, arguments, _ in MODEL_PROJECTION_PHASES
        )
        caller = (
            "{ if (bad_header()) { goto mismatch; } if (!" + calls + ") { goto mismatch; } "
            "if (bad_complete_validation()) { goto mismatch; } return true; "
            'mismatch: report("original mismatch"); return false; }'
        )
        phases = tuple(
            "{ size_t index; for (index = 0; index < " + bound
            + "; ++index) { if (bad_" + label + "(index)) { return false; } } return true; }"
            for label, (_, _, bound) in zip(("process", "thread", "hart"), MODEL_PROJECTION_PHASES)
        )
        expected = (
            "{ size_t index; if (bad_header()) { goto mismatch; } "
            "for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) { if (bad_process(index)) { goto mismatch; } } "
            "for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) { if (bad_thread(index)) { goto mismatch; } } "
            "for (index = 0; index < MICROS_HART_CAPACITY; ++index) { if (bad_hart(index)) { goto mismatch; } } "
            "if (bad_complete_validation()) { goto mismatch; } return true; "
            'mismatch: report("original mismatch"); return false; }'
        )
        self.assertEqual(tokens(expected), model_projection_body_tokens(caller, phases))
        for changed in (
            caller.replace("(fixture, model)", "(fixture, other)", 1),
            caller.replace("|| !kernel_object_test_model_threads_match", "&& !kernel_object_test_model_threads_match"),
            caller.replace(") { goto mismatch; } if (bad_complete", ") { return true; } if (bad_complete"),
            caller.replace("{ if (bad_header", "{ size_t index; if (bad_header", 1),
        ):
            self.assertNotEqual(caller, changed)
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                model_projection_body_tokens(changed, phases)
        for changed in (
            phases[0].replace("return false;", "return true;"),
            phases[0].replace("index = 0", "index = 1"),
            phases[0].replace("MICROS_PROCESS_CAPACITY", "MICROS_THREAD_CAPACITY"),
            phases[0].replace("return false;", "goto mismatch;"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                model_projection_body_tokens(caller, (changed, *phases[1:]))
        self.assertNotEqual(
            tokens(expected),
            model_projection_body_tokens(caller.replace("bad_complete_validation", "weakened_validation"), phases),
        )

    def test_hart_stack_installation_exposes_two_typed_conflict_passes_before_publication(self):
        records = function_records(source(CORE_SOURCES[7]))
        for name, arguments, _ in HART_STACK_PHASES:
            with self.subTest(phase=name):
                self.assertIn(name, records)
                parameters = "const struct micros_kernel_objects *objects"
                if "hart" in arguments:
                    parameters += ", const struct micros_hart *hart"
                parameters += (
                    ", uintptr_t primary_stack_bottom, uintptr_t primary_stack_top, "
                    "uintptr_t emergency_stack_bottom, uintptr_t emergency_stack_top"
                )
                self.assertEqual(
                    tokens("static bool " + name + "(" + parameters + ")"),
                    tokens(records[name].signature),
                )
                self.assertLessEqual(records[name].lines, 120)
        self.assertLessEqual(records["micros_hart_install_trap_stacks"].lines, 120)

    def test_hart_stack_inverse_preserves_actual_bytes_counter_bounds_and_publication(self):
        def restore(caller, phases):
            return stack_conflict_body_text("micros_hart_install_trap_stacks", caller, phases)

        caller = """
    struct micros_hart *hart;
    enum micros_kernel_object_error error;

    admit();
    if (
        !kernel_object_hart_stacks_avoid_harts(
            objects,
            hart,
            primary_stack_bottom,
            primary_stack_top,
            emergency_stack_bottom,
            emergency_stack_top
        )
        || !kernel_object_hart_stacks_avoid_threads(
            objects,
            primary_stack_bottom,
            primary_stack_top,
            emergency_stack_bottom,
            emergency_stack_top
        )
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STACK;
    }

    publish();
"""
        phases = (
            "\n    size_t index;\n\n"
            "    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {\n"
            "        if (bad_hart(index)) {\n            return false;\n        }\n    }"
            "\n    return true;\n",
            "\n    size_t index;\n\n"
            "    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {\n"
            "        if (bad_thread(index)) {\n            return false;\n        }\n    }"
            "\n    return true;\n",
        )
        expected = """
    struct micros_hart *hart;
    enum micros_kernel_object_error error;
    size_t index;

    admit();
    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        if (bad_hart(index)) {
            return MICROS_KERNEL_OBJECT_ERROR_STACK;
        }
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        if (bad_thread(index)) {
            return MICROS_KERNEL_OBJECT_ERROR_STACK;
        }
    }

    publish();
"""
        self.assertEqual(expected, restore(caller, phases))
        for changed in (
            caller.replace("            hart,", "            other,"),
            caller.replace("|| !kernel_object_hart_stacks_avoid_threads", "&& !kernel_object_hart_stacks_avoid_threads"),
            caller.replace("return MICROS_KERNEL_OBJECT_ERROR_STACK;", "return MICROS_KERNEL_OBJECT_OK;"),
            caller.replace("    admit();", "    size_t index;\n    admit();"),
        ):
            self.assertNotEqual(caller, changed)
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                restore(changed, phases)
        for changed in (
            phases[0].replace("size_t index", "uint16_t index"),
            phases[0].replace("index = 0", "index = 1"),
            phases[0].replace("MICROS_HART_CAPACITY", "MICROS_THREAD_CAPACITY"),
            phases[0].replace("return false;", "return true;"),
            phases[0].replace("return false;", "goto done;"),
            phases[0].replace("    return true;", "    unchecked();\n    return true;"),
        ):
            self.assertNotEqual(phases[0], changed)
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                restore(caller, (changed, phases[1]))
        self.assertNotEqual(expected, restore(caller.replace("publish();", "publish_early();"), phases))
        self.assertNotEqual(
            expected, restore(caller, (phases[0].replace("bad_hart", "weakened"), phases[1])),
        )
        self.assertNotEqual(
            expected, restore(caller, (phases[0].replace("        if (", "         if ("), phases[1])),
        )

    def test_context_attach_exposes_typed_peer_and_hart_conflicts_before_copy(self):
        records = function_records(source(CORE_SOURCES[6]))
        for name, arguments, _ in CONTEXT_STACK_PHASES:
            with self.subTest(phase=name):
                self.assertIn(name, set(records))
                parameters = "const struct micros_kernel_objects *objects"
                if "thread" in arguments:
                    parameters += ", const struct micros_thread *thread"
                parameters += ", uintptr_t kernel_stack_bottom, uintptr_t kernel_stack_top"
                self.assertEqual(
                    tokens("static bool " + name + "(" + parameters + ")"),
                    tokens(records[name].signature),
                )
        self.assertLess(records["micros_thread_attach_execution_context"].lines, 99)

    def test_context_stack_inverse_preserves_raw_peer_order_and_context_copy(self):
        caller = """
    struct micros_thread *thread;
    enum micros_kernel_object_error error;

    original_admission_and_geometry();
    if (
        !kernel_object_context_stack_avoids_threads(
            objects,
            thread,
            kernel_stack_bottom,
            kernel_stack_top
        )
        || !kernel_object_context_stack_avoids_harts(
            objects,
            kernel_stack_bottom,
            kernel_stack_top
        )
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STACK;
    }

    original_context_copy();
"""
        phases = (
            "\n    size_t index;\n\n"
            "    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {\n"
            "        if (bad_peer(index)) {\n            return false;\n        }\n    }"
            "\n    return true;\n",
            "\n    size_t index;\n\n"
            "    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {\n"
            "        if (bad_hart(index)) {\n            return false;\n        }\n    }"
            "\n    return true;\n",
        )
        expected = """
    struct micros_thread *thread;
    enum micros_kernel_object_error error;
    size_t index;

    original_admission_and_geometry();
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        if (bad_peer(index)) {
            return MICROS_KERNEL_OBJECT_ERROR_STACK;
        }
    }
    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        if (bad_hart(index)) {
            return MICROS_KERNEL_OBJECT_ERROR_STACK;
        }
    }

    original_context_copy();
"""
        original = "micros_thread_attach_execution_context"
        self.assertEqual(expected, stack_conflict_body_text(original, caller, phases))
        for changed in (
            caller.replace("            thread,", "            other,"),
            caller.replace("kernel_stack_bottom,\n", "kernel_stack_top,\n", 1),
            caller.replace("|| !kernel_object_context_stack_avoids_harts", "&& !kernel_object_context_stack_avoids_harts"),
        ):
            self.assertNotEqual(caller, changed)
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                stack_conflict_body_text(original, changed, phases)
        for changed in (
            phases[0].replace("MICROS_THREAD_CAPACITY", "MICROS_HART_CAPACITY"),
            phases[0].replace("return false;", "return true;"),
            phases[0].replace("size_t index;", "unsigned int index;"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                stack_conflict_body_text(original, caller, (changed, phases[1]))
        self.assertNotEqual(
            expected, stack_conflict_body_text(original, caller.replace("original_context_copy", "new_copy"), phases),
        )

    def test_base_validation_exposes_typed_passes_and_original_caller_witnesses(self):
        records = function_records(source(CORE_SOURCES[8]))
        for name, parameters in BASE_VALIDATION_SIGNATURES.items():
            with self.subTest(phase=name):
                self.assertIn(name, set(records))
                self.assertEqual(
                    tokens("static bool " + name + "(" + parameters + ")"),
                    tokens(records[name].signature),
                )
                self.assertLessEqual(records[name].lines, 120)
        root = records["micros_kernel_objects_validate_base"]
        self.assertLessEqual(root.lines, 120)
        for declaration in (
            "size_t process_thread_counts[MICROS_PROCESS_CAPACITY];",
            "bool current_threads[MICROS_THREAD_CAPACITY];",
            "size_t observed_process_count = 0;",
            "size_t observed_thread_count = 0;",
            "size_t observed_hart_count = 0;",
        ):
            self.assertEqual(1, root.body.count(declaration))
            for name in BASE_VALIDATION_SIGNATURES:
                if name in records:
                    self.assertNotIn(declaration, records[name].body)

    def test_base_inverse_preserves_nested_counters_witnesses_continue_and_count_positions(self):
        caller = """
    original_header_and_witness_initialization();
    if (!kernel_object_validation_process_slots_match(objects, &observed_process_count)) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }

    if (!kernel_object_validation_thread_slots_match(objects, process_thread_counts, &observed_thread_count)) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }

    if (!kernel_object_validation_process_counts_match(objects, process_thread_counts)) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }

    if (!kernel_object_validation_hart_slots_match(objects, current_threads, &observed_hart_count)) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    original_final_three_counts_and_policy();
"""
        phases = {
            "kernel_object_validation_process_slots_match": """
    size_t index;

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        if (bad_process(index)) {
            return false;
        }
        ++(*observed_process_count);
    }
    return true;
""",
            "kernel_object_validation_thread_context_matches": """
    size_t other_index;

    if (thread->context_attached) {
        for (other_index = 0; other_index < index; ++other_index) {
            if (bad_peer(other_index)) {
                return false;
            }
        }
    } else if (uncleared_context(thread)) {
        return false;
    }
    return true;
""",
            "kernel_object_validation_thread_slots_match": """
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];
        uint32_t unused_generation;

        switch (thread->slot_state) {
        case MICROS_KERNEL_OBJECT_SLOT_LIVE:
            if (!kernel_object_validation_thread_context_matches(objects, thread, index)) {
                return false;
            }
            ++process_thread_counts[thread->owner.slot];
            ++(*observed_thread_count);
            break;
        default:
            return false;
        }
    }
    return true;
""",
            "kernel_object_validation_process_counts_match": """
    size_t index;

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        if (wrong_thread_count(process_thread_counts[index])) {
            return false;
        }
    }
    return true;
""",
            "kernel_object_validation_free_hart_is_clear": """
    size_t priority;

    if (bad_free_hart(hart)) {
        return false;
    }
    for (priority = 0; priority < MICROS_SCHEDULER_PRIORITY_COUNT; ++priority) {
        if (bad_queue(priority)) {
            return false;
        }
    }
    return true;
""",
            "kernel_object_validation_hart_stacks_avoid_peers": """
    size_t other_index;

    for (other_index = 0; other_index < index; ++other_index) {
        if (bad_hart_stack(other_index)) {
            return false;
        }
    }
    return true;
""",
            "kernel_object_validation_hart_stack_state_matches": """
    if (hart->trap_installed) {
        if (!kernel_object_validation_hart_stacks_avoid_peers(objects, hart, index)) {
            return false;
        }
    } else if (uncleared_stack(hart)) {
        return false;
    }
    return true;
""",
            "kernel_object_validation_current_binding_matches": """
    if (hart->current_thread.generation != 0) {
        if (current_threads[hart->current_thread.slot]) {
            return false;
        }
        current_threads[hart->current_thread.slot] = true;
    }
    return true;
""",
            "kernel_object_validation_hart_slots_match": """
    size_t index;

    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        const struct micros_hart *hart = &objects->harts[index];
        size_t other_index;

        if (hart->slot_state == MICROS_KERNEL_OBJECT_SLOT_FREE) {
            if (!kernel_object_validation_free_hart_is_clear(hart)) {
                return false;
            }
            continue;
        }
        for (other_index = 0; other_index < index; ++other_index) {
            if (duplicate_hart(other_index)) {
                return false;
            }
        }
        if (!kernel_object_validation_hart_stack_state_matches(objects, hart, index)) {
            return false;
        }
        if (!kernel_object_validation_current_binding_matches(objects, hart, current_threads)) {
            return false;
        }
        ++(*observed_hart_count);
    }
    return true;
""",
        }
        expected = """
    original_header_and_witness_initialization();
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        if (bad_process(index)) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        ++observed_process_count;
    }

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];
        uint32_t unused_generation;
        size_t other_index;

        switch (thread->slot_state) {
        case MICROS_KERNEL_OBJECT_SLOT_LIVE:
            if (thread->context_attached) {
                for (other_index = 0; other_index < index; ++other_index) {
                    if (bad_peer(other_index)) {
                        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
                    }
                }
            } else if (uncleared_context(thread)) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            ++process_thread_counts[thread->owner.slot];
            ++observed_thread_count;
            break;
        default:
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
    }

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        if (wrong_thread_count(process_thread_counts[index])) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
    }

    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        const struct micros_hart *hart = &objects->harts[index];
        size_t other_index;
        size_t priority;

        if (hart->slot_state == MICROS_KERNEL_OBJECT_SLOT_FREE) {
            if (bad_free_hart(hart)) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            for (priority = 0; priority < MICROS_SCHEDULER_PRIORITY_COUNT; ++priority) {
                if (bad_queue(priority)) {
                    return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
                }
            }
            continue;
        }
        for (other_index = 0; other_index < index; ++other_index) {
            if (duplicate_hart(other_index)) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
        }
        if (hart->trap_installed) {
            for (other_index = 0; other_index < index; ++other_index) {
                if (bad_hart_stack(other_index)) {
                    return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
                }
            }
        } else if (uncleared_stack(hart)) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        if (hart->current_thread.generation != 0) {
            if (current_threads[hart->current_thread.slot]) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            current_threads[hart->current_thread.slot] = true;
        }
        ++observed_hart_count;
    }
    original_final_three_counts_and_policy();
"""
        self.assertEqual(expected, base_validation_body_text(caller, phases))
        for changed in (
            caller.replace("objects, process_thread_counts,", "objects, other_counts,"),
            caller.replace("&observed_hart_count", "observed_hart_count"),
            caller.replace("objects, current_threads,", "objects, copied_current_threads,"),
            caller.replace("return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;", "return MICROS_KERNEL_OBJECT_OK;", 1),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                base_validation_body_text(changed, phases)
        mutations = (
            ("thread_context_matches", "size_t other_index;", "uint16_t other_index;", True),
            ("free_hart_is_clear", "size_t priority;", "size_t priority = 0;", True),
            ("current_binding_matches", "return false;", "return true;", True),
            ("process_slots_match", "++(*observed_process_count);", "++observed_process_count;", True),
            ("thread_slots_match", "MICROS_THREAD_CAPACITY", "MICROS_PROCESS_CAPACITY", True),
            ("hart_stack_state_matches", "(objects, hart, index)", "(objects, hart, other_index)", True),
            ("current_binding_matches", "] = true;", "] = false;", False),
            ("hart_slots_match", "            continue;", "            return false;", False),
            ("thread_context_matches", "other_index < index", "other_index <= index", False),
            ("thread_slots_match", "++process_thread_counts", "process_thread_counts[0] = 0; ++process_thread_counts", False),
        )
        for suffix, before, after, rejects in mutations:
            name = "kernel_object_validation_" + suffix
            changed = phases[name].replace(before, after, 1)
            self.assertNotEqual(phases[name], changed)
            candidate = {**phases, name: changed}
            with self.subTest(phase=name, mutation=after):
                if rejects:
                    with self.assertRaises(AssertionError):
                        base_validation_body_text(caller, candidate)
                else:
                    self.assertNotEqual(expected, base_validation_body_text(caller, candidate))
        self.assertNotEqual(
            expected, base_validation_body_text(caller.replace("final_three_counts_and_policy", "weakened_final"), phases),
        )

    def test_actual_base_inverse_rejects_changed_types_predicates_witnesses_and_peer_order(self):
        current = {path: source(path) for path in CORE_SOURCES}
        path = CORE_SOURCES[8]
        text = current[path]
        self.assertEqual(BASE_CORE_DIGEST, digest_bytes(restored_kernel_objects_source(current).encode()))
        for before, after in (
            ("size_t *observed_process_count", "uint16_t *observed_process_count"),
            ("const size_t process_thread_counts[MICROS_PROCESS_CAPACITY]", "size_t process_thread_counts[MICROS_PROCESS_CAPACITY]"),
            ("|| !kernel_object_process_handle_is_valid", "|| kernel_object_process_handle_is_valid"),
            ("++(*observed_thread_count);", "++(*observed_thread_count); process_thread_counts[0] = 0;"),
            ("other_index < index", "other_index <= index"),
            ("current_threads[hart->current_thread.slot] = true;", "current_threads[hart->current_thread.slot] = false;"),
            ("            continue;", "            return false;"),
        ):
            changed = text.replace(before, after, 1)
            self.assertNotEqual(text, changed)
            with self.subTest(mutation=after), self.assertRaises(AssertionError):
                restored_kernel_objects_source({**current, path: changed})
        peer = function_records(text)["kernel_object_validation_hart_stacks_avoid_peers"].body
        first_match = re.search(r"other->trap\.primary_stack_bottom,\n +other->trap\.primary_stack_top", peer)
        second_match = re.search(r"other->trap\.emergency_stack_bottom,\n +other->trap\.emergency_stack_top", peer)
        self.assertIsNotNone(first_match)
        self.assertIsNotNone(second_match)
        first, second = first_match.group(), second_match.group()
        changed_peer = peer.replace(first, "__first_pair__", 1).replace(second, first, 1).replace("__first_pair__", second, 1)
        with self.assertRaises(AssertionError):
            restored_kernel_objects_source({**current, path: text.replace(peer, changed_peer, 1)})

    def test_native_cases_expose_exact_typed_original_work_and_tasks(self):
        for original, specification in NATIVE_CASE_TASKS.items():
            owner = source(family_paths(1)[specification["owner"]])
            records = function_records(owner)
            work = specification["work"]
            self.assertIsNotNone(re.search(r"\bstruct " + work + r"\s*\{", owner), work)
            for name in specification["phases"]:
                with self.subTest(case=original, phase=name):
                    self.assertIn(name, set(records))
                    self.assertEqual(
                        tokens("static bool " + name + "(" + case_phase_contract(specification, name)[0] + ")"),
                        tokens(records[name].signature),
                    )
                    self.assertLessEqual(records[name].lines, 120)
            self.assertLessEqual(records[host_name(original)].lines, 120)

    def test_case_work_inverse_preserves_types_uninitialized_fields_sentinels_and_initializer_order(self):
        fields = (
            ("struct handle input;", "aggregate"),
            ("const struct item *sentinel;", "value"),
            ("size_t index;", None),
            ("struct context observed;", "value"),
            ("struct context before;", "value"),
        )
        owner = (
            "struct fixture_work { struct handle input; const struct item *sentinel; "
            "size_t index; struct context observed; struct context before; };"
        )
        declarations = case_work_declarations(owner, "fixture_work", fields)
        caller = (
            "{ struct micros_kernel_objects *fixture = kernel_object_test_registry(); "
            "struct fixture_work work; work.input = (struct handle){0, 1}; "
            "work.sentinel = (const struct item *)(uintptr_t)1; "
            "work.observed = pattern(UINT64_C(0x1000)); work.before = work.observed; "
            "reset(fixture); use(&work.input, &work->sentinel, work.index, "
            "&work.observed, sizeof(work.before)); return true; }"
        )
        expected = (
            "{ struct micros_kernel_objects *fixture = kernel_object_test_registry(); "
            "struct handle input = {0, 1}; const struct item *sentinel = (const struct item *)(uintptr_t)1; "
            "size_t index; struct context observed = pattern(UINT64_C(0x1000)); "
            "struct context before = observed; reset(fixture); "
            "use(&input, &sentinel, index, &observed, sizeof(before)); return true; }"
        )
        self.assertEqual(tokens(expected), restore_case_work_tokens(tokens(caller), "fixture_work", fields, declarations))
        for changed in (
            caller.replace("struct fixture_work work;", "struct fixture_work work = {0};"),
            caller.replace("(struct handle){0, 1}", "(struct other_handle){0, 1}"),
            caller.replace("work.sentinel = (const struct item *)(uintptr_t)1; ", ""),
            caller.replace("work.before = work.observed;", "work.observed = work.before;"),
            caller.replace("reset(fixture);", "memset(&work, 0, sizeof(work)); reset(fixture);"),
            caller.replace("work.index,", "other.work.index,"),
            caller.replace("work.index,", "work.extra,"),
            caller.replace("reset(fixture);", "copy = work; reset(fixture);"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                restore_case_work_tokens(tokens(changed), "fixture_work", fields, declarations)
        for changed in (
            owner.replace("size_t index;", "uint16_t index;"),
            owner.replace("const struct item *sentinel;", "struct item *sentinel;"),
            owner.replace("struct context before;", "struct context before; bool extra;"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                case_work_declarations(changed, "fixture_work", fields)
        self.assertNotEqual(
            tokens(expected),
            restore_case_work_tokens(tokens(caller.replace("(uintptr_t)1", "(uintptr_t)0")), "fixture_work", fields, declarations),
        )

    def test_boolean_case_inverse_preserves_original_scope_and_first_failure(self):
        caller = (
            "{ setup(); if (!first(fixture, &work)) { return false; } "
            "if (!second(fixture, &work)) { return false; } final_validation(); return true; }"
        )
        phases = {
            "first": "{ EXPECT_ERROR(error, create(fixture, &work.output)); return true; }",
            "second": "{ for (work.index = 0; work.index < CAPACITY; ++work.index) { "
            "struct handle owner = work.handles[work.index % 2]; EXPECT_TRUE(owner.slot == work.index); } return true; }",
        }
        calls = (("first", ("fixture", "&work")), ("second", ("fixture", "&work")))
        expected = (
            "{ setup(); EXPECT_ERROR(error, create(fixture, &work.output)); "
            "for (work.index = 0; work.index < CAPACITY; ++work.index) { "
            "struct handle owner = work.handles[work.index % 2]; EXPECT_TRUE(owner.slot == work.index); } "
            "final_validation(); return true; }"
        )
        self.assertEqual(tokens(expected), outline_boolean_case_tokens(caller, phases, calls))
        for changed in (
            caller.replace("first(fixture, &work)", "first(other, &work)"),
            caller.replace("first(fixture, &work)", "first(fixture, &copy)"),
            caller.replace("return false;", "return true;", 1),
            caller.replace("first(", "temporary(").replace("second(", "first(").replace("temporary(", "second("),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                outline_boolean_case_tokens(changed, phases, calls)
        with self.assertRaises(AssertionError):
            outline_boolean_case_tokens(
                caller, {**phases, "first": "{ if (skip()) { return true; } EXPECT_TRUE(check()); return true; }"}, calls,
            )
        self.assertNotEqual(
            tokens(expected), outline_boolean_case_tokens(caller.replace("final_validation", "weakened"), phases, calls),
        )

    def test_case_root_constants_stay_automatic_and_pure_phases_gain_no_registry(self):
        fields = (("struct handle process;", "aggregate"), ("struct registry snapshot;", None))
        declarations = tuple(tokens(field) for field, _ in fields)
        constant = "const uintptr_t root = UINT64_C(0x81000000);"
        caller = (
            "{ struct micros_kernel_objects *fixture = kernel_object_test_registry(); "
            + constant + " struct example_work work; work.process = (struct handle){0}; "
            "EXPECT_ERROR(error, attach(fixture, work.process, root)); work.snapshot = *fixture; return true; }"
        )
        expected = (
            "{ struct micros_kernel_objects *fixture = kernel_object_test_registry(); "
            + constant + " struct handle process = {0}; struct registry snapshot; "
            "EXPECT_ERROR(error, attach(fixture, process, root)); snapshot = *fixture; return true; }"
        )
        self.assertEqual(
            tokens(expected), restore_case_work_tokens(tokens(caller), "example_work", fields, declarations, (constant,)),
        )
        for changed in (
            caller.replace(constant, ""),
            caller.replace("const uintptr_t root", "static const uintptr_t root"),
            caller.replace("UINT64_C(0x81000000)", "UINT64_C(0x82000000)"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                restore_case_work_tokens(tokens(changed), "example_work", fields, declarations, (constant,))
        pure = NATIVE_CASE_TASKS["test_generation_boundaries_and_quarantine"]
        self.assertEqual(
            ("struct kernel_object_test_generation_work *work", ("&work",)),
            case_phase_contract(pure, "kernel_object_test_generation_value_boundaries"),
        )
        phases = {"attach_case": "{ EXPECT_ERROR(error, attach(fixture, work.process, root)); return true; }"}
        calls = (("attach_case", ("fixture", "&work", "root")),)
        checked = "{ if (!attach_case(fixture, &work, root)) { return false; } return true; }"
        self.assertEqual(
            tokens("{ EXPECT_ERROR(error, attach(fixture, work.process, root)); return true; }"),
            outline_boolean_case_tokens(checked, phases, calls),
        )
        with self.assertRaises(AssertionError):
            outline_boolean_case_tokens(checked.replace("&work, root", "&work, root + PAGE_SIZE"), phases, calls)

    def test_context_plan_blocks_remain_distinct_automatic_scopes(self):
        caller = (
            "{ if (!select_case(fixture, &work)) { return false; } "
            "if (!idle_case(fixture, &work)) { return false; } return true; }"
        )
        phases = {
            "select_case": (
                "{ { struct micros_scheduler_return_plan plan; EXPECT_ERROR(OK, admit(fixture)); "
                "EXPECT_ERROR(OK, plan_return(fixture, &plan)); EXPECT_ERROR(OK, commit(fixture, &plan)); } "
                "EXPECT_TRUE(current_is_thread()); return true; }"
            ),
            "idle_case": (
                "{ EXPECT_ERROR(STATE, detach(fixture)); { struct micros_scheduler_return_plan plan; "
                "EXPECT_ERROR(OK, hold(fixture)); EXPECT_ERROR(OK, plan_return(fixture, &plan)); "
                "EXPECT_ERROR(OK, commit(fixture, &plan)); EXPECT_ERROR(OK, remove(fixture)); } "
                "EXPECT_TRUE(current_is_idle()); return true; }"
            ),
        }
        expected = (
            "{ { struct micros_scheduler_return_plan plan; EXPECT_ERROR(OK, admit(fixture)); "
            "EXPECT_ERROR(OK, plan_return(fixture, &plan)); EXPECT_ERROR(OK, commit(fixture, &plan)); } "
            "EXPECT_TRUE(current_is_thread()); EXPECT_ERROR(STATE, detach(fixture)); "
            "{ struct micros_scheduler_return_plan plan; EXPECT_ERROR(OK, hold(fixture)); "
            "EXPECT_ERROR(OK, plan_return(fixture, &plan)); EXPECT_ERROR(OK, commit(fixture, &plan)); "
            "EXPECT_ERROR(OK, remove(fixture)); } EXPECT_TRUE(current_is_idle()); return true; }"
        )
        calls = (("select_case", ("fixture", "&work")), ("idle_case", ("fixture", "&work")))
        self.assertEqual(tokens(expected), outline_boolean_case_tokens(caller, phases, calls))
        changed = phases["select_case"].replace(
            "{ { struct micros_scheduler_return_plan plan;", "{ struct micros_scheduler_return_plan plan;",
        ).replace("commit(fixture, &plan)); }", "commit(fixture, &plan));", 1)
        self.assertNotEqual(
            tokens(expected), outline_boolean_case_tokens(caller, {**phases, "select_case": changed}, calls),
        )
        specification = NATIVE_CASE_TASKS["test_thread_execution_context_lifecycle_is_exact"]
        self.assertEqual(13, len(specification["fields"]))
        self.assertEqual(8, len(specification["constants"]))
        self.assertFalse(any("plan" in tokens(declaration) for declaration, _ in specification["fields"]))

    def test_corruption_macro_actual_bytes_and_file_local_lifetime_are_preserved(self):
        macro = r"""#define EXPECT_CORRUPTION(statement) \
    do { \
        *fixture = work->snapshot; \
        statement; \
        EXPECT_ERROR( \
            MICROS_KERNEL_OBJECT_ERROR_INVARIANT, \
            micros_kernel_objects_validate(fixture) \
        ); \
    } while (false)"""
        spec = NATIVE_CASE_TASKS["test_validator_rejects_corrupt_relationships"]
        definitions = "\n\n".join(
            "static bool " + name + "(void)\n{\n    return true;\n}" for name in spec["phases"]
        )
        root = "bool kernel_object_test_validator_rejects_corrupt_relationships(void)\n{\n    return true;\n}\n"
        owner = macro + "\n\n" + definitions + "\n\n#undef EXPECT_CORRUPTION\n\n" + root
        expected = macro.replace("work->snapshot", "snapshot")
        self.assertEqual(
            (expected, "#undef EXPECT_CORRUPTION"), native_corruption_macro(owner, function_records(owner), spec["phases"]),
        )
        for changed in (
            owner.replace("*fixture = work->snapshot;", "*fixture = *fixture;"),
            owner.replace("MICROS_KERNEL_OBJECT_ERROR_INVARIANT", "MICROS_KERNEL_OBJECT_OK"),
            owner.replace("micros_kernel_objects_validate(fixture)", "weakened_validate(fixture)"),
            owner.replace("#undef EXPECT_CORRUPTION\n\n" + root, root + "\n#undef EXPECT_CORRUPTION\n"),
            owner.replace("\n\n" + definitions, "\n#undef EXPECT_CORRUPTION\n\n" + definitions),
        ):
            self.assertNotEqual(owner, changed)
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                native_corruption_macro(changed, function_records(changed), spec["phases"])
        first = spec["phases"][0]
        changed = owner.replace("#undef EXPECT_CORRUPTION\n\n", "").replace(
            "static bool " + first, "#undef EXPECT_CORRUPTION\n\nstatic bool " + first, 1,
        )
        with self.assertRaises(AssertionError):
            native_corruption_macro(changed, function_records(changed), spec["phases"])

    def test_corruption_tasks_keep_the_original_8_6_7_stream_and_single_snapshot(self):
        spec = NATIVE_CASE_TASKS["test_validator_rejects_corrupt_relationships"]
        owner = source(family_paths(1)[spec["owner"]])
        records = function_records(owner)
        self.assertEqual(
            (8, 6, 7), tuple(records[name].body.count("EXPECT_CORRUPTION(") for name in spec["phases"][1:]),
        )
        self.assertEqual(1, owner.count("work->snapshot = *fixture;"))

    def test_model_runner_exposes_eight_typed_operations_and_only_original_iteration_fields(self):
        records = function_records(source(family_paths(1)[11]))
        for name in MODEL_OPERATION_TASKS:
            with self.subTest(operation=name):
                self.assertIn(name, set(records))
                self.assertEqual(
                    tokens("bool " + name + "(" + MODEL_OPERATION_PARAMETERS + ")"), tokens(records[name].signature),
                )
                self.assertLessEqual(records[name].lines, 120)
        self.assertEqual(4, len(case_work_declarations(source(PRIVATE_HEADERS[1]), MODEL_ITERATION_TAG, MODEL_ITERATION_FIELDS)))
        root = function_records(source(family_paths(1)[8]))[host_name("test_seeded_lifecycle_model")]
        self.assertLessEqual(root.lines, 120)

    def test_model_operation_inverse_preserves_case_locals_and_first_status_output_failure(self):
        phase = (
            "{ struct context context = pattern(BASE + step); struct handle output = {MAX_SLOT, MAX_GENERATION}; "
            "iteration->slot = lowest(model); iteration->expected = expected_error(model, iteration->slot); "
            "iteration->actual = create(fixture, &output); "
            "if (iteration->actual != iteration->expected) { return false; } "
            "if (output.slot != iteration->slot) { return false; } "
            "model->processes[iteration->slot].live = true; ++model->process_count; return true; }"
        )
        expected = (
            "struct context context = pattern(BASE + step); struct handle output = {MAX_SLOT, MAX_GENERATION}; "
            "iteration->slot = lowest(&model); iteration->expected = expected_error(&model, iteration->slot); "
            "iteration->actual = create(fixture, &output); "
            "if (iteration->actual != iteration->expected) { return false; } "
            "if (output.slot != iteration->slot) { return false; } "
            "model.processes[iteration->slot].live = true; ++model.process_count; break;"
        )
        self.assertEqual(tokens(expected), model_operation_body_tokens(phase))
        for changed in (
            phase.replace("return false;", "return true;", 1),
            phase.replace("model->processes", "model->extra"),
            phase.replace("lowest(model)", "lowest(*model)"),
            phase.replace("return true; }", "break; }"),
            phase.replace("return false;", "goto comparison;", 1),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                model_operation_body_tokens(changed)
        self.assertNotEqual(
            tokens(expected), model_operation_body_tokens(phase.replace("output.slot !=", "output.slot ==")),
        )

    def test_model_runner_inverse_preserves_one_draw_numeric_dispatch_and_comparison_after_denials(self):
        caller = (
            "{ enum { MODEL_STEPS = 4096, }; struct lifecycle_model model = {0}; "
            "struct micros_hart_handle harts[2]; uint32_t random_state = UINT32_C(0x7a31d4c9); size_t step; "
            "prepare(fixture, harts); for (step = 0; step < MODEL_STEPS; ++step) { "
            "struct kernel_object_test_model_iteration iteration; iteration.value = model_random(&random_state); "
            "switch (iteration.value % 8) { "
            + " ".join(
                "case " + str(number) + ": if (!" + name
                + "(fixture, &model, step, &iteration)) { return false; } break;"
                for number, name in enumerate(MODEL_OPERATION_TASKS)
            )
            + " } if (!compare(fixture, &model, step)) { return false; } } return true; }"
        )
        phases = {
            name: "{ operation" + str(number)
            + "(fixture, model, step, iteration->value, &iteration->slot, &iteration->expected, &iteration->actual); return true; }"
            for number, name in enumerate(MODEL_OPERATION_TASKS)
        }
        declarations = tuple(tokens(declaration) for declaration, _ in MODEL_ITERATION_FIELDS)
        expected = (
            "{ enum { MODEL_STEPS = 4096, }; struct lifecycle_model model = {0}; "
            "struct micros_hart_handle harts[2]; uint32_t random_state = UINT32_C(0x7a31d4c9); size_t step; "
            "prepare(fixture, harts); for (step = 0; step < MODEL_STEPS; ++step) { "
            "uint32_t value = model_random(&random_state); size_t slot; "
            "enum micros_kernel_object_error expected; enum micros_kernel_object_error actual; switch (value % 8) { "
            "case 0: { operation0(fixture, &model, step, value, &slot, &expected, &actual); break; } "
            "case 1: { operation1(fixture, &model, step, value, &slot, &expected, &actual); break; } "
            "case 2: { operation2(fixture, &model, step, value, &slot, &expected, &actual); break; } "
            "case 3: { operation3(fixture, &model, step, value, &slot, &expected, &actual); break; } "
            "case 4: { operation4(fixture, &model, step, value, &slot, &expected, &actual); break; } "
            "case 5: { operation5(fixture, &model, step, value, &slot, &expected, &actual); break; } "
            "case 6: { operation6(fixture, &model, step, value, &slot, &expected, &actual); break; } "
            "case 7: { operation7(fixture, &model, step, value, &slot, &expected, &actual); break; } "
            "} if (!compare(fixture, &model, step)) { return false; } } return true; }"
        )
        self.assertEqual(tokens(expected), model_runner_body_tokens(caller, phases, declarations))
        for changed in (
            caller.replace("struct kernel_object_test_model_iteration iteration;", "struct kernel_object_test_model_iteration iteration = {0};"),
            caller.replace("(fixture, &model, step, &iteration)", "(fixture, &copy, step, &iteration)", 1),
            caller.replace("(fixture, &model, step, &iteration)", "(fixture, &model, step + 1, &iteration)", 1),
            caller.replace("break; } if (!compare", "default: break; } if (!compare"),
            caller.replace("return false; } break;", "return true; } break;", 1),
        ):
            self.assertNotEqual(caller, changed)
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                model_runner_body_tokens(changed, phases, declarations)
        for changed in (
            caller.replace("model_random(&random_state)", "two_draws(&random_state)"),
            caller.replace("compare(fixture, &model, step)", "weakened(fixture, &model, step)"),
            caller.replace("0x7a31d4c9", "0x7a31d4c8"),
            caller.replace("iteration.value % 8", "iteration.value % 7"),
        ):
            self.assertNotEqual(tokens(expected), model_runner_body_tokens(changed, phases, declarations))

    def test_target_entry_exposes_typed_scenarios_with_root_irq_resources_and_array_bounds(self):
        records = function_records(source(family_paths(2)[1]))
        for name, parameters in TARGET_PARAMETERS.items():
            with self.subTest(scenario=name):
                self.assertIn(name, set(records))
                self.assertEqual(tokens("bool " + name + "(" + parameters + ")"), tokens(records[name].signature))
                self.assertLessEqual(records[name].lines, 120)
        self.assertEqual(11, len(case_work_declarations(source(PRIVATE_HEADERS[2]), TARGET_WORK_TAG, TARGET_WORK_FIELDS)))
        root = function_records(source(family_paths(2)[0]))["micros_kernel_object_runtime_run_self_test"]
        self.assertLessEqual(root.lines, 120)
        for value in (
            "riscv_irq_save()", "riscv_irq_restore(saved_status)", "bool passed = false;",
            "static unsigned char thread_stack[", "static struct micros_user_context context;",
            "sizeof(thread_stack)",
        ):
            self.assertEqual(1, root.body.count(value), value)
        for record in records.values():
            self.assertNotIn("riscv_irq_", record.body)
            self.assertNotIn("sizeof(thread_stack)", record.body)
            self.assertNotIn("micros_kernel_object_runtime_", record.body)

    def test_target_inverse_preserves_actual_array_bounds_one_irq_exit_and_flat_success_only_retirement(self):
        caller = """
{
    struct kernel_object_model_test_work work;
    work.unchanged_thread = (struct micros_thread_handle){UINT16_MAX, UINT32_MAX,};
    work.resolved_process = NULL;
    uintptr_t saved_status;
    bool passed = false;
    static unsigned char thread_stack[STACK_SIZE] __attribute__((aligned(ALIGNMENT)));
    static struct micros_user_context context;
    saved_status = riscv_irq_save();
    work.objects = runtime_registry();
    work.hart = runtime_hart();
    work.hart_handle = runtime_handle();
    if (work.objects == NULL || work.hart == NULL) {
        goto done;
    }
    if (!kernel_object_model_test_create_process_and_thread(&work)) {
        goto done;
    }
    if (!kernel_object_model_test_attach_and_select_thread(
        &work, (uintptr_t)thread_stack, (uintptr_t)thread_stack + sizeof(thread_stack), &context
    )) {
        goto done;
    }
    if (!kernel_object_model_test_retire_thread_and_process(&work) || !kernel_object_model_test_verify_process_reuse(&work)) {
        goto done;
    }
    passed = true;
done:
    riscv_irq_restore(saved_status);
    return passed;
}
"""
        phases = {
            TARGET_TASKS[0]: (
                "{ work->error = create(work->objects, &work->first_process); "
                "if (work->error != OK) { return false; } return true; }"
            ),
            TARGET_TASKS[1]: (
                "{ work->error = attach(work->objects, work->thread, stack_bottom, stack_top, context); "
                "if (work->error != OK) { return false; } return true; }"
            ),
            TARGET_TASKS[2]: (
                "{ if (hold(work->objects, work->thread) != OK || idle(&work->plan) != OK "
                "|| release(work->objects, work->first_process) != OK) { return false; } return true; }"
            ),
            TARGET_TASKS[3]: (
                "{ if (reuse(work->objects, &work->replacement_process) != OK "
                "|| work->replacement_process.generation != work->first_process.generation + 1 "
                "|| resolve(work->objects, work->first_process, &work->resolved_process) != STALE "
                "|| work->resolved_process != NULL || validate(work->objects) != OK) { return false; } return true; }"
            ),
        }
        declarations = tuple(tokens(declaration) for declaration, _ in TARGET_WORK_FIELDS)
        expected = """
{
    struct micros_kernel_objects *objects;
    struct micros_hart *hart;
    struct micros_hart_handle hart_handle;
    struct micros_process_handle first_process;
    struct micros_process_handle replacement_process;
    struct micros_thread_handle thread;
    struct micros_thread_handle unchanged_thread = {UINT16_MAX, UINT32_MAX,};
    struct micros_thread_handle current;
    struct micros_scheduler_return_plan plan;
    const struct micros_process *resolved_process = NULL;
    enum micros_kernel_object_error error;
    uintptr_t saved_status;
    bool passed = false;
    static unsigned char thread_stack[STACK_SIZE] __attribute__((aligned(ALIGNMENT)));
    static struct micros_user_context context;
    saved_status = riscv_irq_save();
    objects = runtime_registry();
    hart = runtime_hart();
    hart_handle = runtime_handle();
    if (objects == NULL || hart == NULL) {
        goto done;
    }
    error = create(objects, &first_process);
    if (error != OK) {
        goto done;
    }
    error = attach(objects, thread, (uintptr_t)thread_stack, (uintptr_t)thread_stack + sizeof(thread_stack), &context);
    if (error != OK) {
        goto done;
    }
    if (hold(objects, thread) != OK || idle(&plan) != OK || release(objects, first_process) != OK
        || reuse(objects, &replacement_process) != OK || replacement_process.generation != first_process.generation + 1
        || resolve(objects, first_process, &resolved_process) != STALE || resolved_process != NULL || validate(objects) != OK) {
        goto done;
    }
    passed = true;
done:
    riscv_irq_restore(saved_status);
    return passed;
}
"""
        self.assertEqual(tokens(expected), target_body_tokens(caller, phases, declarations))
        for changed in (
            caller.replace("sizeof(thread_stack)", "sizeof(stack_pointer)"),
            caller.replace(", &context", ", context"),
            caller.replace("struct kernel_object_model_test_work work;", "struct kernel_object_model_test_work work = {0};"),
            caller.replace("|| !kernel_object_model_test_verify_process_reuse", "&& !kernel_object_model_test_verify_process_reuse"),
            caller.replace("(&work))", "(&copy))", 1),
        ):
            self.assertNotEqual(caller, changed)
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                target_body_tokens(changed, phases, declarations)
        for name, changed in (
            (TARGET_TASKS[0], phases[TARGET_TASKS[0]].replace("return false;", "return true;")),
            (TARGET_TASKS[1], phases[TARGET_TASKS[1]].replace("return false;", "goto done;")),
            (
                TARGET_TASKS[2],
                "{ if (first_bad()) { return false; } if (second_bad()) { return false; } return true; }",
            ),
        ):
            with self.subTest(scenario=name), self.assertRaises(AssertionError):
                target_body_tokens(caller, {**phases, name: changed}, declarations)
        for changed in (
            caller.replace("riscv_irq_restore(saved_status);", ""),
            caller.replace("bool passed = false", "bool passed = true"),
            caller.replace("static struct micros_user_context", "struct micros_user_context"),
        ):
            self.assertNotEqual(tokens(expected), target_body_tokens(changed, phases, declarations))
        self.assertNotEqual(
            tokens(expected),
            target_body_tokens(
                caller, {**phases, TARGET_TASKS[3]: phases[TARGET_TASKS[3]].replace("!= STALE", "!= OK")}, declarations,
            ),
        )

    def test_final_migrations_are_literal_empty_and_all_23_owners_have_bounded_real_tasks(self):
        tree = ast.parse(source(STRUCTURAL_TEST_PATH))
        assignments = {
            target.id: node.value for node in tree.body if isinstance(node, ast.Assign)
            for target in node.targets if isinstance(target, ast.Name)
        }
        for name in ("SYMBOL_OWNER_MIGRATION", "FUNCTION_TASK_MIGRATION"):
            self.assertIsInstance(assignments[name], ast.Tuple)
            self.assertEqual([], assignments[name].elts)
        for family in SOURCE_SETS.values():
            for path in family:
                records = function_records(source(path))
                self.assertTrue(records, path)
                for record in records.values():
                    self.assertLessEqual(record.lines, 120, (path, record.name))

    def test_target_private_edges_are_exact_and_add_no_runtime_or_static_storage_owner(self):
        root_path, scenarios_path = family_paths(2)
        records = function_records(source(scenarios_path))
        header = source(PRIVATE_HEADERS[2])
        declared = re.findall(r"\b(kernel_object_model_test_\w+)\s*\([^;{}]*\)\s*;", c_lexical_text(header))
        self.assertEqual(set(TARGET_TASKS), set(declared))
        self.assertEqual(4, len(declared))
        root = function_records(source(root_path))["micros_kernel_object_runtime_run_self_test"]
        for name in TARGET_TASKS:
            self.assertEqual(1, tokens(root.body).count(name))
        for record in records.values():
            self.assertFalse(set(TARGET_TASKS) & set(tokens(record.body)))
            self.assertNotIn("thread_stack", tokens(record.body))
            self.assertNotIn("saved_status", tokens(record.body))
            self.assertNotIn("passed", tokens(record.body))
        self.assertEqual((), file_scope_objects(source(scenarios_path)))
        self.assertEqual((), file_scope_objects(header))
        self.assertEqual({}, c_function_definitions(header))


if __name__ == "__main__":
    unittest.main()
