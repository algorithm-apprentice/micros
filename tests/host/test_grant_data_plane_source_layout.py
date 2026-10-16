"""Bounded source-contract checks for Accepted ADR-0069."""

import ast
import hashlib
import json
from pathlib import Path
import re
import unittest

from tests.host.c_source_records import function_records, matching_lexical
from tests.host.test_frame_ownership_source_layout import c_function_definitions, c_lexical_text, tokens
from tests.host.test_pm_service_source_layout import cmake_commands
from tests.host.test_vm_service_source_layout import file_scope_objects
from tools import validation_context, validation_plan
from tests.host import kernel_layout_source


ROOT = Path(__file__).resolve().parents[2]
REVIEWED_BASE = "6269d9247de6beafed2448aa0ffd6904de5e8833"
BASE_CMAKE_DIGEST = "44d8a8615e5465ad199ed916b30e72af05643ef00ae5ac9a7df00a0dc4cad2c1"
STRUCTURAL_TEST_PATH = "tests/host/test_grant_data_plane_source_layout.py"
COPY_SOURCES = ("kernel/grant_copy.c", "kernel/grant_copy_range.c")
SYSCALL_TEST_SOURCES = (
    "kernel/grant_syscall_test.c",
    "kernel/grant_syscall_test_fixture.c",
    "kernel/grant_syscall_test_commands.c",
    "kernel/grant_syscall_test_copy_commands.c",
    "kernel/grant_syscall_test_transitions.c",
    "kernel/grant_syscall_test_effects.c",
    "kernel/grant_syscall_test_hooks.c",
    "kernel/grant_syscall_test_lifecycle.c",
)
HOST_SOURCES = (
    "tests/host/grant_test.c",
    "tests/host/grant_test_fixture.c",
    "tests/host/grant_lifecycle_test.c",
    "tests/host/grant_validation_test.c",
    "tests/host/grant_model_test.c",
    "tests/host/grant_model_reference.c",
    "tests/host/grant_model_operations.c",
    "tests/host/grant_model_copy.c",
)
FAMILY_PATHS = (
    COPY_SOURCES, ("kernel/grant_syscall.c",), ("kernel/grant_test.c",),
    SYSCALL_TEST_SOURCES, HOST_SOURCES,
    ("tests/host/grant_copy_test.c",), ("tests/host/grant_syscall_core_test.c",),
)
SOURCE_SETS = {
    "MICROS_GRANT_COPY_TARGET_SOURCES": COPY_SOURCES,
    "MICROS_GRANT_HOST_TEST_SOURCES": HOST_SOURCES,
    "MICROS_GRANT_SYSCALL_QEMU_TEST_SOURCES": SYSCALL_TEST_SOURCES,
}
PRIVATE_HEADERS = (
    "kernel/grant_copy_internal.h",
    "tests/host/grant_test_internal.h",
    "kernel/grant_syscall_test_internal.h",
)
COPY_EXECUTION_TAG = "grant_copy_execution"
COPY_EXECUTION_PHASE = "grant_copy_prepare_participant_plans"
COPY_LOCAL_RANGE_PHASE = "grant_copy_local_range_is_valid"
RANGE_DEVICE_PHASE = "grant_copy_plan_device_span"
RANGE_DEVICE_PARAMETERS = (
    "struct micros_process_handle process, const struct micros_tty_device_authority *device_authority, "
    "uintptr_t *current, size_t *remaining, bool *device_fault, "
    "enum micros_user_address_space_error *address_error, size_t *chunk_length"
)
VALIDATION_WORK_TAG = "grant_copy_validation"
VALIDATION_PHASE = "grant_copy_prepare_remote_plan"
VALIDATION_FIELDS = (
    ("const struct micros_frame_ownership *ledger;", False),
    ("struct micros_grant_copy_authority authority;", False),
    ("struct micros_grant_copy_range_plan remote_plan;", True),
    ("struct micros_tty_device_authority device_authority;", False),
    ("enum micros_grant_error error;", False),
    ("enum micros_tty_device_authority_status device_status;", False),
)
VALIDATION_PARAMETERS = (
    "struct grant_copy_validation *work, const struct micros_endpoint_registry *endpoint_registry, "
    "const struct micros_kernel_objects *objects, size_t length, uint32_t required_permission"
)
SYSCALL_WORK_TAG = "grant_syscall_execution"
SYSCALL_DISPATCH_PHASE = "grant_syscall_execute_request"
SYSCALL_CREATE_PHASE = "grant_syscall_create_request"
SYSCALL_PHASE_PARAMETERS = (
    "struct micros_hart *hart, struct micros_trap_frame *frame, const struct micros_syscall_context *context, "
    "struct grant_syscall_execution *work"
)
SYSCALL_FIELDS = (
    ("struct micros_grant_registry *grant_registry;", False),
    ("struct micros_endpoint_registry *endpoint_registry;", False),
    ("struct micros_grant_create_plan create_plan;", False),
    ("struct micros_grant_syscall_request request;", False),
    ("struct micros_tty_device_authority device_authority;", False),
    ("micros_grant_t created;", "value"),
    ("enum micros_syscall_abi_result decode_result;", False),
    ("enum micros_grant_error error;", False),
    ("enum micros_tty_device_authority_status device_status;", False),
    ("uint64_t abi_result;", False),
)
COPY_EXECUTION_FIELDS = (
    ("const struct micros_frame_ownership *ledger;", False),
    ("struct micros_grant_copy_authority authority;", False),
    ("struct micros_grant_copy_range_plan remote_plan;", True),
    ("struct micros_grant_copy_range_plan local_plan;", True),
    ("struct micros_tty_device_authority device_authority;", False),
    ("const struct micros_grant_copy_range_plan *source_plan;", False),
    ("const struct micros_grant_copy_range_plan *destination_plan;", False),
    ("enum micros_grant_error remote_error;", False),
    ("enum micros_grant_error local_error;", False),
    ("enum micros_grant_error error;", False),
    ("enum micros_tty_device_authority_status device_status;", False),
)
NATIVE_FIXTURE_TAG = "grant_test_fixture"
NATIVE_FIXTURE_GETTER = "grant_test_context"
NATIVE_FIELDS = (
    ("struct micros_endpoint_registry endpoint_registry;", False),
    ("struct micros_grant_registry grant_registry;", False),
    ("struct micros_kernel_objects objects;", False),
    ("struct micros_process_handle processes[GRANT_PROCESS_COUNT];", False),
    ("struct micros_thread_handle threads[GRANT_PROCESS_COUNT];", False),
    ("micros_endpoint_t endpoints[GRANT_PROCESS_COUNT];", False),
    ("struct micros_hart_handle hart;", False),
    ("struct grant_model_trace_entry model_trace[GRANT_MODEL_TRACE_COUNT];", False),
    ("size_t model_trace_count;", False),
)
NATIVE_FIELD_NAMES = (
    "endpoint_registry", "grant_registry", "objects", "processes", "threads",
    "endpoints", "hart", "model_trace", "model_trace_count",
)
NATIVE_CONTEXT_HELPERS = {
    "setup_fixture": True, "recreate_process": False, "expect_create_failure_unchanged": False,
    "record_model_trace": False, "model_fail": False, "cancel_plan_matches_reference": False,
}
NATIVE_CONTEXT_ROOTS = (
    "test_create_inspect_revoke_and_reuse", "test_create_failures_and_capacity",
    "test_terminal_generation_quarantines", "test_endpoint_cancellation_and_reuse",
    "test_failed_ipc_close_preserves_cancel_plan", "test_error_precedence_and_output_preservation",
    "test_validator_rejects_corruption", "test_grant_model_boundaries", "test_grant_model",
)
NATIVE_TYPES_DIGEST = "471822c9669476abc88e7c5504c3e6257a651f2e3ef6474108f8418dc6201f51"
NATIVE_ASSERTIONS_DIGEST = "e5644256b70b07a218fca2f41a93824ca4f75c66610f08825bed1d04e416d11a"
NATIVE_CASE_OUTLINES = {
    "test_create_inspect_revoke_and_reuse": (
        HOST_SOURCES[2], "grant_test_create_case",
        (
            ("struct micros_grant_record observed;", False),
            ("struct micros_grant_record sentinel;", False),
            ("struct micros_grant_registry snapshot;", False),
            ("micros_grant_t first;", "value"),
            ("micros_grant_t second;", "value"),
        ),
        (
            "grant_test_create_shape_and_setup", "grant_test_create_record",
            "grant_test_create_foreign_denials", "grant_test_create_revoke_and_reuse",
        ),
    ),
    "test_endpoint_cancellation_and_reuse": (
        HOST_SOURCES[2], "grant_test_cancel_case",
        (
            ("struct micros_grant_cancel_plan plan;", False),
            ("struct micros_grant_cancel_plan corrupted;", False),
            ("struct micros_grant_registry snapshot;", False),
            ("struct micros_grant_registry foreign_registry;", False),
            ("struct micros_grant_record observed;", False),
            ("struct micros_process_handle old_process;", False),
            ("micros_grant_t from_closing;", False),
            ("micros_grant_t to_closing;", False),
            ("micros_grant_t unrelated;", False),
        ),
        (
            "grant_test_cancel_create_participants", "grant_test_cancel_prepare_source",
            "grant_test_cancel_reject_foreign_plans", "grant_test_cancel_commit_and_survivor",
            "grant_test_cancel_generation_reuse",
        ),
    ),
    "test_error_precedence_and_output_preservation": (
        HOST_SOURCES[3], "grant_test_precedence_case",
        (
            ("struct micros_grant_cancel_plan plan;", False),
            ("struct micros_grant_cancel_plan plan_sentinel;", False),
            ("struct micros_grant_record record;", False),
            ("struct micros_grant_record record_sentinel;", False),
            ("struct micros_grant_registry valid;", False),
            ("struct micros_process_handle stale_process;", False),
            ("micros_grant_t grant;", False),
            ("micros_grant_t stale_grant;", False),
        ),
        (
            "grant_test_precedence_range_before_invariant", "grant_test_precedence_record_output",
            "grant_test_precedence_stale_shape", "grant_test_precedence_cancel_shape",
            "grant_test_precedence_closed_endpoint",
        ),
    ),
    "test_grant_model_boundaries": (
        HOST_SOURCES[4], "grant_test_boundary_case",
        (
            ("struct micros_grant_registry expected;", False),
            ("micros_grant_t terminal;", False),
            ("size_t index;", False),
        ),
        (
            "grant_test_boundary_fill_registry", "grant_test_boundary_capacity_output",
            "grant_test_boundary_quarantine_prepare", "grant_test_boundary_terminal_revoke",
        ),
    ),
}
NATIVE_FIXTURE_ONLY_PHASES = ("grant_test_boundary_capacity_output",)
COPY_CASE_OUTLINES = {
    "test_copy_authority_and_bounds": (
        "grant_copy_authority_case",
        (
            ("struct micros_grant_copy_authority authority;", False),
            ("micros_grant_t read_grant;", False),
            ("micros_grant_t write_grant;", False),
            ("micros_grant_t both_grant;", False),
        ),
        (
            "grant_copy_test_directional_grants", "grant_copy_test_authority_and_end_zero",
            "grant_copy_test_participant_bounds_shape", "grant_copy_test_revoked_precedence",
        ),
    ),
    "test_copy_plan_and_commit": (
        "grant_copy_plan_case",
        (
            ("unsigned char source[16];", False),
            ("unsigned char destination[16];", False),
            ("unsigned char unchanged[16];", False),
            ("struct micros_grant_copy_range_plan source_plan;", True),
            ("struct micros_grant_copy_range_plan destination_plan;", True),
            ("size_t index;", False),
        ),
        (
            "grant_copy_test_prepare_chunks", "grant_copy_test_commit_bytes",
            "grant_copy_test_reject_physical_plans",
        ),
    ),
}
COPY_TEST_PREFIX_DIGEST = "fd0ea2f64d076a9bac07b143b51da77591218e477dbd6e91bd1168c1789f7a0c"
ABI_TEST_PREFIX_DIGEST = "aa1a887385bd5f3887691e793b430882e527eb795a7d11d44152c2b2e032b86c"
ABI_WORK_TAG = "grant_syscall_abi_case"
ABI_FIELDS = (
    ("struct micros_syscall_arguments arguments;", True),
    ("struct micros_grant_syscall_request request;", False),
    ("uint64_t output;", False),
    ("size_t index;", False),
)
ABI_PHASES = (
    "grant_abi_test_numbering", "grant_abi_test_recoverable_mapping",
    "grant_abi_test_fatal_mapping", "grant_abi_test_valid_full_width",
    "grant_abi_test_validate_shapes", "grant_abi_test_create_shapes",
    "grant_abi_test_revoke_shapes", "grant_abi_test_copy_and_null_shapes",
)
MODEL_RUN_TAG = "grant_model_run"
MODEL_ITERATION_TAG = "grant_model_iteration"
MODEL_AUTH_TAG = "grant_model_authorization"
MODEL_RUN_FIELDS = (
    ("struct micros_grant_registry expected;", False),
    ("uint16_t expected_process_slot[GRANT_PROCESS_COUNT];", False),
    ("uint32_t expected_process_generation[GRANT_PROCESS_COUNT];", False),
    ("micros_endpoint_t expected_endpoint[GRANT_PROCESS_COUNT];", False),
    ("unsigned char grantor_bytes[64];", False),
    ("unsigned char grantee_bytes[64];", False),
    ("unsigned char expected_grantor_bytes[64];", False),
    ("unsigned char expected_grantee_bytes[64];", False),
    ("uint32_t random_state;", "value"),
)
MODEL_ITERATION_FIELDS = (
    ("uint32_t random;", "value"),
    ("enum grant_model_operation operation;", "value"),
    ("size_t grantor;", "value"),
    ("size_t grantee;", "value"),
    ("size_t slot;", "value"),
    ("micros_grant_t grant;", "value"),
    ("size_t trace_offset;", "value"),
    ("size_t trace_length;", "value"),
    ("uint32_t trace_permission;", "value"),
    ("enum micros_grant_error actual;", False),
)
MODEL_AUTH_FIELDS = (
    ("struct micros_grant_registry snapshot;", "value"),
    ("struct micros_grant_copy_authority authority;", False),
    ("struct micros_grant_copy_authority sentinel;", False),
    ("struct micros_grant_copy_range_plan source_plan;", True),
    ("struct micros_grant_copy_range_plan destination_plan;", True),
    ("const struct micros_grant_record *record;", False),
    ("struct micros_process_handle candidate_grantee;", "value"),
    ("micros_endpoint_t candidate_grantor;", "value"),
    ("enum micros_grant_error expected_result;", False),
    ("size_t selected_slot;", "value"),
    ("size_t record_grantor;", "value"),
    ("size_t record_grantee;", "value"),
    ("size_t index;", False),
)
MODEL_PARAMETERS = (
    "struct grant_test_fixture *fixture, struct grant_model_run *run, "
    "struct grant_model_iteration *iteration, size_t step"
)
MODEL_OPERATIONS = (
    ("GRANT_MODEL_CREATE", "grant_test_model_create", True),
    ("GRANT_MODEL_REVOKE", "grant_test_model_revoke", True),
    ("GRANT_MODEL_INSPECT", "grant_test_model_inspect", True),
    ("GRANT_MODEL_CANCEL", "grant_test_model_cancel", True),
    ("GRANT_MODEL_CLOSE_REUSE", "grant_test_model_close_reuse", True),
    ("GRANT_MODEL_MALFORMED_TOKEN", "grant_test_model_malformed_token", False),
    ("GRANT_MODEL_PLAN_FAILURE", "grant_test_model_plan_failure", True),
    ("GRANT_MODEL_INVALID_CREATE", "grant_test_model_invalid_create", False),
)
MODEL_AUTH_LABELS = (
    "GRANT_MODEL_AUTHORIZE_FROM", "GRANT_MODEL_AUTHORIZE_TO", "GRANT_MODEL_AUTHORIZE_DENIAL",
    "GRANT_MODEL_AUTHORIZE_STALE", "GRANT_MODEL_AUTHORIZE_BOUNDS",
)
MODEL_SELECT = "grant_test_model_select_operation"
MODEL_DISPATCH = "grant_test_model_apply_operation"
MODEL_COMPARE = "grant_test_model_compare_state"
MODEL_AUTHORIZE = "grant_test_model_authorize"
MODEL_AUTH_PHASES = (
    "grant_test_model_select_reference", "grant_test_model_expect_authority",
    "grant_test_model_select_participants", "grant_test_model_observe_authority",
    "grant_test_model_denial_outputs", "grant_test_model_authority_output",
    "grant_test_model_copy_bytes",
)
MODEL_AUTH_PARAMETERS = {
    MODEL_AUTH_PHASES[0]: (
        "static void", "struct grant_model_run *run, struct grant_model_iteration *iteration, "
        "struct grant_model_authorization *work", "run, iteration, &work",
    ),
    MODEL_AUTH_PHASES[1]: (
        "static bool", "struct grant_test_fixture *fixture, struct grant_model_run *run, "
        "struct grant_model_iteration *iteration, struct grant_model_authorization *work, size_t step",
        "fixture, run, iteration, &work, step",
    ),
    MODEL_AUTH_PHASES[2]: (
        "static bool", "struct grant_test_fixture *fixture, struct grant_model_iteration *iteration, "
        "struct grant_model_authorization *work, size_t step", "fixture, iteration, work, step",
    ),
    MODEL_AUTH_PHASES[3]: (
        "static bool", "struct grant_test_fixture *fixture, struct grant_model_iteration *iteration, "
        "struct grant_model_authorization *work, size_t step", "fixture, iteration, &work, step",
    ),
    MODEL_AUTH_PHASES[4]: (
        "static bool", "struct grant_test_fixture *fixture, struct grant_model_run *run, "
        "struct grant_model_authorization *work, size_t step", "fixture, run, &work, step",
    ),
    MODEL_AUTH_PHASES[5]: (
        "static bool", "struct grant_test_fixture *fixture, struct grant_model_iteration *iteration, "
        "struct grant_model_authorization *work, size_t step", "fixture, iteration, &work, step",
    ),
    MODEL_AUTH_PHASES[6]: (
        "static bool", "struct grant_test_fixture *fixture, struct grant_model_run *run, "
        "struct grant_model_iteration *iteration, struct grant_model_authorization *work, size_t step",
        "fixture, run, iteration, &work, step",
    ),
}
MECHANISM_TAG = "grant_mechanism_case"
MECHANISM_FIELDS = (
    ("const struct micros_frame_ownership *ledger;", False),
    ("struct micros_kernel_objects *objects;", False),
    ("struct micros_endpoint_registry *endpoint_registry;", False),
    ("struct micros_grant_registry *grant_registry;", False),
    ("struct micros_process_handle processes[GRANT_TEST_PROCESS_COUNT];", False),
    ("struct micros_process_handle replacement;", False),
    ("micros_endpoint_t endpoints[GRANT_TEST_PROCESS_COUNT];", False),
    ("micros_endpoint_t replacement_endpoint;", False),
    ("struct micros_grant_cancel_plan cancel_plan;", False),
    ("micros_grant_t read_grant;", False),
    ("micros_grant_t write_grant;", False),
    ("micros_grant_t remote_read_only_grant;", False),
    ("micros_grant_t unmapped_grant;", False),
    ("micros_grant_t cross_remote_permission_grant;", False),
    ("micros_grant_t cross_remote_unmapped_grant;", False),
    ("micros_grant_t stale_grant;", False),
    ("size_t baseline_processes;", False),
    ("size_t baseline_threads;", False),
    ("uint64_t baseline_owned;", False),
    ("uint64_t baseline_free;", False),
    ("micros_endpoint_t old_grantor_endpoint;", False),
    ("size_t process_index;", False),
    ("size_t page_index;", False),
    ("uint64_t failure_stage;", "value"),
    ("unsigned char wrong_local_before;", False),
    ("unsigned char grantor_remote_before;", False),
    ("unsigned char grantee_local_before;", False),
    ("unsigned char grantor_read_only_before;", False),
    ("unsigned char grantee_read_only_before;", False),
    ("enum micros_frame_ownership_phase saved_phase;", False),
)
MECHANISM_PHASES = (
    "grant_test_mechanism_prepare_processes", "grant_test_mechanism_create_grants",
    "grant_test_mechanism_cross_from", "grant_test_mechanism_cross_to_and_local",
    "grant_test_mechanism_prepare_fault_canaries", "grant_test_mechanism_cross_faults",
    "grant_test_mechanism_unmapped_local_fault", "grant_test_mechanism_authority_range_faults",
    "grant_test_mechanism_phase_and_stale_grant", "grant_test_mechanism_stale_endpoint",
    "grant_test_mechanism_cancel_close_reuse", "grant_test_mechanism_cleanup_baseline",
)
MECHANISM_DENIALS = (
    "grant_test_mechanism_authority_denials", "grant_test_mechanism_range_denials",
    "grant_test_mechanism_mapping_zero_and_outputs",
)
MECHANISM_PREFIX_DIGEST = "a7310b3d2640261410dd4fea964cbea7c11e5cb2934d4c17c255069b13b5ade1"
TARGET_FIXTURE_TAG = "grant_syscall_test_fixture"
TARGET_FIXTURE_GETTER = "grant_syscall_test_context"
TARGET_FIELDS = (
    ("struct micros_process_handle processes[GRANT_SYSCALL_PROCESS_COUNT];", False),
    ("struct micros_thread_handle threads[GRANT_SYSCALL_PROCESS_COUNT];", False),
    ("micros_endpoint_t endpoints[GRANT_SYSCALL_PROCESS_COUNT];", False),
    ("bool thread_live[GRANT_SYSCALL_PROCESS_COUNT];", False),
    ("bool address_space_live[GRANT_SYSCALL_PROCESS_COUNT];", False),
    ("uint64_t user_physical[GRANT_SYSCALL_PROCESS_COUNT][GRANT_SYSCALL_PAGE_COUNT];", False),
    ("struct grant_syscall_script scripts[GRANT_SYSCALL_PROCESS_COUNT];", False),
    ("struct micros_endpoint_registry *endpoint_registry;", False),
    ("struct micros_grant_registry *grant_registry;", False),
    ("micros_grant_t read_grant;", False),
    ("micros_grant_t write_grant;", False),
    ("micros_grant_t remote_read_only_grant;", False),
    ("micros_grant_t unmapped_grant;", False),
    ("micros_grant_t stale_grant;", False),
    ("micros_endpoint_t old_grantor_endpoint;", False),
    ("enum grant_syscall_control_action control_action;", False),
    ("size_t control_next_actor;", False),
    ("enum grant_syscall_command control_next_command;", False),
    ("size_t dispatch_actor;", "value"),
    ("uint64_t baseline_owned;", False),
    ("uint64_t baseline_free;", False),
    ("size_t baseline_processes;", False),
    ("size_t baseline_threads;", False),
    ("size_t baseline_harts;", False),
    ("struct micros_frame_ownership ownership_snapshot;", False),
    ("struct micros_frame_allocator allocator_snapshot;", False),
    ("struct micros_kernel_objects objects_snapshot;", False),
    ("struct micros_kernel_objects objects_observed;", False),
    ("struct micros_endpoint_registry endpoint_snapshot;", False),
    ("struct micros_grant_registry grant_snapshot;", False),
    ("bool root_snapshot[GRANT_SYSCALL_PROCESS_COUNT];", False),
    ("bool root_observed[GRANT_SYSCALL_PROCESS_COUNT];", False),
    ("unsigned char page_table_snapshot[GRANT_SYSCALL_PROCESS_COUNT][3][MICROS_SV39_PAGE_SIZE];", False),
    ("unsigned char page_table_observed[GRANT_SYSCALL_PROCESS_COUNT][3][MICROS_SV39_PAGE_SIZE];", False),
    ("uint64_t user_physical_snapshot[GRANT_SYSCALL_PROCESS_COUNT][GRANT_SYSCALL_PAGE_COUNT];", False),
    ("unsigned char user_bytes_snapshot[GRANT_SYSCALL_PROCESS_COUNT][GRANT_SYSCALL_PAGE_COUNT][MICROS_SV39_PAGE_SIZE];", False),
    ("unsigned char user_bytes_observed[GRANT_SYSCALL_PROCESS_COUNT][GRANT_SYSCALL_PAGE_COUNT][MICROS_SV39_PAGE_SIZE];", False),
    ("bool failure_snapshot_active;", False),
)
TARGET_CONTEXT_HELPERS = {
    "arm_command": False, "arm_raw": False, "capture_page_tables": False, "close_endpoint": False,
    "current_actor": False, "endpoint_registry_is_empty": True, "failure_state_matches": True,
    "pattern_matches": False, "physical_for": False, "prepare_process": False, "range_is_value": False,
    "release_address_space": False, "release_thread": False, "reuse_grantor": True,
    "schedule_finish": False, "schedule_next": False, "schedule_reuse": False, "scheduler_state_valid": False,
    "snapshot_failure_state": True, "snapshot_user_bytes": True, "transition_after_return": False,
    "user_bytes_match": True, "validate_command_effect": False, "write_pattern": False,
}
TARGET_CONTEXT_ROOTS = (
    "micros_grant_syscall_runtime_run_self_test", "micros_grant_syscall_test_finish",
    "micros_grant_syscall_test_before_ecall", "micros_grant_syscall_test_after_dispatch",
    "micros_grant_syscall_test_after_return", "micros_grant_syscall_test_handle_control_trap",
)
TARGET_SAVED_OBJECTS = tuple(
    "uintptr_t micros_grant_syscall_test_saved_" + register + ";" for register in ("sp", "gp", "tp")
)
TARGET_TYPES_DIGEST = "126262b93b4bcb61beaa1cc8ec449c5626b9dae669d24c5bd711b3f56a098ce1"
TARGET_CONSTANTS_DIGEST = "deded705274f60d65ef06aae677c737cf0e2026971b38e6ec04b6e47077299e7"
TARGET_EXTERNS_DIGEST = "2da61971c68bcd102a5c344ee3c8ad6be9d5c6131e61b1bcd773601c781fce66"
TARGET_SETUP_PHASE = "grant_syscall_test_prepare_actors"
TARGET_FINISH_PHASES = ("grant_syscall_test_close_all_endpoints", "grant_syscall_test_release_all_resources")
TARGET_CONTROL_PHASES = ("grant_syscall_test_switch_control", "grant_syscall_test_reuse_control")
TARGET_EFFECT_PHASES = (
    ("GRANT_COMMAND_COPY_FROM_CROSS", "grant_syscall_test_effect_copy_from_cross"),
    ("GRANT_COMMAND_COPY_TO_CROSS", "grant_syscall_test_effect_copy_to_cross"),
    ("GRANT_COMMAND_COPY_FROM_LOCAL", "grant_syscall_test_effect_copy_from_local"),
    ("GRANT_COMMAND_COPY_TO_LOCAL", "grant_syscall_test_effect_copy_to_local"),
)
TARGET_BOUND_PHASES = {
    TARGET_SETUP_PHASE: (
        0, "static bool", "struct grant_syscall_test_fixture *fixture, struct micros_kernel_objects *objects, "
        "size_t payload_size, size_t *actor",
    ),
    **{
        name: (0, "static bool", "struct grant_syscall_test_fixture *fixture, struct micros_kernel_objects *objects, "
               "size_t *actor, uint64_t *failure_stage")
        for name in TARGET_FINISH_PHASES
    },
    TARGET_CONTROL_PHASES[0]: (
        6, "static enum micros_grant_syscall_test_control_result",
        "struct grant_syscall_test_fixture *fixture, struct micros_kernel_objects *objects, "
        "struct micros_hart *hart, struct micros_trap_frame *frame, size_t actor, struct micros_user_context *expected",
    ),
    TARGET_CONTROL_PHASES[1]: (
        6, "static enum micros_grant_syscall_test_control_result",
        "struct grant_syscall_test_fixture *fixture, struct micros_hart *hart, struct micros_trap_frame *frame, size_t actor",
    ),
    **{
        name: (5, "static bool", "struct grant_syscall_test_fixture *fixture")
        for _, name in TARGET_EFFECT_PHASES
    },
}
TARGET_TRANSITION_TAG = "grant_syscall_transition"
TARGET_TRANSITION_TABLE = "transition_bindings"
TARGET_TRANSITION_FIELDS = (
    ("enum grant_syscall_command command;", False),
    ("enum grant_syscall_control_action action;", False),
    ("size_t next_actor;", False),
    ("enum grant_syscall_command next_command;", False),
)
TARGET_ARMING_TAG = "grant_syscall_arming_input"
TARGET_ARMING_FIELDS = (
    ("size_t actor;", False),
    ("struct micros_user_context *context;", False),
    ("enum grant_syscall_command command;", False),
    ("uint64_t stale_endpoint;", False),
)
TARGET_COMMAND_BINDING_TAG = "grant_syscall_command_binding"
TARGET_COMMAND_TABLE = "command_bindings"
TARGET_COMMAND_BINDING_FIELDS = (
    ("enum grant_syscall_command command;", False),
    ("bool (*arm)(struct grant_syscall_test_fixture *fixture, const struct grant_syscall_arming_input *input);", False),
)
TARGET_ARMING_HELPERS = tuple(
    "grant_syscall_test_arm_" + suffix for suffix in (
        "ipc_notify", "create_read", "create_upper_endpoint", "create_upper_permission", "create_unused",
        "revoke_stale", "revoke_upper_token", "revoke_unused", "validate_read", "validate_zero",
        "validate_wrong_direction", "validate_unmapped", "validate_overflow_stale", "validate_upper_permission",
        "copy_from_cross", "copy_to_cross", "copy_from_local", "copy_to_local", "copy_from_zero",
        "copy_from_wrong_direction", "copy_wrong_grantor", "copy_stale_endpoint", "copy_stale_token", "copy_range",
        "copy_overflow", "copy_oversize", "copy_local_unmapped", "copy_remote_unmapped", "copy_local_permission",
        "copy_remote_permission", "copy_upper_endpoint", "copy_upper_token", "copy_unused",
        "unknown_operation", "bootstrap_unavailable", "copy_wrong_grantee", "copy_revoked", "copy_reused_endpoint",
    )
)
ORIGINAL_FUNCTIONS = {
    0: {
        0: "copy_grant micros_grant_validate_range micros_grant_copy_from micros_grant_copy_to".split(),
        1: "map_address_error plan_range".split(),
    },
    1: {0: "panic_grant_syscall micros_grant_handle_captured_user_ecall".split()},
    2: {0: (
        "copy_storage storage_matches snapshot_failure_state failure_state_matches fill_bytes "
        "physical_for write_pattern pattern_matches range_is_value micros_grant_runtime_run_self_test"
    ).split()},
    3: {
        0: "micros_grant_syscall_runtime_run_self_test micros_grant_syscall_test_finish".split(),
        1: (
            "copy_bytes bytes_equal fill_bytes read_satp user_address_of expected_satp abi_result "
            "physical_for write_pattern pattern_matches range_is_value fill_context_pattern current_actor "
            "test_mismatch normalize_objects capture_page_tables snapshot_user_bytes user_bytes_match "
            "snapshot_failure_state failure_state_matches scheduler_state_valid arm_raw arm_control"
        ).split(),
        2: ["arm_command"],
        3: [],
        4: "schedule_next schedule_reuse schedule_finish transition_after_return".split(),
        5: ["validate_command_effect"],
        6: (
            "micros_grant_syscall_test_before_ecall micros_grant_syscall_test_after_dispatch "
            "micros_grant_syscall_test_after_return micros_grant_syscall_test_handle_control_trap"
        ).split(),
        7: (
            "release_address_space release_thread close_endpoint reuse_grantor prepare_process "
            "endpoint_registry_is_empty scheduler_is_at_idle_baseline"
        ).split(),
    },
    4: {
        0: ["micros_grant_test_run"],
        1: "setup_fixture recreate_process expect_create_failure_unchanged".split(),
        2: (
            "context_pattern test_token_boundaries test_create_inspect_revoke_and_reuse "
            "test_create_failures_and_capacity test_terminal_generation_quarantines "
            "test_endpoint_cancellation_and_reuse test_failed_ipc_close_preserves_cancel_plan"
        ).split(),
        3: "test_error_precedence_and_output_preservation test_validator_rejects_corruption".split(),
        4: "test_grant_model_boundaries test_grant_model micros_grant_model_test_run".split(),
        5: (
            "process_handles_equal records_equal registries_equal model_next model_operation_name "
            "record_model_trace model_fail cancel_plan_matches_reference"
        ).split(),
        6: [],
        7: [],
    },
    5: {0: (
        "process_handles_equal authority_equal setup_fixture create_grant expect_authority_failure_unchanged "
        "test_copy_authority_and_bounds test_copy_plan_and_commit micros_grant_copy_test_run"
    ).split()},
    6: {0: "request_matches expect_decode_failure micros_grant_syscall_core_test_run".split()},
}
NATIVE_EXPORTS = tuple(
    name for owner in (1, 2, 3, 5) for name in ORIGINAL_FUNCTIONS[4][owner]
    if name not in ("context_pattern", "model_operation_name")
)
TARGET_EXPORTS = tuple(
    name for owner in (1, 2, 4, 5, 7) for name in ORIGINAL_FUNCTIONS[3][owner]
    if name not in (
        "normalize_objects", "capture_page_tables", "snapshot_user_bytes", "user_bytes_match",
        "schedule_next", "schedule_reuse", "schedule_finish",
    )
)
LONG_FUNCTIONS = (
    (0, "copy_grant"),
    (1, "micros_grant_handle_captured_user_ecall"),
    (2, "micros_grant_runtime_run_self_test"),
    (3, "arm_command"),
    (3, "transition_after_return"),
    (3, "validate_command_effect"),
    (3, "micros_grant_syscall_runtime_run_self_test"),
    (4, "test_create_inspect_revoke_and_reuse"),
    (4, "test_endpoint_cancellation_and_reuse"),
    (4, "test_error_precedence_and_output_preservation"),
    (4, "test_grant_model_boundaries"),
    (4, "test_grant_model"),
    (5, "test_copy_authority_and_bounds"),
    (6, "micros_grant_syscall_core_test_run"),
)
SHORTER_TASKS = (
    (0, "plan_range"), (0, "micros_grant_validate_range"),
    (5, "test_copy_plan_and_commit"),
    (3, "micros_grant_syscall_test_handle_control_trap"),
    (3, "micros_grant_syscall_test_finish"),
)
REQUIRED_TASKS = (*LONG_FUNCTIONS, *SHORTER_TASKS)
INITIAL_SYMBOL_OWNER_MIGRATION = tuple(
    (family, name) for family, owners in ORIGINAL_FUNCTIONS.items()
    for owner, names in owners.items() if owner != 0 for name in names
)
SYMBOL_OWNER_MIGRATION = ()
FUNCTION_TASK_MIGRATION = ()

