from __future__ import annotations

import ast
import hashlib
from pathlib import Path
import re
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from tests.host import test_vm_service_source_layout as vm_layout
from tests.host.test_frame_ownership_source_layout import (
    c_function_definitions,
    c_lexical_text,
    cmake_command,
    cmake_if_blocks,
)
from tests.host.test_kernel_pm_control_source_layout import macro_definition_text
from tests.host.test_vm_service_source_layout import (
    c_declaration_text,
    file_scope_objects,
)
from tools import validation_plan


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
FUNCTION_REVIEW_LINE_LIMIT = 120
GENERATOR_PATH = "tools/generate_pm_service_fixture.py"

SOURCE_SETS = {
    "MICROS_PM_CORE_SOURCES": (
        "servers/pm/pm_common.c",
        "servers/pm/pm_core.c",
        "servers/pm/pm_protocol.c",
        "servers/pm/pm_validation.c",
        "servers/pm/pm_spawn.c",
        "servers/pm/pm_rollback.c",
        "servers/pm/pm_exit.c",
        "servers/pm/pm_wait.c",
    ),
    "MICROS_PM_COORDINATOR_SOURCES": (
        "servers/pm/pm_coordinator.c",
        "servers/pm/pm_coordinator_protocol.c",
        "servers/pm/pm_coordinator_peers.c",
        "servers/pm/pm_coordinator_spawn.c",
        "servers/pm/pm_coordinator_rollback.c",
        "servers/pm/pm_coordinator_requests.c",
        "servers/pm/pm_coordinator_frontend.c",
        "servers/pm/pm_coordinator_exit.c",
        "servers/pm/pm_coordinator_wait.c",
    ),
    "MICROS_PM_RUNTIME_BACKEND_SOURCES": (
        "servers/pm/pm_service_backend.c",
    ),
    "MICROS_SPAWN_REQUEST_SOURCES": (
        "lib/spawn/spawn_request.c",
        "lib/spawn/spawn_stack_layout.c",
    ),
    "MICROS_PM_SERVICE_SOURCES": (
        "servers/pm/pm_service.c",
        "servers/pm/pm_service_fixture.c",
        "servers/pm/pm_service_bootstrap.c",
        "servers/pm/pm_service_requests.c",
        "servers/pm/pm_service_self_test.c",
        "servers/pm/pm_service_scratch.c",
    ),
    "MICROS_PM_CORE_HOST_TEST_SOURCES": (
        "tests/host/pm_test.c",
        "tests/host/pm_test_fixture.c",
        "tests/host/pm_protocol_test.c",
        "tests/host/pm_spawn_test.c",
        "tests/host/pm_validation_test.c",
        "tests/host/pm_exit_wait_test.c",
        "tests/host/pm_model_test.c",
        "tests/host/pm_model_reference.c",
        "tests/host/pm_model_operations.c",
    ),
    "MICROS_PM_COORDINATOR_HOST_TEST_SOURCES": (
        "tests/host/pm_coordinator_test.c",
        "tests/host/pm_coordinator_configuration_test.c",
        "tests/host/pm_coordinator_protocol_test.c",
        "tests/host/pm_coordinator_spawn_fixture.c",
        "tests/host/pm_coordinator_spawn_test.c",
        "tests/host/pm_coordinator_request_test.c",
        "tests/host/pm_coordinator_exit_fixture.c",
        "tests/host/pm_coordinator_exit_wait_test.c",
        "tests/host/pm_coordinator_backend_test.c",
    ),
    "MICROS_SPAWN_ABI_HOST_TEST_SOURCES": (
        "tests/host/spawn_abi_test.c",
        "tests/host/spawn_abi_test_fixture.c",
        "tests/host/spawn_request_test.c",
        "tests/host/application_stack_test.c",
        "tests/host/vm_elf_test.c",
    ),
    "MICROS_PM_SERVICE_QEMU_TEST_SOURCES": (
        "kernel/pm_service_test.c",
        "kernel/pm_service_test_fixture.c",
        "kernel/pm_service_test_report.c",
    ),
}

PRIVATE_HEADERS = (
    "servers/pm/pm_internal.h",
    "servers/pm/pm_service_internal.h",
    "tests/host/pm_test_internal.h",
    "tests/host/pm_model_test_internal.h",
    "tests/host/spawn_abi_test_internal.h",
    "kernel/pm_service_test_internal.h",
    "servers/pm/pm_coordinator.h",
    "tests/host/pm_coordinator_test_internal.h",
    "servers/pm/pm_coordinator_internal.h",
    "tests/host/pm_coordinator_spawn_fixture.h",
)

SHARED_PROTOCOL_HEADERS = ("include/micros/vm_service.h",)

STABLE_HEADER_HASHES = {
    "include/micros/pm.h":
        "03d4c85565fa859046fcc48140331affc8da4ca453e7f6972d448287b037240c",
    "include/micros/spawn.h":
        "d78e2c03f02ae06a24fdfc3fd1f2908cb184068f298a428b9be1a7789459f5f0",
    "lib/spawn/spawn_request.h":
        "98d4cd7beec17b7d7ef00070e31b41c6a4cdf10a856dea1e1b545bd69c8966cf",
    "servers/pm/pm_core.h":
        "1aa15efd1ada1f595968898e4a0c5c04a9ec102dca8ff8f4687b7c2e94c454a9",
    "servers/pm/pm_control.h":
        "8ea7047976c776e75d5e4bd46e36fb5de2232b4f025e337673cde3d0820fb6ef",
    "kernel/pm_service_test.h":
        "2977f85b83c1fca182ccdecc594529fc3e8dbf4fb05a2f35e9244ee7213af443",
    "kernel/pm_service_test_fixture.h":
        "90645c9d069a09d15840676c79d81a86d7922363d227d5e83ef59e873a29861e",
    "tests/qemu/pm_service_protocol.h":
        "b04b32c0bf1617d7b774d00e9f74146d5f25c7381ce3f615f11449275bdf00d1",
}

WRAPPER_PATH = "servers/pm/pm_control.c"
WRAPPER_HASH = "3ab44cebd8f6c8783edc6ebeb7f46594e0303768150f2a28d654f3bceef1c7a6"
DECLARATION_ONLY_HEADERS = (
    *PRIVATE_HEADERS, *SHARED_PROTOCOL_HEADERS, *STABLE_HEADER_HASHES,
    "include/micros/runtime.h",
)

RETAINED_OWNER_GROUPS = {
    "servers/pm/pm_coordinator.c": (
        "micros_pm_coordinator_initialize", "micros_pm_coordinator_spawn_init",
        "micros_pm_coordinator_parent_lost", "pm_coordinator_spawn_backend_is_complete",
        "pm_coordinator_validate_instance", "pm_coordinator_prepare_snapshot",
    ),
    "servers/pm/pm_coordinator_requests.c": ("micros_pm_coordinator_spawn_request",),
    "servers/pm/pm_coordinator_frontend.c": (
        "pm_coordinator_reply_response", "pm_coordinator_no_reply_response",
        "pm_coordinator_preflight_call",
    ),
    "servers/pm/pm_coordinator_exit.c": ("micros_pm_coordinator_exit_request",),
    "servers/pm/pm_coordinator_wait.c": (
        "micros_pm_coordinator_wait_request", "pm_coordinator_complete_wait_outcomes",
    ),
    "servers/pm/pm_service_backend.c": ("pm_service_backend_get",),
    "servers/pm/pm_coordinator_peers.c": (
        "pm_coordinator_classify_vfs_result", "pm_coordinator_vfs_call", "pm_coordinator_vm_call",
        "pm_coordinator_vfs_exchange", "pm_coordinator_vm_exchange",
    ),
    "servers/pm/pm_coordinator_spawn.c": (
        "pm_coordinator_complete_phase", "pm_coordinator_run_spawn",
    ),
    "servers/pm/pm_coordinator_rollback.c": ("pm_coordinator_rollback_spawn",),
    "servers/pm/pm_coordinator_protocol.c": (
        "micros_pm_coordinator_build_vfs_control", "micros_pm_coordinator_build_vm_control",
        "micros_pm_coordinator_decode_vfs_result", "micros_pm_coordinator_decode_vm_result",
    ),
    "servers/pm/pm_core.c": (
        "micros_pm_table_initialize", "micros_pm_enable_runtime",
        "micros_pm_process_resolve", "micros_pm_find_running_by_endpoint",
    ),
    "lib/spawn/spawn_request.c": ("micros_spawn_request_decode",),
    "servers/pm/pm_service.c": ("micros_service_main",),
    "kernel/pm_service_test.c": ("micros_pm_service_test_launch",),
}