# Immutable oracle literals are inserted from the offline reviewed Git manifest.
BASELINE_BODIES = {0: {'copy_grant': '1c7df8ef2d611dc882f8e0617dcc03afafe45bc344edab5b32e386bf973172af',
     'map_address_error': '05b7818b280c6fb9eef074137eb5efd8bae8db18e50e2a5439862d486e44a356',
     'micros_grant_copy_from': 'bb7569b02157a044462fdf9f5ba6329048dc2fa59efa8f368aeae4d5ac5e6312',
     'micros_grant_copy_to': '77d3a32941c480be4d27ac75833a4ed05bd1a56ba1a98f763683463ff1837de2',
     'micros_grant_validate_range': 'd38328ae50c4208037c8e7c2ca6e4e629ceb26f71a1191a270e8be125532bb77',
     'plan_range': 'c8d5c14c2e1e218dbbbe4631d6851419830e52ffeca8c9f3f271f898eeb992a4'},
 1: {'micros_grant_handle_captured_user_ecall': 'ef22d946db8b189fbd91825154a83cfb11ddc2e258d315155931d48a53a1abd2',
     'panic_grant_syscall': '230b3b9d69d753928e27b11039796536bfdcc6d54a001797a7211a87827eeb4c'},
 2: {'copy_storage': '4b480638cdb607c5cc090e482b8427a0a73209fe3d249c13e8cf513b8528a036',
     'failure_state_matches': '5ec079ae8b78f7b7af2025512e8dd41279f960078f66b6d2617c150c844a0716',
     'fill_bytes': '11b5f5403205b7afe3c10346cf86a035777959dc7314b6b98bac8886d35bd489',
     'micros_grant_runtime_run_self_test': '96f17663b48175f054e1f67174ed8f983b32ef686050ef0867bd3991868ce8de',
     'pattern_matches': '513733b9d0669d3bf0b4a3c7b7e8224bfa1f8fe1cbdac34fb0a0785f115d7ff3',
     'physical_for': 'bd794c9790dcbfb2c7f9c75163f492f4268a3d7625fa902b016f07611fc299c4',
     'range_is_value': 'f1494ceb6590ca81ae05775c5e4bd52d99949e2adbca8bac8923911be69cba00',
     'snapshot_failure_state': 'e6534f398f87f6dec7367cb2009f8c1042d6eaaf127f887f4f76b8eb77726470',
     'storage_matches': '2cf2a64c99b80613e115a17e7c95e674a8fe695a665679f138c0fc4d520a984a',
     'write_pattern': 'c64840a8bd05240000d50443f2ad099d26db92760627865978d736e9474d9a78'},
 3: {'abi_result': 'f1692d52685cb41fe5d24e268980509a899f33fae0586cd5c70c229264c8e56c',
     'arm_command': 'd7330e6cb635194fbb77a6ae886b9a04fcd5c368246b50e32a23ded6f47335b9',
     'arm_control': 'fa351cd110a332b190a811b8bd069bca97cac639294fae66e7b2930fbf3e155f',
     'arm_raw': 'a06cd4f352a340747e9047fea6f01de0e7d18126de6a6db7af61f2872c497625',
     'bytes_equal': '2cf2a64c99b80613e115a17e7c95e674a8fe695a665679f138c0fc4d520a984a',
     'capture_page_tables': '7ca19b0d8f3d4a7a077f624e4faaf7f66c666aa9d671f8825842171ba1918853',
     'close_endpoint': '233b26dabc4923e2f13842f913c995c186dd04cf9620c80df8c4b7e41c69dd60',
     'copy_bytes': '4b480638cdb607c5cc090e482b8427a0a73209fe3d249c13e8cf513b8528a036',
     'current_actor': '252b0f08808ae07b3d0729463366ab1ccf65e26a453488d909077a66179e53d0',
     'endpoint_registry_is_empty': 'fb7e5f696e449d1b95fc73fc357e904ff0028cdc758700995741c5faa4320d48',
     'expected_satp': '649a8a28c787a5e26c825f5ab9a658506f81549ba1c6c7f5f1289e6a00f5067f',
     'failure_state_matches': '2a5e547c22f381b3834a443464f28a08ffa0dcb1dfa4a3d84dd2dabf8e0ea1a2',
     'fill_bytes': '11b5f5403205b7afe3c10346cf86a035777959dc7314b6b98bac8886d35bd489',
     'fill_context_pattern': '91141dcbd5aed935b672c5118aaf76ea31afbeddc700c94974406600046a6669',
     'micros_grant_syscall_runtime_run_self_test': '04577ecb59c300ea6058749a8e36eae2a9cd1e0160729ce1295a1bb75c4eb133',
     'micros_grant_syscall_test_after_dispatch': '2ba37746ca157d41333440efc5d6cb8f245a72880c48a95ab3361c2086d20988',
     'micros_grant_syscall_test_after_return': 'e649b102803449c945b436517d660698c8955172700a076a1c93a7856e0f4507',
     'micros_grant_syscall_test_before_ecall': 'b2d2217e4d7f6c0cbd780a2ac322422480a6dda144bedaebbb371b19b84d765f',
     'micros_grant_syscall_test_finish': '520601ce1fbfac7e2087c94be021eb7cb571873c391fe02f826b417152638b3e',
     'micros_grant_syscall_test_handle_control_trap': '3b6ae115bbe0a2e3a67302171eadb969744f91f969d831551d9c74fc7bf23dfa',
     'normalize_objects': '30a155e23cc5b75858d3cc87d591d5626ba1ce433288317bd1435cff7a8a7760',
     'pattern_matches': '593a14e157a1261d084fa312322243e7aeae28756b7c926b8131e88a832274aa',
     'physical_for': '92a2f6347bd20712565c8579230b3b9e532010cdbd5c4285a2e5769c2a3f4d42',
     'prepare_process': '96fadd3809d025d02309d47aad9f3360b438942bf5ae00f278766d0ce04c1436',
     'range_is_value': '7e9bcc44bbfd214f5ee258bcaa91b9f6e0e1c9dd7f1c02777865ed1c99396ddc',
     'read_satp': '24735af2a51205a6ac12d4b983c10f4bde6e6cef0b8e4e11d6aa49d9d881ff19',
     'release_address_space': '3597ec571e10cb024d841459d32c2b085917fa140aabb522d136535eedcb2a80',
     'release_thread': 'b6f09ef1b71aa9638e50406e095a82763f59d1d6b44cefe3f7c3117cf8cdfd3a',
     'reuse_grantor': '7043242b39c5f8f8dde1c5b11cf9aa43ddab4fd74fbde18eb405293f80f28aa4',
     'schedule_finish': '208d7f31520580a7e764ce29337349576411c38a970d16d98aede274b042172b',
     'schedule_next': '8a8be5bcd206630956fd1c4403d0c4fe875f24eb32c67caede18e6fadeb32b5c',
     'schedule_reuse': '190e0b48617ef7b9cba545c7b3853d80f6b9695d17250990ef29567d4b0e9684',
     'scheduler_is_at_idle_baseline': '232cf0dbef916ce30807a524d31df4268b1522017f1340daa7ccc73608850f10',
     'scheduler_state_valid': '044453286a297467e5141b330fa1d3af7d645419ae3885221a56de896740a94f',
     'snapshot_failure_state': '5838de5e639ca2ecd93f44f4efa1ebd60595f7e9057021bc502f276a7cf7f8ce',
     'snapshot_user_bytes': '4fd61bdc8f3a82d371267ede7c58bfc1259a7ef7fa3cbd0ff6bcf7584171c5a8',
     'test_mismatch': '971ac087d0c589d9873cbbf91afe0ff5a3d6b6b9cf1fa73fb958baef35e69f65',
     'transition_after_return': '395b31f068b274bcf9ac046a89662c90e35721f811364412418e34cb91fea5fb',
     'user_address_of': '4d70f3075c22136fcbddf26d0d72d7f7c762677794668c8578fa48fad7f8ff95',
     'user_bytes_match': '3ed05758e22bdee4ef8153d22f5aff77f9bd7debdec04ffce690e6aaf400af38',
     'validate_command_effect': '728476346cdd62c58f5e6a386434e550b5d917604c552771cf9f152eeac4f8fe',
     'write_pattern': '31c38f222e115baf2a8d5b7dd41e12be44ab330416eca2cab89b3ea2f7abc188'},
 4: {'cancel_plan_matches_reference': '525bf853385483ad1ca7ee251a76c44b9968da92645e34c85f66cffc519d802d',
     'context_pattern': '27beff55faa7c13139dcbfb8ca672501f612ff42033bf579e2651fabbb46d06c',
     'expect_create_failure_unchanged': '55d0a0797793ca471bb23b96afc0b0ad65bb952a371ce8d60b8add5201fcfc11',
     'micros_grant_model_test_run': '2ce591393ca4c2e019123f21962867b8e75e7aeb94fb3a2863ddad151a5be11c',
     'micros_grant_test_run': '07c4731e2f7c30beac8df79c3e0a7c24db2fb02a49d7846f1d7368ba3a5d9e2b',
     'model_fail': '74216a4783838e9482161632752398313a68bd785af9dede3921e3e0a1eec41f',
     'model_next': '88d510643c80dabe81f46f60351cf9cbf6fbb71a36caa967392e9c72ad448cca',
     'model_operation_name': '0269ed27d678ff510deba1f57b386ac4393b719cd23bc9947a7d8d729b6b8ce8',
     'process_handles_equal': '1eb9ca3d14ed288168beacc0916c15eb03aa6ecf2e283e022b4d3ccb7e92f678',
     'record_model_trace': '45c57c90177532671f75043947bffb2c90ec64b1bd53d94dabaa5a3b3c2d0d47',
     'records_equal': 'fa25c1e8fd5861430084c5924ee77fa5c535c39d53bf874a7e1c0ddb6b52943d',
     'recreate_process': 'a421aa2f691e0899f9d6339535d8a0f0ae6bc874c3c553b10e0a7048a8d65a29',
     'registries_equal': '0c558d0539e047bd16033e24598831e00b4c739f328e2478cc7a8514f6e3acd3',
     'setup_fixture': '0268e61f54ecc97a9957772241ad3b2b78db08c07919621fd096cc5364966930',
     'test_create_failures_and_capacity': '5cd990e98c56d43d5c60601b9352d274e0dc6c0922b42f5afe40641de592c91d',
     'test_create_inspect_revoke_and_reuse': 'c20a2cfe17b520b58cce1de46e6e6b907d695ad04ccfc40238ddc960d9a2d6d5',
     'test_endpoint_cancellation_and_reuse': '452d6f373f9e96322d35a4630f42ce5861aa60ec6ec5874c1ed86e8dc13ed915',
     'test_error_precedence_and_output_preservation': '847c4f009d1cb62429971684e7a8bc4da8d70bb0c95ba6117e3d6cdf548bc9a4',
     'test_failed_ipc_close_preserves_cancel_plan': '8ab2a01717bacf101b8e17163b87bb4480a10549920d947cab6ad821c879159b',
     'test_grant_model': 'beafadec38361483ab7d3a1ef4bdf4030ba49c2482006ce168bda566bb20b996',
     'test_grant_model_boundaries': '21ac68bf5d658c4b3efb098db617f72354d205d8924bd8669ca2dbb0ac3d1516',
     'test_terminal_generation_quarantines': '9d00b1f7737ddd1778a7351d87cc36882516e14642ad05bd906cf634e5d4528a',
     'test_token_boundaries': 'b2b5fd3ed3627730a922d55889865ad253a91961ff298c68f39ec397cfa3174a',
     'test_validator_rejects_corruption': 'da12bb718d9d9388b83bebd6ae06e4b678bd3b575b3ce6533d0aa8dfb7f1dc0e'},
 5: {'authority_equal': '42d59de8c82740c0cf279615d3f067a5e0793240944a85eecd81d0f42597c812',
     'create_grant': '9832f4fa141bb6954a50e7f0a5c5cb2c42825fab44b0957d0348919621cd3afb',
     'expect_authority_failure_unchanged': '1b64989ca1f45521c94bebee351e5c5e7e3498d9c79db8f7cda03f510b2f5926',
     'micros_grant_copy_test_run': 'ba940a6ead9b1d5ca49fdc4e6b2287454dc7774f8a5219f4647a111b4bb1db5b',
     'process_handles_equal': '1eb9ca3d14ed288168beacc0916c15eb03aa6ecf2e283e022b4d3ccb7e92f678',
     'setup_fixture': '8eddb16c4121e6fb8093083dd3505eea0ea012df577b5544b898916e3d1d79c4',
     'test_copy_authority_and_bounds': 'a44e0767b9f824aaeba3b6649b85f3fe1823ed096af279a4966ec883b08b86e2',
     'test_copy_plan_and_commit': '00ca3e0e1bbd211f41b3eb49069238d9eb289ee7a3f9cf657082b642463eb8ef'},
 6: {'expect_decode_failure': 'd8fc7322f7be70ea53cf9d133069c63581643834263fe4cd4b16bcaad93da503',
     'micros_grant_syscall_core_test_run': 'e48bcc5892f4c244b369fd6737d3c95668c625f6b884a944f3996c037f1cde54',
     'request_matches': 'be3f9e8ad71bfcae79c291207cd380ba9e6c72709c8b6e2972ec6b9c85053320'}}