HELPER_OWNER_GROUPS = {
    "servers/pm/pm_coordinator.c": (
        "coordinator_bindings_are_valid",
    ),
    "servers/pm/pm_coordinator_requests.c": (
        "build_spawn_response", "parent_grant_failure_is_expected",
        "copy_parent_request", "finish_public_spawn", "reserve_public_child",
    ),
    "servers/pm/pm_coordinator_exit.c": (
        "release_vfs_process", "detach_execution", "release_vm_mappings",
        "release_process_and_complete_waits", "run_stopped_exit",
        "exit_backend_is_complete", "stop_and_commit_exit",
    ),
    "servers/pm/pm_coordinator_wait.c": ("wait_reply_failure_is_expected",),
    "servers/pm/pm_coordinator_spawn.c": (
        "reserve_kernel_process", "prepare_vfs_image", "freeze_vm_mappings",
        "prepare_vfs_descriptors", "prepare_kernel_execution", "commit_hidden_records",
        "activate_and_commit_peers",
    ),
    "servers/pm/pm_coordinator_rollback.c": ("release_spawn_resource",),
    "servers/pm/pm_coordinator_protocol.c": (
        "endpoint_word_is_valid", "grant_pair_is_valid", "vfs_command_is_defined",
        "vm_command_is_defined", "vfs_arguments_are_valid", "vm_arguments_are_valid",
        "build_control_message", "outputs_are_zero", "decode_control_envelope",
        "vfs_success_output_is_valid", "vm_success_output_is_valid",
    ),
    "servers/pm/pm_common.c": tuple(
        "pm_common_" + name for name in (
            "zero_bytes", "bytes_are_zero", "read_u64_le", "write_u64_le", "handle_is_none",
            "handles_equal", "endpoint_is_concrete", "endpoint_is_published_elsewhere",
            "clear_record", "record_handle", "resolve_record", "resolve_spawning_record",
            "release_record", "clear_wait", "wait_matches_pid", "record_is_child_of",
            "has_matching_child",
        )
    ),
    "servers/pm/pm_protocol.c": (
        "read_u32_le", "write_u32_le",
        "result_is_defined", "exit_kind_is_defined",
    ),
    "servers/pm/pm_validation.c": (
        "spawn_resources_are_valid", "spawn_is_clear", "exit_is_clear", "wait_is_clear",
        "validation_mapping_identity_is_valid",
    ),
    "servers/pm/pm_spawn.c": (
        "resolve_forward_spawning_record", "table_has_spawning_record",
        "find_free_slot", "allocate_record",
        "pm_spawn_capacity_is_available",
        "pm_spawn_prepare_activation", "pm_spawn_commit_activation",
    ),
    "servers/pm/pm_rollback.c": (
        "rollback_resource_for_stage", "populate_rollback_action",
        "populate_completed_rollback",
    ),
    "servers/pm/pm_exit.c": (
        "clear_wait_outcomes", "pm_exit_prepare_begin", "pm_exit_commit_begin",
    ),
    "servers/pm/pm_wait.c": (
        "clear_wait_outcome", "find_lowest_matching_zombie",
        "populate_wait_outcome", "pm_wait_stage_result",
    ),
    "servers/pm/pm_service_fixture.c": (
        "pm_service_context_get", "pm_service_clear_bytes", "pm_service_copy_bytes",
        "pm_service_bytes_equal", "pm_service_fill_bytes", "pm_service_bytes_have_value",
        "pm_service_bytes_are_zero", "pm_service_write_u32_le", "pm_service_write_u64_le",
        "pm_service_read_u64_le", "pm_service_read_u32_le",
        "pm_service_validate_configuration",
        "pm_service_configure_coordinator",
    ),
    "servers/pm/pm_service_bootstrap.c": ("pm_service_send_ready",),
    "servers/pm/pm_service_requests.c": (
        "protocol_result", "serve_coordinator_request", "pm_service_serve_request",
    ),
    "servers/pm/pm_service_backend.c": (
        "service_backend_reserve", "service_backend_abort_reserved", "service_backend_prepare",
        "service_backend_abort_prepared", "service_backend_activate", "service_backend_exit_begin",
        "service_backend_exit_detach", "service_backend_release", "service_backend_grant_create",
        "service_backend_grant_revoke", "service_backend_grant_copy", "service_backend_call",
        "service_backend_reply",
    ),
    "servers/pm/pm_service_self_test.c": (
        "pm_service_vm_load", "pm_service_vm_commit", "pm_service_vm_exit_release",
        "pm_service_run_self_test",
    ),
    "servers/pm/pm_service_scratch.c": ("pm_service_run_scratch_preemption_test",),
    "tests/host/pm_test_fixture.c": tuple(
        "pm_test_" + name for name in (
            "write_u32_le", "write_u64_le", "read_u32_le", "read_u64_le",
            "handles_equal", "handle_is_none", "bytes_are_zero", "exit_request",
            "wait_request", "spawn_request", "sealed_event", "complete_spawn",
            "initialize_running_init", "expected_resources", "advance_to_stage",
            "rollback_resource_for_stage", "rollback_resource_bit", "rollback_spawn",
            "spawn_running_child", "complete_exit",
        )
    ),
    "tests/host/pm_model_test.c": ("pm_model_random", "pm_model_failure"),
    "tests/host/pm_model_reference.c": tuple(
        "pm_model_" + name for name in (
            "select_state", "select_present", "find_zombie",
            "has_child", "set_wait_result", "compare",
        )
    ),
    "tests/host/pm_model_operations.c": (
        "activate_selected_spawn", "pm_model_run_operation",
    ),
    "tests/host/spawn_abi_test_fixture.c": tuple(
        "spawn_abi_test_" + name for name in (
            "context_get", "write_u16_le", "write_u32_le", "write_u64_le",
            "read_u64_le", "bytes_are_zero", "literal_string",
            "write_string_reference", "build_spawn_request", "fill_plain_string",
            "fill_environment_string", "build_occupancy_request", "build_valid_elf",
        )
    ),
    "tests/host/spawn_request_test.c": ("decode_rejects_current_request",),
    "tests/host/vm_elf_test.c": ("elf_plan_is_unchanged_on_error",),
    "kernel/pm_service_test_report.c": (
        "test_failure", "process_has_no_thread", "bytes_are_zero",
        "acquire_report_authority", "report_fault_and_authority_are_valid",
        "report_transactions_are_clear", "report_execution_resources_are_clear",
        "report_generation_and_frames_are_valid", "report_runtime_and_current_thread_are_valid",
        "validate_report_current_and_readiness", "report_success_and_shutdown",
    ),
}