BASELINE_SIGNATURES = ('e6ef14db1e52a16ee0a9849d3417175acc6dade9d2a737fe691d918d62566769',
 'de2964c3d47c9ce24490c268790f7c7f3fc0f2a586254d18f27b98ec3fa9413e',
 '05855d7b82814e75bdaf558d2ba74f455938a7567fb9fbd242510faf99027f3a',
 'a0c5c1782c2e655a15597000133e8c94b8e8ecb9915b41b36c0af8ef09011125',
 'b09f3b171a08c0e895c023977b9ca81fc4c788d1ca6dac153bef801d023a5835',
 'cfbf69249a716352b942640c7184810a42681bbc990debd0c8444ed2487f1b5a',
 'b197f4936ffb7cfe345ab6067cd3a8acafe1fbb8bd3c52d14478eeddfc59ec46')
ORIGINAL_ORDER = (('map_address_error',
  'plan_range',
  'copy_grant',
  'micros_grant_validate_range',
  'micros_grant_copy_from',
  'micros_grant_copy_to'),
 ('panic_grant_syscall', 'micros_grant_handle_captured_user_ecall'),
 ('copy_storage',
  'storage_matches',
  'snapshot_failure_state',
  'failure_state_matches',
  'fill_bytes',
  'physical_for',
  'write_pattern',
  'pattern_matches',
  'range_is_value',
  'micros_grant_runtime_run_self_test'),
 ('copy_bytes',
  'bytes_equal',
  'fill_bytes',
  'read_satp',
  'user_address_of',
  'expected_satp',
  'abi_result',
  'physical_for',
  'write_pattern',
  'pattern_matches',
  'range_is_value',
  'fill_context_pattern',
  'current_actor',
  'test_mismatch',
  'normalize_objects',
  'capture_page_tables',
  'snapshot_user_bytes',
  'user_bytes_match',
  'snapshot_failure_state',
  'failure_state_matches',
  'scheduler_state_valid',
  'arm_raw',
  'arm_command',
  'arm_control',
  'schedule_next',
  'schedule_reuse',
  'schedule_finish',
  'transition_after_return',
  'validate_command_effect',
  'micros_grant_syscall_test_before_ecall',
  'micros_grant_syscall_test_after_dispatch',
  'micros_grant_syscall_test_after_return',
  'release_address_space',
  'release_thread',
  'close_endpoint',
  'reuse_grantor',
  'micros_grant_syscall_test_handle_control_trap',
  'prepare_process',
  'endpoint_registry_is_empty',
  'scheduler_is_at_idle_baseline',
  'micros_grant_syscall_test_finish',
  'micros_grant_syscall_runtime_run_self_test'),
 ('process_handles_equal',
  'records_equal',
  'registries_equal',
  'setup_fixture',
  'recreate_process',
  'context_pattern',
  'test_token_boundaries',
  'test_create_inspect_revoke_and_reuse',
  'expect_create_failure_unchanged',
  'test_create_failures_and_capacity',
  'test_terminal_generation_quarantines',
  'test_endpoint_cancellation_and_reuse',
  'test_failed_ipc_close_preserves_cancel_plan',
  'test_error_precedence_and_output_preservation',
  'test_validator_rejects_corruption',
  'model_next',
  'model_operation_name',
  'record_model_trace',
  'model_fail',
  'cancel_plan_matches_reference',
  'test_grant_model_boundaries',
  'test_grant_model',
  'micros_grant_test_run',
  'micros_grant_model_test_run'),
 ('process_handles_equal',
  'authority_equal',
  'setup_fixture',
  'create_grant',
  'expect_authority_failure_unchanged',
  'test_copy_authority_and_bounds',
  'test_copy_plan_and_commit',
  'micros_grant_copy_test_run'),
 ('request_matches', 'expect_decode_failure', 'micros_grant_syscall_core_test_run'))
STABLE_HASHES = {'CMakePresets.json': 'eae6c86ce5ac8f0e1b39605ebbd6816135558f0b4f8ea293b3680d7ecb79df86',
 'arch/riscv64/grant_syscall_test.S': '23e1c37e054c0028ae8f78a14d888a594c161d0bea591805d7ea5723670c0d7d',
 'include/micros/grant.h': '9a3723ad14c99db1d54b5b2cb4d051a42a191379f33547a44b5ff769d8346d29',
 'include/micros/grant_copy.h': 'fc3a2a7c30cda2ca701e5f369c39870eccf3c060dcf6c8ded9b955a7a0c5fce4',
 'include/micros/grant_runtime.h': '1c1d153c4a8935f22963a506c93a8b62d72303b89d671634f4ac2672d8f7abb2',
 'include/micros/grant_syscall_core.h': '282b7a4499617a7b0eeaa7a52347466dde28da13215feab8ad011c839d0662d5',
 'include/micros/ipc_abi.h': 'ddf21e7cf2752908e7297a051e0c74bc8db0a37c8f3487413147961fca4eb5c3',
 'include/micros/syscall_abi.h': '8bd61b7bacc4b8c29fb62270aa8678930bb15a54e27e9aaaee03fafc5a9e8cad',
 'kernel/boot_component_tests.c': '6a22c42e953c6af39be80baee6334a583658e150f0dde2944d637692b4a53cc3',
 'kernel/grant.c': '00da274e5ec7ef45e03a68392bf33d32ba7b9eb2b5218e8ca9908a582291e707',
 'kernel/grant_abi.c': 'a53c92b09f814d43393d1d3515b09db98bbb17b0de211f17a067917b5c7665ad',
 'kernel/grant_copy_core.c': '84a1c1fdd005d27d044c19d81a1f3b69d4804380f0b9d1bbca1ddacbf3b9a501',
 'kernel/grant_internal.h': '96e3f0939ec325e618be5d089f0d53585b0a65a16ad995989d0761a0c315b6bf',
 'kernel/grant_runtime.c': '3e9ca759068b2f0f7e4c2c4a75a0a9b65d86a4717bb95e543dbfa0ef9c5ae267',
 'kernel/grant_runtime_internal.h': '03ada0e660616efea7c6d97cffbd803a4a07c253fbe7a515dbbd25459a2cc816',
 'kernel/grant_syscall.h': '860e12969e918a7517c96949c116393882b659a6d20157b17fda71f735c89573',
 'kernel/grant_syscall_core.c': '9959f9e3913133b95aac32adf954e83516bb40c71cec6e0e164101978e28a5ca',
 'kernel/grant_syscall_test.h': '5d9613bc532e0aff143123e73e2c297785dd7a0ad5fd0827f6ea4a4efbec9319',
 'kernel/grant_test.h': 'e9b7541252d594b5d77d7c85dfe3bf1361d1336231983e89fb2613edf861b171',
 'kernel/ipc_syscall.c': 'a281324a7c82fab9aebfbc238dad427af803ac09683cc1d0e3378fe385f85fa8',
 'kernel/syscall.c': '1f256f61b29759e004f1d26d0c5c634f6975a6c126e885320f1ca3a879a52803',
 'kernel/trap_test_hooks.c': '1a037ffb5e162083cfb4dcbb7a23b9cb9fdbec7a28cd843297dc71267745e6df',
 'tests/host/endpoint_test.c': 'e4bd7fe82fe2a878cecb6a940cba48a85d32d7900cc909f44dbe03c1c0602ccb',
 'tests/host/ipc_model_main.c': '451170f1a93a19ac85d347121257d89817de1ff2e928990d514fc3b719a813f3',
 'tools/run_qemu_smoke.py': '9e54f744074d077d05b52880495b73f7c74ae1db0a6ad751f8b7024688bf79e3'}
SHARED_INSPECTOR_DIGEST = 'efab84d99e3d5c49fc416a161816431b9e54615ac23af9aef3e9f0e995bc2e0d'
PLANNER_AST_DIGEST = '79cbadcd7b9bf78529f6c0c4b1930e5db27179f6f5af4bfaf09f5c458c921732'
CONTEXT_AST_DIGEST = '0c2af36928f0b9646d25c12f6c24b9a3e5cff97222f1041cbd902889738f0d3c'


def source(path):
    return kernel_layout_source.read_path(ROOT / path)


def digest_bytes(value):
    return hashlib.sha256(value).hexdigest()


def digest_tokens(value):
    return digest_bytes(json.dumps(tuple(value), separators=(",", ":")).encode())


def normal_names(family):
    if family == 0:
        return {
            "grant_copy_execute": "copy_grant",
            "grant_copy_map_address_error": "map_address_error",
            "grant_copy_plan_range": "plan_range",
        }
    if family in (3, 4):
        prefix = "grant_syscall_test_" if family == 3 else "grant_test_"
        return {
            prefix + (name[5:] if family == 4 and name.startswith("test_") else name): name
            for name in BASELINE_BODIES[family] if not name.startswith("micros_")
        }
    return {}


def owned_records(family, texts=None):
    texts = texts if texts is not None else {
        path: source(path) for path in FAMILY_PATHS[family] if kernel_layout_source.is_file_path(ROOT / path)
    }
    return {
        (path, name): record for path, text in texts.items()
        for name, record in function_records(text).items()
    }


def original_locations(family, texts=None):
    locations = {}
    names = normal_names(family)
    for (path, spelling), record in owned_records(family, texts).items():
        if family == 0 and spelling in (COPY_EXECUTION_PHASE, COPY_LOCAL_RANGE_PHASE, VALIDATION_PHASE):
            if path != COPY_SOURCES[0]:
                raise AssertionError("the private participant-plan phase has its exact copy owner")
            continue
        if family == 0 and spelling == RANGE_DEVICE_PHASE:
            if path != COPY_SOURCES[1]:
                raise AssertionError("the device-span phase has only its original range owner")
            continue
        if family == 1 and spelling in (SYSCALL_DISPATCH_PHASE, SYSCALL_CREATE_PHASE):
            if path != FAMILY_PATHS[1][0]:
                raise AssertionError("captured syscall phases remain in their exact file-local adapter")
            continue
        if family == 4 and spelling == NATIVE_FIXTURE_GETTER:
            if path != HOST_SOURCES[1]:
                raise AssertionError("the native fixture getter has its sole state owner")
            continue
        if family == 3 and spelling == TARGET_FIXTURE_GETTER:
            if path != SYSCALL_TEST_SOURCES[1]:
                raise AssertionError("the target fixture getter has its one original state owner")
            continue
        if family == 3 and spelling in TARGET_BOUND_PHASES:
            if path != SYSCALL_TEST_SOURCES[TARGET_BOUND_PHASES[spelling][0]]:
                raise AssertionError("target boundary phases remain in their original root/hook/effect owner")
            continue
        if family == 3 and spelling in TARGET_ARMING_HELPERS:
            owner = 2 if TARGET_ARMING_HELPERS.index(spelling) < 14 else 3
            if path != SYSCALL_TEST_SOURCES[owner]:
                raise AssertionError("all fourteen non-copy and twenty-four copy templates have their exact owner")
            continue
        if family == 4 and any(spelling in specification[3] for specification in NATIVE_CASE_OUTLINES.values()):
            paths = tuple(
                specification[0] for specification in NATIVE_CASE_OUTLINES.values() if spelling in specification[3]
            )
            if paths != (path,):
                raise AssertionError("native case phases have one exact file-local domain owner")
            continue
        if family == 5 and any(spelling in specification[2] for specification in COPY_CASE_OUTLINES.values()):
            if path != FAMILY_PATHS[5][0]:
                raise AssertionError("copy evidence phases remain in their separate original fixture")
            continue
        if family == 6 and spelling in ABI_PHASES:
            if path != FAMILY_PATHS[6][0]:
                raise AssertionError("ABI evidence phases have only the original native ABI owner")
            continue
        if family == 2 and spelling in (*MECHANISM_PHASES, *MECHANISM_DENIALS):
            if path != FAMILY_PATHS[2][0]:
                raise AssertionError("the actual mechanism retains its one original target fixture owner")
            continue
        if family == 4 and spelling in (
            MODEL_SELECT, MODEL_DISPATCH, MODEL_COMPARE, MODEL_AUTHORIZE,
            *MODEL_AUTH_PHASES, *(name for _, name, _ in MODEL_OPERATIONS),
        ):
            owner = (
                HOST_SOURCES[4] if spelling in (MODEL_SELECT, MODEL_DISPATCH, MODEL_COMPARE)
                else HOST_SOURCES[6] if spelling in tuple(name for _, name, _ in MODEL_OPERATIONS)
                else HOST_SOURCES[7]
            )
            if path != owner:
                raise AssertionError("model phases have exactly their reviewed root/operation/copy owner")
            continue
        original = names.get(spelling, spelling)
        if original not in BASELINE_BODIES[family] or original in locations:
            raise AssertionError("unknown/duplicate original or unreviewed phase: " + spelling)
        locations[original] = (path, record)
    if set(locations) != set(BASELINE_BODIES[family]):
        raise AssertionError("every complete original grant definition remains present")
    return locations


def original_signature_tokens(family, original, record):
    names = normal_names(family)
    signature = tuple(names.get(token, token) for token in tokens(record.signature))
    if family == 0 and original == "plan_range" and record.name == "grant_copy_plan_range":
        if signature[:1] == ("static",):
            raise AssertionError("the one required range interface has its actual external declaration")
        signature = ("static", *signature)
    if family == 4 and (4, "setup_fixture") not in SYMBOL_OWNER_MIGRATION:
        signature = native_fixture_signature_tokens(original, signature)
    if family == 3 and (3, "copy_bytes") not in SYMBOL_OWNER_MIGRATION:
        signature = fixture_signature_tokens(original, signature, TARGET_EXPORTS, TARGET_FIXTURE_TAG, TARGET_CONTEXT_HELPERS)
    return signature


def original_body_digest(family, original, record):
    names = normal_names(family)
    body = tokens("{" + record.body + "}")
    if family == 0 and original == "copy_grant" and (0, original) not in FUNCTION_TASK_MIGRATION:
        body = copy_execution_actual_body_tokens(record)
    if family == 0 and original == "plan_range" and (0, original) not in FUNCTION_TASK_MIGRATION:
        body = range_device_actual_body_tokens(record)
    if family == 0 and original == "micros_grant_validate_range" and (0, original) not in FUNCTION_TASK_MIGRATION:
        body = validation_actual_body_tokens(record)
    if family == 1 and original == "micros_grant_handle_captured_user_ecall" and (1, original) not in FUNCTION_TASK_MIGRATION:
        body = syscall_actual_body_tokens(record)
    if family == 2 and original == "micros_grant_runtime_run_self_test" and (2, original) not in FUNCTION_TASK_MIGRATION:
        body = mechanism_actual_body_tokens(record)
    if family == 3 and (3, "copy_bytes") not in SYMBOL_OWNER_MIGRATION:
        if original in (
            "micros_grant_syscall_runtime_run_self_test", "micros_grant_syscall_test_finish",
            "micros_grant_syscall_test_handle_control_trap", "validate_command_effect",
        ) and (3, original) not in FUNCTION_TASK_MIGRATION:
            body = target_boundary_actual_body_tokens(original, record)
        if original == "transition_after_return" and (3, original) not in FUNCTION_TASK_MIGRATION:
            body = transition_actual_body_tokens(record)
        if original == "arm_command" and (3, original) not in FUNCTION_TASK_MIGRATION:
            body = arming_actual_body_tokens(record)
        body = target_fixture_body_tokens(original, tuple(names.get(token, token) for token in body))
    if family == 4 and (4, "setup_fixture") not in SYMBOL_OWNER_MIGRATION:
        if original in NATIVE_CASE_OUTLINES and (4, original) not in FUNCTION_TASK_MIGRATION:
            body = native_case_actual_body_tokens(original, record)
        if original == "test_grant_model" and (4, original) not in FUNCTION_TASK_MIGRATION:
            body = model_actual_body_tokens(record)
        body = native_fixture_body_tokens(original, tuple(names.get(token, token) for token in body))
    if family == 5 and original in COPY_CASE_OUTLINES and (5, original) not in FUNCTION_TASK_MIGRATION:
        body = copy_case_actual_body_tokens(original, record)
    if family == 6 and original == "micros_grant_syscall_core_test_run" and (6, original) not in FUNCTION_TASK_MIGRATION:
        body = abi_actual_body_tokens(record)
    return digest_tokens(tuple(names.get(token, token) for token in body))


def work_declarations(owner, tag, fields):
    lexical = c_lexical_text(owner)
    starts = tuple(re.finditer(r"\bstruct " + tag + r"\s*\{", lexical))
    if len(starts) != 1:
        raise AssertionError("one file-local execution work type owns only original automatic objects")
    opening = lexical.index("{", starts[0].start())
    closing = matching_lexical(lexical, opening, "{", "}")
    declarations = tuple(tokens(part + ";") for part in lexical[opening + 1:closing].split(";") if part.strip())
    if declarations != tuple(tokens(declaration) for declaration, _ in fields):
        raise AssertionError("automatic work retains original fields, complete types and declaration order")
    return declarations


def copy_execution_declarations(owner):
    return work_declarations(owner, COPY_EXECUTION_TAG, COPY_EXECUTION_FIELDS)


def copy_execution_body_tokens(caller, phase, declarations, local_phase):
    if tuple(declarations) != tuple(tokens(declaration) for declaration, _ in COPY_EXECUTION_FIELDS):
        raise AssertionError("original declarations come from actual verified execution fields")
    body = list(tokens(caller))
    local_guard = list(tokens(
        "if (!" + COPY_LOCAL_RANGE_PHASE + "(local_address, length)) { return MICROS_GRANT_ERROR_RANGE; }"
    ))
    local_positions = [
        index for index in range(len(body)) if body[index:index + len(local_guard)] == local_guard
    ]
    local = list(tokens(local_phase))
    if len(local_positions) != 1 or local[:1] != ["{"] or local[-4:] != ["return", "true", ";", "}"]:
        raise AssertionError("local range admission retains exact original scalar arguments before IRQ save")
    local = local[1:-4]
    local_restored = []
    index = 0
    while index < len(local):
        if local[index] == "return":
            if local[index:index + 3] != ["return", "false", ";"]:
                raise AssertionError("local range success cannot bypass its original full shape check")
            local_restored.extend(tokens("return MICROS_GRANT_ERROR_RANGE;"))
            index += 3
        else:
            local_restored.append(local[index])
            index += 1
    start = local_positions[0]
    body[start:start + len(local_guard)] = local_restored
    guard = list(tokens(
        "if (!" + COPY_EXECUTION_PHASE + "(&work, local_address, length, required_permission)) { goto done; }"
    ))
    positions = [index for index in range(len(body)) if body[index:index + len(guard)] == guard]
    if len(positions) != 1:
        raise AssertionError("dual-plan execution binds exact caller state/scalars and common failure label")
    phase = list(tokens(phase))
    if phase[:1] != ["{"] or phase[-4:] != ["return", "true", ";", "}"]:
        raise AssertionError("participant-plan success follows both original plans and complete error classification")
    inner = phase[1:-4]
    if {"goto", "break", "continue"} & set(inner):
        raise AssertionError("participant planning cannot borrow caller exit or loop scope")
    restored = []
    index = 0
    while index < len(inner):
        if inner[index] == "return":
            if inner[index:index + 3] != ["return", "false", ";"]:
                raise AssertionError("partial participant planning cannot bypass remaining classification")
            restored.extend(tokens("goto done;"))
            index += 3
        else:
            restored.append(inner[index])
            index += 1
    start = positions[0]
    body[start:start + len(guard)] = restored
    return restore_automatic_work_tokens(body, COPY_EXECUTION_TAG, COPY_EXECUTION_FIELDS, declarations)


def restore_automatic_work_tokens(body, tag, fields, declarations, variable="work"):
    body = list(body)
    if tuple(declarations) != tuple(tokens(declaration) for declaration, _ in fields):
        raise AssertionError("automatic declarations come from the exact verified actual work fields")
    prefix = list(tokens("{ struct " + tag + " " + variable + ";"))
    if body[:len(prefix)] != prefix:
        raise AssertionError("execution work is one original uninitialized automatic root object")
    del body[1:len(prefix)]
    names = tuple(declaration_name(declaration) for declaration in declarations)
    restored = list(restore_state_members(body, variable, names))
    initializers = []
    for actual, (_, initialized) in zip(declarations, fields):
        if not initialized:
            initializers.extend(actual)
            continue
        name = declaration_name(actual)
        if restored[1:3] != [name, "="]:
            raise AssertionError("only original explicit plan initializers execute, in original order")
        end = restored.index(";", 3)
        value = restored[3:end]
        if initialized is True:
            cast = ["(", *actual[:-2], ")"]
            if value[:len(cast) + 1] != cast + ["{"]:
                raise AssertionError("plan initializer retains its exact original aggregate type")
            value = value[len(cast):]
        elif initialized != "value":
            raise AssertionError("automatic initializer transport is not declared by the original scope")
        initializers.extend((*actual[:-1], "=", *value, ";"))
        del restored[1:end + 1]
    restored[1:1] = initializers
    return tuple(restored)


def declaration_name(declaration):
    return declaration[declaration.index("[") - 1] if "[" in declaration else declaration[-2]


def restore_state_members(body, variable, names, access=(".", "->")):
    body = list(body)
    restored = []
    index = 0
    while index < len(body):
        if body[index] == variable:
            if (
                body[index + 1:index + 2] not in ([operator] for operator in access)
                or body[index + 2:index + 3] not in ([name] for name in names)
                or (index and body[index - 1] in (".", "->"))
            ):
                raise AssertionError("typed state is not copied, reset, nested-qualified or retained")
            restored.append(body[index + 2])
            index += 3
        else:
            restored.append(body[index])
            index += 1
    return tuple(restored)


def native_fixture_signature_tokens(original, signature):
    return fixture_signature_tokens(original, signature, NATIVE_EXPORTS, NATIVE_FIXTURE_TAG, NATIVE_CONTEXT_HELPERS)


def fixture_signature_tokens(original, signature, exports, tag, helpers):
    signature = list(signature)
    if original in exports:
        if signature[:1] == ["static"]:
            raise AssertionError("only required cross-owner native helpers have actual external declarations")
        signature.insert(0, "static")
    if original in helpers:
        start = signature.index("(") + 1
        parameter = list(tokens("struct " + tag + " *fixture"))
        delimiter = ")" if helpers[original] else ","
        if signature[start:start + len(parameter) + 1] != parameter + [delimiter]:
            raise AssertionError("native helpers borrow only the exact original family as first typed parameter")
        signature[start:start + len(parameter) + (delimiter == ",")] = (
            ["void"] if delimiter == ")" else []
        )
    return tuple(signature)


def native_fixture_body_tokens(original, body):
    return fixture_body_tokens(
        original, body, NATIVE_FIXTURE_TAG, NATIVE_FIXTURE_GETTER,
        NATIVE_CONTEXT_ROOTS, NATIVE_CONTEXT_HELPERS, NATIVE_FIELD_NAMES,
    )


def target_fixture_body_tokens(original, body):
    names = tuple(declaration_name(tokens(declaration)) for declaration, _ in TARGET_FIELDS)
    return fixture_body_tokens(
        original, body, TARGET_FIXTURE_TAG, TARGET_FIXTURE_GETTER,
        TARGET_CONTEXT_ROOTS, TARGET_CONTEXT_HELPERS, names,
    )


def fixture_body_tokens(original, body, tag, getter, roots, helpers, fields):
    body = list(body)
    borrow = list(tokens("struct " + tag + " *fixture = " + getter + "();"))
    if original in roots:
        if body[:1 + len(borrow)] != ["{"] + borrow:
            raise AssertionError("stateful native case roots borrow their original fixture once without resetting it")
        del body[1:1 + len(borrow)]
    restored = []
    index = 0
    while index < len(body):
        name = body[index]
        if name in helpers and body[index + 1:index + 2] == ["("]:
            delimiter = ")" if helpers[name] else ","
            if body[index + 2:index + 4] != ["fixture", delimiter]:
                raise AssertionError("native calls pass only the exact original borrowed fixture")
            restored.extend((name, "("))
            index += 3 if delimiter == ")" else 4
        else:
            restored.append(name)
            index += 1
    return restore_state_members(restored, "fixture", fields, ("->",))


def boolean_case_body_tokens(caller, phases):
    caller = list(caller)
    for name, arguments, phase, terminal in phases:
        binding = name + "(" + arguments + ")"
        call = list(tokens(
            "return " + binding + ";" if terminal else "if (!" + binding + ") { return false; }"
        ))
        positions = [
            index for index in range(len(caller)) if caller[index:index + len(call)] == call
        ]
        body = list(tokens(phase))
        if len(positions) != 1 or body[:1] != ["{"] or body[-1:] != ["}"]:
            raise AssertionError("each domain phase binds exactly one caller-owned work/fixture and failure exit")
        if terminal:
            inner = body[1:-1]
        else:
            if body[-4:] != ["return", "true", ";", "}"]:
                raise AssertionError("a nonterminal phase continues only after its entire original task succeeds")
            inner = body[1:-4]
        start = positions[0]
        caller[start:start + len(call)] = inner
    return tuple(caller)


def native_case_actual_body_tokens(original, record, owner=None):
    path, tag, fields, names = NATIVE_CASE_OUTLINES[original]
    owner = source(path) if owner is None else owner
    declarations = work_declarations(owner, tag, fields)
    records = function_records(owner)
    phases = []
    for index, name in enumerate(names):
        phase = records[name]
        parameters = "struct grant_test_fixture *fixture"
        if name not in NATIVE_FIXTURE_ONLY_PHASES:
            parameters += ", struct " + tag + " *work"
        if phase.guards or tokens(phase.signature) != tokens(
            "static bool " + name + "(" + parameters + ")"
        ):
            raise AssertionError("native phases borrow only their original fixture and exact typed automatic work")
        arguments = "fixture" if name in NATIVE_FIXTURE_ONLY_PHASES else "fixture, &work"
        phases.append((name, arguments, "{" + phase.body + "}", index == len(names) - 1))
    body = list(tokens("{" + record.body + "}"))
    borrow = list(tokens("struct grant_test_fixture *fixture = " + NATIVE_FIXTURE_GETTER + "();"))
    if body[:1 + len(borrow)] != ["{"] + borrow:
        raise AssertionError("the original native case root retains the sole non-resetting fixture borrow")
    del body[1:1 + len(borrow)]
    body = list(restore_automatic_work_tokens(boolean_case_body_tokens(body, phases), tag, fields, declarations))
    body[1:1] = borrow
    return tuple(body)


def copy_case_actual_body_tokens(original, record, owner=None):
    tag, fields, names = COPY_CASE_OUTLINES[original]
    owner = source(FAMILY_PATHS[5][0]) if owner is None else owner
    declarations = work_declarations(owner, tag, fields)
    records = function_records(owner)
    phases = []
    for index, name in enumerate(names):
        phase = records[name]
        if phase.guards or tokens(phase.signature) != tokens(
            "static bool " + name + "(struct " + tag + " *work)"
        ):
            raise AssertionError("local copy phases retain original typed buffer/authority work and no other state")
        phases.append((name, "&work", "{" + phase.body + "}", index == len(names) - 1))
    body = boolean_case_body_tokens(tokens("{" + record.body + "}"), phases)
    return restore_automatic_work_tokens(body, tag, fields, declarations)


def abi_mapping_body_tokens(caller, phase):
    phase = list(tokens(phase))
    restored = []
    aliases = {
        "error": tokens("mappings[work.index].error"),
        "result": tokens("mappings[work.index].result"),
    }
    for index, token in enumerate(phase):
        if token in aliases and (not index or phase[index - 1] not in (".", "->")):
            restored.extend(aliases[token])
        else:
            restored.append(token)
    return boolean_case_body_tokens(caller, ((
        ABI_PHASES[1], "&work, mappings[work.index].error, mappings[work.index].result",
        " ".join(restored), False,
    ),))


def abi_actual_body_tokens(record, owner=None):
    owner = source(FAMILY_PATHS[6][0]) if owner is None else owner
    declarations = work_declarations(owner, ABI_WORK_TAG, ABI_FIELDS)
    records = function_records(owner)
    phases = []
    for index, name in enumerate(ABI_PHASES):
        parameters = "void" if index == 0 else "struct " + ABI_WORK_TAG + " *work"
        if index == 1:
            parameters += ", enum micros_grant_error error, int64_t result"
        phase = records[name]
        if phase.guards or tokens(phase.signature) != tokens("static bool " + name + "(" + parameters + ")"):
            raise AssertionError("ABI phases retain exact pure/work/readonly-scalar bindings and widths")
        if index != 1:
            phases.append((name, "" if index == 0 else "&work", "{" + phase.body + "}", index == len(ABI_PHASES) - 1))
    body = boolean_case_body_tokens(tokens("{" + record.body + "}"), phases)
    body = list(abi_mapping_body_tokens(body, "{" + records[ABI_PHASES[1]].body + "}"))
    declaration = list(tokens("struct " + ABI_WORK_TAG + " work;"))
    positions = [index for index in range(len(body)) if body[index:index + len(declaration)] == declaration]
    if len(positions) != 1:
        raise AssertionError("ABI work follows the root-owned actual static mapping array")
    start = positions[0]
    prefix = body[1:start]
    restored = list(restore_automatic_work_tokens(["{", *body[start:]], ABI_WORK_TAG, ABI_FIELDS, declarations))
    restored[1:1] = prefix
    return tuple(restored)


def bound_body_tokens(caller, binding, body):
    caller = list(caller)
    binding = list(tokens(binding))
    body = list(tokens(body))
    positions = [index for index in range(len(caller)) if caller[index:index + len(binding)] == binding]
    if len(positions) != 1 or body[:1] != ["{"] or body[-1:] != ["}"]:
        raise AssertionError("an actual complete phase has one exact caller binding and scope")
    start = positions[0]
    caller[start:start + len(binding)] = body[1:-1]
    return tuple(caller)


def model_case_success_tokens(body, expected_successes):
    body = list(body)
    if body[:1] != ["{"] or body[-4:] != ["return", "true", ";", "}"]:
        raise AssertionError("a model case completes its original switch attempt only on actual success")
    restored = []
    successes = 0
    index = 0
    while index < len(body):
        if body[index:index + 3] == ["return", "true", ";"]:
            restored.extend(tokens("break;"))
            successes += 1
            index += 3
        else:
            restored.append(body[index])
            index += 1
    if successes != expected_successes:
        raise AssertionError("only the original terminal or expected-denial case exits complete an attempt")
    return tuple(restored)


def model_authorization_body_tokens(copy):
    records = function_records(copy)
    declarations = work_declarations(copy, MODEL_AUTH_TAG, MODEL_AUTH_FIELDS)
    for name, (result, parameters, _) in MODEL_AUTH_PARAMETERS.items():
        phase = records[name]
        if phase.guards or tokens(phase.signature) != tokens(result + " " + name + "(" + parameters + ")"):
            raise AssertionError("authorization phases retain only their exact original typed work/input bindings")
    phases = []
    for index in (1, 3, 4, 5, 6):
        name = MODEL_AUTH_PHASES[index]
        inner = tokens("{" + records[name].body + "}")
        if index == 1:
            participant = MODEL_AUTH_PHASES[2]
            inner = boolean_case_body_tokens(inner, ((
                participant, MODEL_AUTH_PARAMETERS[participant][2], "{" + records[participant].body + "}", False,
            ),))
        phases.append((name, MODEL_AUTH_PARAMETERS[name][2], " ".join(inner), index == 6))
    body = boolean_case_body_tokens(tokens("{" + records[MODEL_AUTHORIZE].body + "}"), phases)
    selection = MODEL_AUTH_PHASES[0]
    body = bound_body_tokens(
        body, selection + "(" + MODEL_AUTH_PARAMETERS[selection][2] + ");",
        "{" + records[selection].body + "}",
    )
    body = restore_automatic_work_tokens(body, MODEL_AUTH_TAG, MODEL_AUTH_FIELDS, declarations)
    return model_case_success_tokens(body, 2)


def model_dispatch_body_tokens(dispatch, operations, authorization):
    body = list(tokens(dispatch))
    for label, name, uses_run in MODEL_OPERATIONS:
        arguments = "fixture, run, iteration, step" if uses_run else "fixture, iteration, step"
        case = "case " + label + ": return " + name + "(" + arguments + ");"
        complete_case = model_case_success_tokens(tokens(operations[name]), 1)
        body = list(bound_body_tokens(body, case, "{ case " + label + ": " + " ".join(complete_case) + " }"))
    labels = " ".join("case " + label + ":" for label in MODEL_AUTH_LABELS)
    case = labels + " return " + MODEL_AUTHORIZE + "(fixture, run, iteration, step);"
    return bound_body_tokens(body, case, "{ " + labels + " " + " ".join(authorization) + " }")


def model_actual_body_tokens(record, texts=None, header=None):
    paths = (HOST_SOURCES[4], HOST_SOURCES[6], HOST_SOURCES[7])
    texts = {path: source(path) for path in paths} if texts is None else texts
    if set(texts) != set(paths):
        raise AssertionError("model reconstruction reads all and only the actual three outlined owners")
    header = source(PRIVATE_HEADERS[1]) if header is None else header
    run_declarations = work_declarations(header, MODEL_RUN_TAG, MODEL_RUN_FIELDS)
    iteration_declarations = work_declarations(header, MODEL_ITERATION_TAG, MODEL_ITERATION_FIELDS)
    root = function_records(texts[HOST_SOURCES[4]])
    operations = function_records(texts[HOST_SOURCES[6]])
    authorization = function_records(texts[HOST_SOURCES[7]])
    for name, result, parameters in (
        (MODEL_SELECT, "static void", "struct grant_model_run *run, struct grant_model_iteration *iteration, size_t step"),
        (MODEL_DISPATCH, "static bool", MODEL_PARAMETERS),
        (MODEL_COMPARE, "static bool", MODEL_PARAMETERS),
    ):
        if root[name].guards or tokens(root[name].signature) != tokens(result + " " + name + "(" + parameters + ")"):
            raise AssertionError("seeded root phases preserve exact run/iteration/step types and bindings")
    for _, name, uses_run in MODEL_OPERATIONS:
        parameters = MODEL_PARAMETERS if uses_run else (
            "struct grant_test_fixture *fixture, struct grant_model_iteration *iteration, size_t step"
        )
        if operations[name].guards or tokens(operations[name].signature) != tokens("bool " + name + "(" + parameters + ")"):
            raise AssertionError("cross-owner cases retain exact actual inputs without unused state")
    if authorization[MODEL_AUTHORIZE].guards or tokens(authorization[MODEL_AUTHORIZE].signature) != tokens(
        "bool " + MODEL_AUTHORIZE + "(" + MODEL_PARAMETERS + ")"
    ):
        raise AssertionError("authorization dispatch retains only its required private model interface")
    dispatch = model_dispatch_body_tokens(
        "{" + root[MODEL_DISPATCH].body + "}",
        {name: "{" + phase.body + "}" for name, phase in operations.items()},
        model_authorization_body_tokens(texts[HOST_SOURCES[7]]),
    )
    body = bound_body_tokens(
        tokens("{" + record.body + "}"),
        "if (!" + MODEL_DISPATCH + "(fixture, &run, &iteration, step)) { return false; }",
        " ".join(dispatch),
    )
    body = bound_body_tokens(
        body, MODEL_SELECT + "(&run, &iteration, step);", "{" + root[MODEL_SELECT].body + "}",
    )
    body = list(boolean_case_body_tokens(body, ((
        MODEL_COMPARE, "fixture, &run, &iteration, step", "{" + root[MODEL_COMPARE].body + "}", False,
    ),)))
    loop = list(tokens("for (step = 0; step < GRANT_MODEL_STEPS; ++step) {"))
    positions = [index for index in range(len(body)) if body[index:index + len(loop)] == loop]
    if len(positions) != 1:
        raise AssertionError("one original model attempt loop owns each iteration's complete automatic state")
    opening = positions[0] + len(loop) - 1
    closing = matching_lexical(body, opening, "{", "}")
    body[opening:closing + 1] = restore_automatic_work_tokens(
        body[opening:closing + 1], MODEL_ITERATION_TAG, MODEL_ITERATION_FIELDS, iteration_declarations, "iteration",
    )
    prefix = list(tokens(
        "struct grant_test_fixture *fixture = " + NATIVE_FIXTURE_GETTER + "(); "
        "const uint32_t seed = UINT32_C(0x38a110c5);"
    ))
    if body[:1 + len(prefix)] != ["{"] + prefix:
        raise AssertionError("the actual model root retains its sole fixture borrow and original constant seed")
    del body[1:1 + len(prefix)]
    body = list(restore_automatic_work_tokens(body, MODEL_RUN_TAG, MODEL_RUN_FIELDS, run_declarations, "run"))
    body[1:1] = prefix
    return tuple(body)


def common_failure_body_tokens(caller, phases):
    caller = list(caller)
    for name, phase in phases:
        phase = list(tokens(phase))
        if phase[:1] != ["{"] or phase[-4:] != ["return", "true", ";", "}"]:
            raise AssertionError("a mechanism task succeeds only after its entire original ordered body")
        inner = phase[1:-4]
        restored = []
        index = 0
        while index < len(inner):
            if inner[index] == "return":
                if inner[index:index + 3] != ["return", "false", ";"]:
                    raise AssertionError("a mechanism task preserves only its original common failure exit")
                restored.extend(tokens("goto done;"))
                index += 3
            else:
                restored.append(inner[index])
                index += 1
        caller = list(bound_body_tokens(
            caller, "if (!" + name + "(&work)) { goto done; }", "{ " + " ".join(restored) + " }",
        ))
    return tuple(caller)


def denial_predicate_body_tokens(caller, predicates):
    caller = list(caller)
    for name, predicate in predicates:
        predicate = list(tokens(predicate))
        if (
            predicate[:4] != ["{", "return", "!", "("] or predicate[-3:] != [")", ";", "}"]
            or matching_lexical(predicate, 3, "(", ")") != len(predicate) - 3
        ):
            raise AssertionError("a denial predicate is only its complete original ordered short-circuit condition")
        call = list(tokens("!" + name + "(work)"))
        positions = [index for index in range(len(caller)) if caller[index:index + len(call)] == call]
        if len(positions) != 1:
            raise AssertionError("each original denial group has one exact borrowed-work binding")
        start = positions[0]
        caller[start:start + len(call)] = predicate[4:-3]
    return tuple(caller)


def mechanism_actual_body_tokens(record, owner=None):
    owner = source(FAMILY_PATHS[2][0]) if owner is None else owner
    records = function_records(owner)
    declarations = work_declarations(owner, MECHANISM_TAG, MECHANISM_FIELDS)
    phases = []
    for index, name in enumerate(MECHANISM_PHASES):
        result = "static void" if index == 4 else "static bool"
        phase = records[name]
        if phase.guards or tokens(phase.signature) != tokens(result + " " + name + "(struct " + MECHANISM_TAG + " *work)"):
            raise AssertionError("mechanism phases borrow only the complete original typed caller work")
        if index != 4:
            phases.append((name, "{" + phase.body + "}"))
    body = common_failure_body_tokens(tokens("{" + record.body + "}"), phases)
    canaries = MECHANISM_PHASES[4]
    body = bound_body_tokens(body, canaries + "(&work);", "{" + records[canaries].body + "}")
    predicates = []
    for name in MECHANISM_DENIALS:
        phase = records[name]
        if phase.guards or tokens(phase.signature) != tokens("static bool " + name + "(struct " + MECHANISM_TAG + " *work)"):
            raise AssertionError("denial predicates retain their original state/participant/canary work")
        predicates.append((name, "{" + phase.body + "}"))
    body = list(denial_predicate_body_tokens(body, predicates))
    declaration = list(tokens("struct " + MECHANISM_TAG + " work;"))
    positions = [index for index in range(len(body)) if body[index:index + len(declaration)] == declaration]
    if len(positions) != 1:
        raise AssertionError("the mechanism root owns one uninitialized automatic work object")
    start = positions[0]
    prefix = body[1:start]
    fixed = list(tokens("uintptr_t saved_status; bool passed = false;"))
    fixed_start = start + len(declaration)
    if body[fixed_start:fixed_start + len(fixed)] != fixed:
        raise AssertionError("IRQ status and initialized pass/failure control remain separate in actual root")
    del body[fixed_start:fixed_start + len(fixed)]
    body = list(restore_automatic_work_tokens(["{", *body[start:]], MECHANISM_TAG, MECHANISM_FIELDS, declarations))
    for marker, original in (
        ("uint64_t baseline_free;", "uintptr_t saved_status;"),
        ("micros_endpoint_t old_grantor_endpoint;", "bool passed = false;"),
    ):
        after = list(tokens(marker))
        positions = [index for index in range(len(body)) if body[index:index + len(after)] == after]
        if len(positions) != 1:
            raise AssertionError("original root-only declarations retain their exact original field order/lifetime")
        insert = positions[0] + len(after)
        body[insert:insert] = tokens(original)
    body[1:1] = prefix
    return tuple(body)


def borrowed_phase_body_tokens(caller, binding, phase, dereferences=(), addresses=(), failure=None, success=False):
    phase = list(tokens(phase))
    if phase[:1] != ["{"] or phase[-1:] != ["}"]:
        raise AssertionError("borrowed phases retain their complete original function scope")
    if success:
        if phase[-4:] != ["return", "true", ";", "}"]:
            raise AssertionError("a borrowed teardown/setup phase finishes its original ordered task before success")
        inner = phase[1:-4]
    else:
        inner = phase[1:-1]
    restored = []
    index = 0
    while index < len(inner):
        token = inner[index]
        if token == "*" and inner[index + 1:index + 2] in ([name] for name in (*dereferences, *addresses)):
            restored.append(inner[index + 1])
            index += 2
        elif token in dereferences:
            raise AssertionError("a borrowed scalar is only its original caller-owned lvalue, never a retained pointer")
        elif token in addresses and (not index or inner[index - 1] not in (".", "->")):
            restored.extend(("&", token))
            index += 1
        elif token == "return" and failure is not None:
            if inner[index:index + 3] != ["return", "false", ";"]:
                raise AssertionError("setup/teardown failure preserves only its original common fatal exit")
            restored.extend(tokens("goto " + failure + ";"))
            index += 3
        else:
            restored.append(token)
            index += 1
    return bound_body_tokens(caller, binding, "{ " + " ".join(restored) + " }")


def target_boundary_actual_body_tokens(original, record, owner=None):
    owner_index = (
        0 if original in ("micros_grant_syscall_runtime_run_self_test", "micros_grant_syscall_test_finish")
        else 6 if original == "micros_grant_syscall_test_handle_control_trap" else 5
    )
    owner = source(SYSCALL_TEST_SOURCES[owner_index]) if owner is None else owner
    records = function_records(owner)
    names = (
        (TARGET_SETUP_PHASE,) if original == "micros_grant_syscall_runtime_run_self_test"
        else TARGET_FINISH_PHASES if original == "micros_grant_syscall_test_finish"
        else TARGET_CONTROL_PHASES if original == "micros_grant_syscall_test_handle_control_trap"
        else tuple(name for _, name in TARGET_EFFECT_PHASES)
    )
    for name in names:
        _, result, parameters = TARGET_BOUND_PHASES[name]
        phase = records[name]
        if phase.guards or tokens(phase.signature) != tokens(result + " " + name + "(" + parameters + ")"):
            raise AssertionError("actual target boundary helpers retain all exact pointer/scalar/status types")
    body = tokens("{" + record.body + "}")
    if original == "micros_grant_syscall_runtime_run_self_test":
        name = TARGET_SETUP_PHASE
        return borrowed_phase_body_tokens(
            body, "if (!" + name + "(fixture, objects, payload_size, &actor)) { goto failure; }",
            "{" + records[name].body + "}", ("actor",), failure="failure", success=True,
        )
    if original == "micros_grant_syscall_test_finish":
        for name in TARGET_FINISH_PHASES:
            body = borrowed_phase_body_tokens(
                body, "if (!" + name + "(fixture, objects, &actor, &failure_stage)) { goto failure; }",
                "{" + records[name].body + "}", ("actor", "failure_stage"), failure="failure", success=True,
            )
        return body
    if original == "micros_grant_syscall_test_handle_control_trap":
        switch, reuse = TARGET_CONTROL_PHASES
        body = borrowed_phase_body_tokens(
            body, "return " + switch + "(fixture, objects, hart, frame, actor, &expected);",
            "{" + records[switch].body + "}", addresses=("expected",),
        )
        return borrowed_phase_body_tokens(
            body, "return " + reuse + "(fixture, hart, frame, actor);", "{" + records[reuse].body + "}",
        )
    for label, name in TARGET_EFFECT_PHASES:
        phase = records[name].body
        body = bound_body_tokens(
            body, "case " + label + ": return " + name + "(fixture);",
            "{ case " + label + ": " + phase + " }",
        )
    return body


def typed_identifier_rows(owner, tag, table, columns):
    prefix = tokens("static const struct " + tag + " " + table + "[] = {")
    declarations = [
        declaration for declaration in file_scope_objects(owner) if tokens(declaration)[:len(prefix)] == prefix
    ]
    if len(declarations) != 1:
        raise AssertionError("one actual typed readonly table owns all original ordered binding rows")
    body = list(tokens(declarations[0]))
    if body[-2:] != ["}", ";"]:
        raise AssertionError("the complete binding array has its original actual storage and finite initializer")
    body = body[len(prefix):-2]
    rows = []
    width = columns * 2 + 2
    for start in range(0, len(body), width):
        row = body[start:start + width]
        if len(row) != width or row[0] != "{" or row[-2:] != ["}", ","]:
            raise AssertionError("each actual binding row has exactly its declared scalar fields")
        values = []
        for index in range(columns):
            value = row[1 + index * 2]
            if not re.fullmatch(r"[A-Za-z_]\w*", value) or row[2 + index * 2] != ("}" if index == columns - 1 else ","):
                raise AssertionError("binding rows contain only complete original typed identifier values")
            values.append(value)
        rows.append(tuple(values))
    if not rows:
        raise AssertionError("the actual original binding table cannot be empty")
    return declarations[0], tuple(rows)


def transition_rows_body_tokens(rows):
    result = list(tokens("{ switch (command) {"))
    seen = set()
    for command, action, actor, next_command in rows:
        if command in seen or command == "GRANT_COMMAND_NONE" or not command.startswith("GRANT_COMMAND_"):
            raise AssertionError("every original non-NONE transition has one actual ordered row")
        seen.add(command)
        if action == "GRANT_CONTROL_SWITCH":
            if actor not in (
                "GRANT_SYSCALL_PROCESS_COUNT", "GRANT_SYSCALL_GRANTOR", "GRANT_SYSCALL_GRANTEE", "GRANT_SYSCALL_WRONG",
            ) or not next_command.startswith("GRANT_COMMAND_") or next_command == "GRANT_COMMAND_NONE":
                raise AssertionError("a next-step row preserves only its original current/fixed actor and command")
            actor = "actor" if actor == "GRANT_SYSCALL_PROCESS_COUNT" else actor
            call = "grant_syscall_test_schedule_next(fixture, actor, " + actor + ", " + next_command + ", frame)"
        elif action in ("GRANT_CONTROL_REUSE_GRANTOR", "GRANT_CONTROL_FINISH"):
            if actor != "GRANT_SYSCALL_PROCESS_COUNT" or next_command != "GRANT_COMMAND_NONE":
                raise AssertionError("reuse/finish rows cannot hide unused alternate actor/command inputs")
            call = (
                "grant_syscall_test_schedule_reuse" if action == "GRANT_CONTROL_REUSE_GRANTOR"
                else "grant_syscall_test_schedule_finish"
            ) + "(fixture, frame)"
        else:
            raise AssertionError("a transition row cannot introduce another control action")
        result.extend(tokens("case " + command + ": return " + call + ";"))
    result.extend(tokens("case GRANT_COMMAND_NONE: return false; } return false; }"))
    return tuple(result)