COMMON_OWNER = "servers/pm/pm_common.c"
VALIDATION_OWNER = "servers/pm/pm_validation.c"
SERVICE_FIXTURE_OWNER = "servers/pm/pm_service_fixture.c"
PERMITTED_OWNER_DEPENDENCIES = {
    COMMON_OWNER: (),
    "servers/pm/pm_core.c": (COMMON_OWNER, VALIDATION_OWNER),
    "servers/pm/pm_protocol.c": (COMMON_OWNER,),
    VALIDATION_OWNER: (COMMON_OWNER,),
    "servers/pm/pm_coordinator.c": (
        COMMON_OWNER, VALIDATION_OWNER, "servers/pm/pm_core.c", "servers/pm/pm_spawn.c",
        "lib/spawn/spawn_request.c", "lib/spawn/spawn_stack_layout.c",
        "servers/pm/pm_coordinator_spawn.c", "servers/pm/pm_coordinator_rollback.c",
    ),
    "servers/pm/pm_coordinator_protocol.c": (COMMON_OWNER,),
    "servers/pm/pm_coordinator_peers.c": ("servers/pm/pm_coordinator_protocol.c",),
    "servers/pm/pm_coordinator_spawn.c": (
        COMMON_OWNER, "servers/pm/pm_spawn.c", "servers/pm/pm_coordinator_protocol.c",
        "servers/pm/pm_coordinator_peers.c",
    ),
    "servers/pm/pm_coordinator_rollback.c": (
        "servers/pm/pm_rollback.c", "servers/pm/pm_coordinator_peers.c",
    ),
    "servers/pm/pm_coordinator_requests.c": (
        COMMON_OWNER, "servers/pm/pm_core.c",
        "servers/pm/pm_spawn.c", "servers/pm/pm_coordinator.c",
        "servers/pm/pm_coordinator_spawn.c", "servers/pm/pm_coordinator_rollback.c",
        "servers/pm/pm_coordinator_frontend.c",
    ),
    "servers/pm/pm_coordinator_frontend.c": (
        COMMON_OWNER, "servers/pm/pm_protocol.c", "servers/pm/pm_core.c",
        "servers/pm/pm_coordinator.c",
    ),
    "servers/pm/pm_coordinator_exit.c": (
        COMMON_OWNER, "servers/pm/pm_exit.c", "servers/pm/pm_coordinator_frontend.c",
        "servers/pm/pm_coordinator_peers.c", "servers/pm/pm_coordinator_wait.c",
    ),
    "servers/pm/pm_coordinator_wait.c": (
        COMMON_OWNER, "servers/pm/pm_wait.c", "servers/pm/pm_protocol.c",
        "servers/pm/pm_coordinator_frontend.c",
    ),
    "servers/pm/pm_spawn.c": (COMMON_OWNER, VALIDATION_OWNER),
    "servers/pm/pm_rollback.c": (COMMON_OWNER, VALIDATION_OWNER),
    "servers/pm/pm_wait.c": (COMMON_OWNER, VALIDATION_OWNER),
    "servers/pm/pm_exit.c": (
        COMMON_OWNER, VALIDATION_OWNER, "servers/pm/pm_wait.c",
    ),
    "lib/spawn/spawn_request.c": (),
    "lib/spawn/spawn_stack_layout.c": (),
    "servers/pm/pm_service.c": (
        *SOURCE_SETS["MICROS_PM_CORE_SOURCES"],
        *SOURCE_SETS["MICROS_PM_SERVICE_SOURCES"],
    ),
    SERVICE_FIXTURE_OWNER: ("servers/pm/pm_coordinator.c",),
    **{
        path: (SERVICE_FIXTURE_OWNER, *SOURCE_SETS["MICROS_PM_CORE_SOURCES"])
        for path in SOURCE_SETS["MICROS_PM_SERVICE_SOURCES"][2:]
    },
    "servers/pm/pm_service_requests.c": (
        SERVICE_FIXTURE_OWNER, *SOURCE_SETS["MICROS_PM_CORE_SOURCES"],
        *SOURCE_SETS["MICROS_PM_COORDINATOR_SOURCES"], "servers/pm/pm_service_backend.c",
    ),
    "servers/pm/pm_service_backend.c": (WRAPPER_PATH,),
    "servers/pm/pm_service_self_test.c": (
        SERVICE_FIXTURE_OWNER, *SOURCE_SETS["MICROS_PM_CORE_SOURCES"], WRAPPER_PATH,
    ),
    "servers/pm/pm_service_scratch.c": (
        SERVICE_FIXTURE_OWNER, *SOURCE_SETS["MICROS_PM_CORE_SOURCES"], WRAPPER_PATH,
    ),
    WRAPPER_PATH: (),
    "kernel/pm_service_test.c": ("kernel/pm_service_test_fixture.c",),
    "kernel/pm_service_test_fixture.c": (),
    "kernel/pm_service_test_report.c": (),
}

FILE_SCOPE_OBJECT_PATTERNS = {
    "tests/host/pm_coordinator_backend_test.c": (
        r"static\s+struct\s+coordinator_backend_capture\s+backend_capture\s*;",
    ),
    "servers/pm/pm_service_fixture.c": (
        r"volatile\s+struct\s+micros_bootstrap_service_config\s+"
        r"micros_bootstrap_service_config\s*;",
        r"static\s+struct\s+pm_service_context\s+service_context\s*=\s*"
        r"\{\s*\.pm_service_data\s*=\s*UINT64_C\(0x504d534552564943\)\s*,\s*\}\s*;",
    ),
    "tests/host/spawn_abi_test_fixture.c": (
        r"static\s+struct\s+spawn_abi_test_context\s+test_context\s*;",
    ),
    "kernel/pm_service_test_fixture.c": (
        r"static\s+const\s+struct\s+micros_bootstrap_expected_service\s+"
        r"expected_services\s*\[\s*\]\s*=\s*\{.*\}\s*;",
        r"static\s+const\s+struct\s+micros_privilege_profile\s+"
        r"profiles\s*\[\s*\]\s*=\s*\{.*\}\s*;",
        r"static\s+const\s+struct\s+micros_bootstrap_scheduler_policy\s+"
        r"policies\s*\[\s*\]\s*=\s*\{.*\}\s*;",
        r"const\s+struct\s+micros_bootstrap_runtime_config\s+"
        r"pm_service_test_runtime_config\s*=\s*\{.*\}\s*;",
    ),
}

STATIC_LOCAL_DECLARATIONS = {
    "servers/pm/pm_service_backend.c": (
        "static const struct micros_pm_coordinator_backend backend = {"
        ".reserve = service_backend_reserve,"
        ".abort_reserved = service_backend_abort_reserved,"
        ".prepare_execution = service_backend_prepare,"
        ".abort_prepared = service_backend_abort_prepared,"
        ".activate = service_backend_activate,"
        ".exit_begin = service_backend_exit_begin,"
        ".exit_detach = service_backend_exit_detach,"
        ".release_exited_process = service_backend_release,"
        ".grant_create = service_backend_grant_create,"
        ".grant_revoke = service_backend_grant_revoke,"
        ".grant_copy_from = service_backend_grant_copy,"
        ".call = service_backend_call,"
        ".reply = service_backend_reply, };",
    ),
    "tests/host/pm_coordinator_spawn_fixture.c": (
        "static const struct micros_pm_coordinator_backend backend = {"
        ".reserve = spawn_fixture_reserve,"
        ".abort_reserved = spawn_fixture_abort_reserved,"
        ".prepare_execution = spawn_fixture_prepare,"
        ".abort_prepared = spawn_fixture_abort_prepared,"
        ".activate = spawn_fixture_activate,"
        ".exit_begin = coordinator_exit_fixture_begin,"
        ".exit_detach = coordinator_exit_fixture_detach,"
        ".release_exited_process = coordinator_exit_fixture_release,"
        ".grant_create = spawn_fixture_grant_create,"
        ".grant_revoke = spawn_fixture_grant_revoke,"
        ".grant_copy_from = spawn_fixture_copy_parent,"
        ".call = spawn_fixture_call,"
        ".reply = coordinator_exit_fixture_reply, };",
    ),
    "tests/host/spawn_abi_test_fixture.c": (
        "static const uint16_t argument_sizes[8] = {"
        "256, 256, 256, 256, 256, 256, 1, 1, };",
        "static const uint16_t environment_sizes[8] = {"
        "249, 3, 3, 3, 3, 3, 3, 3, };",
        "static const uint32_t flags[3] = {"
        "UINT32_C(0x5), UINT32_C(0x4), UINT32_C(0x6), };",
    ),
}

PM_CASE_ORDER = (
    "micros_pm_control_test_run", "micros_pm_control_execution_test_run",
    "test_protocol_contract", "test_runtime_gate_and_spawn",
    "test_reverse_rollback_and_identity", "test_rollback_acknowledgment_identity",
    "test_generation_and_pid_exhaustion", "test_failed_init_is_terminal",
    "test_endpoint_lookup_and_failure_preservation", "test_orphaned_lifecycle_is_invalid",
    "test_exit_wait_and_reply_restore", "test_wait_selection",
    "test_parent_exit_reparents_and_aborts",
    "test_nested_parent_exit_stages_independent_waits", "test_replayable_transition_model",
)
SPAWN_CASE_ORDER = (
    "test_spawn_request_contract", "test_spawn_request_boundaries",
    "test_spawn_request_rejections", "test_application_stack",
    "test_application_stack_boundaries", "test_elf_plan",
    "test_elf_header_rejections", "test_elf_segment_rejections",
)
MODEL_OPERATION_ORDER = (
    "model_reserve_spawn", "model_advance_spawn", "model_abort_spawn", "model_begin_exit",
    "model_advance_exit", "model_begin_wait", "model_complete_wait", "model_lookup_endpoint",
)