def transition_actual_body_tokens(record, owner=None):
    owner = source(SYSCALL_TEST_SOURCES[4]) if owner is None else owner
    work_declarations(owner, TARGET_TRANSITION_TAG, TARGET_TRANSITION_FIELDS)
    _, rows = typed_identifier_rows(owner, TARGET_TRANSITION_TAG, TARGET_TRANSITION_TABLE, 4)
    if len(rows) != 46:
        raise AssertionError("all forty-six original transition rows remain actual and ordered")
    expected = tokens("""
        {
            size_t index;
            for (index = 0; index < sizeof(transition_bindings) / sizeof(transition_bindings[0]); ++index) {
                const struct grant_syscall_transition *binding = &transition_bindings[index];
                if (binding->command != command) { continue; }
                switch (binding->action) {
                case GRANT_CONTROL_SWITCH:
                    return grant_syscall_test_schedule_next(
                        fixture, actor,
                        binding->next_actor == GRANT_SYSCALL_PROCESS_COUNT ? actor : binding->next_actor,
                        binding->next_command, frame
                    );
                case GRANT_CONTROL_REUSE_GRANTOR:
                    return grant_syscall_test_schedule_reuse(fixture, frame);
                case GRANT_CONTROL_FINISH:
                    return grant_syscall_test_schedule_finish(fixture, frame);
                case GRANT_CONTROL_NONE:
                    return false;
                }
            }
            return false;
        }
    """)
    if tokens("{" + record.body + "}") != expected:
        raise AssertionError("transition lookup binds the actual array extent/row and original scheduler arguments/fallback")
    return transition_rows_body_tokens(rows)


def arming_rows_body_tokens(rows, templates, scalar):
    result = ["{", *scalar, *tokens("switch (command) {")]
    seen = set()
    groups = []
    index = 0
    while index < len(rows):
        helper = rows[index][1]
        if helper not in templates or helper in groups:
            raise AssertionError("each actual original template has one contiguous ordered label group")
        groups.append(helper)
        while index < len(rows) and rows[index][1] == helper:
            command = rows[index][0]
            if command in seen or command == "GRANT_COMMAND_NONE" or not command.startswith("GRANT_COMMAND_"):
                raise AssertionError("actual command bindings preserve each non-NONE identity exactly once")
            seen.add(command)
            result.extend(tokens("case " + command + ":"))
            index += 1
        template = list(tokens(templates[helper]))
        if template[:1] != ["{"] or template[-1:] != ["}"]:
            raise AssertionError("the complete actual arming function retains its original nested case scope")
        result.extend(restore_state_members(
            template[1:-1], "input", ("actor", "context", "command", "stale_endpoint"), ("->",),
        ))
    if set(groups) != set(templates):
        raise AssertionError("every actual original arming template participates in its real command table")
    result.extend(tokens("case GRANT_COMMAND_NONE: return false; } return false; }"))
    return tuple(result)


def arming_actual_body_tokens(record, commands=None, copies=None, header=None):
    commands = source(SYSCALL_TEST_SOURCES[2]) if commands is None else commands
    copies = source(SYSCALL_TEST_SOURCES[3]) if copies is None else copies
    header = source(PRIVATE_HEADERS[2]) if header is None else header
    work_declarations(header, TARGET_ARMING_TAG, TARGET_ARMING_FIELDS)
    work_declarations(commands, TARGET_COMMAND_BINDING_TAG, TARGET_COMMAND_BINDING_FIELDS)
    _, rows = typed_identifier_rows(commands, TARGET_COMMAND_BINDING_TAG, TARGET_COMMAND_TABLE, 2)
    if len(rows) != 46 or {row[1] for row in rows} != set(TARGET_ARMING_HELPERS):
        raise AssertionError("all forty-six real labels bind only the thirty-eight original typed templates")
    owners = (function_records(commands), function_records(copies))
    templates = {}
    for index, name in enumerate(TARGET_ARMING_HELPERS):
        helper = owners[index >= 14][name]
        result = "static bool" if index < 14 else "bool"
        if helper.guards or tokens(helper.signature) != tokens(
            result + " " + name + "(struct grant_syscall_test_fixture *fixture, const struct grant_syscall_arming_input *input)"
        ):
            raise AssertionError("arming templates retain only exact fixture and non-retained original inputs")
        if len(re.findall(r"\bgrant_syscall_test_arm_raw\s*\(", c_lexical_text(helper.body))) != 1:
            raise AssertionError("each complete original raw-arming body remains actual exactly once")
        templates[name] = "{" + helper.body + "}"
    body = list(tokens("{" + record.body + "}"))
    if body[:4] != ["{", "uint64_t", "stale_endpoint", "="]:
        raise AssertionError("the original full-width stale endpoint is still evaluated before every dispatch")
    end = body.index(";")
    scalar = body[1:end + 1]
    expected = tokens("""
        const struct grant_syscall_arming_input input = {
            .actor = actor, .context = context, .command = command, .stale_endpoint = stale_endpoint,
        };
        size_t index;
        for (index = 0; index < sizeof(command_bindings) / sizeof(command_bindings[0]); ++index) {
            if (command_bindings[index].command == command) {
                return command_bindings[index].arm(fixture, &input);
            }
        }
        return false;
    """)
    if tuple(body[end + 1:-1]) != expected:
        raise AssertionError("arming lookup retains exact original input bindings, actual array bounds and false fallback")
    return arming_rows_body_tokens(rows, templates, scalar)


def copy_execution_actual_body_tokens(record, owner=None):
    owner = source(COPY_SOURCES[0]) if owner is None else owner
    declarations = copy_execution_declarations(owner)
    phase = function_records(owner)[COPY_EXECUTION_PHASE]
    local = function_records(owner)[COPY_LOCAL_RANGE_PHASE]
    expected = (
        "static bool " + COPY_EXECUTION_PHASE
        + "(struct grant_copy_execution *work, uintptr_t local_address, size_t length, uint32_t required_permission)"
    )
    if phase.guards or tokens(phase.signature) != tokens(expected):
        raise AssertionError("actual participant-plan phase retains its complete file-local typed contract")
    if local.guards or tokens(local.signature) != tokens(
        "static bool " + COPY_LOCAL_RANGE_PHASE + "(uintptr_t local_address, size_t length)"
    ):
        raise AssertionError("local range admission retains its complete pure scalar types")
    return copy_execution_body_tokens(
        "{" + record.body + "}", "{" + phase.body + "}", declarations, "{" + local.body + "}",
    )


def validation_body_tokens(caller, phase, declarations):
    body = list(tokens(caller))
    call = list(tokens(
        VALIDATION_PHASE + "(&work, endpoint_registry, objects, length, required_permission);"
    ))
    positions = [index for index in range(len(body)) if body[index:index + len(call)] == call]
    phase = list(tokens(phase))
    if (
        len(positions) != 1 or phase[:1] != ["{"] or phase[-1:] != ["}"]
        or body[positions[0] + len(call):positions[0] + len(call) + 2] != ["done", ":"]
    ):
        raise AssertionError("remote-only planning returns directly to the original common IRQ/result exit")
    inner = phase[1:-1]
    if {"goto", "break", "continue"} & set(inner):
        raise AssertionError("remote-only planning cannot borrow a caller label or loop")
    restored = []
    index = 0
    while index < len(inner):
        if inner[index] == "return":
            if inner[index:index + 2] != ["return", ";"]:
                raise AssertionError("the void remote phase preserves the original caller-owned error result")
            restored.extend(tokens("goto done;"))
            index += 2
        else:
            restored.append(inner[index])
            index += 1
    start = positions[0]
    body[start:start + len(call)] = restored
    return restore_automatic_work_tokens(body, VALIDATION_WORK_TAG, VALIDATION_FIELDS, declarations)


def validation_actual_body_tokens(record, owner=None):
    owner = source(COPY_SOURCES[0]) if owner is None else owner
    declarations = work_declarations(owner, VALIDATION_WORK_TAG, VALIDATION_FIELDS)
    phase = function_records(owner)[VALIDATION_PHASE]
    if phase.guards or tokens(phase.signature) != tokens(
        "static void " + VALIDATION_PHASE + "(" + VALIDATION_PARAMETERS + ")"
    ):
        raise AssertionError("remote-only planning retains exact complete file-local scalar and state types")
    return validation_body_tokens("{" + record.body + "}", "{" + phase.body + "}", declarations)


def syscall_body_tokens(caller, dispatch, create, declarations):
    body = list(tokens(caller))
    dispatch = list(tokens(dispatch))
    create = list(tokens(create))
    if (
        dispatch[:1] != ["{"] or dispatch[-1:] != ["}"]
        or create[:1] != ["{"] or create[-1:] != ["}"]
    ):
        raise AssertionError("actual captured phases retain complete function scope")
    create_inner = create[1:-1]
    if {"break", "continue", "goto", "switch"} & set(create_inner):
        raise AssertionError("create preparation cannot borrow another switch/loop/caller exit")
    restored = []
    index = 0
    while index < len(create_inner):
        if create_inner[index] == "return":
            if create_inner[index:index + 2] != ["return", ";"]:
                raise AssertionError("create helper preserves only original case exits and caller-owned status")
            restored.extend(tokens("break;"))
            index += 2
        else:
            restored.append(create_inner[index])
            index += 1
    restored.extend(tokens("break;"))
    dispatch_inner = dispatch[1:-1]
    if {"return", "continue", "goto"} & set(dispatch_inner):
        raise AssertionError("dispatch retains its original complete operation switch")
    branch = list(tokens(
        "case MICROS_SYSCALL_ABI_GRANT_CREATE: "
        + SYSCALL_CREATE_PHASE + "(hart, frame, context, work); break;"
    ))
    positions = [
        index for index in range(len(dispatch_inner)) if dispatch_inner[index:index + len(branch)] == branch
    ]
    if len(positions) != 1:
        raise AssertionError("create dispatch binds the exact captured hart/frame/process/work and sole case exit")
    start = positions[0]
    dispatch_inner[start:start + len(branch)] = [
        *tokens("case MICROS_SYSCALL_ABI_GRANT_CREATE:"), *restored,
    ]
    call = list(tokens(SYSCALL_DISPATCH_PHASE + "(hart, frame, context, &work);"))
    positions = [index for index in range(len(body)) if body[index:index + len(call)] == call]
    if len(positions) != 1:
        raise AssertionError("captured dispatch binds the exact original runtime context and caller-owned state")
    start = positions[0]
    body[start:start + len(call)] = dispatch_inner
    return restore_automatic_work_tokens(body, SYSCALL_WORK_TAG, SYSCALL_FIELDS, declarations)


def syscall_actual_body_tokens(record, owner=None):
    owner = source(FAMILY_PATHS[1][0]) if owner is None else owner
    declarations = work_declarations(owner, SYSCALL_WORK_TAG, SYSCALL_FIELDS)
    records = function_records(owner)
    for name in (SYSCALL_DISPATCH_PHASE, SYSCALL_CREATE_PHASE):
        phase = records[name]
        if phase.guards or tokens(phase.signature) != tokens(
            "static void " + name + "(" + SYSCALL_PHASE_PARAMETERS + ")"
        ):
            raise AssertionError("captured private phases retain every complete original pointer/type binding")
    return syscall_body_tokens(
        "{" + record.body + "}", "{" + records[SYSCALL_DISPATCH_PHASE].body + "}",
        "{" + records[SYSCALL_CREATE_PHASE].body + "}", declarations,
    )


def range_device_body_tokens(caller, phase):
    body = list(tokens(caller))
    guard = list(tokens(
        "switch (" + RANGE_DEVICE_PHASE
        + "(process, device_authority, &current, &remaining, &device_fault, &address_error, &chunk_length)) {"
        "case MICROS_GRANT_OK: continue;"
        "case MICROS_GRANT_ERROR_PHASE: return MICROS_GRANT_ERROR_PHASE;"
        "case MICROS_GRANT_ERROR_INVARIANT: return MICROS_GRANT_ERROR_INVARIANT;"
        "default: return MICROS_GRANT_ERROR_INVARIANT; }"
    ))
    positions = [index for index in range(len(body)) if body[index:index + len(guard)] == guard]
    phase = list(tokens(phase))
    if len(positions) != 1 or phase[:1] != ["{"] or phase[-4:] != ["return", "MICROS_GRANT_OK", ";", "}"]:
        raise AssertionError("device span binds the exact original caller objects and checked success/failure scope")
    inner = phase[1:-4]
    if {"goto", "break", "continue"} & set(inner):
        raise AssertionError("device span cannot borrow an unverified loop or exit label")
    for index, token in enumerate(inner):
        if token == "return" and (
            inner[index + 1:index + 2] not in (["MICROS_GRANT_ERROR_PHASE"], ["MICROS_GRANT_ERROR_INVARIANT"])
            or inner[index + 2:index + 3] != [";"]
        ):
            raise AssertionError("device success occurs only after original validation and deferred-fault advancement")
    mutable = {"current", "remaining", "device_fault", "address_error", "chunk_length"}
    restored = []
    index = 0
    while index < len(inner):
        if inner[index] == "*" and inner[index + 1:index + 2] and inner[index + 1] in mutable:
            restored.append(inner[index + 1])
            index += 2
        else:
            if inner[index] in mutable:
                raise AssertionError("the original range objects are borrowed typed lvalues, not copied pointer state")
            restored.append(inner[index])
            index += 1
    restored.extend(tokens("continue;"))
    start = positions[0]
    body[start:start + len(guard)] = restored
    return tuple(body)


def range_device_actual_body_tokens(record, owner=None):
    owner = source(COPY_SOURCES[1]) if owner is None else owner
    phase = function_records(owner)[RANGE_DEVICE_PHASE]
    if phase.guards or tokens(phase.signature) != tokens(
        "static enum micros_grant_error " + RANGE_DEVICE_PHASE + "(" + RANGE_DEVICE_PARAMETERS + ")"
    ):
        raise AssertionError("actual device span retains every complete original pointer/handle type")
    return range_device_body_tokens("{" + record.body + "}", "{" + phase.body + "}")


def source_set_block(name, paths):
    return "set(\n    " + name + "\n" + "".join("    " + path + "\n" for path in paths) + ")"


def alias_rows():
    return tuple(
        (paths[0], (*paths[1:], header)) for paths, header in zip(SOURCE_SETS.values(), PRIVATE_HEADERS)
    )


def grant_data_plane_cmake_projection(text):
    from tests.host.test_tty_source_layout import tty_source_cmake_projection

    text = tty_source_cmake_projection(text)
    blocks = [(name, paths, cmake_commands(text, "set", name)) for name, paths in SOURCE_SETS.items()]
    if not any(found for _, _, found in blocks):
        if any("${" + name + "}" in text for name in SOURCE_SETS):
            raise AssertionError("grant operands require their complete actual source sets")
        return text
    for index, (name, paths, found) in enumerate(blocks):
        expected = source_set_block(name, paths)
        if len(found) != 1 or found[0] != expected or text.count(expected + "\n\n") != 1:
            raise AssertionError("exact complete ordered grant source sets are required")
        text = text.replace(expected + "\n\n", "", 1)
        variable = "${" + name + "}"
        if text.count(variable) != (1, 2, 1)[index]:
            raise AssertionError("grant source sets have exactly their original four consumer operands")
        text = text.replace(variable, paths[0])
    return text


def restored_cmake(text):
    restored = grant_data_plane_cmake_projection(text)
    if digest_bytes(restored.encode()) != BASE_CMAKE_DIGEST:
        raise AssertionError("whole grant recipe inverse differs outside three sets/four operands")
    return restored


def planner_ast_without_grant_paths(text):
    from tests.host.test_tty_source_layout import tty_planner_projection

    tree = ast.parse(text)
    tree = tty_planner_projection(tree)
    found = set()
    expected = dict(alias_rows())
    for node in tree.body:
        if not isinstance(node, ast.Assign) or len(node.targets) != 1 or not isinstance(node.targets[0], ast.Name):
            continue
        name = node.targets[0].id
        if name == "PATH_CLASSIFICATION_EQUIVALENTS":
            actual = ast.literal_eval(node.value)
            selected = {old: aliases for old, aliases in actual if old in expected}
            if selected != expected or sum(old in expected for old, _ in actual) != 3:
                raise AssertionError("grant aliases preserve complete independent baseline families once")
            node.value.elts = [item for item in node.value.elts if ast.literal_eval(item)[0] not in expected]
            found.add(name)
        elif name == "HOST_UNIT_ONLY_INPUTS":
            actual = ast.literal_eval(node.value)
            if actual.count(STRUCTURAL_TEST_PATH) != 1:
                raise AssertionError("the new grant gate is explicitly host-unit-only once")
            node.value.elts = [item for item in node.value.elts if ast.literal_eval(item) != STRUCTURAL_TEST_PATH]
            found.add(name)
    if found != {"PATH_CLASSIFICATION_EQUIVALENTS", "HOST_UNIT_ONLY_INPUTS"}:
        raise AssertionError("complete grant metadata assignments remain present")
    return tree


def context_ast_without_recipe_pin(text):
    from tests.host.test_qemu_runner_source_layout import context_declaration_projection

    tree = context_declaration_projection(text)
    selected = []
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(
            isinstance(target, ast.Name) and target.id == "RECIPE_DIGESTS" for target in node.targets
        ):
            selected.append(node)
    if len(selected) != 1 or not isinstance(selected[0].value, ast.Dict):
        raise AssertionError("one actual complete recipe digest declaration is required")
    mapping = selected[0].value
    positions = [index for index, key in enumerate(mapping.keys) if ast.literal_eval(key) == "CMakeLists.txt"]
    if len(positions) != 1:
        raise AssertionError("one maintained CMake recipe entry is required")
    mapping.values[positions[0]] = ast.Constant(value=BASE_CMAKE_DIGEST)
    return tree


class GrantDataPlaneSourceLayoutTest(unittest.TestCase):
    def test_completed_contract_has_literal_empty_migrations_and_all_22_real_bounded_owners(self):
        tree = ast.parse(source(STRUCTURAL_TEST_PATH))
        for name in ("SYMBOL_OWNER_MIGRATION", "FUNCTION_TASK_MIGRATION"):
            assigned = [
                node.value for node in tree.body if isinstance(node, ast.Assign)
                and any(isinstance(target, ast.Name) and target.id == name for target in node.targets)
            ]
            self.assertEqual(1, len(assigned), name)
            self.assertIsInstance(assigned[0], ast.Tuple)
            self.assertEqual([], assigned[0].elts, name)
        for paths in FAMILY_PATHS:
            for path in paths:
                with self.subTest(path=path):
                    records = function_records(source(path))
                    self.assertTrue(records)
                    self.assertLessEqual(len(records), 25)
                    for record in records.values():
                        self.assertLessEqual(record.lines, 120, record.name)

    def test_command_arming_has_actual_46_rows_38_typed_templates_and_original_scalar_admission(self):
        header = source(PRIVATE_HEADERS[2])
        work_declarations(header, TARGET_ARMING_TAG, TARGET_ARMING_FIELDS)
        commands = source(SYSCALL_TEST_SOURCES[2])
        work_declarations(commands, TARGET_COMMAND_BINDING_TAG, TARGET_COMMAND_BINDING_FIELDS)
        _, rows = typed_identifier_rows(commands, TARGET_COMMAND_BINDING_TAG, TARGET_COMMAND_TABLE, 2)
        self.assertEqual(46, len(rows))
        self.assertEqual(46, len({row[0] for row in rows}))
        self.assertEqual(set(TARGET_ARMING_HELPERS), {row[1] for row in rows})
        for index, name in enumerate(TARGET_ARMING_HELPERS):
            owner = SYSCALL_TEST_SOURCES[2 if index < 14 else 3]
            record = function_records(source(owner))[name]
            self.assertLessEqual(record.lines, 120)
        self.assertLessEqual(function_records(commands)["grant_syscall_test_arm_command"].lines, 120)

    def test_actual_arming_fixture_keeps_group_scope_flags_scalar_binding_and_every_argument(self):
        table = (
            "static const struct grant_syscall_command_binding command_bindings[] = {"
            "{GRANT_COMMAND_CREATE_READ, arm_create}, {GRANT_COMMAND_CREATE_WRITE, arm_create}, "
            "{GRANT_COMMAND_COPY_STALE_ENDPOINT, arm_stale}, };"
        )
        templates = {
            "arm_create": (
                "{ { uint64_t base = TEST_DATA_ADDRESS; "
                "arm_raw(fixture, input->actor, input->context, input->command, CREATE, base, 64, READ, "
                "0, 0, 0, 0, true, false, NORMAL); return true; } }"
            ),
            "arm_stale": (
                "{ arm_raw(fixture, input->actor, input->context, input->command, COPY_FROM, "
                "input->stale_endpoint, grant, 0, DATA, 1, 0, 0, DEAD_ENDPOINT, false, true, NORMAL); return true; }"
            ),
        }
        scalar = tokens("uint64_t stale_endpoint = (uint64_t)fixture->endpoints[0] + (UINT64_C(1) << 6);")
        expected = (
            "{ uint64_t stale_endpoint = (uint64_t)fixture->endpoints[0] + (UINT64_C(1) << 6); switch (command) { "
            "case GRANT_COMMAND_CREATE_READ: case GRANT_COMMAND_CREATE_WRITE: { uint64_t base = TEST_DATA_ADDRESS; "
            "arm_raw(fixture, actor, context, command, CREATE, base, 64, READ, 0, 0, 0, 0, true, false, NORMAL); return true; } "
            "case GRANT_COMMAND_COPY_STALE_ENDPOINT: arm_raw(fixture, actor, context, command, COPY_FROM, "
            "stale_endpoint, grant, 0, DATA, 1, 0, 0, DEAD_ENDPOINT, false, true, NORMAL); return true; "
            "case GRANT_COMMAND_NONE: return false; } return false; }"
        )
        _, rows = typed_identifier_rows(table, TARGET_COMMAND_BINDING_TAG, TARGET_COMMAND_TABLE, 2)
        self.assertEqual(tokens(expected), arming_rows_body_tokens(rows, templates, scalar))
        for changed in (
            table.replace("arm_stale", "unknown_template"),
            table.replace("GRANT_COMMAND_COPY_STALE_ENDPOINT", "GRANT_COMMAND_CREATE_WRITE"),
            table.replace("GRANT_COMMAND_COPY_STALE_ENDPOINT", "GRANT_COMMAND_NONE"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                _, changed_rows = typed_identifier_rows(changed, TARGET_COMMAND_BINDING_TAG, TARGET_COMMAND_TABLE, 2)
                arming_rows_body_tokens(changed_rows, templates, scalar)
        for before, after in (
            ("input->stale_endpoint", "input->extra"),
            ("return true; }", "saved_input = input; return true; }"),
        ):
            with self.subTest(before=before), self.assertRaises(AssertionError):
                changed = dict(templates)
                changed["arm_stale"] = changed["arm_stale"].replace(before, after)
                arming_rows_body_tokens(rows, changed, scalar)
        for before, after in (
            ("false, true, NORMAL", "false, false, NORMAL"),
            ("grant, 0, DATA, 1", "grant, 1, DATA, 1"),
            ("NORMAL", "CAPTURED"),
        ):
            changed = dict(templates)
            changed["arm_stale"] = changed["arm_stale"].replace(before, after)
            self.assertNotEqual(tokens(expected), arming_rows_body_tokens(rows, changed, scalar))
        with self.assertRaises(AssertionError):
            arming_rows_body_tokens((rows[0], rows[2], rows[1]), templates, scalar)
        with self.assertRaises(AssertionError):
            arming_rows_body_tokens(rows, dict(templates, unused="{ return true; }"), scalar)

    def test_target_transitions_use_actual_typed_ordered_rows_and_bounded_dispatch(self):
        owner = source(SYSCALL_TEST_SOURCES[4])
        work_declarations(owner, TARGET_TRANSITION_TAG, TARGET_TRANSITION_FIELDS)
        _, rows = typed_identifier_rows(owner, TARGET_TRANSITION_TAG, TARGET_TRANSITION_TABLE, 4)
        self.assertEqual(46, len(rows))
        self.assertEqual(46, len({row[0] for row in rows}))
        self.assertNotIn("GRANT_COMMAND_NONE", {row[0] for row in rows})
        self.assertLessEqual(function_records(owner)["grant_syscall_test_transition_after_return"].lines, 120)

    def test_transition_row_fixture_preserves_actual_order_current_fixed_actor_and_control_actions(self):
        actual = (
            "static const struct grant_syscall_transition transition_bindings[] = {"
            "{GRANT_COMMAND_IPC_NOTIFY, GRANT_CONTROL_SWITCH, GRANT_SYSCALL_PROCESS_COUNT, GRANT_COMMAND_CREATE_READ},"
            "{GRANT_COMMAND_REVOKE_UNUSED, GRANT_CONTROL_SWITCH, GRANT_SYSCALL_GRANTEE, GRANT_COMMAND_VALIDATE_READ},"
            "{GRANT_COMMAND_COPY_REVOKED, GRANT_CONTROL_REUSE_GRANTOR, GRANT_SYSCALL_PROCESS_COUNT, GRANT_COMMAND_NONE},"
            "{GRANT_COMMAND_COPY_REUSED_ENDPOINT, GRANT_CONTROL_FINISH, GRANT_SYSCALL_PROCESS_COUNT, GRANT_COMMAND_NONE},"
            "};"
        )
        expected = (
            "{ switch (command) { "
            "case GRANT_COMMAND_IPC_NOTIFY: return grant_syscall_test_schedule_next(fixture, actor, actor, GRANT_COMMAND_CREATE_READ, frame); "
            "case GRANT_COMMAND_REVOKE_UNUSED: return grant_syscall_test_schedule_next(fixture, actor, GRANT_SYSCALL_GRANTEE, GRANT_COMMAND_VALIDATE_READ, frame); "
            "case GRANT_COMMAND_COPY_REVOKED: return grant_syscall_test_schedule_reuse(fixture, frame); "
            "case GRANT_COMMAND_COPY_REUSED_ENDPOINT: return grant_syscall_test_schedule_finish(fixture, frame); "
            "case GRANT_COMMAND_NONE: return false; } return false; }"
        )
        _, rows = typed_identifier_rows(actual, TARGET_TRANSITION_TAG, TARGET_TRANSITION_TABLE, 4)
        self.assertEqual(tokens(expected), transition_rows_body_tokens(rows))
        for changed in (
            actual.replace("GRANT_CONTROL_REUSE_GRANTOR, GRANT_SYSCALL_PROCESS_COUNT", "GRANT_CONTROL_REUSE_GRANTOR, GRANT_SYSCALL_WRONG"),
            actual.replace("GRANT_CONTROL_FINISH, GRANT_SYSCALL_PROCESS_COUNT, GRANT_COMMAND_NONE", "GRANT_CONTROL_FINISH, GRANT_SYSCALL_PROCESS_COUNT, GRANT_COMMAND_CREATE_READ"),
            actual.replace("GRANT_CONTROL_FINISH", "GRANT_CONTROL_NONE"),
            actual.replace("GRANT_COMMAND_COPY_REVOKED", "GRANT_COMMAND_IPC_NOTIFY"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                _, changed_rows = typed_identifier_rows(changed, TARGET_TRANSITION_TAG, TARGET_TRANSITION_TABLE, 4)
                transition_rows_body_tokens(changed_rows)
        for changed in (
            actual.replace("static const struct", "static struct"),
            actual.replace("GRANT_COMMAND_CREATE_READ}", "GRANT_COMMAND_CREATE_READ, EXTRA}"),
            actual.replace("GRANT_SYSCALL_GRANTEE", "(GRANT_SYSCALL_GRANTEE + 1)"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                typed_identifier_rows(changed, TARGET_TRANSITION_TAG, TARGET_TRANSITION_TABLE, 4)
        for changed in (
            actual.replace("GRANT_SYSCALL_GRANTEE", "GRANT_SYSCALL_WRONG"),
            actual.replace("GRANT_COMMAND_VALIDATE_READ", "GRANT_COMMAND_VALIDATE_ZERO"),
        ):
            _, changed_rows = typed_identifier_rows(changed, TARGET_TRANSITION_TAG, TARGET_TRANSITION_TABLE, 4)
            self.assertNotEqual(tokens(expected), transition_rows_body_tokens(changed_rows))
        self.assertNotEqual(tokens(expected), transition_rows_body_tokens(tuple(reversed(rows))))

    def test_target_setup_finish_control_and_effect_expose_exact_bounded_domains(self):
        for name, (owner, result, parameters) in TARGET_BOUND_PHASES.items():
            with self.subTest(phase=name):
                record = function_records(source(SYSCALL_TEST_SOURCES[owner]))[name]
                self.assertEqual(tokens(result + " " + name + "(" + parameters + ")"), tokens(record.signature))
                self.assertLessEqual(record.lines, 120)
        for owner, name in (
            (0, "micros_grant_syscall_runtime_run_self_test"), (0, "micros_grant_syscall_test_finish"),
            (6, "micros_grant_syscall_test_handle_control_trap"), (5, "grant_syscall_test_validate_command_effect"),
        ):
            self.assertLessEqual(function_records(source(SYSCALL_TEST_SOURCES[owner]))[name].lines, 120)

    def test_borrowed_target_scalars_keep_actual_actor_stage_loops_and_fatal_root_exit(self):
        caller = "{ if (!release(fixture, objects, &actor, &failure_stage)) { goto failure; } original_root_report(); }"
        phase = (
            "{ for (*actor = 0; *actor < 3; ++*actor) { "
            "if (!original_release(*actor)) { *failure_stage = 0x20 + *actor; return false; } } return true; }"
        )
        expected = (
            "{ for (actor = 0; actor < 3; ++actor) { "
            "if (!original_release(actor)) { failure_stage = 0x20 + actor; goto failure; } } original_root_report(); }"
        )
        binding = "if (!release(fixture, objects, &actor, &failure_stage)) { goto failure; }"
        self.assertEqual(tokens(expected), borrowed_phase_body_tokens(
            tokens(caller), binding, phase, ("actor", "failure_stage"), failure="failure", success=True,
        ))
        for changed in (
            caller.replace("&actor", "actor"),
            caller.replace("&failure_stage", "&other"),
            caller.replace("goto failure;", "return false;"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                borrowed_phase_body_tokens(
                    tokens(changed), binding, phase, ("actor", "failure_stage"), failure="failure", success=True,
                )
        for changed in (
            phase.replace("original_release(*actor)", "original_release(actor)"),
            phase.replace("return false;", "return true;"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                borrowed_phase_body_tokens(
                    tokens(caller), binding, changed, ("actor", "failure_stage"), failure="failure", success=True,
                )

    def test_borrowed_control_context_keeps_root_expected_object_and_supervisor_boundary(self):
        caller = (
            "{ if (action == SWITCH) { return switch_control(fixture, objects, hart, frame, actor, &expected); } "
            "frame->sp = saved_sp; frame->sstatus &= ~control_mask; return SUPERVISOR; }"
        )
        phase = (
            "{ original_copy(expected, &next->context, sizeof(*expected)); "
            "if (!original_compare(frame, expected, sizeof(*expected))) { return MISMATCH; } return USER; }"
        )
        expected = (
            "{ if (action == SWITCH) { original_copy(&expected, &next->context, sizeof(expected)); "
            "if (!original_compare(frame, &expected, sizeof(expected))) { return MISMATCH; } return USER; } "
            "frame->sp = saved_sp; frame->sstatus &= ~control_mask; return SUPERVISOR; }"
        )
        binding = "return switch_control(fixture, objects, hart, frame, actor, &expected);"
        self.assertEqual(
            tokens(expected), borrowed_phase_body_tokens(tokens(caller), binding, phase, addresses=("expected",)),
        )
        for changed in (
            caller.replace("&expected", "&copy"),
            caller.replace("actor, &expected", "other_actor, &expected"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                borrowed_phase_body_tokens(tokens(changed), binding, phase, addresses=("expected",))
        self.assertNotEqual(
            tokens(expected), borrowed_phase_body_tokens(
                tokens(caller.replace("~control_mask", "0")), binding, phase, addresses=("expected",),
            ),
        )

    def test_syscall_fixture_owns_exact_38_fields_but_not_root_assembly_save_objects(self):
        header = source(PRIVATE_HEADERS[2])
        work_declarations(header, TARGET_FIXTURE_TAG, TARGET_FIELDS)
        self.assertEqual(38, len(TARGET_FIELDS))
        self.assertEqual(29, len(TARGET_EXPORTS))
        fixture = source(SYSCALL_TEST_SOURCES[1])
        getter = function_records(fixture)[TARGET_FIXTURE_GETTER]
        self.assertEqual(tokens("{ return &fixture_state; }"), tokens("{" + getter.body + "}"))
        self.assertIn(
            "static struct grant_syscall_test_fixture fixture_state = {\n"
            "    .dispatch_actor = GRANT_SYSCALL_PROCESS_COUNT,\n};", fixture,
        )
        constants = tuple(
            declaration for declaration in file_scope_objects(fixture) if declaration.startswith("const uint64_t TEST_")
        )
        self.assertEqual(7, len(constants))
        self.assertEqual(8, len(tuple(file_scope_objects(fixture))))
        self.assertEqual(TARGET_CONSTANTS_DIGEST, digest_tokens(tokens("\n".join("static " + value for value in constants))))
        for path in SYSCALL_TEST_SOURCES[2:]:
            expected = ()
            if path == SYSCALL_TEST_SOURCES[4] and (3, "transition_after_return") not in FUNCTION_TASK_MIGRATION:
                expected = (typed_identifier_rows(source(path), TARGET_TRANSITION_TAG, TARGET_TRANSITION_TABLE, 4)[0],)
            if path == SYSCALL_TEST_SOURCES[2] and (3, "arm_command") not in FUNCTION_TASK_MIGRATION:
                expected = (typed_identifier_rows(source(path), TARGET_COMMAND_BINDING_TAG, TARGET_COMMAND_TABLE, 2)[0],)
            self.assertEqual(expected, tuple(file_scope_objects(source(path))), path)
        self.assertEqual(TARGET_SAVED_OBJECTS, tuple(file_scope_objects(source(SYSCALL_TEST_SOURCES[0]))))
        for declaration in TARGET_SAVED_OBJECTS:
            self.assertIn("extern " + declaration, header)
        types = header[header.index("#define TEST_SSTATUS_UBE"):header.index("struct grant_syscall_test_fixture {")]
        self.assertEqual(TARGET_TYPES_DIGEST, digest_tokens(tokens(types)))
        externs = header[header.index("extern const unsigned char micros_grant_syscall_payload_start"):header.index("struct grant_syscall_test_fixture *grant_syscall_test_context")]
        self.assertEqual(TARGET_EXTERNS_DIGEST, digest_tokens(tokens(externs)))
        root = function_records(source(SYSCALL_TEST_SOURCES[0]))["micros_grant_syscall_runtime_run_self_test"]
        self.assertIn('__asm__ volatile("fence.i"', root.body)
        self.assertIn('"mv %0, sp\\n"', root.body)
        self.assertIn("micros_scheduler_test_enter_without_timer(", root.body)

    def test_target_fixture_inverse_preserves_raw_control_snapshot_arrays_and_pure_helpers(self):
        caller = (
            "{ struct grant_syscall_test_fixture *fixture = grant_syscall_test_context(); "
            "if (!snapshot_failure_state(fixture)) { return false; } "
            "arm_raw(fixture, actor, &fixture->scripts[actor].captured, command); "
            "arm_control(context); original_copy(fixture->page_table_snapshot, sizeof(fixture->page_table_snapshot)); "
            "micros_grant_syscall_test_saved_sp = original_sp; return fixture->thread_live[actor]; }"
        )
        expected = (
            "{ if (!snapshot_failure_state()) { return false; } "
            "arm_raw(actor, &scripts[actor].captured, command); "
            "arm_control(context); original_copy(page_table_snapshot, sizeof(page_table_snapshot)); "
            "micros_grant_syscall_test_saved_sp = original_sp; return thread_live[actor]; }"
        )
        original = "micros_grant_syscall_test_after_return"
        self.assertEqual(tokens(expected), target_fixture_body_tokens(original, tokens(caller)))
        for changed in (
            caller.replace("grant_syscall_test_context()", "reset_fixture()"),
            caller.replace("arm_raw(fixture,", "arm_raw(other,"),
            caller.replace("snapshot_failure_state(fixture)", "snapshot_failure_state()"),
            caller.replace("fixture->page_table_snapshot", "fixture->unknown_tables"),
            caller.replace("return fixture->thread_live", "copy = *fixture; return fixture->thread_live"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                target_fixture_body_tokens(original, tokens(changed))
        self.assertEqual(
            tokens("static void arm_raw(size_t actor, struct micros_user_context *context)"),
            fixture_signature_tokens(
                "arm_raw", tokens("void arm_raw(struct grant_syscall_test_fixture *fixture, size_t actor, "
                                  "struct micros_user_context *context)"),
                TARGET_EXPORTS, TARGET_FIXTURE_TAG, TARGET_CONTEXT_HELPERS,
            ),
        )
        self.assertEqual(
            tokens("static void arm_control(struct micros_user_context *context)"),
            fixture_signature_tokens(
                "arm_control", tokens("void arm_control(struct micros_user_context *context)"),
                TARGET_EXPORTS, TARGET_FIXTURE_TAG, TARGET_CONTEXT_HELPERS,
            ),
        )
        owner = "struct grant_syscall_test_fixture {" + "".join(declaration for declaration, _ in TARGET_FIELDS) + "};"
        work_declarations(owner, TARGET_FIXTURE_TAG, TARGET_FIELDS)
        for changed in (
            owner.replace("page_table_snapshot[GRANT_SYSCALL_PROCESS_COUNT][3]", "page_table_snapshot[GRANT_SYSCALL_PROCESS_COUNT][2]"),
            owner.replace("user_bytes_snapshot[GRANT_SYSCALL_PROCESS_COUNT][GRANT_SYSCALL_PAGE_COUNT]", "*user_bytes_snapshot"),
            owner.replace("size_t dispatch_actor;", "uint32_t dispatch_actor;"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                work_declarations(changed, TARGET_FIXTURE_TAG, TARGET_FIELDS)

    def test_actual_private_owner_directions_are_acyclic_and_raw_control_are_fixture_leaves(self):
        directions = {
            0: {0: (1,), 1: ()},
            3: {0: (1, 2, 7), 1: (), 2: (1, 3), 3: (1,), 4: (1, 2), 5: (1,), 6: (1, 2, 4, 5, 7), 7: (1,)},
            4: {0: (2, 3), 1: (5,), 2: (1, 5), 3: (1, 5), 4: (1, 5, 6, 7), 5: (), 6: (1, 5), 7: (1, 5)},
        }
        for family, allowed in directions.items():
            records = owned_records(family)
            owners = {name: FAMILY_PATHS[family].index(path) for path, name in records}
            for (path, name), record in records.items():
                origin = FAMILY_PATHS[family].index(path)
                destinations = {
                    owners[word] for word in tokens(record.body) if word in owners and owners[word] != origin
                }
                self.assertLessEqual(destinations, set(allowed[origin]), (path, name, destinations))
            for origin, path in enumerate(FAMILY_PATHS[family]):
                destinations = {
                    owners[word] for declaration in file_scope_objects(source(path))
                    for word in tokens(declaration) if word in owners and owners[word] != origin
                }
                self.assertLessEqual(destinations, set(allowed[origin]), (path, destinations))
            remaining = set(allowed)
            while remaining:
                leaves = {owner for owner in remaining if not set(allowed[owner]) & remaining}
                self.assertTrue(leaves, (family, remaining))
                remaining -= leaves

    def test_target_mechanism_has_bounded_domains_and_actual_root_irq_report_lifetime(self):
        owner = source(FAMILY_PATHS[2][0])
        records = function_records(owner)
        first = next(iter(records.values()))
        prefix = owner[:owner.index(first.signature)]
        self.assertEqual(MECHANISM_PREFIX_DIGEST, digest_tokens(tokens(prefix)))
        self.assertEqual(tuple(file_scope_objects(prefix)), tuple(file_scope_objects(owner)))
        work_declarations(owner, MECHANISM_TAG, MECHANISM_FIELDS)
        root = records["micros_grant_runtime_run_self_test"]
        self.assertLessEqual(root.lines, 120)
        self.assertIn("static const struct micros_privilege_profile profile", root.body)
        self.assertIn("micros_ipc_runtime_initialize(&profile, 1)", root.body)
        self.assertEqual(1, root.body.count("riscv_irq_save()"))
        self.assertEqual(1, root.body.count("riscv_irq_restore(saved_status)"))
        for name in (*MECHANISM_PHASES, *MECHANISM_DENIALS):
            self.assertIn(name, records)
            self.assertLessEqual(records[name].lines, 120)
            self.assertNotIn("riscv_irq_", records[name].body)

    def test_mechanism_failure_inverse_keeps_stage_root_report_irq_and_success_only_cleanup(self):
        caller = (
            "{ saved_status = riscv_irq_save(); "
            "if (!exercise(&work)) { goto done; } if (!cleanup(&work)) { goto done; } "
            "passed = true; done: original_failure_report(work.failure_stage); "
            "riscv_irq_restore(saved_status); return passed; }"
        )
        exercise = "{ work->failure_stage = 4; original_snapshot(); if (copy_failed()) { return false; } return true; }"
        cleanup = "{ original_cancel_and_release(); if (!original_baseline()) { return false; } return true; }"
        expected = (
            "{ saved_status = riscv_irq_save(); "
            "work->failure_stage = 4; original_snapshot(); if (copy_failed()) { goto done; } "
            "original_cancel_and_release(); if (!original_baseline()) { goto done; } "
            "passed = true; done: original_failure_report(work.failure_stage); "
            "riscv_irq_restore(saved_status); return passed; }"
        )
        phases = (("exercise", exercise), ("cleanup", cleanup))
        self.assertEqual(tokens(expected), common_failure_body_tokens(tokens(caller), phases))
        for changed in (
            caller.replace("exercise(&work)", "exercise(&copy)"),
            caller.replace("goto done;", "return false;", 1),
            caller.replace("cleanup(&work)", "cleanup(work)"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                common_failure_body_tokens(tokens(changed), phases)
        with self.assertRaises(AssertionError):
            common_failure_body_tokens(tokens(caller), (
                ("exercise", exercise.replace("return false;", "return true;")), phases[1],
            ))
        self.assertNotEqual(
            tokens(expected), common_failure_body_tokens(tokens(caller.replace("original_failure_report", "changed_report")), phases),
        )

    def test_denial_predicates_keep_actual_short_circuit_calls_canaries_and_bound_work(self):
        caller = "{ original_canary_snapshot(); if (!authority(work) || !range(work) || !mapping(work)) { return false; } return true; }"
        predicates = (
            ("authority", "{ return !(wrong_actor() != UNAUTHORIZED || wrong_endpoint() != UNAUTHORIZED); }"),
            ("range", "{ return !(overflow() != RANGE || oversize() != RANGE); }"),
            ("mapping", "{ return !(unmapped() != FAULT || zero_length() != OK || !original_canaries()); }"),
        )
        expected = (
            "{ original_canary_snapshot(); if (wrong_actor() != UNAUTHORIZED || wrong_endpoint() != UNAUTHORIZED "
            "|| overflow() != RANGE || oversize() != RANGE "
            "|| unmapped() != FAULT || zero_length() != OK || !original_canaries()) { return false; } return true; }"
        )
        self.assertEqual(tokens(expected), denial_predicate_body_tokens(tokens(caller), predicates))
        for changed in (
            caller.replace("authority(work)", "authority(other)"),
            caller.replace("range(work)", "range(&work)"),
            caller.replace("!mapping(work)", "mapping(work)"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                denial_predicate_body_tokens(tokens(changed), predicates)
        with self.assertRaises(AssertionError):
            denial_predicate_body_tokens(tokens(caller), (
                predicates[0], ("range", "{ return true; }"), predicates[2],
            ))
        changed = (predicates[0], predicates[1], ("mapping", predicates[2][1].replace("zero_length() != OK || ", "")))
        self.assertNotEqual(tokens(expected), denial_predicate_body_tokens(tokens(caller), changed))

    def test_seeded_model_exposes_real_operation_and_authorization_owners(self):
        header = source(PRIVATE_HEADERS[1])
        for tag, fields in ((MODEL_RUN_TAG, MODEL_RUN_FIELDS), (MODEL_ITERATION_TAG, MODEL_ITERATION_FIELDS)):
            work_declarations(header, tag, fields)
        operations = function_records(source(HOST_SOURCES[6]))
        for _, name, _ in MODEL_OPERATIONS:
            self.assertIn(name, operations)
            self.assertLessEqual(operations[name].lines, 120)
        copy = source(HOST_SOURCES[7])
        work_declarations(copy, MODEL_AUTH_TAG, MODEL_AUTH_FIELDS)
        authorization = function_records(copy)
        for name in (*MODEL_AUTH_PHASES, MODEL_AUTHORIZE):
            self.assertIn(name, authorization)
            self.assertLessEqual(authorization[name].lines, 120)
        root = function_records(source(HOST_SOURCES[4]))
        for name in (MODEL_SELECT, MODEL_DISPATCH, MODEL_COMPARE, "grant_test_grant_model"):
            self.assertIn(name, root)
            self.assertLessEqual(root[name].lines, 120)
        self.assertIn("const uint32_t seed = UINT32_C(0x38a110c5);", root["grant_test_grant_model"].body)
        self.assertIn("size_t coverage[GRANT_MODEL_OPERATION_COUNT] = {0};", root["grant_test_grant_model"].body)
        self.assertIn("size_t step;", root["grant_test_grant_model"].body)

    def test_model_case_inverse_preserves_denial_completion_and_inner_loop_breaks(self):
        actual = (
            "{ if (denied) { if (changed_output) { return model_fail(step); } return true; } "
            "for (index = 0; index < capacity; ++index) { if (selected) { break; } } "
            "original_copy_and_compare(); return true; }"
        )
        expected = (
            "{ if (denied) { if (changed_output) { return model_fail(step); } break; } "
            "for (index = 0; index < capacity; ++index) { if (selected) { break; } } "
            "original_copy_and_compare(); break; }"
        )
        self.assertEqual(tokens(expected), model_case_success_tokens(tokens(actual), 2))
        for changed in (
            actual.replace("return true;", "return false;", 1),
            actual.replace("original_copy_and_compare();", "return true; original_copy_and_compare();"),
            actual[:-len("return true; }")] + "return false; }",
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                model_case_success_tokens(tokens(changed), 2)
        self.assertNotEqual(
            tokens(expected), model_case_success_tokens(tokens(actual.replace("if (selected) { break; }", "if (selected) { continue; }")), 2),
        )

    def test_model_phase_fixture_keeps_one_draw_forced_prefix_and_coverage_trace_compare_order(self):
        caller = (
            "{ for (step = 0; step < 4096; ++step) { "
            "grant_test_model_select_operation(&run, &iteration, step); "
            "if (!grant_test_model_apply_operation(fixture, &run, &iteration, step)) { return false; } "
            "++coverage[iteration.operation]; original_trace(iteration); original_full_compare(iteration); "
            "} original_final_coverage_and_report(); return true; }"
        )
        selection = (
            "{ iteration->random = model_next(&run->random_state); "
            "iteration->operation = (iteration->random >> 24) % 13; "
            "if (step < 13) { iteration->operation = step; } }"
        )
        dispatch = (
            "{ switch (iteration->operation) { case 0: { "
            "if (expected_denial) { break; } original_success_copy(); break; "
            "} default: return model_fail(step); } }"
        )
        binding = "if (!grant_test_model_apply_operation(fixture, &run, &iteration, step)) { return false; }"
        select_binding = "grant_test_model_select_operation(&run, &iteration, step);"
        expected = (
            "{ for (step = 0; step < 4096; ++step) { "
            "iteration->random = model_next(&run->random_state); "
            "iteration->operation = (iteration->random >> 24) % 13; "
            "if (step < 13) { iteration->operation = step; } "
            "switch (iteration->operation) { case 0: { if (expected_denial) { break; } original_success_copy(); break; } "
            "default: return model_fail(step); } "
            "++coverage[iteration.operation]; original_trace(iteration); original_full_compare(iteration); "
            "} original_final_coverage_and_report(); return true; }"
        )
        expand = lambda text, draw=selection: bound_body_tokens(
            bound_body_tokens(tokens(text), binding, dispatch), select_binding, draw,
        )
        self.assertEqual(tokens(expected), expand(caller))
        for changed in (
            caller.replace(select_binding, select_binding + " " + select_binding),
            caller.replace("&run, &iteration, step);", "&other, &iteration, step);"),
            caller.replace("return false;", "return true;"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                expand(changed)
        for changed in (
            caller.replace("original_trace(iteration); ", ""),
            caller.replace("++coverage[iteration.operation];", "++coverage[0];"),
            caller.replace(
                "original_trace(iteration); original_full_compare(iteration);",
                "original_full_compare(iteration); original_trace(iteration);",
            ),
            caller.replace("4096", "4095"),
        ):
            self.assertNotEqual(tokens(expected), expand(changed))
        self.assertNotEqual(
            tokens(expected), expand(caller, selection.replace(
                "iteration->random = model_next", "model_next(&run->random_state); iteration->random = model_next",
            )),
        )

    def test_actual_model_inverse_rejects_draw_coverage_trace_case_exit_and_extent_drift(self):
        paths = (HOST_SOURCES[4], HOST_SOURCES[6], HOST_SOURCES[7])
        texts = {path: source(path) for path in paths}
        header = source(PRIVATE_HEADERS[1])

        def restored(current, actual_header=header):
            record = function_records(current[HOST_SOURCES[4]])["grant_test_grant_model"]
            body = model_actual_body_tokens(record, current, actual_header)
            body = tuple(normal_names(4).get(token, token) for token in body)
            return digest_tokens(native_fixture_body_tokens("test_grant_model", body))

        expected = BASELINE_BODIES[4]["test_grant_model"]
        self.assertEqual(expected, restored(texts))
        for path, before, after in (
            (paths[0], "step < GRANT_MODEL_OPERATION_COUNT", "step <= GRANT_MODEL_OPERATION_COUNT"),
            (paths[0], "++coverage[iteration.operation]", "++coverage[0]"),
            (paths[1], "produced = MICROS_GRANT_NONE", "produced = UINT32_C(0xdeadbeef)"),
            (paths[2], "memset(&work->sentinel, 0xa5", "memset(&work->sentinel, 0"),
        ):
            with self.subTest(before=before):
                self.assertIn(before, texts[path])
                changed = dict(texts)
                changed[path] = texts[path].replace(before, after, 1)
                self.assertNotEqual(expected, restored(changed))
        for path, before, after in (
            (paths[0], "iteration->random = grant_test_model_next", "grant_test_model_next(&run->random_state); iteration->random = grant_test_model_next"),
            (paths[0], "grant_test_record_model_trace(", "mutated_trace("),
            (paths[0], "struct grant_model_run run;", "struct grant_model_run run = {0};"),
            (paths[0], "fixture, &run, &iteration, step", "fixture, &other, &iteration, step"),
            (paths[2], "        return true;", "        return false;"),
        ):
            with self.subTest(before=before):
                self.assertIn(before, texts[path])
                changed = dict(texts)
                changed[path] = texts[path].replace(before, after, 1)
                with self.assertRaises(AssertionError):
                    restored(changed)
        with self.assertRaises(AssertionError):
            restored(texts, header.replace("grantor_bytes[64]", "grantor_bytes[63]", 1))

    def test_abi_cases_keep_root_static_mapping_rows_and_expose_bounded_domains(self):
        owner = source(FAMILY_PATHS[6][0])
        records = function_records(owner)
        first = next(iter(records.values()))
        self.assertEqual(ABI_TEST_PREFIX_DIGEST, digest_tokens(tokens(owner[:owner.index(first.signature)])))
        work_declarations(owner, ABI_WORK_TAG, ABI_FIELDS)
        root = records["micros_grant_syscall_core_test_run"]
        self.assertIn("static const struct {", root.body)
        self.assertIn("sizeof(mappings) / sizeof(mappings[0])", root.body)
        for phase in ABI_PHASES:
            self.assertIn(phase, records)
            self.assertLessEqual(records[phase].lines, 120)
        self.assertLessEqual(root.lines, 120)

    def test_native_copy_cases_expose_typed_array_chunk_and_authority_phases(self):
        owner = source(FAMILY_PATHS[5][0])
        records = function_records(owner)
        first = next(iter(records.values()))
        prefix = owner[:owner.index(first.signature)]
        self.assertEqual(COPY_TEST_PREFIX_DIGEST, digest_tokens(tokens(prefix)))
        self.assertEqual(tuple(file_scope_objects(prefix)), tuple(file_scope_objects(owner)))
        for original, (tag, fields, phases) in COPY_CASE_OUTLINES.items():
            with self.subTest(original=original):
                work_declarations(owner, tag, fields)
                for phase in phases:
                    self.assertIn(phase, records)
                    self.assertLessEqual(records[phase].lines, 120)
                self.assertLessEqual(records[original].lines, 120)

    def test_native_deterministic_cases_expose_ordered_bounded_domain_phases(self):
        for original, (path, tag, fields, phases) in NATIVE_CASE_OUTLINES.items():
            owner = source(path)
            records = function_records(owner)
            with self.subTest(original=original):
                self.assertEqual(
                    tuple(tokens(declaration) for declaration, _ in fields), work_declarations(owner, tag, fields),
                )
                for phase in phases:
                    self.assertIn(phase, records)
                    self.assertLessEqual(records[phase].lines, 120)
                name = next(name for name, old in normal_names(4).items() if old == original)
                self.assertLessEqual(records[name].lines, 120)

    def test_boolean_case_inverse_preserves_first_failure_nested_scope_and_terminal_result(self):
        caller = "{ if (!fill(fixture, &work)) { return false; } return finish(fixture, &work); }"
        first = "{ EXPECT_TRUE(original_setup(fixture)); { uint32_t sentinel = 0xa5; original_check(sentinel); } return true; }"
        final = "{ original_restore(fixture); return original_final_comparison(fixture); }"
        phases = (
            ("fill", "fixture, &work", first, False),
            ("finish", "fixture, &work", final, True),
        )
        expected = (
            "{ EXPECT_TRUE(original_setup(fixture)); { uint32_t sentinel = 0xa5; original_check(sentinel); } "
            "original_restore(fixture); return original_final_comparison(fixture); }"
        )
        self.assertEqual(tokens(expected), boolean_case_body_tokens(tokens(caller), phases))
        for changed in (
            caller.replace("fixture, &work", "other, &work", 1),
            caller.replace("fixture, &work", "fixture, &copy", 1),
            caller.replace("return false;", "return true;", 1),
            caller.replace("return finish", "finish"),
            caller.replace("return finish", "if (!fill(fixture, &work)) { return false; } return finish"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                boolean_case_body_tokens(tokens(changed), phases)
        with self.assertRaises(AssertionError):
            boolean_case_body_tokens(tokens(caller), (
                ("fill", "fixture, &work", first.replace("return true;", "return false;"), False), phases[1],
            ))
        changed = (
            ("fill", "fixture, &work", first.replace("0xa5", "0"), False), phases[1],
        )
        self.assertNotEqual(tokens(expected), boolean_case_body_tokens(tokens(caller), changed))

    def test_array_work_inverse_preserves_extents_initializers_sizeof_and_original_commit_bytes(self):
        fields = (
            ("unsigned char bytes[16];", False),
            ("struct plan retained;", True),
            ("size_t index;", False),
        )
        declarations = tuple(tokens(declaration) for declaration, _ in fields)
        caller = (
            "{ struct copy_case work; work.retained = (struct plan){0}; "
            "if (!seed(&work)) { return false; } return check(&work); }"
        )
        phases = (
            ("seed", "&work", "{ for (work->index = 0; work->index < sizeof(work->bytes); ++work->index) "
             "{ work->bytes[work->index] = 0xcc; } return true; }", False),
            ("check", "&work", "{ original_commit(&work->retained, work->bytes); return work->bytes[12] == 0xcc; }", True),
        )
        expected = (
            "{ unsigned char bytes[16]; struct plan retained = {0}; size_t index; "
            "for (index = 0; index < sizeof(bytes); ++index) { bytes[index] = 0xcc; } "
            "original_commit(&retained, bytes); return bytes[12] == 0xcc; }"
        )
        self.assertEqual(
            tokens(expected), restore_automatic_work_tokens(
                boolean_case_body_tokens(tokens(caller), phases), "copy_case", fields, declarations,
            ),
        )
        for changed in (
            caller.replace("struct copy_case work;", "struct copy_case work = {0};"),
            caller.replace("(struct plan){0}", "(struct other_plan){0}"),
            caller.replace("return check", "copy = work; return check"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                restore_automatic_work_tokens(
                    boolean_case_body_tokens(tokens(changed), phases), "copy_case", fields, declarations,
                )
        changed = (phases[0], ("check", "&work", phases[1][2].replace("bytes[12]", "bytes[11]"), True))
        self.assertNotEqual(
            tokens(expected), restore_automatic_work_tokens(
                boolean_case_body_tokens(tokens(caller), changed), "copy_case", fields, declarations,
            ),
        )

    def test_abi_mapping_inverse_reads_actual_static_rows_and_keeps_root_sizeof_and_sentinels(self):
        caller = (
            "{ static const struct { enum micros_grant_error error; int64_t result; } mappings[] = "
            "{{OK, 0}, {FAULT, -6}}; "
            "for (work.index = 0; work.index < sizeof(mappings) / sizeof(mappings[0]); ++work.index) { "
            "if (!grant_abi_test_recoverable_mapping(&work, mappings[work.index].error, mappings[work.index].result)) "
            "{ return false; } } return true; }"
        )
        phase = (
            "{ work->output = UINT64_C(0xa5a5a5a5a5a5a5a5); "
            "EXPECT_TRUE(micros_grant_abi_map_error(error, &work->output) "
            "&& work->output == (uint64_t)result); return true; }"
        )
        expected = (
            "{ static const struct { enum micros_grant_error error; int64_t result; } mappings[] = "
            "{{OK, 0}, {FAULT, -6}}; "
            "for (work.index = 0; work.index < sizeof(mappings) / sizeof(mappings[0]); ++work.index) { "
            "work->output = UINT64_C(0xa5a5a5a5a5a5a5a5); "
            "EXPECT_TRUE(micros_grant_abi_map_error(mappings[work.index].error, &work->output) "
            "&& work->output == (uint64_t)mappings[work.index].result); } return true; }"
        )
        self.assertEqual(tokens(expected), abi_mapping_body_tokens(tokens(caller), phase))
        for changed in (
            caller.replace("&work, mappings", "&copy, mappings"),
            caller.replace("mappings[work.index].error", "mappings[0].error"),
            caller.replace("mappings[work.index].result", "mappings[0].result"),
            caller.replace("return false;", "return true;"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                abi_mapping_body_tokens(tokens(changed), phase)
        for changed in (
            caller.replace("{FAULT, -6}", "{FAULT, -1}"),
            caller.replace("static const struct", "const struct"),
            caller.replace("sizeof(mappings) / sizeof(mappings[0])", "2"),
        ):
            self.assertNotEqual(tokens(expected), abi_mapping_body_tokens(tokens(changed), phase))
        self.assertNotEqual(
            tokens(expected), abi_mapping_body_tokens(tokens(caller), phase.replace("0xa5a5a5a5a5a5a5a5", "0")),
        )

    def test_native_fixture_has_only_nine_original_fields_and_nonresetting_storage(self):
        header = source(PRIVATE_HEADERS[1])
        self.assertEqual(
            tuple(tokens(declaration) for declaration, _ in NATIVE_FIELDS),
            work_declarations(header, NATIVE_FIXTURE_TAG, NATIVE_FIELDS),
        )
        fixture = source(HOST_SOURCES[1])
        self.assertEqual(
            tokens("{ return &fixture_state; }"),
            tokens("{" + function_records(fixture)[NATIVE_FIXTURE_GETTER].body + "}"),
        )
        for index, path in enumerate(HOST_SOURCES):
            expected = ("static struct grant_test_fixture fixture_state;",) if index == 1 else ()
            self.assertEqual(expected, tuple(file_scope_objects(source(path))), path)
        types = header[header.index("enum {"):header.index("struct grant_test_fixture {")]
        self.assertEqual(NATIVE_TYPES_DIGEST, digest_tokens(tokens(types)))
        assertions = header[header.index("#define EXPECT_TRUE"):header.rindex("#endif")]
        self.assertEqual(NATIVE_ASSERTIONS_DIGEST, digest_tokens(tokens(assertions)))

    def test_native_fixture_inverse_preserves_actual_arrays_resets_calls_and_pure_signatures(self):
        caller = (
            "{ struct grant_test_fixture *fixture = grant_test_context(); "
            "EXPECT_TRUE(setup_fixture(fixture)); "
            "memset(fixture->processes, 0, sizeof(fixture->processes)); "
            "record_model_trace(fixture, step, operation); "
            "return registries_equal(&fixture->grant_registry, &snapshot); }"
        )
        expected = (
            "{ EXPECT_TRUE(setup_fixture()); memset(processes, 0, sizeof(processes)); "
            "record_model_trace(step, operation); return registries_equal(&grant_registry, &snapshot); }"
        )
        self.assertEqual(
            tokens(expected),
            native_fixture_body_tokens("test_create_inspect_revoke_and_reuse", tokens(caller)),
        )
        for changed in (
            caller.replace("grant_test_context()", "reset_context()"),
            caller.replace("setup_fixture(fixture)", "setup_fixture(other)"),
            caller.replace("record_model_trace(fixture,", "record_model_trace(other,"),
            caller.replace("fixture->processes", "fixture->extra"),
            caller.replace("fixture->processes", "record.fixture->processes"),
            caller.replace("return registries_equal", "copy = *fixture; return registries_equal"),
            caller.replace("return registries_equal", "memset(fixture, 0, sizeof(*fixture)); return registries_equal"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                native_fixture_body_tokens("test_create_inspect_revoke_and_reuse", tokens(changed))
        self.assertEqual(
            tokens("static bool setup_fixture(void)"),
            native_fixture_signature_tokens("setup_fixture", tokens("bool setup_fixture(struct grant_test_fixture *fixture)")),
        )
        self.assertEqual(
            tokens("static bool recreate_process(size_t index)"),
            native_fixture_signature_tokens(
                "recreate_process", tokens("bool recreate_process(struct grant_test_fixture *fixture, size_t index)"),
            ),
        )
        pure = tokens("static uint32_t model_next(uint32_t *state)")
        self.assertEqual(pure, native_fixture_signature_tokens("model_next", pure[1:]))
        for changed in (
            "static bool setup_fixture(struct grant_test_fixture *fixture)",
            "bool setup_fixture(struct micros_kernel_objects *fixture)",
            "bool setup_fixture(struct grant_test_fixture *other)",
            "bool setup_fixture(struct grant_test_fixture *fixture, size_t added)",
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                native_fixture_signature_tokens("setup_fixture", tokens(changed))
        owner = "struct grant_test_fixture {" + "".join(declaration for declaration, _ in NATIVE_FIELDS) + "};"
        self.assertEqual(
            tuple(tokens(declaration) for declaration, _ in NATIVE_FIELDS),
            work_declarations(owner, NATIVE_FIXTURE_TAG, NATIVE_FIELDS),
        )
        for changed in (
            owner.replace("processes[GRANT_PROCESS_COUNT]", "*processes"),
            owner.replace("model_trace[GRANT_MODEL_TRACE_COUNT]", "model_trace[1]"),
            owner.replace("size_t model_trace_count;", "uint32_t model_trace_count;"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                work_declarations(changed, NATIVE_FIXTURE_TAG, NATIVE_FIELDS)

    def test_range_planning_has_its_exact_read_only_leaf_owner(self):
        facade = function_records(source(COPY_SOURCES[0]))
        self.assertNotIn("plan_range", set(facade))
        self.assertNotIn("grant_copy_plan_range", set(facade))
        leaf = ROOT / COPY_SOURCES[1]
        self.assertTrue(kernel_layout_source.is_file_path(leaf))
        self.assertIn("grant_copy_plan_range", set(function_records(kernel_layout_source.read_path(leaf))))

    def test_immutable_inventories_and_explicit_remaining_work_are_exact(self):
        self.assertEqual((6, 2, 10, 42, 24, 8, 3), tuple(len(value) for value in BASELINE_BODIES.values()))
        self.assertEqual(95, sum(map(len, BASELINE_BODIES.values())))
        self.assertEqual(65, len(INITIAL_SYMBOL_OWNER_MIGRATION))
        self.assertEqual(14, len(LONG_FUNCTIONS))
        self.assertEqual(5, len(SHORTER_TASKS))
        self.assertEqual(19, len(REQUIRED_TASKS))
        for active, initial in (
            (SYMBOL_OWNER_MIGRATION, INITIAL_SYMBOL_OWNER_MIGRATION),
            (FUNCTION_TASK_MIGRATION, REQUIRED_TASKS),
        ):
            self.assertIsInstance(active, tuple)
            self.assertEqual(len(active), len(set(active)))
            self.assertLessEqual(set(active), set(initial))
        for family, owners in ORIGINAL_FUNCTIONS.items():
            names = tuple(name for values in owners.values() for name in values)
            self.assertEqual(len(names), len(set(names)))
            self.assertEqual(set(BASELINE_BODIES[family]), set(names))

    def test_all_original_definitions_have_their_exact_current_and_final_owner(self):
        for family, owners in ORIGINAL_FUNCTIONS.items():
            locations = original_locations(family)
            for owner, names in owners.items():
                for name in names:
                    with self.subTest(family=family, original=name):
                        current = 0 if (family, name) in SYMBOL_OWNER_MIGRATION else owner
                        self.assertEqual(FAMILY_PATHS[family][current], locations[name][0])

    def test_all_95_complete_bodies_and_parameter_types_remain_exact(self):
        for family in ORIGINAL_FUNCTIONS:
            locations = original_locations(family)
            signatures = []
            for original in ORIGINAL_ORDER[family]:
                _, record = locations[original]
                with self.subTest(family=family, original=original):
                    self.assertEqual(BASELINE_BODIES[family][original], original_body_digest(family, original, record), REVIEWED_BASE)
                    signatures.append((original, original_signature_tokens(family, original, record)))
            self.assertEqual(BASELINE_SIGNATURES[family], digest_tokens(signatures))

    def test_only_original_pending_long_tasks_exceed_the_signal(self):
        actual = {
            (family, normal_names(family).get(name, name))
            for family in ORIGINAL_FUNCTIONS for (_, name), record in owned_records(family).items()
            if record.lines > 120
        }
        self.assertEqual(set(LONG_FUNCTIONS) & set(FUNCTION_TASK_MIGRATION), actual)

    def test_all_22_source_paths_three_headers_sets_and_real_consumers_are_exact(self):
        self.assertEqual(22, sum(map(len, FAMILY_PATHS)))
        self.assertEqual(22, len({path for paths in FAMILY_PATHS for path in paths}))
        cmake = source("CMakeLists.txt")
        for paths in FAMILY_PATHS:
            for path in paths:
                self.assertTrue(kernel_layout_source.is_file_path(ROOT / path), path)
        for index, (name, paths) in enumerate(SOURCE_SETS.items()):
            self.assertEqual((source_set_block(name, paths),), cmake_commands(cmake, "set", name))
            self.assertEqual((1, 2, 1)[index], cmake.count("${" + name + "}"))
        for header in PRIVATE_HEADERS:
            self.assertTrue(kernel_layout_source.is_file_path(ROOT / header), header)
            self.assertEqual({}, c_function_definitions(source(header)))
        self.assertEqual(1, cmake_commands(cmake, "add_executable", "micros")[0].count("${MICROS_GRANT_COPY_TARGET_SOURCES}"))
        for consumer in ("endpoint_test", "ipc_model_test"):
            self.assertEqual(1, cmake_commands(cmake, "add_executable", consumer)[0].count("${MICROS_GRANT_HOST_TEST_SOURCES}"))
        guarded = re.findall(r"if\(MICROS_BUILD_GRANT_SYSCALL_TEST\)([\s\S]*?)\nendif\(\)", cmake)
        self.assertEqual(1, sum("${MICROS_GRANT_SYSCALL_QEMU_TEST_SOURCES}" in block for block in guarded))
        self.assertEqual(BASE_CMAKE_DIGEST, digest_bytes(restored_cmake(cmake).encode()))
        self.assertEqual(digest_bytes(cmake.encode()), validation_context.RECIPE_DIGESTS["CMakeLists.txt"])

    def test_registry_runtime_public_abi_payload_and_drivers_remain_byte_exact(self):
        self.assertEqual(25, len(STABLE_HASHES))
        for path, expected in STABLE_HASHES.items():
            with self.subTest(path=path):
                from tests.host.test_qemu_runner_source_layout import restored_runner_source

                value = restored_runner_source().encode() if path == "tools/run_qemu_smoke.py" else kernel_layout_source.read_bytes_path(ROOT / path)
                self.assertEqual(expected, digest_bytes(value))
        self.assertEqual(SHARED_INSPECTOR_DIGEST, digest_bytes(kernel_layout_source.read_bytes_path(ROOT / "tests/host/c_source_records.py")))

    def test_only_declared_recipe_pin_and_classification_metadata_change(self):
        for name, tree, expected in (
            ("planner", planner_ast_without_grant_paths(source("tools/validation_plan.py")), PLANNER_AST_DIGEST),
            ("context", context_ast_without_recipe_pin(source("tools/validation_context.py")), CONTEXT_AST_DIGEST),
        ):
            self.assertEqual(expected, digest_bytes(ast.dump(tree, include_attributes=False).encode()), name)

    def test_all_four_independent_classification_dimensions_remain_exact(self):
        copy_gates = tuple("test-qemu-" + name for name in (
            "grant", "grant-syscall", "user-runtime", "ipc-ecall-core", "ipc-syscall", "ipc-syscall-panic",
            "frame-ownership", "user-address-space", "address-space-handoff", "user-execution", "scheduler",
        ))
        targets = ("test-qemu-grant-syscall", "test-qemu-address-space-handoff")
        dimensions = ((False, copy_gates, ("test-ipc-model",), ()), (False, (), ("test-ipc-model",), ()), (False, targets, (), ()))
        for (_, aliases), expected in zip(alias_rows(), dimensions):
            for path in aliases:
                with self.subTest(path=path):
                    self.assertEqual(expected, (
                        validation_plan.requires_full_tier([path]), tuple(validation_plan.affected_workflows([path])),
                        tuple(validation_plan.affected_slow_models([path])), tuple(validation_plan.affected_image_workflows([path])),
                    ))
        self.assertEqual((False, [], [], []), (
            validation_plan.requires_full_tier([STRUCTURAL_TEST_PATH]),
            validation_plan.affected_workflows([STRUCTURAL_TEST_PATH]),
            validation_plan.affected_slow_models([STRUCTURAL_TEST_PATH]),
            validation_plan.affected_image_workflows([STRUCTURAL_TEST_PATH]),
        ))

    def test_full_recipe_inverse_rejects_unknown_members_operands_and_other_code(self):
        current = source("CMakeLists.txt")
        self.assertEqual(BASE_CMAKE_DIGEST, digest_bytes(restored_cmake(current).encode()))
        for changed in (
            current.replace("kernel/grant_copy_range.c", "kernel/unreviewed_range.c", 1),
            current.replace("${MICROS_GRANT_HOST_TEST_SOURCES}", "tests/host/grant_test.c", 1),
            current + "\nconsumer(${MICROS_GRANT_COPY_TARGET_SOURCES})\n",
            current.replace("--timeout\n            8", "--timeout\n            9", 1),
        ):
            self.assertNotEqual(current, changed)
            with self.subTest(changed=changed[-100:]), self.assertRaises(AssertionError):
                restored_cmake(changed)

    def test_inverse_retains_real_range_predicates_and_complete_signatures(self):
        text = source(COPY_SOURCES[1])
        records = function_records(text)
        original = records["grant_copy_plan_range"]
        changed = original.body.replace("candidate.chunk_count >= 2", "candidate.chunk_count > 2", 1)
        self.assertNotEqual(original.body, changed)
        candidate = function_records(text.replace(original.body, changed))["grant_copy_plan_range"]
        self.assertNotEqual(BASELINE_BODIES[0]["plan_range"], original_body_digest(0, "plan_range", candidate))
        changed = text.replace("size_t length,", "uint32_t length,", 1)
        candidate = function_records(changed)["grant_copy_plan_range"]
        self.assertNotEqual(
            original_signature_tokens(0, "plan_range", original), original_signature_tokens(0, "plan_range", candidate),
        )

    def test_independent_recipe_fixture_preserves_all_four_original_consumers(self):
        expected = (
            "add_executable(micros before kernel/grant_copy.c after)\n"
            "add_executable(endpoint_test tests/host/grant_test.c)\n"
            "add_executable(ipc_model_test tests/host/grant_test.c)\n"
            "if(MICROS_BUILD_GRANT_SYSCALL_TEST)\n"
            "target_sources(micros PRIVATE arch/riscv64/grant_syscall_test.S kernel/grant_syscall_test.c)\nendif()\n"
        )
        current = (
            "".join(source_set_block(name, paths) + "\n\n" for name, paths in SOURCE_SETS.items())
            + "add_executable(micros before ${MICROS_GRANT_COPY_TARGET_SOURCES} after)\n"
            "add_executable(endpoint_test ${MICROS_GRANT_HOST_TEST_SOURCES})\n"
            "add_executable(ipc_model_test ${MICROS_GRANT_HOST_TEST_SOURCES})\n"
            "if(MICROS_BUILD_GRANT_SYSCALL_TEST)\n"
            "target_sources(micros PRIVATE arch/riscv64/grant_syscall_test.S ${MICROS_GRANT_SYSCALL_QEMU_TEST_SOURCES})\nendif()\n"
        )
        self.assertEqual(expected, grant_data_plane_cmake_projection(current))
        for changed in (
            current.replace("kernel/grant_copy_range.c", "kernel/other_range.c", 1),
            current.replace("${MICROS_GRANT_HOST_TEST_SOURCES}", "tests/host/grant_test.c", 1),
            current + "\nconsumer(${MICROS_GRANT_SYSCALL_QEMU_TEST_SOURCES})\n",
        ):
            with self.subTest(changed=changed[-100:]), self.assertRaises(AssertionError):
                grant_data_plane_cmake_projection(changed)
        self.assertNotEqual(
            expected, grant_data_plane_cmake_projection(current.replace(" before ", " mutated_before ")),
        )

    def test_independent_metadata_fixture_retains_unrelated_rows_and_algorithms(self):
        expected = (
            "PATH_CLASSIFICATION_EQUIVALENTS = (('original', ('kept',)),)\n"
            "HOST_UNIT_ONLY_INPUTS = ('original_host',)\n"
            "def algorithm(value):\n    return value + 1\n"
        )
        current = (
            "PATH_CLASSIFICATION_EQUIVALENTS = " + repr((("original", ("kept",)), *alias_rows())) + "\n"
            "HOST_UNIT_ONLY_INPUTS = " + repr(("original_host", STRUCTURAL_TEST_PATH)) + "\n"
            "def algorithm(value):\n    return value + 1\n"
        )
        fingerprint = lambda tree: ast.dump(tree, include_attributes=False)
        self.assertEqual(fingerprint(ast.parse(expected)), fingerprint(planner_ast_without_grant_paths(current)))
        for changed in (
            current.replace("kernel/grant_copy_range.c", "kernel/unchecked_range.c"),
            current.replace(STRUCTURAL_TEST_PATH, "tests/host/other.py"),
            current.replace("'kernel/grant_copy_range.c', ", ""),
        ):
            self.assertNotEqual(current, changed)
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                planner_ast_without_grant_paths(changed)
        self.assertNotEqual(
            fingerprint(ast.parse(expected)),
            fingerprint(planner_ast_without_grant_paths(current.replace("value + 1", "value - 1"))),
        )

    def test_copy_execution_exposes_typed_dual_plan_phase_and_root_irq_lifetime(self):
        owner = source(COPY_SOURCES[0])
        records = function_records(owner)
        self.assertIn(COPY_EXECUTION_PHASE, set(records))
        self.assertEqual(
            tokens("static bool " + COPY_EXECUTION_PHASE + "(struct grant_copy_execution *work, "
                   "uintptr_t local_address, size_t length, uint32_t required_permission)"),
            tokens(records[COPY_EXECUTION_PHASE].signature),
        )
        self.assertIsNotNone(re.search(r"\bstruct grant_copy_execution\s*\{", owner))
        root = records["grant_copy_execute"]
        self.assertLessEqual(root.lines, 120)
        self.assertEqual(1, root.body.count("riscv_irq_save()"))
        self.assertEqual(1, root.body.count("riscv_irq_restore(saved_status)"))
        self.assertNotIn("riscv_irq_", records[COPY_EXECUTION_PHASE].body)
        self.assertEqual(
            tokens("static bool " + COPY_LOCAL_RANGE_PHASE + "(uintptr_t local_address, size_t length)"),
            tokens(records[COPY_LOCAL_RANGE_PHASE].signature),
        )
        self.assertLess(root.body.index(COPY_LOCAL_RANGE_PHASE), root.body.index("riscv_irq_save()"))

    def test_copy_execution_inverse_preserves_dual_plan_order_scope_initializers_and_common_exit(self):
        caller = (
            "{ struct grant_copy_execution work; "
            "work.remote_plan = (struct micros_grant_copy_range_plan){0}; "
            "work.local_plan = (struct micros_grant_copy_range_plan){0}; uintptr_t saved_status; "
            "if (bad_shape()) { return ARGUMENT; } "
            "if (!grant_copy_local_range_is_valid(local_address, length)) { return MICROS_GRANT_ERROR_RANGE; } "
            "saved_status = riscv_irq_save(); "
            "original_authority_phase_zero_device(&work.authority, &work.ledger); "
            "if (!grant_copy_prepare_participant_plans(&work, local_address, length, required_permission)) { goto done; } "
            "select_direction(&work.remote_plan, &work.local_plan, &work.source_plan, &work.destination_plan); "
            "work.error = validate(work.source_plan, work.destination_plan, length); "
            "if (work.error != OK) { goto done; } commit(work.source_plan, work.destination_plan, length); "
            "done: riscv_irq_restore(saved_status); return work.error; }"
        )
        phase = (
            "{ work->remote_error = remote(work->authority.grantor, length, required_permission); "
            "work->local_error = local(work->authority.grantee, local_address, length, required_permission); "
            "if (work->remote_error == INVARIANT || work->local_error == INVARIANT) { work->error = INVARIANT; return false; } "
            "if (work->remote_error == PHASE || work->local_error == PHASE) { work->error = PHASE; return false; } "
            "if (work->remote_error != OK || work->local_error != OK) { work->error = FAULT; return false; } return true; }"
        )
        declarations = tuple(tokens(declaration) for declaration, _ in COPY_EXECUTION_FIELDS)
        local_phase = "{ if (length != 0 && local_address == 0) { return false; } return true; }"

        def restore(caller, phase, declarations):
            return copy_execution_body_tokens(caller, phase, declarations, local_phase)

        expected = (
            "{ const struct micros_frame_ownership *ledger; struct micros_grant_copy_authority authority; "
            "struct micros_grant_copy_range_plan remote_plan = {0}; struct micros_grant_copy_range_plan local_plan = {0}; "
            "struct micros_tty_device_authority device_authority; "
            "const struct micros_grant_copy_range_plan *source_plan; "
            "const struct micros_grant_copy_range_plan *destination_plan; "
            "enum micros_grant_error remote_error; enum micros_grant_error local_error; enum micros_grant_error error; "
            "enum micros_tty_device_authority_status device_status; uintptr_t saved_status; "
            "if (bad_shape()) { return ARGUMENT; } "
            "if (length != 0 && local_address == 0) { return MICROS_GRANT_ERROR_RANGE; } "
            "saved_status = riscv_irq_save(); "
            "original_authority_phase_zero_device(&authority, &ledger); "
            "remote_error = remote(authority.grantor, length, required_permission); "
            "local_error = local(authority.grantee, local_address, length, required_permission); "
            "if (remote_error == INVARIANT || local_error == INVARIANT) { error = INVARIANT; goto done; } "
            "if (remote_error == PHASE || local_error == PHASE) { error = PHASE; goto done; } "
            "if (remote_error != OK || local_error != OK) { error = FAULT; goto done; } "
            "select_direction(&remote_plan, &local_plan, &source_plan, &destination_plan); "
            "error = validate(source_plan, destination_plan, length); if (error != OK) { goto done; } "
            "commit(source_plan, destination_plan, length); done: riscv_irq_restore(saved_status); return error; }"
        )
        self.assertEqual(tokens(expected), restore(caller, phase, declarations))
        for changed in (
            caller.replace("struct grant_copy_execution work;", "struct grant_copy_execution work = {0};"),
            caller.replace("(&work, local_address,", "(&other, local_address,"),
            caller.replace("local_address, length, required_permission", "other_address, length, required_permission"),
            caller.replace("work.error = validate", "copy = work; work.error = validate"),
            caller.replace("(struct micros_grant_copy_range_plan){0}", "(struct other_plan){0}", 1),
        ):
            self.assertNotEqual(caller, changed)
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                restore(changed, phase, declarations)
        for changed in (
            phase.replace("return false;", "return true;", 1),
            phase.replace("work->remote_error", "work->extra", 1),
            phase.replace("return false;", "goto done;", 1),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                restore(caller, changed, declarations)
        self.assertNotEqual(
            tokens(expected), restore(caller, phase.replace("work->error = PHASE", "work->error = FAULT"), declarations),
        )
        self.assertNotEqual(
            tokens(expected), restore(caller.replace("riscv_irq_restore(saved_status);", ""), phase, declarations),
        )
        with self.assertRaises(AssertionError):
            copy_execution_body_tokens(caller, phase, declarations, local_phase.replace("return false;", "return true;"))
        with self.assertRaises(AssertionError):
            copy_execution_body_tokens(caller.replace("local_range_is_valid(local_address", "local_range_is_valid(other_address"),
                                       phase, declarations, local_phase)
        owner = "struct grant_copy_execution { " + " ".join(declaration for declaration, _ in COPY_EXECUTION_FIELDS) + " };"
        self.assertEqual(declarations, copy_execution_declarations(owner))
        with self.assertRaises(AssertionError):
            copy_execution_declarations(owner.replace("enum micros_grant_error remote_error;", "uint32_t remote_error;"))

    def test_range_planner_exposes_typed_device_span_without_new_state_or_irq(self):
        records = function_records(source(COPY_SOURCES[1]))
        self.assertIn(RANGE_DEVICE_PHASE, set(records))
        phase = records[RANGE_DEVICE_PHASE]
        self.assertEqual(
            tokens("static enum micros_grant_error " + RANGE_DEVICE_PHASE + "(" + RANGE_DEVICE_PARAMETERS + ")"),
            tokens(phase.signature),
        )
        root = records["grant_copy_plan_range"]
        self.assertLess(root.lines, 94)
        self.assertNotIn("riscv_irq_", phase.body)
        for declaration in (
            "struct micros_grant_copy_range_plan candidate = {0};", "uintptr_t current = address;",
            "size_t remaining = length;", "bool device_fault = false;",
            "uint64_t physical_address;", "uint32_t permissions;", "size_t contiguous;",
            "size_t chunk_length;", "enum micros_user_address_space_error address_error;",
        ):
            self.assertIn(declaration, root.body)

    def test_device_span_inverse_preserves_original_lvalues_deferred_fault_and_continue(self):
        caller = (
            "{ candidate_init(); while (remaining != 0) { original_five_locals(); "
            "if (device_authority != NULL && intersects(device_authority, process, current, 1)) { "
            "switch (grant_copy_plan_device_span(process, device_authority, &current, &remaining, "
            "&device_fault, &address_error, &chunk_length)) { "
            "case MICROS_GRANT_OK: continue; "
            "case MICROS_GRANT_ERROR_PHASE: return MICROS_GRANT_ERROR_PHASE; "
            "case MICROS_GRANT_ERROR_INVARIANT: return MICROS_GRANT_ERROR_INVARIANT; "
            "default: return MICROS_GRANT_ERROR_INVARIANT; } } "
            "original_regular_page_plan(); } "
            "if (device_fault) { return MICROS_GRANT_ERROR_FAULT; } *plan = candidate; return MICROS_GRANT_OK; }"
        )
        phase = (
            "{ *address_error = validate_device(process, device_authority->root_physical_address); "
            "if (*address_error == ADDRESS_PHASE) { return MICROS_GRANT_ERROR_PHASE; } "
            "if (*address_error != ADDRESS_OK) { return MICROS_GRANT_ERROR_INVARIANT; } "
            "*chunk_length = (DEVICE_BASE + DEVICE_LENGTH) - *current; "
            "if (*chunk_length > *remaining) { *chunk_length = *remaining; } "
            "*current += *chunk_length; *remaining -= *chunk_length; *device_fault = true; return MICROS_GRANT_OK; }"
        )
        expected = (
            "{ candidate_init(); while (remaining != 0) { original_five_locals(); "
            "if (device_authority != NULL && intersects(device_authority, process, current, 1)) { "
            "address_error = validate_device(process, device_authority->root_physical_address); "
            "if (address_error == ADDRESS_PHASE) { return MICROS_GRANT_ERROR_PHASE; } "
            "if (address_error != ADDRESS_OK) { return MICROS_GRANT_ERROR_INVARIANT; } "
            "chunk_length = (DEVICE_BASE + DEVICE_LENGTH) - current; "
            "if (chunk_length > remaining) { chunk_length = remaining; } "
            "current += chunk_length; remaining -= chunk_length; device_fault = true; continue; } "
            "original_regular_page_plan(); } "
            "if (device_fault) { return MICROS_GRANT_ERROR_FAULT; } *plan = candidate; return MICROS_GRANT_OK; }"
        )
        self.assertEqual(tokens(expected), range_device_body_tokens(caller, phase))
        for changed in (
            caller.replace("&current, &remaining", "&other_current, &remaining"),
            caller.replace("case MICROS_GRANT_OK: continue;", "case MICROS_GRANT_OK: return MICROS_GRANT_OK;"),
            caller.replace("default: return MICROS_GRANT_ERROR_INVARIANT;", "default: return MICROS_GRANT_OK;"),
        ):
            self.assertNotEqual(caller, changed)
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                range_device_body_tokens(changed, phase)
        for changed in (
            phase.replace("return MICROS_GRANT_ERROR_PHASE;", "return MICROS_GRANT_OK;"),
            phase.replace("*current +=", "current +="),
            phase.replace("return MICROS_GRANT_OK; }", "continue; }"),
            phase.replace("return MICROS_GRANT_ERROR_INVARIANT;", "goto done;"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                range_device_body_tokens(caller, changed)
        self.assertNotEqual(
            tokens(expected), range_device_body_tokens(caller, phase.replace("*device_fault = true", "*device_fault = false")),
        )
        self.assertNotEqual(
            tokens(expected), range_device_body_tokens(caller.replace("if (device_fault)", "if (false)"), phase),
        )

    def test_remote_only_validation_exposes_typed_plan_phase_without_copy_or_local_range(self):
        records = function_records(source(COPY_SOURCES[0]))
        self.assertIn(VALIDATION_PHASE, set(records))
        phase = records[VALIDATION_PHASE]
        self.assertEqual(
            tokens("static void " + VALIDATION_PHASE + "(" + VALIDATION_PARAMETERS + ")"), tokens(phase.signature),
        )
        root = records["micros_grant_validate_range"]
        self.assertLess(root.lines, 98)
        self.assertEqual(1, root.body.count("riscv_irq_save()"))
        self.assertEqual(1, root.body.count("riscv_irq_restore(saved_status)"))
        for excluded in ("local_address", "micros_grant_copy_commit", "riscv_irq_"):
            self.assertNotIn(excluded, phase.body)

    def test_remote_validation_inverse_preserves_one_plan_no_copy_and_original_void_error_exit(self):
        caller = (
            "{ struct grant_copy_validation work; work.remote_plan = (struct micros_grant_copy_range_plan){0}; "
            "uintptr_t saved_status; original_early_arguments(); saved_status = riscv_irq_save(); "
            "original_authority_phase_zero(&work.authority, &work.ledger, &work.error); "
            "grant_copy_prepare_remote_plan(&work, endpoint_registry, objects, length, required_permission); "
            "done: riscv_irq_restore(saved_status); return work.error; }"
        )
        phase = (
            "{ work->device_status = capture_device_authority(endpoint_registry, objects, &work->device_authority); "
            "if (work->device_status == INVALID) { work->error = INVARIANT; return; } "
            "work->error = prepare_remote(work->authority.grantor, work->authority.remote_address, length, "
            "required_permission, &work->remote_plan); }"
        )
        expected = (
            "{ const struct micros_frame_ownership *ledger; struct micros_grant_copy_authority authority; "
            "struct micros_grant_copy_range_plan remote_plan = {0}; "
            "struct micros_tty_device_authority device_authority; enum micros_grant_error error; "
            "enum micros_tty_device_authority_status device_status; uintptr_t saved_status; "
            "original_early_arguments(); saved_status = riscv_irq_save(); "
            "original_authority_phase_zero(&authority, &ledger, &error); "
            "device_status = capture_device_authority(endpoint_registry, objects, &device_authority); "
            "if (device_status == INVALID) { error = INVARIANT; goto done; } "
            "error = prepare_remote(authority.grantor, authority.remote_address, length, required_permission, &remote_plan); "
            "done: riscv_irq_restore(saved_status); return error; }"
        )
        declarations = tuple(tokens(declaration) for declaration, _ in VALIDATION_FIELDS)
        self.assertEqual(tokens(expected), validation_body_tokens(caller, phase, declarations))
        for changed in (
            caller.replace("struct grant_copy_validation work;", "struct grant_copy_validation work = {0};"),
            caller.replace("(&work, endpoint_registry,", "(&copy, endpoint_registry,"),
            caller.replace("endpoint_registry, objects, length,", "other_registry, objects, length,"),
            caller.replace("done: riscv_irq_restore", "new_step(); done: riscv_irq_restore"),
            caller.replace("(struct micros_grant_copy_range_plan){0}", "(struct other_plan){0}"),
        ):
            self.assertNotEqual(caller, changed)
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                validation_body_tokens(changed, phase, declarations)
        for changed in (
            phase.replace("return;", "return true;"),
            phase.replace("return;", "goto done;"),
            phase.replace("work->authority.grantor", "work->local_plan"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                validation_body_tokens(caller, changed, declarations)
        self.assertNotEqual(
            tokens(expected), validation_body_tokens(caller, phase.replace("work->error = INVARIANT", "work->error = OK"), declarations),
        )
        self.assertNotEqual(
            tokens(expected), validation_body_tokens(caller, phase.replace("length, required_permission", "length, READ"), declarations),
        )
        self.assertNotEqual(
            tokens(expected), validation_body_tokens(caller, phase.replace("work->error = prepare_remote", "copy_bytes(); work->error = prepare_remote"), declarations),
        )

    def test_captured_syscall_exposes_typed_dispatch_and_prepare_device_publication(self):
        records = function_records(source(FAMILY_PATHS[1][0]))
        for name in (SYSCALL_DISPATCH_PHASE, SYSCALL_CREATE_PHASE):
            with self.subTest(phase=name):
                self.assertIn(name, set(records))
                self.assertEqual(
                    tokens("static void " + name + "(" + SYSCALL_PHASE_PARAMETERS + ")"),
                    tokens(records[name].signature),
                )
                self.assertLessEqual(records[name].lines, 120)
        self.assertLessEqual(records["micros_grant_handle_captured_user_ecall"].lines, 120)

    def test_syscall_inverse_preserves_runtime_decode_create_case_and_success_only_publication(self):
        caller = (
            "{ struct grant_syscall_execution work; work.created = MICROS_GRANT_NONE; "
            "original_runtime_checks(&work.grant_registry, &work.endpoint_registry); "
            "work.decode_result = decode(arguments, &work.request); "
            "if (work.decode_result != OK) { frame->a0 = work.decode_result; return NORMAL; } "
            "grant_syscall_execute_request(hart, frame, context, &work); "
            "original_publication(frame, work.request.operation, work.created, work.error, &work.abi_result); return NORMAL; }"
        )
        dispatch = (
            "{ switch (work->request.operation) { "
            "case MICROS_SYSCALL_ABI_GRANT_CREATE: grant_syscall_create_request(hart, frame, context, work); break; "
            "case REVOKE: work->error = revoke(work->grant_registry, context->process, work->request.grant); break; "
            "default: panic_grant_syscall(hart, frame, \"grant-request-invariant\"); } }"
        )
        create = (
            "{ work->error = prepare_create(work->grant_registry, context->process, &work->create_plan); "
            "if (work->error != OK) { return; } "
            "work->device_status = capture_device(work->endpoint_registry, context->objects, &work->device_authority); "
            "if (work->device_status == INVARIANT) { panic_grant_syscall(hart, frame, \"grant-device-authority-invariant\"); } "
            "if (device_intersects(&work->device_authority, context->process, work->request.local_address)) { "
            "work->error = FAULT; return; } "
            "commit_create(work->grant_registry, &work->create_plan, &work->created); }"
        )
        expected = (
            "{ struct micros_grant_registry *grant_registry; struct micros_endpoint_registry *endpoint_registry; "
            "struct micros_grant_create_plan create_plan; struct micros_grant_syscall_request request; "
            "struct micros_tty_device_authority device_authority; micros_grant_t created = MICROS_GRANT_NONE; "
            "enum micros_syscall_abi_result decode_result; enum micros_grant_error error; "
            "enum micros_tty_device_authority_status device_status; uint64_t abi_result; "
            "original_runtime_checks(&grant_registry, &endpoint_registry); "
            "decode_result = decode(arguments, &request); "
            "if (decode_result != OK) { frame->a0 = decode_result; return NORMAL; } "
            "switch (request.operation) { case MICROS_SYSCALL_ABI_GRANT_CREATE: "
            "error = prepare_create(grant_registry, context->process, &create_plan); "
            "if (error != OK) { break; } "
            "device_status = capture_device(endpoint_registry, context->objects, &device_authority); "
            "if (device_status == INVARIANT) { panic_grant_syscall(hart, frame, \"grant-device-authority-invariant\"); } "
            "if (device_intersects(&device_authority, context->process, request.local_address)) { error = FAULT; break; } "
            "commit_create(grant_registry, &create_plan, &created); break; "
            "case REVOKE: error = revoke(grant_registry, context->process, request.grant); break; "
            "default: panic_grant_syscall(hart, frame, \"grant-request-invariant\"); } "
            "original_publication(frame, request.operation, created, error, &abi_result); return NORMAL; }"
        )
        declarations = tuple(tokens(declaration) for declaration, _ in SYSCALL_FIELDS)
        self.assertEqual(tokens(expected), syscall_body_tokens(caller, dispatch, create, declarations))
        for changed in (
            caller.replace("struct grant_syscall_execution work;", "struct grant_syscall_execution work = {0};"),
            caller.replace("context, &work", "other_context, &work"),
            caller.replace("context, &work", "context, &copy"),
            caller.replace("original_publication", "copy = work; original_publication"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                syscall_body_tokens(changed, dispatch, create, declarations)
        for changed in (
            dispatch.replace("context, work", "other_context, work"),
            dispatch.replace("work); break;", "work);", 1),
            dispatch.replace("case REVOKE:", "return; case REVOKE:"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                syscall_body_tokens(caller, changed, create, declarations)
        for changed in (
            create.replace("return;", "return true;", 1),
            create.replace("return;", "break;", 1),
            create.replace("return;", "goto done;", 1),
            create.replace("work->created", "work->extra"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                syscall_body_tokens(caller, dispatch, changed, declarations)
        for changed in (
            caller.replace("MICROS_GRANT_NONE", "0"),
            caller.replace("original_runtime_checks", "weakened_runtime_checks"),
            caller.replace("original_publication", "early_publication"),
        ):
            self.assertNotEqual(tokens(expected), syscall_body_tokens(changed, dispatch, create, declarations))
        self.assertNotEqual(
            tokens(expected), syscall_body_tokens(caller, dispatch, create.replace("work->error = FAULT", "work->error = OK"), declarations),
        )


if __name__ == "__main__":
    unittest.main()