FINAL_OWNER_GROUPS = {
    "servers/pm/pm_protocol.c": (
        "micros_pm_decode_request", "micros_pm_build_result",
    ),
    "servers/pm/pm_validation.c": ("micros_pm_table_validate",),
    "servers/pm/pm_spawn.c": (
        "micros_pm_begin_init", "micros_pm_begin_spawn",
        "micros_pm_spawn_reserve_kernel", "micros_pm_spawn_prepare_image",
        "micros_pm_spawn_freeze_mappings", "micros_pm_spawn_prepare_descriptors",
        "micros_pm_spawn_prepare_execution", "micros_pm_spawn_commit_hidden",
        "micros_pm_spawn_activate",
    ),
    "servers/pm/pm_rollback.c": (
        "micros_pm_spawn_rollback_begin", "micros_pm_spawn_rollback_ack",
    ),
    "servers/pm/pm_exit.c": (
        "micros_pm_exit_begin", "micros_pm_exit_detach",
        "micros_pm_exit_release_vm", "micros_pm_exit_release_process",
    ),
    "servers/pm/pm_wait.c": (
        "micros_pm_wait_begin", "micros_pm_wait_resume",
        "micros_pm_wait_reply_complete",
    ),
    "lib/spawn/spawn_stack_layout.c": ("micros_spawn_stack_layout_plan",),
    "kernel/pm_service_test_report.c": ("micros_pm_service_test_handle_trap",),
    "tests/host/pm_protocol_test.c": ("test_protocol_contract",),
    "tests/host/pm_spawn_test.c": (
        "test_runtime_gate_and_spawn", "test_reverse_rollback_and_identity",
        "test_rollback_acknowledgment_identity",
        "test_generation_and_pid_exhaustion", "test_failed_init_is_terminal",
    ),
    "tests/host/pm_validation_test.c": (
        "test_endpoint_lookup_and_failure_preservation",
        "test_orphaned_lifecycle_is_invalid",
    ),
    "tests/host/pm_exit_wait_test.c": (
        "test_exit_wait_and_reply_restore", "test_wait_selection",
        "test_parent_exit_reparents_and_aborts",
        "test_nested_parent_exit_stages_independent_waits",
    ),
    "tests/host/pm_model_test.c": ("test_replayable_transition_model",),
    "tests/host/spawn_request_test.c": (
        "test_spawn_request_contract", "test_spawn_request_boundaries",
        "test_spawn_request_rejections",
    ),
    "tests/host/application_stack_test.c": (
        "test_application_stack", "test_application_stack_boundaries",
    ),
    "tests/host/vm_elf_test.c": (
        "test_elf_plan", "test_elf_header_rejections",
        "test_elf_segment_rejections",
    ),
}


def original_owner(final_owner: str) -> str:
    if final_owner.startswith("servers/pm/"):
        return "servers/pm/pm_core.c"
    if final_owner.startswith("lib/spawn/"):
        return "lib/spawn/spawn_request.c"
    if final_owner.startswith("kernel/"):
        return "kernel/pm_service_test.c"
    if final_owner in {
        "tests/host/spawn_request_test.c",
        "tests/host/application_stack_test.c",
        "tests/host/vm_elf_test.c",
    }:
        return "tests/host/spawn_abi_test.c"
    return "tests/host/pm_test.c"


INITIAL_SYMBOL_OWNER_MIGRATION = tuple(
    (original_owner(owner), owner, name)
    for owner, names in FINAL_OWNER_GROUPS.items()
    for name in names
)
SYMBOL_OWNER_MIGRATION = ()

INITIAL_LONG_FUNCTION_MIGRATION = (
    ("servers/pm/pm_core.c", "micros_pm_table_validate"),
    ("servers/pm/pm_service.c", "run_self_test"),
    ("lib/spawn/spawn_request.c", "micros_spawn_request_decode"),
    ("tests/host/pm_test.c", "test_protocol_contract"),
    ("tests/host/pm_test.c", "test_rollback_acknowledgment_identity"),
    ("tests/host/pm_test.c", "test_endpoint_lookup_and_failure_preservation"),
    ("tests/host/pm_test.c", "test_exit_wait_and_reply_restore"),
    ("tests/host/pm_test.c", "test_parent_exit_reparents_and_aborts"),
    ("tests/host/pm_test.c", "test_nested_parent_exit_stages_independent_waits"),
    ("tests/host/pm_test.c", "test_replayable_transition_model"),
    ("tests/host/spawn_abi_test.c", "test_spawn_request_rejections"),
    ("tests/host/spawn_abi_test.c", "test_application_stack"),
    ("tests/host/spawn_abi_test.c", "test_elf_header_rejections"),
    ("tests/host/spawn_abi_test.c", "test_elf_segment_rejections"),
    ("kernel/pm_service_test.c", "micros_pm_service_test_handle_trap"),
    (GENERATOR_PATH, "render_fixture"),
)
LONG_FUNCTION_MIGRATION = ()


def all_sources() -> tuple[str, ...]:
    return tuple(path for sources in SOURCE_SETS.values() for path in sources)


def accepted_family_paths() -> set[str]:
    return {
        *all_sources(), *PRIVATE_HEADERS, *SHARED_PROTOCOL_HEADERS, *STABLE_HEADER_HASHES,
        WRAPPER_PATH, GENERATOR_PATH,
    }


def discovered_family_paths() -> set[str]:
    patterns = (
        "servers/pm/*.[ch]", "lib/spawn/*.[ch]", "kernel/pm_service_test*.[ch]",
        "tests/host/pm*.[ch]", "tests/host/spawn_abi_test*.[ch]",
        "tests/host/spawn_request_test*.[ch]", "tests/host/application_stack_test*.[ch]",
        "tests/host/vm_elf_test*.[ch]",
    )
    return {
        str(path.relative_to(REPOSITORY_ROOT))
        for pattern in patterns
        for path in REPOSITORY_ROOT.glob(pattern)
        if not path.name.startswith("pm_control_")
    } | {"include/micros/pm.h", "include/micros/spawn.h",
         *SHARED_PROTOCOL_HEADERS, "tests/qemu/pm_service_protocol.h", GENERATOR_PATH}


def compiled_source_header_closure() -> tuple[str, ...]:
    return tuple(sorted({
        dependency for path in accepted_family_paths()
        if Path(path).suffix in {".c", ".h"}
        for dependency in vm_layout.local_include_closure(path)
    }))


def owner_dependency_text(relative: str) -> str:
    selected = []
    for dependency in vm_layout.local_include_closure(relative):
        text = (REPOSITORY_ROOT / dependency).read_text(encoding="utf-8")
        if dependency in DECLARATION_ONLY_HEADERS and dependency != relative:
            text = macro_definition_text(text)
        selected.append(re.sub(
            r"\b(?:struct|union|enum)\s+[A-Za-z_][A-Za-z0-9_]*",
            "", c_lexical_text(text),
        ))
    return "\n".join(selected)


def static_local_storage(text: str) -> tuple[str, ...]:
    lexical = c_declaration_text(text)
    depth = 0
    observed = []
    for match in re.finditer(r"[{}]|\bstatic\b", lexical):
        token = match.group()
        if token == "{":
            depth += 1
        elif token == "}":
            depth -= 1
        elif depth != 0:
            end = lexical.find(";", match.end())
            if end == -1:
                raise AssertionError("unterminated static-local declaration")
            observed.append(lexical[match.start():end + 1].strip())
    return tuple(observed)


def cmake_commands(text: str, command: str, first_argument: str) -> tuple[str, ...]:
    return tuple(
        cmake_command(text[match.start():], command, first_argument)
        for match in re.finditer(
            rf"\b{re.escape(command)}\s*\(\s*{re.escape(first_argument)}\b", text,
        )
    )


class PmServiceSourceLayoutTest(unittest.TestCase):
    def test_static_local_storage_cannot_hide_behind_a_type_or_initializer(self) -> None:
        for declaration in (
            "static struct pm_model_context hidden;",
            "static struct micros_pm_table hidden = {0};",
            "static model_alias hidden[2];",
            "static uint8_t hidden[4], second[4];",
            "static int (*hidden)(void);",
        ):
            with self.subTest(declaration=declaration):
                self.assertEqual(
                    (declaration,),
                    static_local_storage(f"void helper(void) {{ {declaration} }}"),
                )
        self.assertEqual(
            (),
            static_local_storage(
                "static int helper(void) { return 1; }\n"
                "static struct pm_service_context service_context = {0};\n"
                '#define IGNORE "static"\n'
            ),
        )

    def test_recursive_dependencies_keep_hidden_code_and_ignore_struct_tags(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            (root / "owner.c").write_text(
                '#include "forward.h"\nvoid owner(void) {}\n', encoding="utf-8",
            )
            (root / "forward.h").write_text(
                '#include "nested.h"\n', encoding="utf-8",
            )
            (root / "nested.h").write_text(
                "struct micros_pm_wait_begin;\n"
                "static inline void hidden(void) { pm_wait_stage_result(0, 0, 0); }\n",
                encoding="utf-8",
            )
            with patch(f"{__name__}.REPOSITORY_ROOT", root), \
                    patch.object(vm_layout, "REPOSITORY_ROOT", root):
                text = owner_dependency_text("owner.c")
            self.assertIn("pm_wait_stage_result", text)
            self.assertNotIn("micros_pm_wait_begin", text)

    def test_all_accepted_sources_and_private_headers_exist(self) -> None:
        self.assertEqual(
            [],
            [
                path for path in (*all_sources(), *PRIVATE_HEADERS)
                if not (REPOSITORY_ROOT / path).is_file()
            ],
        )
        self.assertEqual(accepted_family_paths(), discovered_family_paths())

    def test_stable_headers_are_byte_exact(self) -> None:
        self.assertEqual(
            STABLE_HEADER_HASHES,
            {
                path: hashlib.sha256((REPOSITORY_ROOT / path).read_bytes()).hexdigest()
                for path in STABLE_HEADER_HASHES
            },
        )
        self.assertEqual(
            WRAPPER_HASH, hashlib.sha256((REPOSITORY_ROOT / WRAPPER_PATH).read_bytes()).hexdigest(),
        )

    def test_source_sets_and_current_consumers_are_exact(self) -> None:
        cmake = (REPOSITORY_ROOT / "CMakeLists.txt").read_text()
        for name, sources in SOURCE_SETS.items():
            with self.subTest(source_set=name):
                block = cmake_command(cmake, "set", name)
                self.assertEqual(
                    sources,
                    tuple(re.findall(
                        r"(?:servers/pm|lib/spawn|kernel|tests/host)/"
                        r"[A-Za-z0-9_]+\.c", block,
                    )),
                )
                for source in sources:
                    expected = 2 if source == "tests/host/spawn_abi_test_fixture.c" else 1
                    self.assertEqual(expected, cmake.count(source), source)
        for target, required in {
            "pm_test": ("MICROS_PM_CORE_SOURCES", "MICROS_PM_CORE_HOST_TEST_SOURCES"),
            "pm_coordinator_test": (
                "MICROS_PM_CORE_SOURCES", "MICROS_PM_COORDINATOR_SOURCES",
                "MICROS_PM_COORDINATOR_HOST_TEST_SOURCES",
                "MICROS_SPAWN_REQUEST_SOURCES",
                "MICROS_PM_RUNTIME_BACKEND_SOURCES",
            ),
            "spawn_abi_test": (
                "MICROS_SPAWN_REQUEST_SOURCES", "MICROS_SPAWN_ABI_HOST_TEST_SOURCES",
            ),
            "vm_core_test": ("MICROS_SPAWN_REQUEST_SOURCES",),
            "elf_spawn_model_test": (
                "MICROS_PM_CORE_SOURCES", "MICROS_PM_COORDINATOR_SOURCES",
                "MICROS_SPAWN_REQUEST_SOURCES",
            ),
            "vfs_test": ("MICROS_SPAWN_REQUEST_SOURCES",),
            "vfs_model_test": ("MICROS_SPAWN_REQUEST_SOURCES",),
            "vfs_service_test": ("MICROS_SPAWN_REQUEST_SOURCES",),
        }.items():
            block = cmake_command(cmake, "add_executable", target)
            for name in SOURCE_SETS:
                self.assertEqual(int(name in required), block.count(f"${{{name}}}"))
            for source in all_sources():
                self.assertNotIn(source, block)
        self.assertEqual(5, cmake.count("${MICROS_PM_SERVICE_SOURCES}"))
        self.assertEqual(8, cmake.count("${MICROS_PM_CORE_SOURCES}"))
        self.assertEqual(7, cmake.count("${MICROS_PM_COORDINATOR_SOURCES}"))
        self.assertEqual(1, cmake.count("${MICROS_PM_COORDINATOR_HOST_TEST_SOURCES}"))
        self.assertEqual(6, cmake.count("${MICROS_PM_RUNTIME_BACKEND_SOURCES}"))
        self.assertEqual(15, cmake.count("${MICROS_SPAWN_REQUEST_SOURCES}"))
        application = cmake_command(cmake, "add_executable", "application_runtime_test")
        self.assertEqual(1, application.count("${MICROS_SPAWN_REQUEST_SOURCES}"))
        self.assertEqual(1, application.count("tests/host/spawn_abi_test_fixture.c"))
        self.assertNotIn("${MICROS_SPAWN_ABI_HOST_TEST_SOURCES}", application)
        self.assertEqual(1, cmake.count("${MICROS_PM_SERVICE_QEMU_TEST_SOURCES}"))
        for target in ("micros_vm_server", "micros_vfs_service"):
            block = cmake_command(cmake, "micros_add_bootstrap_user", target)
            self.assertEqual(1, block.count("${MICROS_SPAWN_REQUEST_SOURCES}"))
        pm_consumers = cmake_commands(cmake, "micros_add_bootstrap_user", "micros_pm_service")
        self.assertEqual(5, len(pm_consumers))
        for block in pm_consumers:
            self.assertEqual(1, block.count("${MICROS_PM_CORE_SOURCES}"))
            self.assertEqual(1, block.count("${MICROS_PM_SERVICE_SOURCES}"))
            self.assertEqual(1, block.count(WRAPPER_PATH))
            self.assertEqual(1, block.count("${MICROS_SPAWN_REQUEST_SOURCES}"))
            self.assertEqual(1, block.count("${MICROS_PM_COORDINATOR_SOURCES}"))
            self.assertEqual(1, block.count("${MICROS_PM_RUNTIME_BACKEND_SOURCES}"))
        branches = re.findall(
            r"\b(?:if|elseif)\(\s*(MICROS_BUILD_[A-Z_]+)\s*\)\s*"
            r"micros_add_bootstrap_user\(\s*micros_pm_service\b", cmake,
        )
        self.assertEqual(
            [
                "MICROS_BUILD_VM_SCRATCH_PREEMPTION_TEST", "MICROS_BUILD_PM_SERVICE_TEST",
                "MICROS_BUILD_TTY_SERVICE_TEST", "MICROS_BUILD_VFS_SERVICE_TEST",
                "MICROS_BUILD_RAMFS_SERVICE_TEST",
            ],
            branches,
        )
        pm_kernel = "\n".join(cmake_if_blocks(cmake, "MICROS_BUILD_PM_SERVICE_TEST"))
        self.assertEqual(1, pm_kernel.count("${MICROS_PM_SERVICE_QEMU_TEST_SOURCES}"))

    def test_common_pm_post_link_check_enforces_exact_budget_once(self) -> None:
        cmake = (REPOSITORY_ROOT / "CMakeLists.txt").read_text()
        start = cmake.index("function(micros_add_bootstrap_user ")
        helper = cmake[start:cmake.index("endfunction()", start)]
        self.assertIn('if("${target}" STREQUAL "micros_pm_service")', helper)
        self.assertRegex(
            helper,
            r"set\(\s+user_elf_budget\s+--resident-page-limit\s+32\s+"
            r"--stack-page-count\s+1\s*\)",
        )
        self.assertEqual(1, helper.count("add_custom_command("))
        post_link = cmake_command(helper, "add_custom_command", "TARGET")
        self.assertEqual(1, post_link.count('"${CMAKE_CURRENT_SOURCE_DIR}/tools/check_user_elf.py"'))
        self.assertEqual(1, post_link.count("${user_elf_budget}"))
        self.assertEqual(
            0, sum(
                "micros_pm_service" in block
                for block in cmake_commands(cmake, "add_custom_command", "TARGET")
            ),
        )

    def test_public_and_case_owners_follow_exact_migration(self) -> None:
        self.assertEqual(44, len(INITIAL_SYMBOL_OWNER_MIGRATION))
        migration = set(SYMBOL_OWNER_MIGRATION)
        self.assertEqual(len(migration), len(SYMBOL_OWNER_MIGRATION))
        self.assertLessEqual(migration, set(INITIAL_SYMBOL_OWNER_MIGRATION))
        self.assertEqual((), SYMBOL_OWNER_MIGRATION)
        current = {name: owner for owner, _, name in migration}
        definitions = {}
        for path in all_sources():
            if not (REPOSITORY_ROOT / path).is_file():
                continue
            for name in c_function_definitions((REPOSITORY_ROOT / path).read_text()):
                definitions.setdefault(name, []).append(path)
        for owner, names in FINAL_OWNER_GROUPS.items():
            for name in names:
                self.assertEqual([current.get(name, owner)], definitions.get(name, []), name)

    def test_retained_and_private_helpers_have_exact_owners(self) -> None:
        definitions = {}
        for path in compiled_source_header_closure():
            for name in c_function_definitions((REPOSITORY_ROOT / path).read_text()):
                definitions.setdefault(name, []).append(path)
        for owner, names in {**RETAINED_OWNER_GROUPS, **HELPER_OWNER_GROUPS}.items():
            for name in names:
                self.assertEqual([owner], definitions.get(name, []), name)
        expected_declarations = {
            "servers/pm/pm_internal.h": (
                *HELPER_OWNER_GROUPS[COMMON_OWNER], "pm_wait_stage_result",
                "pm_spawn_prepare_activation", "pm_spawn_commit_activation",
                "pm_spawn_capacity_is_available",
                "pm_exit_prepare_begin", "pm_exit_commit_begin",
            ),
            "servers/pm/pm_service_internal.h": (
                *HELPER_OWNER_GROUPS[SERVICE_FIXTURE_OWNER],
                "pm_service_send_ready", "pm_service_serve_request",
                "pm_service_backend_get",
                "pm_service_run_self_test", "pm_service_run_scratch_preemption_test",
                "micros_pm_service_report", "micros_vm_scratch_preemption_peer_hold",
                "micros_vm_scratch_preemption_peer_run",
            ),
            "tests/host/pm_test_internal.h": (
                *HELPER_OWNER_GROUPS["tests/host/pm_test_fixture.c"], *PM_CASE_ORDER[2:],
            ),
            "tests/host/pm_model_test_internal.h": (
                "pm_model_failure", "pm_model_run_operation",
                *HELPER_OWNER_GROUPS["tests/host/pm_model_reference.c"],
            ),
            "tests/host/spawn_abi_test_internal.h": (
                *HELPER_OWNER_GROUPS["tests/host/spawn_abi_test_fixture.c"], *SPAWN_CASE_ORDER,
            ),
            "kernel/pm_service_test_internal.h": (),
            "servers/pm/pm_coordinator.h": (
                "micros_pm_coordinator_initialize", "micros_pm_coordinator_spawn_init",
                "micros_pm_coordinator_spawn_request", "micros_pm_coordinator_parent_lost",
                "micros_pm_coordinator_exit_request", "micros_pm_coordinator_wait_request",
                *RETAINED_OWNER_GROUPS["servers/pm/pm_coordinator_protocol.c"],
            ),
            "tests/host/pm_coordinator_test_internal.h": (
                "test_coordinator_initialization", "test_coordinator_embedded_bindings",
                "test_coordinator_binding_failures", "test_coordinator_table_failures",
                "test_coordinator_vfs_control_contract", "test_coordinator_vm_control_contract",
                "test_coordinator_control_rejections",
                "test_coordinator_directed_init", "test_coordinator_init_failure_rollback",
                "test_coordinator_init_postpublication_failures",
                "test_coordinator_init_preflight_preserves_pid",
                "test_coordinator_public_spawn", "test_coordinator_public_failure_results",
                "test_coordinator_public_preflight_results", "test_coordinator_parent_loss_rollback",
                "test_coordinator_parent_loss_during_rollback",
                "test_coordinator_public_capacity_and_unknown_grant",
                "test_coordinator_parent_loss_after_publication",
                "test_coordinator_exit_and_blocked_wait", "test_coordinator_exit_failure_boundaries",
                "test_coordinator_immediate_wait_reply_restore",
                "test_coordinator_nested_independent_wait_replies",
                "test_coordinator_real_runtime_backend",
            ),
            "servers/pm/pm_coordinator_internal.h": (
                "pm_coordinator_classify_vfs_result", "pm_coordinator_vfs_call",
                "pm_coordinator_vm_call", "pm_coordinator_run_spawn",
                "pm_coordinator_rollback_spawn",
                "pm_coordinator_spawn_backend_is_complete", "pm_coordinator_validate_instance",
                "pm_coordinator_prepare_snapshot", "pm_coordinator_complete_phase",
                "pm_coordinator_preflight_call", "pm_coordinator_reply_response",
                "pm_coordinator_no_reply_response", "pm_coordinator_vfs_exchange",
                "pm_coordinator_vm_exchange", "pm_coordinator_complete_wait_outcomes",
            ),
            "tests/host/pm_coordinator_spawn_fixture.h": (
                "coordinator_spawn_fixture_initialize", "coordinator_spawn_fixture_backend",
                "coordinator_spawn_fixture_resources_are_empty",
                "coordinator_spawn_fixture_initialize_parent", "coordinator_spawn_fixture_request",
                "coordinator_spawn_fixture_result_code", "coordinator_spawn_fixture_result_pid",
                "spawn_fixture_write_word", "spawn_fixture_read_word", "spawn_fixture_step",
                "spawn_fixture_result", "coordinator_exit_fixture_begin",
                "coordinator_exit_fixture_detach", "coordinator_exit_fixture_release",
                "coordinator_exit_fixture_reply", "coordinator_exit_fixture_vfs",
                "coordinator_exit_fixture_vm",
            ),
        }
        for private_header, expected in expected_declarations.items():
            text = c_declaration_text((REPOSITORY_ROOT / private_header).read_text())
            declarations = re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\((?!\s*\*)", text)
            self.assertEqual(len(expected), len(declarations), private_header)
            self.assertEqual(set(expected), set(declarations), private_header)
            for name in declarations:
                if name in definitions:
                    self.assertEqual(1, len(definitions[name]), (private_header, name))

    def test_irreversible_local_commits_do_not_revalidate_or_return_early(self) -> None:
        for path, commit, public in (
            ("servers/pm/pm_spawn.c", "pm_spawn_commit_activation", "micros_pm_spawn_activate"),
            ("servers/pm/pm_exit.c", "pm_exit_commit_begin", "micros_pm_exit_begin"),
        ):
            with self.subTest(commit=commit):
                text = c_lexical_text((REPOSITORY_ROOT / path).read_text())
                begin = re.search(rf"\bvoid\s+{commit}\s*\(", text)
                end = re.search(rf"\benum\s+micros_pm_model_error\s+{public}\s*\(", text)
                self.assertIsNotNone(begin)
                self.assertIsNotNone(end)
                self.assertLess(begin.start(), end.start())
                body = text[begin.start():end.start()]
                self.assertNotRegex(
                    body,
                    r"\b(?:micros_pm_[A-Za-z0-9_]+|pm_common_resolve_[A-Za-z0-9_]+|"
                    r"pm_spawn_prepare_activation|pm_exit_prepare_begin)\s*\(",
                )
                self.assertNotRegex(body, r"\breturn\b")

    def test_private_headers_are_declarations_only(self) -> None:
        for path in DECLARATION_ONLY_HEADERS:
            if not (REPOSITORY_ROOT / path).is_file():
                continue
            text = (REPOSITORY_ROOT / path).read_text()
            self.assertEqual({}, c_function_definitions(text), path)
            self.assertEqual((), file_scope_objects(text), path)
            self.assertNotRegex(c_declaration_text(text), r"\bstatic\b")

    def test_recursive_includes_and_dependency_direction_are_bounded(self) -> None:
        symbols = {}
        for path in PERMITTED_OWNER_DEPENDENCIES:
            for name in c_function_definitions((REPOSITORY_ROOT / path).read_text()):
                symbols.setdefault(name, set()).add(path)
        private_functions = {
            *HELPER_OWNER_GROUPS[COMMON_OWNER], "pm_wait_stage_result",
            "pm_spawn_prepare_activation", "pm_spawn_commit_activation",
            "pm_spawn_capacity_is_available",
            "pm_exit_prepare_begin", "pm_exit_commit_begin",
        }
        for path in all_sources():
            closure = vm_layout.local_include_closure(path)
            for dependency in closure:
                self.assertNotRegex(
                    (REPOSITORY_ROOT / dependency).read_text(),
                    r'(?m)^\s*#\s*include\s*[<"][^">]+\.(?:c|py)[">]',
                    dependency,
                )
            if path not in (
                *SOURCE_SETS["MICROS_PM_CORE_SOURCES"],
                *SOURCE_SETS["MICROS_PM_COORDINATOR_SOURCES"],
            ):
                self.assertNotIn("servers/pm/pm_internal.h", closure, path)
                text = owner_dependency_text(path)
                for name in private_functions:
                    self.assertNotRegex(text, rf"\b{re.escape(name)}\b", (path, name))
            if not path.startswith("tests/host/"):
                self.assertFalse(
                    set(closure) & {
                        header for header in PRIVATE_HEADERS
                        if header.startswith("tests/host/")
                    }, (path, closure),
                )
        for path, dependencies in PERMITTED_OWNER_DEPENDENCIES.items():
            text = owner_dependency_text(path)
            allowed = {path, *dependencies}
            violations = {
                name: sorted(owners) for name, owners in symbols.items()
                if not owners & allowed and re.search(rf"\b{re.escape(name)}\b", text)
            }
            self.assertEqual({}, violations, path)
        exit_text = owner_dependency_text("servers/pm/pm_exit.c")
        for name in c_function_definitions(
            (REPOSITORY_ROOT / "servers/pm/pm_wait.c").read_text(),
        ):
            if name != "pm_wait_stage_result":
                self.assertNotRegex(exit_text, rf"\b{re.escape(name)}\b")

    def test_file_scope_and_static_local_storage_have_exact_owners(self) -> None:
        observed = {}
        for path in compiled_source_header_closure():
            text = (REPOSITORY_ROOT / path).read_text()
            objects = file_scope_objects(text)
            if objects:
                observed[path] = objects
            self.assertEqual(
                tuple(re.sub(r"\s+", "", item)
                      for item in STATIC_LOCAL_DECLARATIONS.get(path, ())),
                tuple(re.sub(r"\s+", "", item) for item in static_local_storage(text)),
                path,
            )
        self.assertEqual(set(FILE_SCOPE_OBJECT_PATTERNS), set(observed))
        for path, patterns in FILE_SCOPE_OBJECT_PATTERNS.items():
            self.assertEqual(len(patterns), len(observed[path]), path)
            for pattern in patterns:
                self.assertEqual(
                    1, sum(bool(re.fullmatch(pattern, obj, re.DOTALL)) for obj in observed[path]),
                    (path, pattern),
                )

    def test_context_patterns_reject_arrays_aliases_and_codeclared_state(self) -> None:
        pattern = FILE_SCOPE_OBJECT_PATTERNS[SERVICE_FIXTURE_OWNER][1]
        for declaration in (
            "static struct pm_service_context service_context[2] = {0};",
            "static service_alias service_context = {0};",
            "extern struct pm_service_context service_context = {0};",
        ):
            self.assertEqual(1, len(file_scope_objects(declaration)))
            self.assertIsNone(re.fullmatch(pattern, declaration))
        for declaration in (
            "static struct pm_service_context service_context, hidden;",
            "static struct pm_service_context service_context = {0}, hidden = {0};",
            "static struct pm_service_context service_context[2], hidden[2];",
        ):
            with self.assertRaisesRegex(AssertionError, "multiple objects"):
                file_scope_objects(declaration)

    def test_contexts_retain_exact_members_and_buffer_geometry(self) -> None:
        service = c_declaration_text(
            (REPOSITORY_ROOT / "servers/pm/pm_service_internal.h").read_text(),
        )
        spawn = c_declaration_text(
            (REPOSITORY_ROOT / "tests/host/spawn_abi_test_internal.h").read_text(),
        )
        for text, name, expected in (
            (
                service, "pm_service_context",
                "struct micros_pm_table process_table;"
                "volatile uint64_t pm_service_data;"
                "struct micros_pm_coordinator coordinator;"
                "enum pm_service_coordinator_mode coordinator_mode;"
                "struct micros_pm_table initial_table;",
            ),
            (
                spawn, "spawn_abi_test_context",
                "uint8_t request_bytes[MICROS_SPAWN_REQUEST_SIZE_MAX];"
                "uint8_t elf_bytes[MICROS_APPLICATION_EXECUTABLE_SIZE_MAX + 1];"
                "uint8_t boundary_path[MICROS_SPAWN_STRING_SIZE_MAX];"
                "uint8_t boundary_arguments[MICROS_SPAWN_ARGUMENT_CAPACITY][MICROS_SPAWN_STRING_SIZE_MAX];"
                "uint8_t boundary_environment[MICROS_SPAWN_ENVIRONMENT_CAPACITY][MICROS_SPAWN_STRING_SIZE_MAX];"
                "uint8_t occupancy_arguments[8][MICROS_SPAWN_STRING_SIZE_MAX];"
                "uint8_t occupancy_environment[8][MICROS_SPAWN_STRING_SIZE_MAX];",
            ),
        ):
            match = re.search(rf"\bstruct\s+{name}\s*\{{([^}}]*)\}}\s*;", text)
            self.assertIsNotNone(match, name)
            self.assertEqual(
                re.sub(r"\s+", "", expected), re.sub(r"\s+", "", match.group(1)), name,
            )

    def test_current_static_state_has_exact_owners(self) -> None:
        expected = {
            "process_table": [],
            "pm_service_data": [],
            "initial_table": [],
            "request_bytes": [],
            "elf_bytes": [],
            "path_storage": [],
            "argument_storage": [],
            "environment_storage": [],
        }
        for name, owners in expected.items():
            observed = []
            pattern = re.compile(
                r"\bstatic\s+(?:volatile\s+)?"
                r"(?:struct\s+[A-Za-z_][A-Za-z0-9_]*|uint8_t|uint64_t)\s+"
                rf"{re.escape(name)}\b"
            )
            for path in all_sources():
                text = c_lexical_text((REPOSITORY_ROOT / path).read_text())
                observed.extend(path for _ in pattern.finditer(text))
            self.assertEqual(owners, observed, name)
        for path in (*SOURCE_SETS["MICROS_PM_CORE_HOST_TEST_SOURCES"],
                     *SOURCE_SETS["MICROS_PM_SERVICE_QEMU_TEST_SOURCES"]):
            text = c_lexical_text((REPOSITORY_ROOT / path).read_text())
            self.assertNotRegex(
                text, r"\bstatic\s+(?:volatile\s+)?struct\s+micros_pm_table\b",
            )
        contexts = []
        for path in all_sources():
            text = c_lexical_text((REPOSITORY_ROOT / path).read_text())
            contexts.extend(
                (path, match.group(1)) for match in re.finditer(
                    r"\bstatic\s+struct\s+(spawn_abi_test_context|pm_service_context)\b",
                    text,
                )
            )
        self.assertEqual(
            [
                ("servers/pm/pm_service_fixture.c", "pm_service_context"),
                ("tests/host/spawn_abi_test_fixture.c", "spawn_abi_test_context"),
            ],
            contexts,
        )

    def test_new_paths_inherit_exact_original_classification(self) -> None:
        baselines = {
            "servers/pm/pm_core.c",
            "servers/pm/pm_service.c",
            "lib/spawn/spawn_request.c",
            "tests/host/pm_test.c",
            "tests/host/spawn_abi_test.c",
            "kernel/pm_service_test.c",
        }
        observed = [
            entry for entry in validation_plan.PATH_CLASSIFICATION_EQUIVALENTS
            if entry[0] in baselines
        ]
        self.assertEqual(baselines, {baseline for baseline, _ in observed})
        expected = set(all_sources()) | set(PRIVATE_HEADERS)
        inherited = set(baselines)
        for baseline, paths in observed:
            inherited.update(paths)
            for path in paths:
                for tier in ("fast", "pr"):
                    self.assertEqual(
                        validation_plan.plan([baseline], tier, "main"),
                        validation_plan.plan([path], tier, "main"),
                        path,
                    )
        self.assertEqual(expected, inherited)
        for header in SHARED_PROTOCOL_HEADERS:
            self.assertTrue(validation_plan.path_matches(
                header, validation_plan.SHARED_QEMU_PATHS
            ))
            self.assertTrue(validation_plan.requires_full_tier([header]))
            self.assertEqual(
                list(validation_plan.QEMU_WORKFLOWS),
                validation_plan.affected_workflows([header]),
            )
        self.assertIn(
            "tests/host/test_pm_service_source_layout.py",
            validation_plan.HOST_UNIT_ONLY_INPUTS,
        )

    def test_native_cases_and_existing_executions_remain_exact(self) -> None:
        cmake = (REPOSITORY_ROOT / "CMakeLists.txt").read_text()
        for path, expected in (
            ("tests/host/pm_test.c", PM_CASE_ORDER),
            ("tests/host/spawn_abi_test.c", SPAWN_CASE_ORDER),
        ):
            text = (REPOSITORY_ROOT / path).read_text()
            main = text[text.index("int main("):]
            self.assertEqual(
                expected,
                tuple(re.findall(r"\b((?:test_|micros_pm_control_)[A-Za-z0-9_]+)\s*\(", main)),
            )
        self.assertEqual(1, cmake.count("NAME pm-lifecycle"))
        self.assertEqual(1, cmake.count("NAME pm-coordinator"))
        self.assertEqual(1, cmake.count("NAME spawn-abi"))
        self.assertIn(
            'printf("spawn ABI tests passed\\n");',
            (REPOSITORY_ROOT / "tests/host/spawn_abi_test.c").read_text(),
        )
        for target in (
            "test-qemu-pm-service", "test-qemu-vm-scratch-preemption",
            "test-qemu-tty", "test-qemu-ramfs", "test-qemu-vfs",
        ):
            cmake_command(cmake, "add_custom_target", target)

    def test_seeded_model_draw_dispatch_trace_and_comparison_are_retained(self) -> None:
        model = (REPOSITORY_ROOT / "tests/host/pm_model_test.c").read_text()
        operations = (REPOSITORY_ROOT / "tests/host/pm_model_operations.c").read_text()
        header = (REPOSITORY_ROOT / "tests/host/pm_model_test_internal.h").read_text()
        self.assertIn("PM_MODEL_STEP_COUNT = 4096", header)
        self.assertIn("context->seed = UINT64_C(0x4d4943524f53504d)", model)
        self.assertIn("struct pm_model_context state;", model)
        self.assertIn("context->step < PM_MODEL_STEP_COUNT", model)
        self.assertEqual(2, len(re.findall(r"\bpm_model_random\s*\(", model)))
        self.assertIn("uint32_t operation = (uint32_t)(choice % 8)", model)
        self.assertIn("context->operation_trace[context->step] = operation << 24", model)
        self.assertEqual(
            tuple(enumerate(MODEL_OPERATION_ORDER)),
            tuple(
                (int(index), name) for index, name in re.findall(
                    r"case\s+(\d+)\s*:\s*return\s+(model_[A-Za-z_]+)\s*\(", operations,
                )
            ),
        )
        self.assertEqual(
            6, operations.count("context->operation_trace[context->step] |= (uint32_t)slot << 16"),
        )
        self.assertIn("(uint32_t)flags << 16", operations)
        run = model.index("pm_model_run_operation(context, operation, choice)")
        validate = model.index("micros_pm_table_validate(&context->table)", run)
        compare = model.index("pm_model_compare(&context->table, &context->reference, context->init)")
        self.assertLess(run, validate)
        self.assertLess(validate, compare)
        self.assertIn('fprintf(stderr, "operation trace:")', model)
        self.assertIn("index <= step", model)

    def test_service_guards_and_startup_dispatch_order_are_retained(self) -> None:
        service = (REPOSITORY_ROOT / "servers/pm/pm_service.c").read_text()
        ordered = (
            "pm_service_context_get", "context->pm_service_data",
            "pm_service_validate_configuration", "micros_pm_table_initialize",
            "micros_pm_table_validate", "pm_service_configure_coordinator",
            "pm_service_copy_bytes", "pm_service_send_ready",
            "micros_runtime_receive", "MICROS_IPC_TYPE_KERNEL_NOTIFICATION",
            "micros_pm_enable_runtime", "pm_service_run_self_test",
            "pm_service_run_scratch_preemption_test", "pm_service_serve_request",
            "micros_runtime_reply_receive",
        )
        self.assertEqual(sorted(service.index(name) for name in ordered),
                         [service.index(name) for name in ordered])
        for owner, guard in (
            ("pm_service_requests.c", "#ifndef MICROS_VM_SCRATCH_PREEMPTION_TEST"),
            ("pm_service_self_test.c", "#ifdef MICROS_PM_SERVICE_TEST"),
            ("pm_service_scratch.c", "#ifdef MICROS_VM_SCRATCH_PREEMPTION_TEST"),
        ):
            text = (REPOSITORY_ROOT / "servers/pm" / owner).read_text()
            self.assertTrue(text.startswith(
                '#include "servers/pm/pm_service_internal.h"\n\n' + guard,
            ), owner)
            self.assertTrue(text.rstrip().endswith("#endif"), owner)
        scenario = (REPOSITORY_ROOT / "servers/pm/pm_service_self_test.c").read_text()
        body = scenario[scenario.index("_Noreturn void pm_service_run_self_test"):]
        phases = (
            "initialize_self_test_outputs", "reserve_and_load_child",
            "prepare_abort_and_reprepare_child", "activate_commit_and_capture_child_exit",
            "begin_child_exit_with_preservation", "detach_release_and_validate_child",
            "micros_pm_service_report",
        )
        self.assertEqual(sorted(body.index(name) for name in phases),
                         [body.index(name) for name in phases])

    def test_report_and_generator_evidence_identities_remain_stable(self) -> None:
        cmake = (REPOSITORY_ROOT / "CMakeLists.txt").read_text()
        target = cmake_command(cmake, "add_custom_target", "test-qemu-pm-service")
        self.assertIn("--timeout\n            45", target)
        self.assertEqual(1, cmake.count("MICROS_PM_SERVICE_TEST_PASS"))
        generator = ast.parse((REPOSITORY_ROOT / GENERATOR_PATH).read_text())
        flags = tuple(
            node.args[0].value for node in ast.walk(generator)
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Attribute) and node.func.attr == "add_argument"
        )
        self.assertEqual(
            ("--launcher-elf", "--vm-elf", "--pm-elf", "--probe-elf", "--output"), flags,
        )
        constants = {
            node.value for node in ast.walk(generator)
            if isinstance(node, ast.Constant) and isinstance(node.value, str)
        }
        self.assertIn("micros_pm_service_report", constants)
        self.assertIn('#include "kernel/pm_service_test_fixture.h"', constants)
        self.assertIn("PM service fixture generation failed: ", constants)
        golden = (REPOSITORY_ROOT / "tests/host/test_generate_pm_service_fixture.py").read_text()
        self.assertIn(
            "0792855c7e977b78c07e47b08da10e849e753cbd833f2ebeb46ba2725fda8585", golden,
        )

    def test_long_function_migration_matches_real_signal(self) -> None:
        self.assertEqual(16, len(INITIAL_LONG_FUNCTION_MIGRATION))
        oversized = set()
        for path in all_sources():
            if not (REPOSITORY_ROOT / path).is_file():
                continue
            for name, count in c_function_definitions(
                (REPOSITORY_ROOT / path).read_text()
            ).items():
                if count > FUNCTION_REVIEW_LINE_LIMIT:
                    oversized.add((path, name))
        for node in ast.walk(ast.parse((REPOSITORY_ROOT / GENERATOR_PATH).read_text())):
            if (
                isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
                and node.end_lineno - node.lineno + 1 > FUNCTION_REVIEW_LINE_LIMIT
            ):
                oversized.add((GENERATOR_PATH, node.name))
        self.assertLessEqual(
            set(LONG_FUNCTION_MIGRATION), set(INITIAL_LONG_FUNCTION_MIGRATION),
        )
        self.assertEqual(set(LONG_FUNCTION_MIGRATION), oversized)
        self.assertEqual((), LONG_FUNCTION_MIGRATION)


if __name__ == "__main__":
    unittest.main()
