"""Bounded source-contract checks for Accepted ADR-0070."""

import ast
import hashlib
import json
from pathlib import Path
import re
import unittest

from tests.host.ast_evidence import ast_dump
from tests.host.c_source_records import function_records
from tests.host.test_frame_ownership_source_layout import tokens
from tests.host.test_pm_service_source_layout import cmake_commands
from tests.host.test_vm_service_source_layout import file_scope_objects
from tools import validation_context, validation_plan
from tests.host import kernel_layout_source


ROOT = Path(__file__).resolve().parents[2]
REVIEWED_BASE = "ac94a21141b86c2e9b661cf90f500cca82e0c0c4"
BASE_CMAKE_DIGEST = "4e370ba5578b0b114a51d2d5ae738320ce7503a3d51bd7c2e6fb2623284981b3"
STRUCTURAL_TEST_PATH = "tests/host/test_tty_source_layout.py"
RUNTIME_SOURCES = (
    "kernel/tty_handoff_runtime.c", "kernel/tty_handoff_runtime_state.c",
    "kernel/tty_handoff_runtime_validate.c", "kernel/tty_handoff_runtime_transitions.c",
)
SERVICE_SOURCES = (
    "servers/tty/tty_service_core.c", "servers/tty/tty_service_validate.c",
    "servers/tty/tty_service_read.c", "servers/tty/tty_service_write.c",
    "servers/tty/tty_service_request.c",
)
MODEL_SOURCES = (
    "tests/host/tty_model_test.c", "tests/host/tty_model_common.c",
    "tests/host/tty_model_reference.c", "tests/host/tty_model_io.c",
    "tests/host/tty_model_protocol.c", "tests/host/tty_model_operations.c",
    "tests/host/tty_model_validate.c",
)
HANDOFF_SOURCES = (
    "tests/host/tty_handoff_test.c", "tests/host/tty_handoff_test_fixture.c",
    "tests/host/tty_handoff_device_test.c", "tests/host/tty_handoff_runtime_test.c",
)
CONTROL_SOURCES = (
    "tests/host/tty_control_syscall_test.c", "tests/host/tty_control_syscall_test_fixture.c",
    "tests/host/tty_control_syscall_test_observe.c", "tests/host/tty_control_syscall_test_controller.c",
)
FAMILY_PATHS = (
    RUNTIME_SOURCES, ("kernel/uart_console_test.c",), SERVICE_SOURCES,
    MODEL_SOURCES, HANDOFF_SOURCES, CONTROL_SOURCES, ("tests/host/tty_control_test.c",),
)
SOURCE_SETS = {
    "MICROS_TTY_HANDOFF_RUNTIME_SOURCES": RUNTIME_SOURCES,
    "MICROS_TTY_SERVICE_CORE_SOURCES": SERVICE_SOURCES,
    "MICROS_TTY_MODEL_TEST_SOURCES": MODEL_SOURCES,
    "MICROS_TTY_HANDOFF_HOST_TEST_SOURCES": HANDOFF_SOURCES,
    "MICROS_TTY_CONTROL_SYSCALL_HOST_TEST_SOURCES": CONTROL_SOURCES,
}
PRIVATE_HEADERS = (
    "kernel/tty_handoff_runtime_internal.h", "servers/tty/tty_service_internal.h",
    "tests/host/tty_model_internal.h", "tests/host/tty_handoff_test_internal.h",
    "tests/host/tty_control_syscall_test_internal.h",
)
GENERATOR_PATH = "tools/generate_tty_service_fixture.py"
ORIGINAL_FUNCTIONS = {
    0: {
        0: (
            "micros_tty_handoff_runtime_prepare micros_tty_handoff_runtime_bind_bootstrap "
            "micros_tty_handoff_runtime_reset micros_tty_handoff_runtime_state "
            "micros_tty_handoff_runtime_authoritative_state micros_tty_handoff_runtime_begin "
            "micros_tty_handoff_runtime_deadline_expired micros_tty_handoff_runtime_panic"
        ).split(),
        1: "clear_bytes copy_bytes".split(),
        2: (
            "process_handles_equal thread_handles_equal endpoint_matches_process identity_is_valid runtime_entry "
            "binding_identity_matches live_binding_matches micros_tty_handoff_runtime_role_ready "
            "micros_tty_handoff_runtime_device_authority micros_tty_handoff_runtime_validate"
        ).split(),
        3: (
            "micros_tty_handoff_runtime_prepare_begin micros_tty_handoff_runtime_commit_begin_deadline_prevalidated "
            "micros_tty_handoff_runtime_commit_begin_phase_prevalidated micros_tty_handoff_runtime_prepare_mapped "
            "micros_tty_handoff_runtime_commit_mapped_prevalidated micros_tty_handoff_runtime_prepare_release "
            "micros_tty_handoff_runtime_commit_release_prevalidated micros_tty_handoff_runtime_prepare_commit "
            "micros_tty_handoff_runtime_commit_console_prevalidated micros_tty_handoff_runtime_prepare_claim "
            "micros_tty_handoff_runtime_commit_claim_prevalidated micros_tty_handoff_runtime_prepare_complete "
            "micros_tty_handoff_runtime_commit_complete_prevalidated micros_tty_handoff_runtime_prepare_ready "
            "micros_tty_handoff_runtime_commit_ready_prevalidated"
        ).split(),
    },
    1: {0: "uart_registers micros_uart_console_runtime_run_self_test".split()},
    2: {
        0: "micros_tty_service_initialize micros_tty_service_commit_ownership micros_tty_service_handle_call".split(),
        1: "endpoint_is_valid request_is_clear completion_is_clear completion_is_valid micros_tty_service_validate".split(),
        2: "publish_read_completion micros_tty_service_complete_read handle_submit_read".split(),
        3: ["handle_submit_write"],
        4: "clear_bytes prepare_reply prepare_protocol_reply copy_grant validate_submit handle_cancel handle_collect micros_tty_service_commit_reply".split(),
    },
    3: {
        0: "model_collect_request_id model_random_transition model_scripted_transition test_replayable_mixed_transition_model main".split(),
        1: "model_failure write_u32_le write_u64_le read_u32_le read_u64_le model_random model_record".split(),
        2: (
            "reference_input_index reference_echo_index reference_output_pending reference_publish_output "
            "reference_enqueue_echo reference_receive_byte reference_first_line_length reference_consume_input "
            "reference_write_accept reference_write_arm_retry reference_output_take"
        ).split(),
        3: "model_copy_grant model_bus_read model_bus_write".split(),
        4: "submit_request identity_request model_check_reply model_commit_reply".split(),
        5: "model_submit_read model_submit_write model_cancel model_collect model_input_irq model_transmit_irq model_malformed_submit".split(),
        6: ["model_validate"],
    },
    4: {
        0: (
            "test_complete_handoff test_deadline_covers_every_pre_ready_phase test_ready_preserves_retained_route "
            "test_uart_console_ownership test_failure_preservation advance_to_phase test_terminal_panic "
            "test_invariant_rejection micros_tty_handoff_test_run"
        ).split(),
        1: "micros_bootstrap_control_find_binding micros_process_resolve micros_thread_resolve micros_endpoint_resolve_internal micros_privilege_profile_allows_kernel_operation".split(),
        2: ["test_device_leaf_classification"],
        3: ["test_runtime_lifecycle_validation"],
    },
    5: {
        0: (
            "test_commit_orders_state_before_delivery test_completion_orders_hardware_before_idle "
            "test_returned_failures_preserve_state test_invariant_failure_is_fatal_before_mutation "
            "test_hardware_corruption_precedes_tuple_failure main"
        ).split(),
        1: "commit_arguments complete_arguments reset_fixture invoke".split(),
        2: (
            "micros_test_irq_is_enabled micros_test_timer_interrupt_is_enabled micros_test_external_interrupt_is_enabled "
            "micros_bootstrap_runtime_state micros_bootstrap_runtime_authoritative_state micros_bootstrap_runtime_validate "
            "micros_ipc_runtime_registry micros_ipc_runtime_authoritative_registry micros_kernel_object_runtime_registry "
            "micros_tty_handoff_runtime_state micros_tty_handoff_runtime_authoritative_state "
            "micros_tty_control_syscall_authority_resolve micros_tty_handoff_runtime_validate micros_plic_validate "
            "uart_console_handoff_is_active micros_user_address_space_validate_tty_uart_mapping"
        ).split(),
        3: (
            "micros_test_external_interrupt_enable micros_bootstrap_runtime_fail "
            "micros_tty_handoff_runtime_prepare_commit micros_tty_handoff_runtime_commit_console_prevalidated "
            "micros_tty_handoff_runtime_prepare_complete micros_tty_handoff_runtime_commit_complete_prevalidated "
            "micros_plic_prepare_tty_enable micros_plic_commit_tty_prepare_prevalidated "
            "micros_plic_commit_tty_enable_prevalidated micros_plic_complete micros_tty_owner_fault_record micros_trap_panic_entry"
        ).split(),
    },
    6: {0: (
        "decode_failure_preserves_request test_contract_and_decode test_commit_width_rejection "
        "test_complete_shape_rejection test_syscall_authority test_no_console_authority micros_tty_control_test_run"
    ).split()},
}
LONG_FUNCTIONS = (
    (0, "live_binding_matches"), (1, "micros_uart_console_runtime_run_self_test"),
    (2, "handle_submit_write"), (3, "model_validate"),
    (4, "test_device_leaf_classification"), (4, "test_runtime_lifecycle_validation"),
    (6, "test_syscall_authority"),
)
REQUIRED_TASKS = (*LONG_FUNCTIONS, (7, "render_fixture"))
INITIAL_SYMBOL_OWNER_MIGRATION = tuple(
    (family, name) for family, owners in ORIGINAL_FUNCTIONS.items()
    for owner, names in owners.items() if owner != 0 for name in names
)
SYMBOL_OWNER_MIGRATION = ()
FUNCTION_TASK_MIGRATION = ()
RUNTIME_CONTEXT_TAG = "tty_handoff_context"
RUNTIME_CONTEXT_GETTER = "tty_handoff_context"
RUNTIME_CONTEXT_FIELDS = (
    "struct micros_tty_handoff_runtime_state tty_handoff_state;",
    "const struct micros_bootstrap_control_state *tty_bootstrap_state;",
)
RUNTIME_CONTEXT_HELPERS = ("binding_identity_matches", "live_binding_matches")
RUNTIME_PRIVATE_EXPORTS = (
    "clear_bytes", "copy_bytes", "endpoint_matches_process", "identity_is_valid", "binding_identity_matches",
)
LIVE_WORK_TAG = "tty_handoff_live_binding"
LIVE_PHASE = "tty_handoff_classify_live_binding"
LIVE_FIELDS = (
    ("const struct micros_process *process;", False),
    ("const struct micros_thread *thread;", False),
    ("const struct micros_endpoint_record *endpoint;", False),
    ("bool prepared;", False),
    ("bool active;", False),
)
LIVE_PARAMETERS = (
    "struct tty_handoff_context *context, const struct micros_bootstrap_binding *binding, "
    "const struct micros_bootstrap_runtime_entry *transition, struct tty_handoff_live_binding *work"
)
SERVICE_PRIVATE_EXPORTS = (
    "clear_bytes", "copy_grant", "endpoint_is_valid", "handle_cancel", "handle_collect",
    "handle_submit_read", "handle_submit_write", "prepare_protocol_reply", "prepare_reply", "validate_submit",
)
SERVICE_MAGIC_DIGEST = "66be1b9eddf34c6e64e89c838b272dc5c86320c15bedaac301d570cda2c4f3d1"
WRITE_WORK_TAG = "tty_service_write_work"
WRITE_PHASE = "tty_service_publish_write"
WRITE_FIELDS = (
    ("enum micros_tty_service_copy_result copy_result;", False),
    ("enum micros_tty_service_error error;", False),
    ("struct micros_tty_effects effects;", False),
)
WRITE_PARAMETERS = (
    "struct micros_tty_service_state *state, const struct micros_tty_request *request, "
    "struct micros_tty_service_reply_action *action, struct tty_service_write_work *work"
)
MODEL_PRIVATE_EXPORTS = (
    "identity_request", "model_bus_read", "model_bus_write", "model_cancel", "model_check_reply",
    "model_collect", "model_commit_reply", "model_copy_grant", "model_failure", "model_input_irq",
    "model_malformed_submit", "model_random", "model_record", "model_submit_read", "model_submit_write",
    "model_transmit_irq", "model_validate", "read_u32_le", "read_u64_le", "reference_consume_input",
    "reference_first_line_length", "reference_output_pending", "reference_output_take", "reference_receive_byte",
    "reference_write_accept", "reference_write_arm_retry", "submit_request", "write_u32_le", "write_u64_le",
)
MODEL_READONLY_NAMES = {"tty_model_tty_endpoint": "tty_endpoint", "tty_model_vfs_endpoint": "vfs_endpoint"}
MODEL_PREFIX_DIGEST = "720118eb98561c97ab5b721fbdfa1329cdca3bb690695530e67727f3d6f321f2"
MODEL_MACRO_DIGEST = "6c4301b5c2309dbb1c1c1af28fbb35438c945005d399d97acc749b1ab15a3bca"
MODEL_VALIDATE_WORK_TAG = "tty_model_validation_work"
MODEL_VALIDATE_PHASE = "tty_model_compare_resident_buffers"
MODEL_VALIDATE_FIELDS = (
    ("const struct micros_tty_state *terminal;", "value"),
    ("const struct model_reference *reference;", "value"),
    ("size_t index;", False),
)
MODEL_VALIDATE_PARAMETERS = "struct tty_model *model, struct tty_model_validation_work *work"
HANDOFF_FIXTURE_TAG = "tty_handoff_test_fixture"
HANDOFF_FIXTURE_GETTER = "tty_handoff_test_fixture"
HANDOFF_FIXTURE_FIELDS = (
    "struct micros_process runtime_process;",
    "struct micros_thread runtime_thread;",
    "struct micros_endpoint_record runtime_endpoint;",
    "struct micros_process_handle runtime_process_handle;",
    "struct micros_thread_handle runtime_thread_handle;",
)
HANDOFF_CONTEXT_ROOTS = (
    "micros_process_resolve", "micros_thread_resolve", "micros_endpoint_resolve_internal",
    "test_runtime_lifecycle_validation",
)
HANDOFF_PRIVATE_EXPORTS = ("test_device_leaf_classification", "test_runtime_lifecycle_validation")
HANDOFF_MACRO_DIGEST = "dc1fbf95f12b7896694addb93aa8603ed20b7bd57022a9d01f7b8c7342234fb6"
DEVICE_WORK_TAG = "tty_handoff_device_work"
DEVICE_PHASE = "tty_handoff_test_check_device_exclusions"
DEVICE_FIELDS = (
    ("struct micros_tty_device_authority authority;", True),
    ("enum micros_tty_device_leaf_class classification;", False),
)
DEVICE_PARAMETERS = (
    "struct tty_handoff_device_work *work, const struct micros_process_handle tty_process, "
    "const struct micros_process_handle foreign_process, const uint64_t tty_root, const uint32_t exact_permissions"
)
DEVICE_ARGUMENTS = "&work, tty_process, foreign_process, tty_root, exact_permissions"
LIFECYCLE_WORK_TAG = "tty_handoff_runtime_case"
LIFECYCLE_FIELDS = (
    ("struct micros_bootstrap_control_state bootstrap;", True),
    ("struct micros_bootstrap_binding *binding;", "value"),
    ("struct micros_bootstrap_manifest_entry *entry;", "value"),
    ("struct micros_bootstrap_runtime_entry *transition;", "value"),
    ("struct micros_endpoint_registry registry;", True),
    ("struct micros_kernel_objects objects;", True),
    ("struct micros_syscall_context context;", True),
    ("struct micros_tty_handoff mapped;", False),
    ("struct micros_tty_handoff begin;", False),
    ("struct micros_tty_handoff released;", False),
    ("struct micros_tty_handoff committed;", False),
    ("struct micros_tty_handoff claimed;", False),
    ("struct micros_tty_handoff completed;", False),
    ("struct micros_tty_handoff ready;", False),
    ("struct micros_tty_device_authority authority;", False),
    ("struct micros_tty_device_authority authority_sentinel;", False),
    ("struct micros_tty_handoff_runtime_state unexpected_state;", True),
    ("struct micros_tty_handoff_runtime_state *runtime;", False),
)
LIFECYCLE_PHASES = (
    (
        "tty_handoff_test_seed_runtime_records", "void",
        "struct tty_handoff_test_fixture *fixture, struct tty_handoff_runtime_case *work, "
        "const struct micros_process_handle process, const struct micros_thread_handle thread, "
        "const micros_endpoint_t endpoint, const uint64_t root",
        "fixture, &work, process, thread, endpoint, root",
    ),
    (
        "tty_handoff_test_check_initial_runtime_authority", "bool",
        "struct tty_handoff_test_fixture *fixture, struct tty_handoff_runtime_case *work", "fixture, &work",
    ),
    (
        "tty_handoff_test_begin_and_map_runtime", "bool",
        "struct tty_handoff_test_fixture *fixture, struct tty_handoff_runtime_case *work, "
        "const struct micros_process_handle process, const uint64_t root", "fixture, &work, process, root",
    ),
    (
        "tty_handoff_test_release_and_commit_runtime", "bool",
        "struct tty_handoff_test_fixture *fixture, struct tty_handoff_runtime_case *work", "fixture, &work",
    ),
    (
        "tty_handoff_test_claim_and_ready_runtime", "bool",
        "struct tty_handoff_runtime_case *work", "&work",
    ),
    (
        "tty_handoff_test_complete_and_panic_runtime", "bool",
        "struct tty_handoff_test_fixture *fixture, struct tty_handoff_runtime_case *work", "fixture, &work",
    ),
)
CONTROL_FIXTURE_TAG = "tty_control_syscall_test_fixture"
CONTROL_FIXTURE_GETTER = "tty_control_syscall_test_fixture"
CONTROL_FIXTURE_FIELDS = (
    "struct micros_bootstrap_control_state bootstrap;",
    "struct micros_endpoint_registry registry;",
    "struct micros_kernel_objects objects;",
    "struct micros_tty_handoff_runtime_state tty_state;",
    "struct micros_syscall_context syscall_context;",
    "enum micros_tty_control_authority_result authority_result;",
    "enum micros_plic_phase plic_phase;",
    "bool runtime_valid;",
    "bool mapping_valid;",
    "bool uart_active;",
    "bool irq_enabled;",
    "bool timer_enabled;",
    "bool external_enabled;",
    "bool plic_prepare_valid;",
    "bool order_violation;",
    "unsigned int authority_calls;",
    "unsigned int owner_fault_count;",
    "int operation_stage;",
    "jmp_buf panic_target;",
    "bool panic_armed;",
)
CONTROL_PRIVATE_EXPORTS = ("commit_arguments", "complete_arguments", "invoke", "reset_fixture")
CONTROL_CONTEXT_HELPERS = {"commit_arguments": True, "invoke": False, "reset_fixture": False}
CONTROL_CONTEXT_ROOTS = (
    *tuple(name for name in ORIGINAL_FUNCTIONS[5][0] if name != "main"),
    *ORIGINAL_FUNCTIONS[5][2], *ORIGINAL_FUNCTIONS[5][3],
)
CONTROL_JUMP_ROOTS = ("test_invariant_failure_is_fatal_before_mutation", "test_hardware_corruption_precedes_tuple_failure")
AUTHORITY_WORK_TAG = "tty_control_authority_case"
AUTHORITY_PHASE = "tty_control_test_check_authority_rejections"
AUTHORITY_FIELDS = (
    ("struct micros_tty_handoff_runtime_state state;", True),
    ("struct micros_bootstrap_binding binding;", True),
    ("struct micros_bootstrap_manifest_entry entry;", True),
    ("struct micros_syscall_context context;", True),
    ("struct micros_process caller_process;", True),
    ("struct micros_thread caller_thread;", True),
    ("struct micros_endpoint_record endpoint;", True),
    ("struct micros_privilege_profile profile;", True),
)
AUTHORITY_PARAMETERS = "struct tty_control_authority_case *work, const struct micros_thread_handle thread_handle"
UART_WORK_TAG = "uart_console_test_work"
UART_FIELDS = (
    ("volatile uint8_t *registers;", "value"),
    ("const struct micros_tty_handoff_runtime_state *state;", False),
    ("struct micros_tty_handoff candidate;", False),
    ("struct micros_plic_tty_enable_plan plic_plan;", False),
    ("bool timer_was_enabled;", False),
    ("bool irq_was_enabled;", False),
    ("uint32_t source;", "value"),
    ("uint32_t attempt;", False),
)
UART_PHASES = (
    "uart_console_test_prepare_handoff",
    "uart_console_test_quiesce_console",
    "uart_console_test_publish_irq_route",
    "uart_console_test_claim_and_retain",
    "uart_console_test_complete_and_seize",
)
UART_PARAMETERS = "uintptr_t hart_id, struct uart_console_test_work *work"
UART_PREFIX_DIGEST = "09e29d522c8d3da34376de686274b2e5a33bd0e3fbe15abed752eea86442c27f"
GENERATOR_PHASE = "_append_tty_manifest"
GENERATOR_PARAMETERS = (
    "output", "total_limit", "vfs_limit", "launcher_limit", "tty_limit", "pm_limit", "vm_limit", "vfs",
)

# Immutable literals below are generated only from the offline reviewed Git manifest.
BASELINE_BODIES = {0: {'binding_identity_matches': '5e5b9be177a57862b16e65927f297e4eaeee783aceed6efddec7707a6cd92185',
     'clear_bytes': '3aa00a25e0fc31568c312eed2afeebe3f2c1f7977cd3ccd12083d7f7a3cbf907',
     'copy_bytes': '786bedf2fb5ec2243bf61d1e50bd4f56de9f5dedb6bb739a0ce92434c201ff66',
     'endpoint_matches_process': 'bb86c03387ac4aba707c03d707e73c1f109cfb37167136796e7958286a897ab7',
     'identity_is_valid': 'f6eeae26719aff9200386609ee570ea7994bfad9e6fe0e43a1c6b73164c06538',
     'live_binding_matches': '62fd106dce3d97ab127b0733dfe0e2843575199a40bab3572d7721be4846aa32',
     'micros_tty_handoff_runtime_authoritative_state': '7e26ac0fd3ad93490d5cb4d46741232e297ec78a14c36c0ab1f9c03b151fc139',
     'micros_tty_handoff_runtime_begin': 'f16efc006c4295dc710dd66914b2073b23b6859c16c39b309fe91032f318b7f4',
     'micros_tty_handoff_runtime_bind_bootstrap': 'bfeeaa7ec21965ad684489af1075f71bcfbb6e3fff9ad2bb132f033e17031baf',
     'micros_tty_handoff_runtime_commit_begin_deadline_prevalidated': 'aa5090e00289f7287c6b34ccfda87ca062cf9d314e4a925c440aa32ce4b36957',
     'micros_tty_handoff_runtime_commit_begin_phase_prevalidated': 'a6cc87b05c67564784a1b0ce9a4725535003c63229b8fcc74fea970a59d00271',
     'micros_tty_handoff_runtime_commit_claim_prevalidated': 'e71f005a1c21b8e920874683170c450010681b38bf456d0d53f3a2d61efe76a6',
     'micros_tty_handoff_runtime_commit_complete_prevalidated': 'e71f005a1c21b8e920874683170c450010681b38bf456d0d53f3a2d61efe76a6',
     'micros_tty_handoff_runtime_commit_console_prevalidated': 'e71f005a1c21b8e920874683170c450010681b38bf456d0d53f3a2d61efe76a6',
     'micros_tty_handoff_runtime_commit_mapped_prevalidated': 'e71f005a1c21b8e920874683170c450010681b38bf456d0d53f3a2d61efe76a6',
     'micros_tty_handoff_runtime_commit_ready_prevalidated': 'e71f005a1c21b8e920874683170c450010681b38bf456d0d53f3a2d61efe76a6',
     'micros_tty_handoff_runtime_commit_release_prevalidated': 'e71f005a1c21b8e920874683170c450010681b38bf456d0d53f3a2d61efe76a6',
     'micros_tty_handoff_runtime_deadline_expired': '17d2f8b1a76cfcfa23e0a43b3846a5f6b132028d90298f68d7c9d5a9e6102d11',
     'micros_tty_handoff_runtime_device_authority': 'ebaf440b995d2fe1e8d44691251c032d297c41c6c2be5687be70ed7557f5f180',
     'micros_tty_handoff_runtime_panic': '610786f1afeaa5ea7a4200a1921fbe9ed0a94dcbc87ab49efa3e5a3252160a3a',
     'micros_tty_handoff_runtime_prepare': '0be88624b848b939d518dc0355edc1032e834f459c26e4fa9f120351535c8625',
     'micros_tty_handoff_runtime_prepare_begin': 'd71ccbdb8f23bcddbb383dfd16e5eb3680789f4c915a331234a735c73fe95584',
     'micros_tty_handoff_runtime_prepare_claim': 'e0688b598527162095ab0b4f778624ad141f87314c7be82120873e0f970b75ea',
     'micros_tty_handoff_runtime_prepare_commit': 'bb2dc2c87c48dcd08d79e590ba6261b4fda6f504f37da5a091b49d61cfb2a156',
     'micros_tty_handoff_runtime_prepare_complete': 'e09c7b1cfa81e04cea5d03c8cf0ad031ec6f1997225d1bf13623401a0a831401',
     'micros_tty_handoff_runtime_prepare_mapped': 'ce341eaf4053c3e119c457722368ca5426d81a22e81ffd24cab3892d63850663',
     'micros_tty_handoff_runtime_prepare_ready': 'be062894b9602be70ab7d9982432a7f349ab82c52ef2c7a7132577591a9ff625',
     'micros_tty_handoff_runtime_prepare_release': '71d75411719285dbcba758f85885fcca123fcd035ee9f168aae3eecda6dfd358',
     'micros_tty_handoff_runtime_reset': '110959be88ef76059a449bbec0b0326e54e03abbc36ce4268e3823d7b4bbfdd5',
     'micros_tty_handoff_runtime_role_ready': 'fbf1017f4c623929654dfa6cbb988ba9bdbaa1baa11ca08d0105d5fbf38f8c0a',
     'micros_tty_handoff_runtime_state': '7e26ac0fd3ad93490d5cb4d46741232e297ec78a14c36c0ab1f9c03b151fc139',
     'micros_tty_handoff_runtime_validate': '1f243c2faae5f72ceda7254619492f0fa42437d40d1791e7525a3fe32bc84c52',
     'process_handles_equal': '1eb9ca3d14ed288168beacc0916c15eb03aa6ecf2e283e022b4d3ccb7e92f678',
     'runtime_entry': '3b4cb5a9f0d07056caacfebeb1176fd0c2eabc6bccfd479d85fb766f34c4767a',
     'thread_handles_equal': '1eb9ca3d14ed288168beacc0916c15eb03aa6ecf2e283e022b4d3ccb7e92f678'},
 1: {'micros_uart_console_runtime_run_self_test': '70746090260a24d6915653467c4412b76db3220d400c7dd28323a7e7f79a3fd2',
     'uart_registers': '72b92efb334967dbce336687b2e41ed128324646b5a5a0ba9227bc5854eb8cd9'},
 2: {'clear_bytes': '3aa00a25e0fc31568c312eed2afeebe3f2c1f7977cd3ccd12083d7f7a3cbf907',
     'completion_is_clear': '895d942338716b2796a69a8ebe85d82896b67bcefc41bc493b363d64ab4656a6',
     'completion_is_valid': 'a01b35aa06597ad96cf70b9010e0e81af3b34e302346023e8a2b7e4ab1e7be04',
     'copy_grant': 'a91fd9f80a7e327f241bce8088cbc3663b1c5f8dfa7cd88a42d634d22554e49a',
     'endpoint_is_valid': '1d444bb5dd2d542d63f8d63fffc14af1c5800dad1d2daf805bcff0c5bd59c97f',
     'handle_cancel': 'd9dd998b6d96918b40dcc8a60dd6447e3b08b268c0c809fe985d54f6103b1d21',
     'handle_collect': 'dd45d14e2bd9a3830063f2fc48d31d9ca99a4752a18b0e1aa345c3e108781c89',
     'handle_submit_read': '1044a4da80f91ae76da2c65dd87e7972a5c12390df7f031ec5583450a70f6535',
     'handle_submit_write': '73b9a1f5a1bab356c3889c6ab7b53f90fe98f5636eb2641aaf292de3b54b20bf',
     'micros_tty_service_commit_ownership': 'bb49cc67481f1f3c7d1fc6caca23523c6c50ca4e1127434fa340b8b8991e4b99',
     'micros_tty_service_commit_reply': '0e78a10eeed267d2de0e2f083b637a7d386f6ae1b971388b9869deac115fe15c',
     'micros_tty_service_complete_read': 'ab34673ed5c2c4140194846d0ac75491df4e85debbaac205b960ae8bad48bc81',
     'micros_tty_service_handle_call': '510521a49f6a000aa6500556a35398b885efecbe7da1acc8f1a90defecefbcc5',
     'micros_tty_service_initialize': '5dd3e464006e040b87505e6660aff95029a7a3af742b8e5529e2c5f14d0abb93',
     'micros_tty_service_validate': '044ab026de76173603bdab12213a55e2e4266b042c0ce0684da7dbd5b3fd176a',
     'prepare_protocol_reply': 'a0294ee2ba1be871525da452e04e7ad899fac954a3eb696d9aad963e30ed6773',
     'prepare_reply': '451d3198898d6ff1756f8f8fffd1f519b84f3f276c78633a0fdbb797500dd384',
     'publish_read_completion': 'b30182d44e010b4400b19330f62546b54368bc3adf70d683ed0f0149eaa57a4e',
     'request_is_clear': 'd1c9068864bff9a03cc9fb457f36211e01cd922b3560ffe99e85f9130117ce78',
     'validate_submit': '24668ded6af84717b34760efc0cfab4469245c3350fe678593667217b13677ff'},
 3: {'identity_request': 'e5406390b6d0d6a6af769b1e3ed3ca0d61b172cf1ec89ffd2583800d99506ab5',
     'main': '9f2389c15fa63e0ee8df0e206f61e9daaadece1aaa29a72fd272b62e7d302f15',
     'model_bus_read': 'd94cb2377f84684e232417eb5265de3d0c964dfb64c54b2c52f87c3f1abdb780',
     'model_bus_write': '238a49ffcb3048961dfa18614f4de1cb7d4a5cb51174a04bed06c17426633020',
     'model_cancel': '9910d5622412e49747b37856a12bbb6f518efbc2c171a647b375343e030ab955',
     'model_check_reply': 'ccdbad5d6b6cb043e72cc4901f86ff6381801b7d83c8780f25b81fab6ed0b14b',
     'model_collect': 'e2f9cffe3b0d37809d10e4c7fafa8c8eff6ca7a682f297083c9561b084c18059',
     'model_collect_request_id': '67983083f009829339b66569ac96cb51644f497dc5ae009ccb67a27c7793c2cb',
     'model_commit_reply': '66cbad972b37011cebffe02dfeaa46b7c5bdfe7ac0a2ec0801f8bed857bb2933',
     'model_copy_grant': 'dc615f213f4697a3d7bf22620821ebb21d31308cf5c72ddb00f9c33d5f5c0bcf',
     'model_failure': 'c1743d52da10ea23ee85a42008114e6818ee59aac90fa80732df56c32531a728',
     'model_input_irq': 'ed4b0670f35bfdbc2e6c83624bd84b11ee65eaa3d16879e94c9836a00607de0a',
     'model_malformed_submit': '2e86c56bf04af58a2336d7862601780d46008904e1af4fd8c13765ad35310cbd',
     'model_random': '65f65ca5522df2fd5033e07dc62a29d43aafbe881f573bddbe3ae643f158ba67',
     'model_random_transition': 'ffa1ab64b1ec25ab6563eb747d1ed3c501d6f94c63207c447dc3277d0ece85f6',
     'model_record': '6a778ec5500a329b16176e22d405f5c50d49f3ebc5b2bded9e5ab50544c39cc5',
     'model_scripted_transition': '2a404ed93f24ad76c5ec39acd52a3ea514effe356d1f1db63e105289a1525dec',
     'model_submit_read': 'b7421e7025f3abd430474c13556e5c12cd85468e283e89d1ce19c8434f52a893',
     'model_submit_write': 'bc226b69f4a83a38ec45ee167143d428013455eb5824bb76de9bbd8087eab699',
     'model_transmit_irq': '773a64c83b5a05479cb59bb8596b4065275ffd3dda48a7658b5c6326d49ef7e1',
     'model_validate': '4defdfa68d1337f266cfff0c3f27c9f4ab7574504588dca3a483bd94f775cd87',
     'read_u32_le': '150bf59e50562ae2367a42a4b041af482d0e150f4a76e4b5ba0844ffd60ea172',
     'read_u64_le': 'b15de116c390eac1ee04dc37b3a0db64f9644cf50d5ea9ccacc0eb1030fa6002',
     'reference_consume_input': 'f8d0c3fa0ae062c9ca1b4d95004059c64138d6ff700d39eaf20194ef92d9e460',
     'reference_echo_index': '4370539efcb72770cf2c53e84d47cd9d23ac2c1f6cbbe2efce383a9ceca7097d',
     'reference_enqueue_echo': 'eab606032e138bac5e2f80225cf677e02de3fe706e3e088c349751f5519c986e',
     'reference_first_line_length': '5a3d06cc4da388746db9e7a0d4131b26590ee8c93282cf27185b3500fa7f769a',
     'reference_input_index': 'b19d9cde5092a36b68da9d332e0b4b2f581b630f396015531ff4d692e035a857',
     'reference_output_pending': 'b3d600c8aba026c4ef5681c30d87032a9a524e380c9f7e48e5a7f559579cf799',
     'reference_output_take': 'bdde69263f063b489a01d083092e977c48a68fd75d7a8e015a3e74a8136ac4df',
     'reference_publish_output': 'f9c732d34490bf47ff83e9856867613c41d294363776562671d63584b20ed616',
     'reference_receive_byte': '54646cc353cf20253cb3a05ccb94e89f2506fc655f8160de2f7e367c9c7b77cd',
     'reference_write_accept': '8ce79ab9e429316e39e82ef9a958598eaf9cc4c428a5b329542f2f12c5174f6b',
     'reference_write_arm_retry': '3091d9d287a1392c004f7c8a32d5673f8636802932d1e4218047b888c817c9ce',
     'submit_request': '9a7ce61796254617369ffccf1c2eb973e3fb3fe13779f0f506ceeac12b02941c',
     'test_replayable_mixed_transition_model': '4e10a38593e40325fa978249ae577fbb4968ed2bfe534f13f586a7e504ec9003',
     'write_u32_le': 'e8f941db33e782362b97a427da21a66baa14b3f007243e42f90ad8b386a9d811',
     'write_u64_le': 'a36a39d22a553426217f4e75a10221d1706f69a7c578a245e55fd5b07a78125e'},
 4: {'advance_to_phase': 'fde64d842dfb9a216500f0d809c8b7edc803f72a717d4803d03c0d1d4d5310aa',
     'micros_bootstrap_control_find_binding': '55c62c29a7c5fb302ebc7973059dea304d98b3255453c0f7b0e158bd50b09c53',
     'micros_endpoint_resolve_internal': '82ca617818a1a7c02e8f04af4f0902d4649b7d35bf1f8d66952695098824fa32',
     'micros_privilege_profile_allows_kernel_operation': 'fe6a0dec72721f82b9a383c392463db9bc789855daadcfe2d6b941965dacf29e',
     'micros_process_resolve': 'f78e91574bb94c798c45a773b369f4f88b517bb435f7a8e0ec5020926fc1ebe5',
     'micros_thread_resolve': '0401a0545368a4585101b0fad01d23e6856e1e25acda7ec6ba9b0e7f0bbecbb8',
     'micros_tty_handoff_test_run': '1652b1c834a765bed9418f244c574fb4bb0467d04ca2e9bc502347059c68f6f8',
     'test_complete_handoff': 'eebd2f31573d1981731f8e2b9310d1ad719b94026e51b2e41474bbf7ce61a954',
     'test_deadline_covers_every_pre_ready_phase': 'fab93b850f67676645a31998a69dcaa5fcdcd934356eeabae83e07cf8ddf6a14',
     'test_device_leaf_classification': 'cb23c61966e96b02f3f4aa4d43f5f6a2b067dc6b16a0cbfd479cefff5f6ee58e',
     'test_failure_preservation': '4ca44953f2812ea943ae3bd82a2bb40fa298509d49894638f79b9aaa1c6a631d',
     'test_invariant_rejection': '8f73185d7fa25401b3a57d0fd61cb85b5b83fee8675cafd772919ec046ca0543',
     'test_ready_preserves_retained_route': 'fc25508397e337bcbc9d396f1a73e8f0ea2cff2dfb64fe72deb9f7aa4f887991',
     'test_runtime_lifecycle_validation': '9dec158abdd16be4c02625c95d7f8bb2da50ea67dba86e55d0e9b843623f5bb0',
     'test_terminal_panic': '7cca3cef4805d9220f5463e442112bd792b6f87ca50cbb71d67b529cc5fb6740',
     'test_uart_console_ownership': 'd129df3de0e796137bb6ece5500168a9b8fd833d19bda8de4273d96760cc2c25'},
 5: {'commit_arguments': '204450671666b39b736674c5daef6e89c8a424cab86d42a9e706647486f18018',
     'complete_arguments': 'c89dcda31e729e23d283379a311ddda9facda490eb3f57418185ec920cd197a1',
     'invoke': '0076e7032531061ec0797901c86b221f5d3b55cab23b53f90756d7e300e26aaf',
     'main': 'd9f9fc4e7423b45f229568fa2395d65ef90e71e572d34070b3cb5510a65acc05',
     'micros_bootstrap_runtime_authoritative_state': 'e60ae46a13969131ce9622aeea6915fa388b7a6591e6241403686604b4c1ebad',
     'micros_bootstrap_runtime_fail': '29dfc79808aaaf27c6f9ee4ac92c8118fc675b8727e309e7d3ba245592862dba',
     'micros_bootstrap_runtime_state': 'e60ae46a13969131ce9622aeea6915fa388b7a6591e6241403686604b4c1ebad',
     'micros_bootstrap_runtime_validate': '2288879b1fd71bd9b0732766a9d763787a71247665467d920d17722cdddb9e26',
     'micros_ipc_runtime_authoritative_registry': '7fcafddcf1d85f7844afa47791403e5988bc56bd6d373e25f7404ad4b25cb01a',
     'micros_ipc_runtime_registry': '7fcafddcf1d85f7844afa47791403e5988bc56bd6d373e25f7404ad4b25cb01a',
     'micros_kernel_object_runtime_registry': 'ff3b745f10519eaa7fb7c0a82a3f182637925979ede772e64315772746ff07e5',
     'micros_plic_commit_tty_enable_prevalidated': '69105fe7cc7a940209e70c16df6f2400ce8461ef5468297040ccd9577a2a7dee',
     'micros_plic_commit_tty_prepare_prevalidated': '9a3585d381c38e4d6e2e203ea3762b558014ff1da94de4789e7a2a264a8198df',
     'micros_plic_complete': '90b67de2d3f141740efff8ec678be39472664189eeafcb0239fc7ba16b90f873',
     'micros_plic_prepare_tty_enable': 'b404076d75fb3a3362fc0a9e1a6caf71281295d30eebaafe63377b30eefa3760',
     'micros_plic_validate': 'f000810dcecd752f08f76234755ed7034ccd9ab89766af23142a92d555075457',
     'micros_test_external_interrupt_enable': 'fdad8bcdf2d29825298d90114c3e765859f85d351210a0121d72ff71bfdeb07e',
     'micros_test_external_interrupt_is_enabled': '2ded4196219269524ac275051bf666ee5e566701c7bf3493a0a9b8802c4f4f8e',
     'micros_test_irq_is_enabled': 'd3084146ed538689836410557da2567ee015008e3b11d82285a5c807b1f94733',
     'micros_test_timer_interrupt_is_enabled': '4d270fd9b22beaff40ebbcf5c5f4390d1409ac3bbd813f5e88a1a7585230b365',
     'micros_trap_panic_entry': 'd77addd1d3e5282da94d49edf1ffdebd089d4709ec7700e9825e047c17a02e53',
     'micros_tty_control_syscall_authority_resolve': '04c1bd205f50821ebaca0dc74f61d874b0182ae0221c3752569d3f19e0c288dd',
     'micros_tty_handoff_runtime_authoritative_state': '55f56453b5825c731fff13994afe23fc6570b04a60f2db622a7123aff1ce28ce',
     'micros_tty_handoff_runtime_commit_complete_prevalidated': '50b9bedbe5f2aa51dff604f77073b06fbbbe6bb6d81322ea52ed013587efa503',
     'micros_tty_handoff_runtime_commit_console_prevalidated': '50b9bedbe5f2aa51dff604f77073b06fbbbe6bb6d81322ea52ed013587efa503',
     'micros_tty_handoff_runtime_prepare_commit': '1b8cfa814cc85316a6724809782d3a118193714387e5a074dd32488d693ce0c6',
     'micros_tty_handoff_runtime_prepare_complete': '3fcada6aacbf74344f576db2baacd3bbbf6e3c653c66045ba7b441e7048ea677',
     'micros_tty_handoff_runtime_state': '55f56453b5825c731fff13994afe23fc6570b04a60f2db622a7123aff1ce28ce',
     'micros_tty_handoff_runtime_validate': '0dd3aef7a7a61839c9021f8d0a60cfd1d34709a2f399deb793721ba5c175864b',
     'micros_tty_owner_fault_record': '2ffc765bed325f7016795f8dc5281c7c6f86c580c78cc186b0c94f2faf9c26f0',
     'micros_user_address_space_validate_tty_uart_mapping': 'f34ac1d4265e9a1fdb1237dec449a26483d71f9c6c18abbb2f53ebcbabe2970f',
     'reset_fixture': '8ee268dba261dfe07e48b05acbaa4565a954aa0b8151d98f4f6f95ba8824119c',
     'test_commit_orders_state_before_delivery': '462ad53b0d805d0638e4e597c676acb925d92f029ddb8b9c8cd5204c320da425',
     'test_completion_orders_hardware_before_idle': '2e0cddec612f8e226181dde98cce134926db7eb25322734667c132d04be24e83',
     'test_hardware_corruption_precedes_tuple_failure': '057cd21a36a95b970faffe9392a1e03e8efdc8ced298abd8d69ac6ccd6073567',
     'test_invariant_failure_is_fatal_before_mutation': '00dd45e73c551eaa87a8fe195bed7343aff0d566386da5f1a5fcf77b93c07de5',
     'test_returned_failures_preserve_state': '2fd660c5700ba5b6cffce353037ccaaad8c5803c74a837ac071963aea8a47af7',
     'uart_console_handoff_is_active': '16a77c35d125986e208357faf070f93677275b318bf287e76201b468bfcf603d'},
 6: {'decode_failure_preserves_request': 'e9b8ab931f90f7ff3b4616eff9ffe376b6a4b3ac54deaab1e96f53215c5766a0',
     'micros_tty_control_test_run': 'd359f5897a177974f0ee5598357c6a1162d02793ecb6195d1791e0b5a848fc0e',
     'test_commit_width_rejection': '73014b9fcd543225b1a0287bf7c5be375e4b18666c7f683bb9674af4547f65bd',
     'test_complete_shape_rejection': '01ae1dd6615987a2ccb7e9f51cfc7457c5859000407da11f9fa0e04d9b911678',
     'test_contract_and_decode': 'f8d5405c745226c17579590a7da79ef6920ba0407329080ffe645eff10a7392e',
     'test_no_console_authority': 'f2bc21974b6bfea76e5a5b766317ab60465b2a8b68fa50dcce2e700b8e52a572',
     'test_syscall_authority': '051c92560d179d9b87e2eba50d3e2e1048c352a2c11392ed1445d89795e68c9b'}}
BASELINE_SIGNATURES = ('4f7ffc523beade8cce17510119bf9c4c993728d73d6613c7b4fa87dc0c9f2e58',
 'bdbc82acb6d04d7608db2e9166f9af8ad1a5c513460a1d27010c73987b2d05f9',
 '1da05156ce036ef52df74eae2b1085b844b01daf13b2b2c4dc9f4e7c417f688c',
 'eb071e28d48cfd60e5fb085ebd8a42f8e3e056646621200a4c2eb32683bae8cf',
 '32813028c50fab640ded32e002160fdec41f961616302afe1dbe75348d54fc73',
 '81dc86a55d5a0b86b8d2375db13c1c9dead6621bc5c627b4abb0354d0084e3fd',
 'aa01a4482a751a2add11c38b25ce3e1b009cd4a22a347e285558f795d23ffa21')
ORIGINAL_ORDER = (('clear_bytes',
  'copy_bytes',
  'process_handles_equal',
  'thread_handles_equal',
  'endpoint_matches_process',
  'identity_is_valid',
  'runtime_entry',
  'binding_identity_matches',
  'live_binding_matches',
  'micros_tty_handoff_runtime_prepare',
  'micros_tty_handoff_runtime_bind_bootstrap',
  'micros_tty_handoff_runtime_reset',
  'micros_tty_handoff_runtime_state',
  'micros_tty_handoff_runtime_authoritative_state',
  'micros_tty_handoff_runtime_begin',
  'micros_tty_handoff_runtime_prepare_begin',
  'micros_tty_handoff_runtime_commit_begin_deadline_prevalidated',
  'micros_tty_handoff_runtime_commit_begin_phase_prevalidated',
  'micros_tty_handoff_runtime_prepare_mapped',
  'micros_tty_handoff_runtime_commit_mapped_prevalidated',
  'micros_tty_handoff_runtime_prepare_release',
  'micros_tty_handoff_runtime_commit_release_prevalidated',
  'micros_tty_handoff_runtime_prepare_commit',
  'micros_tty_handoff_runtime_commit_console_prevalidated',
  'micros_tty_handoff_runtime_prepare_claim',
  'micros_tty_handoff_runtime_commit_claim_prevalidated',
  'micros_tty_handoff_runtime_prepare_complete',
  'micros_tty_handoff_runtime_commit_complete_prevalidated',
  'micros_tty_handoff_runtime_prepare_ready',
  'micros_tty_handoff_runtime_commit_ready_prevalidated',
  'micros_tty_handoff_runtime_role_ready',
  'micros_tty_handoff_runtime_deadline_expired',
  'micros_tty_handoff_runtime_panic',
  'micros_tty_handoff_runtime_device_authority',
  'micros_tty_handoff_runtime_validate'),
 ('uart_registers', 'micros_uart_console_runtime_run_self_test'),
 ('clear_bytes',
  'endpoint_is_valid',
  'request_is_clear',
  'completion_is_clear',
  'completion_is_valid',
  'prepare_reply',
  'prepare_protocol_reply',
  'copy_grant',
  'validate_submit',
  'publish_read_completion',
  'micros_tty_service_initialize',
  'micros_tty_service_validate',
  'micros_tty_service_commit_ownership',
  'micros_tty_service_complete_read',
  'handle_submit_read',
  'handle_submit_write',
  'handle_cancel',
  'handle_collect',
  'micros_tty_service_handle_call',
  'micros_tty_service_commit_reply'),
 ('model_failure',
  'write_u32_le',
  'write_u64_le',
  'read_u32_le',
  'read_u64_le',
  'model_random',
  'model_record',
  'reference_input_index',
  'reference_echo_index',
  'reference_output_pending',
  'reference_publish_output',
  'reference_enqueue_echo',
  'reference_receive_byte',
  'reference_first_line_length',
  'reference_consume_input',
  'reference_write_accept',
  'reference_write_arm_retry',
  'reference_output_take',
  'model_copy_grant',
  'model_bus_read',
  'model_bus_write',
  'submit_request',
  'identity_request',
  'model_check_reply',
  'model_commit_reply',
  'model_submit_read',
  'model_submit_write',
  'model_cancel',
  'model_collect',
  'model_input_irq',
  'model_transmit_irq',
  'model_malformed_submit',
  'model_validate',
  'model_collect_request_id',
  'model_random_transition',
  'model_scripted_transition',
  'test_replayable_mixed_transition_model',
  'main'),
 ('micros_bootstrap_control_find_binding',
  'micros_process_resolve',
  'micros_thread_resolve',
  'micros_endpoint_resolve_internal',
  'micros_privilege_profile_allows_kernel_operation',
  'test_complete_handoff',
  'test_deadline_covers_every_pre_ready_phase',
  'test_ready_preserves_retained_route',
  'test_uart_console_ownership',
  'test_failure_preservation',
  'test_device_leaf_classification',
  'test_runtime_lifecycle_validation',
  'advance_to_phase',
  'test_terminal_panic',
  'test_invariant_rejection',
  'micros_tty_handoff_test_run'),
 ('commit_arguments',
  'complete_arguments',
  'reset_fixture',
  'invoke',
  'test_commit_orders_state_before_delivery',
  'test_completion_orders_hardware_before_idle',
  'test_returned_failures_preserve_state',
  'test_invariant_failure_is_fatal_before_mutation',
  'test_hardware_corruption_precedes_tuple_failure',
  'main',
  'micros_test_irq_is_enabled',
  'micros_test_timer_interrupt_is_enabled',
  'micros_test_external_interrupt_enable',
  'micros_test_external_interrupt_is_enabled',
  'micros_bootstrap_runtime_state',
  'micros_bootstrap_runtime_authoritative_state',
  'micros_bootstrap_runtime_validate',
  'micros_bootstrap_runtime_fail',
  'micros_ipc_runtime_registry',
  'micros_ipc_runtime_authoritative_registry',
  'micros_kernel_object_runtime_registry',
  'micros_tty_handoff_runtime_state',
  'micros_tty_handoff_runtime_authoritative_state',
  'micros_tty_control_syscall_authority_resolve',
  'micros_tty_handoff_runtime_validate',
  'micros_tty_handoff_runtime_prepare_commit',
  'micros_tty_handoff_runtime_commit_console_prevalidated',
  'micros_tty_handoff_runtime_prepare_complete',
  'micros_tty_handoff_runtime_commit_complete_prevalidated',
  'micros_plic_validate',
  'micros_plic_prepare_tty_enable',
  'micros_plic_commit_tty_prepare_prevalidated',
  'micros_plic_commit_tty_enable_prevalidated',
  'micros_plic_complete',
  'uart_console_handoff_is_active',
  'micros_user_address_space_validate_tty_uart_mapping',
  'micros_tty_owner_fault_record',
  'micros_trap_panic_entry'),
 ('decode_failure_preserves_request',
  'test_contract_and_decode',
  'test_commit_width_rejection',
  'test_complete_shape_rejection',
  'test_syscall_authority',
  'test_no_console_authority',
  'micros_tty_control_test_run'))
GENERATOR_AST_DIGEST = 'c025a4acc1efea1b1fe880bd1adbf014d305625e72878cb9c729be312475c82c'
GENERATOR_FUNCTIONS = {'main': '606d318b94210bb2ae2af669de500c943967dd66bc7977799be5c5ae3588f15e',
 'parse_arguments': '5a288ccaa1329f2bde56e18685752d22edcfdad7d5dc9db6b0fa4ae138079487',
 'render_fixture': 'c4b95d4fdc6bd3c6cf719b4f39f95c068fdf12488778b233a8fe055e34362d32'}
STABLE_HASHES = {'CMakePresets.json': 'eae6c86ce5ac8f0e1b39605ebbd6816135558f0b4f8ea293b3680d7ecb79df86',
 'include/micros/tty.h': 'b8b54687a7dffc989bb6a6342c2efc75a1e4b50a70a1db1ee391b8c9baf54c9a',
 'kernel/plic.c': '75b9938aa949b8d4c2c068ca3cbb2481f1a8a9f50a8740c63b2fd1a0a7278d90',
 'kernel/plic.h': 'e674aee3374d1fb865870b4d21ca733146243b99de44401ecf3873cc4bce2535',
 'kernel/plic_core.c': '075a6972ec5b20aad67bde0630571ce68f9e46ab1b9474ce72a869f7a16b5001',
 'kernel/tty_control_core.c': 'd5a2e99d3a0822ec1fd0918db9d20b9a1e0e813042d04ece6c17247254116b60',
 'kernel/tty_control_core.h': '6fefc08f8c268c4479b51bee2fd9c741e811f409b9c43474382c65b7bc2eebef',
 'kernel/tty_control_syscall.c': '612f1196a6647cf18ab4551055391ca21f8d89647353b881c32200cffbfe7139',
 'kernel/tty_control_syscall.h': '68b0bbe54249dec042167e589f85277f6d946cf4f0ad22c078b28db2e58fbc03',
 'kernel/tty_control_syscall_core.c': 'f81b3254b0a6814d0f2ac2798e8246f8609927e7d7e1b8aafe5844b3b28a30ee',
 'kernel/tty_control_syscall_core.h': 'c073fce72268b62c3591512c5d7e503dd22082c539f803a434323b6796acbf74',
 'kernel/tty_fault.c': '02817c77daf1779fa3dc4a1a2006e9078dc188743ab413d870db73b52e128ade',
 'kernel/tty_fault.h': '627a8d02eab893cfbf3ee984ab57b03e666538b80789cf63de9df0989ada3724',
 'kernel/tty_handoff_core.c': '70fcb3b72880f3cf5371da9fc07337136f711cdce0eeed3d44f9883b8e517993',
 'kernel/tty_handoff_core.h': 'ceb795ed151cc396ad1047d32da832fe0021a40cdccd636bd600eeac43321046',
 'kernel/tty_handoff_runtime.h': '88f68530764e3a845dfed1dc5020f1fea04834e7269e6832052b2cbb777fe7d7',
 'kernel/tty_interrupt.c': 'cc39fdd57ca9dbf41f6c76cb3af8b3ba45cd75edc819ddf64c514b40e2c829e0',
 'kernel/tty_interrupt.h': '1db8e0ab196498bce20d8a0063f9f8d2597a06e97d92e74ab30e4ab21ccadb62',
 'kernel/tty_service_test.c': 'bc2da146dbef6b2c9c885c3fe483e2c5b3bdc28a79744d4ec0b319661f1211b7',
 'kernel/tty_service_test.h': '7a9291125b8de8724aeeb5bdb1b235b1dc65f9ab19f54123d647751915405b7d',
 'kernel/tty_service_test_fixture.h': '9e35bd120fb344aa6b667f51bfa4dbfc1306d7da2a1a97199830921f2990af36',
 'kernel/uart_console_core.c': '39fecd3d9a6ba6ff792e53a8f030e9ffb418879b09f15957645e473df6aa8af2',
 'kernel/uart_console_core.h': '8c0d94bb5be9400d38bbfab079903287623430eb41b0a0b49c8909360a11e978',
 'kernel/uart_console_test.h': '20a93b46bd20b20d71fb42df42d490999fbaf8bd7a3fa5bf2d54f2ec9f0b7fde',
 'servers/tty/tty_control.c': '3594f9dac0358f81b776f2131caac08a11ccaa7d2f25bdbd3d693507d8696b08',
 'servers/tty/tty_control.h': 'ba13a95a250927c0acf5d89cfe84f2b010bb66e1c02f00eac744f74ac2e72b09',
 'servers/tty/tty_core.c': '09d28fa345eafe047a93d60c0539d8c288f23b974a9324bb9501a7c281747251',
 'servers/tty/tty_core.h': 'ad203545320c8fadd5894ce909028fcba1d50dc85134befacacc017e77d7e3fc',
 'servers/tty/tty_service.c': '16c7015dd7bab717ba2510f44468da8ac95b2e36ee9882c9e3fe73893969a53f',
 'servers/tty/tty_service_core.h': '9e9073ef604bbc1c6258851b62ba3a890a20bc6d3d1eb97b6fbf0a3dbf1505c9',
 'servers/tty/tty_uart.c': 'a27be72b981a181d80686ff8d170fd296d68cca7a2e29f64f3f50c6c289d4c3e',
 'servers/tty/tty_uart.h': 'fde19167c9664fca8ab7042aba3e0b65fac17a99db15b2150b65d3aa5c2ab46f',
 'tests/qemu/tty_service_claim_wait.S': '0be9a985c2d57b7d42565e3f5fb29b81be8d4c7ffb56746f7be491157a00db94',
 'tests/qemu/tty_service_vfs.c': 'bcb2cda8518f4681afb8592352a7e6bf728a53ff26742cc54d72135e1ce9f0ae',
 'tools/check_user_elf.py': 'cac14e29a6261ac67313df4727c8cfc3045dcac8fa9e108afaa6540385c0a953',
 'tools/generate_bootstrap_fixture.py': '55eba716c11aa7b2d3d72fd055dcaea5bd9130728a55e1a6489dc0b7dd3a7533',
 'tools/run_qemu_smoke.py': '9e54f744074d077d05b52880495b73f7c74ae1db0a6ad751f8b7024688bf79e3'}
SHARED_INSPECTOR_DIGEST = 'efab84d99e3d5c49fc416a161816431b9e54615ac23af9aef3e9f0e995bc2e0d'
PLANNER_AST_DIGEST = 'ae802c764904262d635ff9c43e203ecd3384244ee7a1eac46fc94afadfcb133a'
CONTEXT_AST_DIGEST = 'da82678bf0371477e724a109153bd4008d2c8b755c9a531a9e04dc40ae2684a5'


def source(path):
    return kernel_layout_source.read_path(ROOT / path)


def digest_bytes(value):
    return hashlib.sha256(value).hexdigest()


def digest_tokens(value):
    return digest_bytes(json.dumps(tuple(value), separators=(",", ":")).encode())


def normal_names(family):
    if family == 0:
        return {
            "tty_handoff_" + name: name for names in ORIGINAL_FUNCTIONS[0].values()
            for name in names if not name.startswith("micros_")
        }
    if family == 2:
        return {
            ("tty_service_request_clear_bytes" if name == "clear_bytes" else "tty_service_" + name): name
            for names in ORIGINAL_FUNCTIONS[2].values()
            for name in names if not name.startswith("micros_")
        }
    if family == 3:
        return {
            **{
                "tty_model_" + (name[6:] if name.startswith("model_") else name): name
                for names in ORIGINAL_FUNCTIONS[3].values() for name in names if name != "main"
            },
            **MODEL_READONLY_NAMES,
        }
    if family == 4:
        return {
            "tty_handoff_test_" + (name[5:] if name.startswith("test_") else name): name
            for names in ORIGINAL_FUNCTIONS[4].values() for name in names if not name.startswith("micros_")
        }
    if family == 5:
        return {
            "tty_control_syscall_test_" + (name[5:] if name.startswith("test_") else name): name
            for names in ORIGINAL_FUNCTIONS[5].values() for name in names
            if not name.startswith("micros_") and name not in ("main", "uart_console_handoff_is_active")
        }
    return {}


def original_locations(family):
    names = normal_names(family)
    locations = {}
    for path in FAMILY_PATHS[family]:
        for spelling, record in function_records(source(path)).items():
            if family == 1 and spelling in UART_PHASES:
                if path != FAMILY_PATHS[1][0]:
                    raise AssertionError("real UART hardware phases retain their original component owner")
                continue
            if family == 6 and spelling == AUTHORITY_PHASE:
                if path != FAMILY_PATHS[6][0]:
                    raise AssertionError("native authority rejections retain their original case owner")
                continue
            if family == 5 and spelling == CONTROL_FIXTURE_GETTER:
                if path != CONTROL_SOURCES[1]:
                    raise AssertionError("captured-control fixture getter has its sole original-family owner")
                continue
            if family == 4 and spelling in {phase[0] for phase in LIFECYCLE_PHASES}:
                if path != HANDOFF_SOURCES[3]:
                    raise AssertionError("runtime lifecycle phases retain their actual native case owner")
                continue
            if family == 4 and spelling == DEVICE_PHASE:
                if path != HANDOFF_SOURCES[2]:
                    raise AssertionError("device exclusions have their exact private classification-case owner")
                continue
            if family == 4 and spelling == HANDOFF_FIXTURE_GETTER:
                if path != HANDOFF_SOURCES[1]:
                    raise AssertionError("native handoff fixture getter has its sole original-family owner")
                continue
            if family == 3 and spelling == MODEL_VALIDATE_PHASE:
                if path != MODEL_SOURCES[6]:
                    raise AssertionError("resident comparisons have their exact private model validation owner")
                continue
            if family == 2 and spelling == WRITE_PHASE:
                if path != SERVICE_SOURCES[3]:
                    raise AssertionError("accepted-write publication has its exact private write owner")
                continue
            if family == 0 and spelling == LIVE_PHASE:
                if path != RUNTIME_SOURCES[2]:
                    raise AssertionError("live-binding predicate phase has its exact private validation owner")
                continue
            if family == 0 and spelling == RUNTIME_CONTEXT_GETTER:
                if path != RUNTIME_SOURCES[1]:
                    raise AssertionError("runtime context getter has its sole state leaf owner")
                continue
            original = names.get(spelling, spelling)
            if original not in BASELINE_BODIES[family] or original in locations:
                raise AssertionError("unknown/duplicate original or unreviewed TTY phase: " + spelling)
            locations[original] = (path, record)
    if set(locations) != set(BASELINE_BODIES[family]):
        raise AssertionError("every complete original TTY definition remains present")
    return locations


def original_signature_tokens(family, original, record):
    signature = tuple(normal_names(family).get(token, token) for token in tokens(record.signature))
    if family == 0 and original in RUNTIME_PRIVATE_EXPORTS and record.name.startswith("tty_handoff_"):
        if signature[:1] == ("static",):
            raise AssertionError("the necessary byte primitives have actual private cross-owner declarations")
        signature = ("static", *signature)
    if family == 0 and original in RUNTIME_CONTEXT_HELPERS:
        if (0, original) not in SYMBOL_OWNER_MIGRATION:
            signature = runtime_context_signature_tokens(signature)
    if family == 2 and original in SERVICE_PRIVATE_EXPORTS and record.name.startswith("tty_service_"):
        if signature[:1] == ("static",):
            raise AssertionError("only necessary cross-owner service helpers gain actual private visibility")
        signature = ("static", *signature)
    if family == 3 and original in MODEL_PRIVATE_EXPORTS and record.name.startswith("tty_model_"):
        if signature[:1] == ("static",):
            raise AssertionError("only required model cross-owner helpers gain complete private declarations")
        signature = ("static", *signature)
    if family == 4 and original in HANDOFF_PRIVATE_EXPORTS and record.name.startswith("tty_handoff_test_"):
        if signature[:1] == ("static",):
            raise AssertionError("only the two cross-owner native handoff cases gain private declarations")
        signature = ("static", *signature)
    if family == 5 and record.name.startswith("tty_control_syscall_test_"):
        from tests.host.test_grant_data_plane_source_layout import fixture_signature_tokens

        signature = fixture_signature_tokens(
            original, signature, CONTROL_PRIVATE_EXPORTS, CONTROL_FIXTURE_TAG, CONTROL_CONTEXT_HELPERS,
        )
    return signature


def original_body_digest(family, record):
    names = normal_names(family)
    body = tokens("{" + record.body + "}")
    if family == 0 and record.name == "tty_handoff_live_binding_matches" and (0, "live_binding_matches") not in FUNCTION_TASK_MIGRATION:
        body = live_binding_actual_body_tokens(record)
    if family == 2 and record.name == "tty_service_handle_submit_write" and (2, "handle_submit_write") not in FUNCTION_TASK_MIGRATION:
        body = write_actual_body_tokens(record)
    if family == 3 and record.name == "tty_model_validate" and (3, "model_validate") not in FUNCTION_TASK_MIGRATION:
        body = model_validation_actual_body_tokens(record)
    if family == 4 and record.name == "tty_handoff_test_device_leaf_classification" and (4, "test_device_leaf_classification") not in FUNCTION_TASK_MIGRATION:
        body = device_actual_body_tokens(record)
    if family == 4 and record.name == "tty_handoff_test_runtime_lifecycle_validation" and (4, "test_runtime_lifecycle_validation") not in FUNCTION_TASK_MIGRATION:
        body = lifecycle_actual_body_tokens(record)
    if family == 6 and record.name == "test_syscall_authority" and (6, "test_syscall_authority") not in FUNCTION_TASK_MIGRATION:
        body = authority_actual_body_tokens(record)
    if family == 1 and record.name == "micros_uart_console_runtime_run_self_test" and (1, record.name) not in FUNCTION_TASK_MIGRATION:
        body = uart_actual_body_tokens(record)
    body = tuple(names.get(token, token) for token in body)
    if family == 0 and (0, "live_binding_matches") not in SYMBOL_OWNER_MIGRATION:
        body = runtime_context_body_tokens(names.get(record.name, record.name), body)
    if family == 4 and (4, "test_runtime_lifecycle_validation") not in SYMBOL_OWNER_MIGRATION:
        body = handoff_fixture_body_tokens(names.get(record.name, record.name), body)
    if family == 5 and not SYMBOL_OWNER_MIGRATION:
        body = control_fixture_body_tokens(names.get(record.name, record.name), body)
    return digest_tokens(body)


def runtime_context_signature_tokens(signature):
    signature = list(signature)
    start = signature.index("(") + 1
    parameter = list(tokens("struct tty_handoff_context *context,"))
    if signature[start:start + len(parameter)] != parameter:
        raise AssertionError("stateful runtime helpers receive the exact original typed context first")
    del signature[start:start + len(parameter)]
    return tuple(signature)


def runtime_context_body_tokens(original, body):
    body = list(body)
    if original.startswith("micros_"):
        borrow = list(tokens("struct tty_handoff_context *context = tty_handoff_context();"))
        if body[:1 + len(borrow)] != ["{"] + borrow:
            raise AssertionError("each stateful runtime API borrows its original family once without reset")
        del body[1:1 + len(borrow)]
    result = []
    index = 0
    while index < len(body):
        token = body[index]
        if token in RUNTIME_CONTEXT_HELPERS and body[index + 1:index + 2] == ["("]:
            if body[index + 2:index + 4] != ["context", ","]:
                raise AssertionError("runtime calls bind their sole original context explicitly")
            result.extend((token, "("))
            index += 4
        elif token == "context":
            if (
                body[index + 1:index + 2] != ["->"]
                or body[index + 2:index + 3] not in (["tty_handoff_state"], ["tty_bootstrap_state"])
                or (index and body[index - 1] in (".", "->"))
            ):
                raise AssertionError("runtime context is not copied, reset, nested-qualified or retained")
            result.append(body[index + 2])
            index += 3
        else:
            result.append(token)
            index += 1
    return tuple(result)


def live_binding_body_tokens(caller, phase, declarations):
    from tests.host.test_grant_data_plane_source_layout import restore_automatic_work_tokens, bound_body_tokens

    phase_tokens = tokens(phase)
    if any(token in ("return", "goto") for token in phase_tokens):
        raise AssertionError("the complete void predicate phase cannot bypass resolution or caller phase/deadline selection")
    body = bound_body_tokens(
        tokens(caller), LIVE_PHASE + "(context, binding, transition, &work);", phase,
    )
    return restore_automatic_work_tokens(body, LIVE_WORK_TAG, LIVE_FIELDS, declarations)


def live_binding_actual_body_tokens(record, owner=None):
    from tests.host.test_grant_data_plane_source_layout import work_declarations

    owner = source(RUNTIME_SOURCES[2]) if owner is None else owner
    declarations = work_declarations(owner, LIVE_WORK_TAG, LIVE_FIELDS)
    phase = function_records(owner)[LIVE_PHASE]
    if phase.guards or tokens(phase.signature) != tokens("static void " + LIVE_PHASE + "(" + LIVE_PARAMETERS + ")"):
        raise AssertionError("the live predicate phase preserves every exact typed context/binding/transition/work input")
    return live_binding_body_tokens("{" + record.body + "}", "{" + phase.body + "}", declarations)


def write_body_tokens(caller, phase, declarations):
    from tests.host.test_grant_data_plane_source_layout import restore_automatic_work_tokens, bound_body_tokens

    if set(tokens(phase)) & {"return", "goto", "break", "continue"}:
        raise AssertionError("write publication cannot bypass the original caller return or rejection boundaries")
    body = bound_body_tokens(tokens(caller), WRITE_PHASE + "(state, request, action, &work);", phase)
    return restore_automatic_work_tokens(body, WRITE_WORK_TAG, WRITE_FIELDS, declarations)


def write_actual_body_tokens(record, owner=None):
    from tests.host.test_grant_data_plane_source_layout import work_declarations

    owner = source(SERVICE_SOURCES[3]) if owner is None else owner
    declarations = work_declarations(owner, WRITE_WORK_TAG, WRITE_FIELDS)
    phase = function_records(owner)[WRITE_PHASE]
    if phase.guards or tokens(phase.signature) != tokens("static void " + WRITE_PHASE + "(" + WRITE_PARAMETERS + ")"):
        raise AssertionError("publication binds the exact original state/request/reply and caller automatic work")
    return write_body_tokens("{" + record.body + "}", "{" + phase.body + "}", declarations)


def model_prefix_tokens(prefix, constants):
    body = list(tokens(prefix))
    for name in MODEL_READONLY_NAMES:
        declaration = [value for value in constants if name in tokens(value)]
        if len(declaration) != 1:
            raise AssertionError("each original readonly endpoint has one complete common-owner definition")
        external = list(tokens("extern const micros_endpoint_t " + name + ";"))
        positions = [index for index in range(len(body)) if body[index:index + len(external)] == external]
        if len(positions) != 1:
            raise AssertionError("the model header exposes only its exact original readonly endpoint type")
        start = positions[0]
        body[start:start + len(external)] = tokens("static " + declaration[0])
    return tuple(normal_names(3).get(token, token) for token in body)


def check_model_owner_dependencies(records):
    allowed = {0: {1, 2, 3, 4, 5, 6}, 1: set(), 2: set(), 3: {2}, 4: {1, 3}, 5: {1, 2, 3, 4}, 6: {1, 2}}
    for name, (owner, record) in records.items():
        callees = {word for word in tokens(record.body) if word in records and records[word][0] != owner}
        destinations = {records[word][0] for word in callees}
        if "MODEL_EXPECT" in tokens(record.body) and owner != 1:
            destinations.add(1)
        if owner == 6 and any(
            records[word][0] == 2 and word != "tty_model_reference_output_pending" for word in callees
        ):
            raise AssertionError("the corrected validation/reference edge permits only the original final output observation")
        if not destinations <= allowed[owner]:
            raise AssertionError("model private owner direction changed: " + name)


def model_validation_body_tokens(caller, phase, declarations):
    from tests.host.test_grant_data_plane_source_layout import boolean_case_body_tokens, restore_automatic_work_tokens

    phase_tokens = tokens(phase)
    if set(phase_tokens[1:-4]) & {"return", "goto", "break", "continue"}:
        raise AssertionError("resident comparisons retain only original first-failure MODEL_EXPECT exits")
    body = boolean_case_body_tokens(tokens(caller), ((MODEL_VALIDATE_PHASE, "model, &work", phase, False),))
    return restore_automatic_work_tokens(body, MODEL_VALIDATE_WORK_TAG, MODEL_VALIDATE_FIELDS, declarations)


def model_validation_actual_body_tokens(record, owner=None):
    from tests.host.test_grant_data_plane_source_layout import work_declarations

    owner = source(MODEL_SOURCES[6]) if owner is None else owner
    declarations = work_declarations(owner, MODEL_VALIDATE_WORK_TAG, MODEL_VALIDATE_FIELDS)
    phase = function_records(owner)[MODEL_VALIDATE_PHASE]
    if phase.guards or tokens(phase.signature) != tokens("static bool " + MODEL_VALIDATE_PHASE + "(" + MODEL_VALIDATE_PARAMETERS + ")"):
        raise AssertionError("comparison phase binds only the real model and exact original automatic work")
    return model_validation_body_tokens("{" + record.body + "}", "{" + phase.body + "}", declarations)


def handoff_fixture_body_tokens(original, body):
    from tests.host.test_grant_data_plane_source_layout import fixture_body_tokens, declaration_name

    return fixture_body_tokens(
        original, body, HANDOFF_FIXTURE_TAG, HANDOFF_FIXTURE_GETTER, HANDOFF_CONTEXT_ROOTS, {},
        tuple(declaration_name(tokens(field)) for field in HANDOFF_FIXTURE_FIELDS),
    )


def root_work_body_tokens(body, tag, fields, declarations):
    from tests.host.test_grant_data_plane_source_layout import restore_automatic_work_tokens

    body = list(body)
    declaration = list(tokens("struct " + tag + " work;"))
    positions = [index for index in range(len(body)) if body[index:index + len(declaration)] == declaration]
    if len(positions) != 1 or body[:1] != ["{"]:
        raise AssertionError("one exact uninitialized automatic work follows original root-owned constants")
    start = positions[0]
    restored = restore_automatic_work_tokens(["{", *body[start:]], tag, fields, declarations)
    return (restored[0], *body[1:start], *restored[1:])


def device_body_tokens(caller, phase, declarations):
    from tests.host.test_grant_data_plane_source_layout import boolean_case_body_tokens

    phase_tokens = tokens(phase)
    if phase_tokens[-4:] != ("return", "true", ";", "}") or set(phase_tokens[1:-4]) & {"return", "goto", "break", "continue"}:
        raise AssertionError("device exclusions complete only after all original first-failure observations")
    body = boolean_case_body_tokens(tokens(caller), ((DEVICE_PHASE, DEVICE_ARGUMENTS, phase, True),))
    return root_work_body_tokens(body, DEVICE_WORK_TAG, DEVICE_FIELDS, declarations)


def device_actual_body_tokens(record, owner=None):
    from tests.host.test_grant_data_plane_source_layout import work_declarations

    owner = source(HANDOFF_SOURCES[2]) if owner is None else owner
    declarations = work_declarations(owner, DEVICE_WORK_TAG, DEVICE_FIELDS)
    phase = function_records(owner)[DEVICE_PHASE]
    if phase.guards or tokens(phase.signature) != tokens("static bool " + DEVICE_PHASE + "(" + DEVICE_PARAMETERS + ")"):
        raise AssertionError("device phase binds exact original caller work and root-owned readonly scalar/handle values")
    return device_body_tokens("{" + record.body + "}", "{" + phase.body + "}", declarations)


def lifecycle_body_tokens(caller, phase_bodies, declarations):
    from tests.host.test_grant_data_plane_source_layout import boolean_case_body_tokens, bound_body_tokens

    body = tokens(caller)
    for index, (name, result, _, arguments) in enumerate(LIFECYCLE_PHASES):
        phase = phase_bodies[name]
        actual = tokens(phase)
        if result == "void":
            if set(actual) & {"return", "goto", "break", "continue"}:
                raise AssertionError("original runtime record seeding adds no failure or new control exit")
            body = bound_body_tokens(body, name + "(" + arguments + ");", phase)
        else:
            if actual[-4:] != ("return", "true", ";", "}") or set(actual[1:-4]) & {"return", "goto", "break", "continue"}:
                raise AssertionError("runtime phases retain original first-failure observations and terminal success")
            body = boolean_case_body_tokens(body, ((name, arguments, phase, index == len(LIFECYCLE_PHASES) - 1),))
    return root_work_body_tokens(body, LIFECYCLE_WORK_TAG, LIFECYCLE_FIELDS, declarations)


def lifecycle_actual_body_tokens(record, owner=None):
    from tests.host.test_grant_data_plane_source_layout import work_declarations

    owner = source(HANDOFF_SOURCES[3]) if owner is None else owner
    declarations = work_declarations(owner, LIFECYCLE_WORK_TAG, LIFECYCLE_FIELDS)
    records = function_records(owner)
    bodies = {}
    for name, result, parameters, _ in LIFECYCLE_PHASES:
        phase = records[name]
        if phase.guards or tokens(phase.signature) != tokens("static " + result + " " + name + "(" + parameters + ")"):
            raise AssertionError("runtime phases bind only original fixture/work and necessary exact readonly values")
        bodies[name] = "{" + phase.body + "}"
    return lifecycle_body_tokens("{" + record.body + "}", bodies, declarations)


def control_fixture_body_tokens(original, body):
    from tests.host.test_grant_data_plane_source_layout import fixture_body_tokens, declaration_name

    return fixture_body_tokens(
        original, body, CONTROL_FIXTURE_TAG, CONTROL_FIXTURE_GETTER, CONTROL_CONTEXT_ROOTS, CONTROL_CONTEXT_HELPERS,
        tuple(declaration_name(tokens(field)) for field in CONTROL_FIXTURE_FIELDS),
    )


def authority_body_tokens(caller, phase, declarations):
    from tests.host.test_grant_data_plane_source_layout import boolean_case_body_tokens

    actual = tokens(phase)
    if actual[-4:] != ("return", "true", ";", "}") or set(actual[1:-4]) & {"return", "goto", "break", "continue"}:
        raise AssertionError("authority rejections retain original ordered first-failure observations and success")
    body = boolean_case_body_tokens(tokens(caller), ((AUTHORITY_PHASE, "&work, thread_handle", phase, True),))
    return root_work_body_tokens(body, AUTHORITY_WORK_TAG, AUTHORITY_FIELDS, declarations)


def authority_actual_body_tokens(record, owner=None):
    from tests.host.test_grant_data_plane_source_layout import work_declarations

    owner = source(FAMILY_PATHS[6][0]) if owner is None else owner
    declarations = work_declarations(owner, AUTHORITY_WORK_TAG, AUTHORITY_FIELDS)
    phase = function_records(owner)[AUTHORITY_PHASE]
    if phase.guards or tokens(phase.signature) != tokens("static bool " + AUTHORITY_PHASE + "(" + AUTHORITY_PARAMETERS + ")"):
        raise AssertionError("authority phase binds only exact original typed automatic fixture and root readonly thread")
    return authority_body_tokens("{" + record.body + "}", "{" + phase.body + "}", declarations)


def uart_body_tokens(caller, phases, declarations):
    from tests.host.test_grant_data_plane_source_layout import bound_body_tokens, restore_automatic_work_tokens

    body = tokens(caller)
    for name in UART_PHASES:
        phase = phases[name]
        if set(tokens(phase)) & {"return", "goto"}:
            raise AssertionError("real UART phases retain original panic and loop scopes without early return")
        body = bound_body_tokens(body, name + "(hart_id, &work);", phase)
    return restore_automatic_work_tokens(body, UART_WORK_TAG, UART_FIELDS, declarations)


def uart_actual_body_tokens(record, owner=None):
    from tests.host.test_grant_data_plane_source_layout import work_declarations

    owner = source(FAMILY_PATHS[1][0]) if owner is None else owner
    declarations = work_declarations(owner, UART_WORK_TAG, UART_FIELDS)
    records = function_records(owner)
    phases = {}
    for name in UART_PHASES:
        phase = records[name]
        if phase.guards or tokens(phase.signature) != tokens("static void " + name + "(" + UART_PARAMETERS + ")"):
            raise AssertionError("real hardware phases bind exact original hart identifier and typed caller automatic work")
        phases[name] = "{" + phase.body + "}"
    return uart_body_tokens("{" + record.body + "}", phases, declarations)


def generator_module_projection(text):
    from tests.host.test_ramfs_source_layout import restored_generator

    module = ast.parse(text)
    phases = [node for node in module.body if isinstance(node, ast.FunctionDef) and node.name == GENERATOR_PHASE]
    roots = [node for node in module.body if isinstance(node, ast.FunctionDef) and node.name == "render_fixture"]
    if len(phases) != 1 or len(roots) != 1 or len(phases[0].body) != 1:
        raise AssertionError("one exact private manifest append statement belongs to one original renderer")
    statement = phases[0].body[0]
    if not isinstance(statement, ast.Expr) or not isinstance(statement.value, ast.Call):
        raise AssertionError("the manifest phase retains the original single output append")
    call = statement.value
    if (
        not isinstance(call.func, ast.Attribute) or call.func.attr != "extend"
        or not isinstance(call.func.value, ast.Name) or call.func.value.id != "output"
        or len(call.args) != 1 or not isinstance(call.args[0], ast.Tuple) or call.keywords
    ):
        raise AssertionError("the original complete manifest/report tuple appends to its actual caller output")
    calls = [
        node for node in ast.walk(roots[0])
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == GENERATOR_PHASE
    ]
    if len(calls) != 1 or not any(isinstance(node, ast.Expr) and node.value is calls[0] for node in roots[0].body):
        raise AssertionError("the manifest phase has one direct original renderer scope and no duplicate or nested call")
    return restored_generator(text, {GENERATOR_PHASE: ("void", GENERATOR_PARAMETERS)})


def function_references(body, names):
    words = tokens(body)
    return {
        word for index, word in enumerate(words) if word in names
        and (index == 0 or words[index - 1] not in ("struct", "enum", "union", ".", "->"))
    }


def source_set_block(name, paths):
    return "set(\n    " + name + "\n" + "".join("    " + path + "\n" for path in paths) + ")"


def alias_rows():
    return tuple((paths[0], (*paths[1:], header)) for paths, header in zip(SOURCE_SETS.values(), PRIVATE_HEADERS))


def tty_source_cmake_projection(text):
    blocks = [(name, paths, cmake_commands(text, "set", name)) for name, paths in SOURCE_SETS.items()]
    if not any(found for _, _, found in blocks):
        if any("${" + name + "}" in text for name in SOURCE_SETS):
            raise AssertionError("TTY operands require complete actual ordered source sets")
        return text
    for index, (name, paths, found) in enumerate(blocks):
        expected = source_set_block(name, paths)
        if len(found) != 1 or found[0] != expected or text.count(expected + "\n\n") != 1:
            raise AssertionError("each complete ordered TTY source set occurs exactly once")
        variable = "${" + name + "}"
        if text.count(variable) != (2, 3, 1, 1, 1)[index]:
            raise AssertionError("TTY sets preserve exactly their original eight consumer operands")
        text = text.replace(expected + "\n\n", "", 1).replace(variable, paths[0])
    return text


def restored_cmake(text):
    restored = tty_source_cmake_projection(text)
    if digest_bytes(restored.encode()) != BASE_CMAKE_DIGEST:
        raise AssertionError("whole TTY recipe inverse differs outside five sets/eight operands")
    return restored


def tty_planner_projection(tree):
    from tests.host.test_qemu_runner_source_layout import qemu_planner_projection

    tree = qemu_planner_projection(tree)
    expected = dict(alias_rows())
    assignments = {
        node.targets[0].id: node for node in tree.body
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name)
    }
    if "PATH_CLASSIFICATION_EQUIVALENTS" not in assignments or "HOST_UNIT_ONLY_INPUTS" not in assignments:
        raise AssertionError("complete original planner metadata declarations remain present")
    aliases = assignments["PATH_CLASSIFICATION_EQUIVALENTS"].value
    host = assignments["HOST_UNIT_ONLY_INPUTS"].value
    rows = ast.literal_eval(aliases)
    paths = ast.literal_eval(host)
    selected = {old: values for old, values in rows if old in expected}
    if not selected and STRUCTURAL_TEST_PATH not in paths:
        return tree
    if selected != expected or sum(old in expected for old, _ in rows) != 5 or paths.count(STRUCTURAL_TEST_PATH) != 1:
        raise AssertionError("TTY adds only complete five family aliases and one host-only structural path")
    aliases.elts = [item for item in aliases.elts if ast.literal_eval(item)[0] not in expected]
    host.elts = [item for item in host.elts if ast.literal_eval(item) != STRUCTURAL_TEST_PATH]
    return tree


def context_ast_without_recipe_pin(text):
    from tests.host.test_qemu_runner_source_layout import context_declaration_projection

    tree = context_declaration_projection(text)
    selected = [
        node for node in tree.body if isinstance(node, ast.Assign)
        and any(isinstance(target, ast.Name) and target.id == "RECIPE_DIGESTS" for target in node.targets)
    ]
    if len(selected) != 1 or not isinstance(selected[0].value, ast.Dict):
        raise AssertionError("one complete current recipe declaration remains")
    mapping = selected[0].value
    positions = [index for index, key in enumerate(mapping.keys) if ast.literal_eval(key) == "CMakeLists.txt"]
    if len(positions) != 1:
        raise AssertionError("one maintained CMake digest remains")
    mapping.values[positions[0]] = ast.Constant(value=BASE_CMAKE_DIGEST)
    return tree


class TtySourceLayoutTest(unittest.TestCase):
    def test_renderer_has_one_exact_manifest_phase_and_bounded_original_entries(self):
        module = ast.parse(source(GENERATOR_PATH))
        functions = {node.name: node for node in module.body if isinstance(node, ast.FunctionDef)}
        self.assertIn(GENERATOR_PHASE, set(functions))
        self.assertEqual({*GENERATOR_FUNCTIONS, GENERATOR_PHASE}, set(functions))
        self.assertEqual(GENERATOR_PARAMETERS, tuple(arg.arg for arg in functions[GENERATOR_PHASE].args.args))
        for name, node in functions.items():
            self.assertLessEqual(node.end_lineno - node.lineno + 1, 120, name)

    def test_generator_inverse_preserves_exact_manifest_order_values_report_expression_and_caller_scope(self):
        original = '''
REPORT_SYMBOL = "report"
def render_fixture(launcher_image, vm_image, pm_image, tty_image, vfs_image):
    launcher_limit = launcher_image["page_count"] + 2
    output = []
    output.extend(("header", f"{total_limit}", "VFS", f"{vfs_limit}", "LAUNCHER", f"{launcher_limit}", "TTY", f"{tty_limit}", "PM", f"{pm_limit}", "VM", f"{vm_limit}", f"{vfs['symbols'][REPORT_SYMBOL].value:016x}"))
    return "\\n".join(output)
def parse_arguments(argv=None):
    return original_parser(argv)
def main(argv=None):
    return original_main(argv)
'''
        outlined = '''
REPORT_SYMBOL = "report"
def _append_tty_manifest(output, total_limit, vfs_limit, launcher_limit, tty_limit, pm_limit, vm_limit, vfs):
    output.extend(("header", f"{total_limit}", "VFS", f"{vfs_limit}", "LAUNCHER", f"{launcher_limit}", "TTY", f"{tty_limit}", "PM", f"{pm_limit}", "VM", f"{vm_limit}", f"{vfs['symbols'][REPORT_SYMBOL].value:016x}"))
def render_fixture(launcher_image, vm_image, pm_image, tty_image, vfs_image):
    launcher_limit = launcher_image["page_count"] + 2
    output = []
    _append_tty_manifest(output, total_limit, vfs_limit, launcher_limit, tty_limit, pm_limit, vm_limit, vfs)
    return "\\n".join(output)
def parse_arguments(argv=None):
    return original_parser(argv)
def main(argv=None):
    return original_main(argv)
'''
        expected = ast_dump(ast.parse(original))
        projected = lambda text: ast_dump(generator_module_projection(text))
        self.assertEqual(expected, projected(outlined))
        for changed in (
            outlined.replace("    _append_tty_manifest(output,", "    _append_tty_manifest(copied_output,"),
            outlined.replace("vm_limit, vfs):", "vm_limit, vfs=effect()):"),
            outlined.replace("def _append_tty_manifest", "@effect()\ndef _append_tty_manifest"),
            outlined.replace("    output.extend(", "    return\n    output.extend(", 1),
            outlined.replace("    output.extend(", "    output.append(", 1),
            outlined.replace(
                "    _append_tty_manifest(output, total_limit, vfs_limit, launcher_limit, tty_limit, pm_limit, vm_limit, vfs)",
                "    if condition:\n        _append_tty_manifest(output, total_limit, vfs_limit, launcher_limit, tty_limit, pm_limit, vm_limit, vfs)",
            ),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                projected(changed)
        for changed in (
            outlined.replace('"VFS", f"{vfs_limit}"', '"TTY", f"{tty_limit}"'),
            outlined.replace(" + 2", " + 1"),
            outlined.replace("REPORT_SYMBOL].value", "REPORT_SYMBOL].size"),
            outlined.replace('return "\\n".join(output)', 'return " ".join(output)'),
            outlined.replace("original_main(argv)", "different_main(argv)"),
        ):
            self.assertNotEqual(expected, projected(changed))

    def test_real_uart_has_exact_typed_hardware_phases_and_retains_original_root_reporting(self):
        owner = source(FAMILY_PATHS[1][0])
        records = function_records(owner)
        for name in UART_PHASES:
            self.assertIn(name, set(records))
            self.assertEqual(tokens("static void " + name + "(" + UART_PARAMETERS + ")"), tokens(records[name].signature))
            self.assertLessEqual(records[name].lines, 120)
        root = records["micros_uart_console_runtime_run_self_test"]
        self.assertLessEqual(root.lines, 120)
        self.assertEqual(3, tokens(root.body).count("uart_write"))
        self.assertEqual(2, tokens(root.body).count("uart_flush"))
        self.assertEqual(1, tokens(root.body).count("sbi_system_reset"))
        self.assertIn('"uart-console-reset-returned"', tokens(root.body))
        prefix = owner[:owner.index("static volatile uint8_t *uart_registers")]
        self.assertEqual(UART_PREFIX_DIGEST, digest_tokens(tokens(prefix)))

    def test_uart_inverse_preserves_volatile_mmio_initializers_flags_bounded_claim_and_root_reporting(self):
        caller = (
            "{ struct uart_console_test_work work; work.registers = uart_registers(); work.source = 0; "
            "uart_console_test_prepare_handoff(hart_id, &work); uart_write(\"begin\"); uart_flush(); "
            "uart_console_test_quiesce_console(hart_id, &work); uart_console_test_publish_irq_route(hart_id, &work); "
            "uart_console_test_claim_and_retain(hart_id, &work); uart_console_test_complete_and_seize(hart_id, &work); "
            "uart_write(\"pass\"); uart_write(\"irqpass\"); uart_flush(); "
            "(void)sbi_system_reset(SHUTDOWN, NONE); MICROS_PANIC(hart_id, \"reset-returned\"); }"
        )
        phases = dict(zip(UART_PHASES, (
            "{ original_prepare_and_release(&work->candidate); }",
            "{ work->registers[OFFSET] = TEST; original_quiesce(); "
            "if (work->registers[OFFSET] != 0) { MICROS_PANIC(hart_id, \"ier\"); } "
            "original_console_commit(); work->registers[OFFSET] = RX; riscv_mmio_fence(); }",
            "{ work->timer_was_enabled = original_timer_bit(); work->irq_was_enabled = original_irq_bit(); "
            "original_state_before_route(&work->candidate, &work->plic_plan); original_external_enable(); "
            "work->state = original_state(); original_observe_bits(work->state, work->timer_was_enabled, work->irq_was_enabled); }",
            "{ work->registers[OFFSET] = TX; riscv_mmio_fence(); "
            "for (work->attempt = 0; work->attempt < CLAIM_ATTEMPTS; ++work->attempt) { "
            "if (!original_claim(&work->source)) { MICROS_PANIC(hart_id, \"claim\"); } if (work->source != 0) { break; } } "
            "original_commit_claim(&work->candidate); work->source = UINT32_MAX; "
            "original_retained_probe(&work->source, work->state); work->registers[OFFSET] = 0; riscv_mmio_fence(); }",
            "{ original_complete(&work->candidate); original_seize(); work->state = original_state(); "
            "original_terminal_panic_observation(work->state); }",
        )))
        expected = tokens(
            "{ volatile uint8_t *registers = uart_registers(); const struct micros_tty_handoff_runtime_state *state; "
            "struct micros_tty_handoff candidate; struct micros_plic_tty_enable_plan plic_plan; "
            "bool timer_was_enabled; bool irq_was_enabled; uint32_t source = 0; uint32_t attempt; "
            "original_prepare_and_release(&candidate); uart_write(\"begin\"); uart_flush(); "
            "registers[OFFSET] = TEST; original_quiesce(); if (registers[OFFSET] != 0) { MICROS_PANIC(hart_id, \"ier\"); } "
            "original_console_commit(); registers[OFFSET] = RX; riscv_mmio_fence(); "
            "timer_was_enabled = original_timer_bit(); irq_was_enabled = original_irq_bit(); "
            "original_state_before_route(&candidate, &plic_plan); original_external_enable(); state = original_state(); "
            "original_observe_bits(state, timer_was_enabled, irq_was_enabled); registers[OFFSET] = TX; riscv_mmio_fence(); "
            "for (attempt = 0; attempt < CLAIM_ATTEMPTS; ++attempt) { "
            "if (!original_claim(&source)) { MICROS_PANIC(hart_id, \"claim\"); } if (source != 0) { break; } } "
            "original_commit_claim(&candidate); source = UINT32_MAX; original_retained_probe(&source, state); "
            "registers[OFFSET] = 0; riscv_mmio_fence(); original_complete(&candidate); original_seize(); state = original_state(); "
            "original_terminal_panic_observation(state); uart_write(\"pass\"); uart_write(\"irqpass\"); uart_flush(); "
            "(void)sbi_system_reset(SHUTDOWN, NONE); MICROS_PANIC(hart_id, \"reset-returned\"); }"
        )
        declarations = tuple(tokens(declaration) for declaration, _ in UART_FIELDS)
        self.assertEqual(expected, uart_body_tokens(caller, phases, declarations))
        for changed in (
            caller.replace("struct uart_console_test_work work;", "struct uart_console_test_work work = {0};"),
            caller.replace("hart_id, &work)", "other_hart, &work)"),
            caller.replace("hart_id, &work)", "hart_id, &copy)"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                uart_body_tokens(changed, phases, declarations)
        with self.assertRaises(AssertionError):
            uart_body_tokens(caller, phases, (tokens("uint8_t *registers;"), *declarations[1:]))
        changed = {**phases, UART_PHASES[0]: "{ return; }"}
        with self.assertRaises(AssertionError):
            uart_body_tokens(caller, changed, declarations)
        for index, old, new in (
            (2, "original_state_before_route", "riscv_irq_save(); original_state_before_route"),
            (3, "< CLAIM_ATTEMPTS", "< OTHER_ATTEMPTS"),
            (3, "UINT32_MAX", "0"),
        ):
            changed = {**phases, UART_PHASES[index]: phases[UART_PHASES[index]].replace(old, new)}
            self.assertNotEqual(expected, uart_body_tokens(caller, changed, declarations))
        self.assertNotEqual(expected, uart_body_tokens(caller.replace("= uart_registers()", "= other_registers()"), phases, declarations))

    def test_native_control_authority_has_exact_typed_rejection_phase_after_authorized_tuple(self):
        records = function_records(source(FAMILY_PATHS[6][0]))
        self.assertIn(AUTHORITY_PHASE, set(records))
        self.assertEqual(tokens("static bool " + AUTHORITY_PHASE + "(" + AUTHORITY_PARAMETERS + ")"), tokens(records[AUTHORITY_PHASE].signature))
        self.assertLessEqual(records["test_syscall_authority"].lines, 120)
        self.assertLessEqual(records[AUTHORITY_PHASE].lines, 120)

    def test_authority_inverse_retains_root_identity_initializers_authorized_first_and_ordered_corruptions(self):
        prefix = (
            "{ const struct micros_process_handle process = {3, 7}; "
            "const struct micros_thread_handle thread_handle = {5, 9}; "
            "const micros_endpoint_t endpoint_value = (process.generation << SLOT_BITS) | process.slot; "
        )
        caller = (
            prefix + "struct tty_control_authority_case work; "
            "work.state = (struct micros_tty_handoff_runtime_state){ .process = process, .thread = thread_handle }; "
            "work.binding = (struct micros_bootstrap_binding){ .endpoint = endpoint_value }; "
            "work.entry = (struct micros_bootstrap_manifest_entry){ .irq_source = SOURCE }; "
            "work.context = (struct micros_syscall_context){ .current = thread_handle, .process = process }; "
            "work.caller_process = (struct micros_process){ .privilege_profile = TTY }; "
            "work.caller_thread = (struct micros_thread){ .owner = process }; "
            "work.endpoint = (struct micros_endpoint_record){ .value = endpoint_value }; "
            "work.profile = (struct micros_privilege_profile){ .kernel_operations = TTY_OP }; "
            "EXPECT_TRUE(original_authorized(&work.state, &work.binding, &work.entry, &work.context, "
            "&work.caller_process, &work.caller_thread, &work.endpoint, &work.profile)); "
            "return tty_control_test_check_authority_rejections(&work, thread_handle); }"
        )
        phase = (
            "{ work->context.current.slot += 1; EXPECT_TRUE(original_foreign(&work->context)); "
            "work->context.current = thread_handle; work->caller_thread.slot_state = FREE; "
            "EXPECT_TRUE(original_free_thread(&work->caller_thread)); work->caller_thread.slot_state = LIVE; "
            "work->profile.kernel_operations = PM_OP; EXPECT_TRUE(original_profile(&work->profile)); "
            "work->profile.kernel_operations = TTY_OP; work->entry.irq_source -= 1; "
            "EXPECT_TRUE(original_manifest(&work->entry)); return true; }"
        )
        expected = tokens(
            prefix + "struct micros_tty_handoff_runtime_state state = { .process = process, .thread = thread_handle }; "
            "struct micros_bootstrap_binding binding = { .endpoint = endpoint_value }; "
            "struct micros_bootstrap_manifest_entry entry = { .irq_source = SOURCE }; "
            "struct micros_syscall_context context = { .current = thread_handle, .process = process }; "
            "struct micros_process caller_process = { .privilege_profile = TTY }; "
            "struct micros_thread caller_thread = { .owner = process }; "
            "struct micros_endpoint_record endpoint = { .value = endpoint_value }; "
            "struct micros_privilege_profile profile = { .kernel_operations = TTY_OP }; "
            "EXPECT_TRUE(original_authorized(&state, &binding, &entry, &context, &caller_process, &caller_thread, &endpoint, &profile)); "
            "context.current.slot += 1; EXPECT_TRUE(original_foreign(&context)); context.current = thread_handle; "
            "caller_thread.slot_state = FREE; EXPECT_TRUE(original_free_thread(&caller_thread)); caller_thread.slot_state = LIVE; "
            "profile.kernel_operations = PM_OP; EXPECT_TRUE(original_profile(&profile)); profile.kernel_operations = TTY_OP; "
            "entry.irq_source -= 1; EXPECT_TRUE(original_manifest(&entry)); return true; }"
        )
        declarations = tuple(tokens(declaration) for declaration, _ in AUTHORITY_FIELDS)
        self.assertEqual(expected, authority_body_tokens(caller, phase, declarations))
        for changed in (
            caller.replace("struct tty_control_authority_case work;", "struct tty_control_authority_case work = {0};"),
            caller.replace("&work, thread_handle)", "&work, other_thread)"),
            caller.replace("&work, thread_handle)", "&copy, thread_handle)"),
            caller.replace("return tty_control_test", "retain(&work); return tty_control_test"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                authority_body_tokens(changed, phase, declarations)
        with self.assertRaises(AssertionError):
            authority_body_tokens(caller, phase.replace("return true;", "return false;"), declarations)
        with self.assertRaises(AssertionError):
            authority_body_tokens(caller, phase, (tokens("struct micros_thread state;"), *declarations[1:]))
        self.assertNotEqual(expected, authority_body_tokens(caller.replace("{5, 9}", "{5, 8}"), phase, declarations))
        self.assertNotEqual(expected, authority_body_tokens(caller, phase.replace("= PM_OP", "= TTY_OP"), declarations))

    def test_captured_control_originals_have_exact_owners_and_one_twenty_field_fixture(self):
        header = source(PRIVATE_HEADERS[4])
        expected = tokens("struct " + CONTROL_FIXTURE_TAG + " {" + "".join(CONTROL_FIXTURE_FIELDS) + "};")
        self.assertIn(" ".join(expected), " ".join(tokens(header)))
        fixture = source(CONTROL_SOURCES[1])
        self.assertEqual(("static struct tty_control_syscall_test_fixture fixture_state;",), tuple(file_scope_objects(fixture)))
        self.assertEqual(tokens("{ return &fixture_state; }"), tokens(
            "{" + function_records(fixture)[CONTROL_FIXTURE_GETTER].body + "}",
        ))
        private = {name: "tty_control_syscall_test_" + (name[5:] if name.startswith("test_") else name)
                   for names in ORIGINAL_FUNCTIONS[5].values() for name in names
                   if not name.startswith("micros_") and name not in ("main", "uart_console_handoff_is_active")}
        for owner, names in ORIGINAL_FUNCTIONS[5].items():
            records = function_records(source(CONTROL_SOURCES[owner]))
            for name in names:
                self.assertIn(private.get(name, name), set(records), CONTROL_SOURCES[owner])
        for path in (CONTROL_SOURCES[0], *CONTROL_SOURCES[2:]):
            self.assertEqual((), tuple(file_scope_objects(source(path))), path)

    def test_captured_control_keeps_both_jump_targets_in_actual_case_roots_and_exact_private_directions(self):
        header = source(PRIVATE_HEADERS[4])
        start = header.index("#define EXPECT_TRUE")
        self.assertEqual(HANDOFF_MACRO_DIGEST, digest_tokens(tokens(header[start:header.index("\n\n", start)])))
        records = {
            name: (owner, record) for owner, path in enumerate(CONTROL_SOURCES)
            for name, record in function_records(source(path)).items()
        }
        jump_roots = {}
        longjmp_roots = set()
        allowed = {0: {1}, 1: set(), 2: {1}, 3: {1}}
        for name, (owner, record) in records.items():
            words = tokens(record.body)
            destinations = {records[word][0] for word in function_references(record.body, records) if records[word][0] != owner}
            self.assertLessEqual(destinations, allowed[owner], name)
            if "setjmp" in words:
                self.assertEqual(0, owner)
                self.assertEqual(1, words.count("setjmp"))
                self.assertIn(" ".join(tokens("setjmp(fixture->panic_target)")), " ".join(words))
                jump_roots[normal_names(5).get(name, name)] = record
            if "longjmp" in words:
                self.assertEqual(3, owner)
                self.assertIn(" ".join(tokens("longjmp(fixture->panic_target, 1)")), " ".join(words))
                longjmp_roots.add(name)
        self.assertEqual(set(CONTROL_JUMP_ROOTS), set(jump_roots))
        self.assertEqual({"micros_bootstrap_runtime_fail", "micros_trap_panic_entry"}, longjmp_roots)

    def test_function_references_distinguish_actual_callbacks_from_separate_c_tag_and_member_namespaces(self):
        names = {"micros_tty_handoff_runtime_state"}
        self.assertEqual(set(), function_references(
            "{ state = (struct micros_tty_handoff_runtime_state){0}; }", names,
        ))
        self.assertEqual(set(), function_references(
            "{ record.micros_tty_handoff_runtime_state = 0; record->micros_tty_handoff_runtime_state = 1; }", names,
        ))
        self.assertEqual(names, function_references(
            "{ callback = &micros_tty_handoff_runtime_state; }", names,
        ))
        self.assertEqual(names, function_references("{ callback = micros_tty_handoff_runtime_state; }", names))
        self.assertEqual(names, function_references("{ return micros_tty_handoff_runtime_state(); }", names))

    def test_captured_control_fixture_inverse_preserves_live_jump_root_and_ordinary_helper_arguments(self):
        caller = (
            "{ struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture(); "
            "struct micros_syscall_arguments arguments; struct micros_trap_frame frame = {0}; "
            "reset_fixture(fixture, false); arguments = commit_arguments(fixture); fixture->panic_armed = true; "
            "if (setjmp(fixture->panic_target) == 0) { (void)invoke(fixture, &arguments, &frame); "
            "fixture->panic_armed = false; return false; } fixture->panic_armed = false; "
            "return original_snapshot(&fixture->tty_state, fixture->operation_stage, fixture->owner_fault_count); }"
        )
        expected = tokens(
            "{ struct micros_syscall_arguments arguments; struct micros_trap_frame frame = {0}; "
            "reset_fixture(false); arguments = commit_arguments(); panic_armed = true; "
            "if (setjmp(panic_target) == 0) { (void)invoke(&arguments, &frame); panic_armed = false; return false; } "
            "panic_armed = false; return original_snapshot(&tty_state, operation_stage, owner_fault_count); }"
        )
        original = CONTROL_JUMP_ROOTS[0]
        self.assertEqual(expected, control_fixture_body_tokens(original, tokens(caller)))
        for changed in (
            caller.replace("tty_control_syscall_test_fixture()", "reset_getter()"),
            caller.replace("invoke(fixture,", "invoke(other,"),
            caller.replace("fixture->panic_armed", "fixture->unknown"),
            caller.replace("return original_snapshot", "retain(fixture); return original_snapshot"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                control_fixture_body_tokens(original, tokens(changed))
        self.assertNotEqual(expected, control_fixture_body_tokens(original, tokens(caller.replace("setjmp(", "returned_helper_setjmp("))))
        signature = (
            "struct micros_syscall_arguments tty_control_syscall_test_commit_arguments("
            "struct tty_control_syscall_test_fixture *fixture) { return original_arguments; }"
        )
        record = function_records(signature)["tty_control_syscall_test_commit_arguments"]
        self.assertEqual(tokens("static struct micros_syscall_arguments commit_arguments(void)"), original_signature_tokens(5, "commit_arguments", record))
        changed = signature.replace("struct tty_control_syscall_test_fixture *fixture", "struct tty_handoff_test_fixture *fixture")
        with self.assertRaises(AssertionError):
            original_signature_tokens(5, "commit_arguments", function_records(changed)[record.name])
        pure = "struct micros_syscall_arguments tty_control_syscall_test_complete_arguments(void) { return original_complete; }"
        self.assertEqual(tokens("static struct micros_syscall_arguments complete_arguments(void)"), original_signature_tokens(
            5, "complete_arguments", function_records(pure)["tty_control_syscall_test_complete_arguments"],
        ))

    def test_runtime_case_has_exact_bounded_seed_authority_begin_map_release_claim_ready_completion_phases(self):
        records = function_records(source(HANDOFF_SOURCES[3]))
        self.assertLessEqual(records["tty_handoff_test_runtime_lifecycle_validation"].lines, 120)
        for name, result, parameters, _ in LIFECYCLE_PHASES:
            self.assertIn(name, set(records))
            self.assertEqual(tokens("static " + result + " " + name + "(" + parameters + ")"), tokens(records[name].signature))
            self.assertLessEqual(records[name].lines, 120)

    def test_runtime_case_inverse_preserves_original_initializers_derived_pointers_and_lifecycle_order(self):
        prefix = (
            "{ struct tty_handoff_test_fixture *fixture = tty_handoff_test_fixture(); "
            "const struct micros_process_handle process = {3, 7}; const struct micros_thread_handle thread = {5, 9}; "
            "const micros_endpoint_t endpoint = (process.generation << SLOT_BITS) | process.slot; const uint64_t root = ROOT; "
        )
        caller = (
            prefix + "struct tty_handoff_runtime_case work; "
            "work.bootstrap = (struct micros_bootstrap_control_state){0}; work.binding = &work.bootstrap.bindings[0]; "
            "work.entry = &work.bootstrap.manifest.entries[0]; work.transition = &work.bootstrap.transitions.entries[0]; "
            "work.registry = (struct micros_endpoint_registry){0}; work.objects = (struct micros_kernel_objects){0}; "
            "work.context = (struct micros_syscall_context){ .objects = &work.objects, .current = thread, .process = process }; "
            "work.unexpected_state = (struct micros_tty_handoff_runtime_state){ .process = {4, 7}, .thread = {6, 9} }; "
            "tty_handoff_test_seed_runtime_records(fixture, &work, process, thread, endpoint, root); "
            "if (!tty_handoff_test_check_initial_runtime_authority(fixture, &work)) { return false; } "
            "if (!tty_handoff_test_begin_and_map_runtime(fixture, &work, process, root)) { return false; } "
            "if (!tty_handoff_test_release_and_commit_runtime(fixture, &work)) { return false; } "
            "if (!tty_handoff_test_claim_and_ready_runtime(&work)) { return false; } "
            "return tty_handoff_test_complete_and_panic_runtime(fixture, &work); }"
        )
        bodies = dict(zip((phase[0] for phase in LIFECYCLE_PHASES), (
            "{ *work->binding = original_binding(process, thread, endpoint, root); fixture->runtime_process_handle = process; }",
            "{ work->bootstrap.plan.console_service_id = 0; EXPECT_TRUE(original_absent_and_unexpected(&work->unexpected_state)); "
            "work->bootstrap.plan.console_service_id = TTY; EXPECT_TRUE(original_prepare_bind(&work->bootstrap)); return true; }",
            "{ work->runtime = original_authoritative(); EXPECT_TRUE(original_begin_deadline150(&work->begin)); "
            "EXPECT_TRUE(original_map_authority(&work->authority, process, root)); "
            "memset(&work->authority_sentinel, 0xa5, sizeof(work->authority_sentinel)); work->authority = work->authority_sentinel; "
            "EXPECT_TRUE(original_preserve_sentinel(&work->authority, &work->authority_sentinel)); return true; }",
            "{ fixture->runtime_thread.scheduler_assigned = true; "
            "EXPECT_TRUE(original_release_then_commit(&work->released, &work->committed)); return true; }",
            "{ EXPECT_TRUE(original_claim_then_ready149(&work->claimed, &work->ready)); "
            "work->transition->ready_deadline = 0; original_commit_ready(&work->ready); return true; }",
            "{ EXPECT_TRUE(original_complete_then_source_only(&work->completed)); "
            "EXPECT_TRUE(original_terminal_panic(work->runtime)); return true; }",
        )))
        expected = tokens(
            prefix + "struct micros_bootstrap_control_state bootstrap = {0}; "
            "struct micros_bootstrap_binding *binding = &bootstrap.bindings[0]; "
            "struct micros_bootstrap_manifest_entry *entry = &bootstrap.manifest.entries[0]; "
            "struct micros_bootstrap_runtime_entry *transition = &bootstrap.transitions.entries[0]; "
            "struct micros_endpoint_registry registry = {0}; struct micros_kernel_objects objects = {0}; "
            "struct micros_syscall_context context = { .objects = &objects, .current = thread, .process = process }; "
            "struct micros_tty_handoff mapped; struct micros_tty_handoff begin; struct micros_tty_handoff released; "
            "struct micros_tty_handoff committed; struct micros_tty_handoff claimed; struct micros_tty_handoff completed; "
            "struct micros_tty_handoff ready; struct micros_tty_device_authority authority; "
            "struct micros_tty_device_authority authority_sentinel; "
            "struct micros_tty_handoff_runtime_state unexpected_state = { .process = {4, 7}, .thread = {6, 9} }; "
            "struct micros_tty_handoff_runtime_state *runtime; "
            "*binding = original_binding(process, thread, endpoint, root); fixture->runtime_process_handle = process; "
            "bootstrap.plan.console_service_id = 0; EXPECT_TRUE(original_absent_and_unexpected(&unexpected_state)); "
            "bootstrap.plan.console_service_id = TTY; EXPECT_TRUE(original_prepare_bind(&bootstrap)); "
            "runtime = original_authoritative(); EXPECT_TRUE(original_begin_deadline150(&begin)); "
            "EXPECT_TRUE(original_map_authority(&authority, process, root)); "
            "memset(&authority_sentinel, 0xa5, sizeof(authority_sentinel)); authority = authority_sentinel; "
            "EXPECT_TRUE(original_preserve_sentinel(&authority, &authority_sentinel)); "
            "fixture->runtime_thread.scheduler_assigned = true; EXPECT_TRUE(original_release_then_commit(&released, &committed)); "
            "EXPECT_TRUE(original_claim_then_ready149(&claimed, &ready)); transition->ready_deadline = 0; original_commit_ready(&ready); "
            "EXPECT_TRUE(original_complete_then_source_only(&completed)); EXPECT_TRUE(original_terminal_panic(runtime)); return true; }"
        )
        declarations = tuple(tokens(declaration) for declaration, _ in LIFECYCLE_FIELDS)
        self.assertEqual(expected, lifecycle_body_tokens(caller, bodies, declarations))
        for changed in (
            caller.replace("struct tty_handoff_runtime_case work;", "struct tty_handoff_runtime_case work = {0};"),
            caller.replace("fixture, &work, process, root)", "other, &work, process, root)"),
            caller.replace("fixture, &work, process, root)", "fixture, &copy, process, root)"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                lifecycle_body_tokens(changed, bodies, declarations)
        changed = dict(bodies)
        name = LIFECYCLE_PHASES[2][0]
        changed[name] = changed[name].replace("EXPECT_TRUE", "return false; EXPECT_TRUE", 1)
        with self.assertRaises(AssertionError):
            lifecycle_body_tokens(caller, changed, declarations)
        with self.assertRaises(AssertionError):
            lifecycle_body_tokens(caller, bodies, (tokens("uint64_t bootstrap;"), *declarations[1:]))
        self.assertNotEqual(expected, lifecycle_body_tokens(caller.replace("&work.bootstrap.bindings[0]", "&copied.bindings[0]"), bodies, declarations))
        changed[name] = bodies[name].replace("0xa5", "0")
        self.assertNotEqual(expected, lifecycle_body_tokens(caller, changed, declarations))

    def test_device_classification_has_one_typed_exclusion_phase_after_exact_range_observations(self):
        records = function_records(source(HANDOFF_SOURCES[2]))
        self.assertIn(DEVICE_PHASE, set(records))
        self.assertEqual(tokens("static bool " + DEVICE_PHASE + "(" + DEVICE_PARAMETERS + ")"), tokens(records[DEVICE_PHASE].signature))
        self.assertLessEqual(records["tty_handoff_test_device_leaf_classification"].lines, 120)
        self.assertLessEqual(records[DEVICE_PHASE].lines, 120)

    def test_device_inverse_retains_root_constants_exact_authority_initializer_and_output_sentinel_order(self):
        caller = (
            "{ const struct micros_process_handle tty_process = {3, 7}; "
            "const struct micros_process_handle foreign_process = {2, 5}; "
            "const uint64_t tty_root = ROOT; const uint32_t exact_permissions = READ | WRITE | USER; "
            "struct tty_handoff_device_work work; "
            "work.authority = (struct micros_tty_device_authority){ .process = tty_process, .root = tty_root, .phase = MAPPED }; "
            "EXPECT_TRUE(original_exact_range(&work.authority, &work.classification)); "
            "return tty_handoff_test_check_device_exclusions(&work, tty_process, foreign_process, tty_root, exact_permissions); }"
        )
        phase = (
            "{ EXPECT_TRUE(original_invalid_execute_alias_foreign(&work->authority, tty_process, foreign_process, tty_root, exact_permissions)); "
            "EXPECT_TRUE(original_physical_substitution_and_managed(&work->classification)); "
            "work->authority.phase = MAP_REQUESTED; work->classification = INVALID; "
            "EXPECT_TRUE(original_out_of_phase(&work->authority, &work->classification) == STATE && work->classification == INVALID); return true; }"
        )
        expected = (
            "{ const struct micros_process_handle tty_process = {3, 7}; "
            "const struct micros_process_handle foreign_process = {2, 5}; "
            "const uint64_t tty_root = ROOT; const uint32_t exact_permissions = READ | WRITE | USER; "
            "struct micros_tty_device_authority authority = { .process = tty_process, .root = tty_root, .phase = MAPPED }; "
            "enum micros_tty_device_leaf_class classification; EXPECT_TRUE(original_exact_range(&authority, &classification)); "
            "EXPECT_TRUE(original_invalid_execute_alias_foreign(&authority, tty_process, foreign_process, tty_root, exact_permissions)); "
            "EXPECT_TRUE(original_physical_substitution_and_managed(&classification)); authority.phase = MAP_REQUESTED; classification = INVALID; "
            "EXPECT_TRUE(original_out_of_phase(&authority, &classification) == STATE && classification == INVALID); return true; }"
        )
        declarations = tuple(tokens(declaration) for declaration, _ in DEVICE_FIELDS)
        self.assertEqual(tokens(expected), device_body_tokens(caller, phase, declarations))
        for changed in (
            caller.replace("struct tty_handoff_device_work work;", "struct tty_handoff_device_work work = {0};"),
            caller.replace("&work, tty_process", "&copy, tty_process"),
            caller.replace("foreign_process, tty_root, exact_permissions);", "foreign_process, tty_root, other_permissions);"),
            caller.replace("return tty_handoff_test_check_device_exclusions", "retain(&work); return tty_handoff_test_check_device_exclusions"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                device_body_tokens(changed, phase, declarations)
        with self.assertRaises(AssertionError):
            device_body_tokens(caller, phase.replace("return true;", "return false;"), declarations)
        with self.assertRaises(AssertionError):
            device_body_tokens(caller, phase, (tokens("uint64_t authority;"), declarations[1]))
        self.assertNotEqual(
            tokens(expected), device_body_tokens(caller.replace(".phase = MAPPED", ".phase = OWNED"), phase, declarations),
        )
        reordered = phase.replace(
            "work->classification = INVALID; EXPECT_TRUE(original_out_of_phase",
            "EXPECT_TRUE(original_out_of_phase",
        ).replace("return true;", "work->classification = INVALID; return true;")
        self.assertNotEqual(tokens(expected), device_body_tokens(caller, reordered, declarations))

    def test_handoff_originals_have_exact_task_owners_and_sole_five_field_fixture(self):
        for owner, names in ORIGINAL_FUNCTIONS[4].items():
            records = function_records(source(HANDOFF_SOURCES[owner]))
            for name in names:
                expected = name if name.startswith("micros_") else "tty_handoff_test_" + (name[5:] if name.startswith("test_") else name)
                self.assertIn(expected, set(records), HANDOFF_SOURCES[owner])
        header = source(PRIVATE_HEADERS[3])
        expected = tokens("struct " + HANDOFF_FIXTURE_TAG + " {" + "".join(HANDOFF_FIXTURE_FIELDS) + "};")
        self.assertIn(" ".join(expected), " ".join(tokens(header)))
        fixture = source(HANDOFF_SOURCES[1])
        self.assertEqual(("static struct tty_handoff_test_fixture fixture_state;",), tuple(file_scope_objects(fixture)))
        self.assertEqual(tokens("{ return &fixture_state; }"), tokens(
            "{" + function_records(fixture)[HANDOFF_FIXTURE_GETTER].body + "}",
        ))
        for path in (HANDOFF_SOURCES[0], *HANDOFF_SOURCES[2:]):
            self.assertEqual((), tuple(file_scope_objects(source(path))), path)

    def test_handoff_macro_and_acyclic_private_directions_remain_original(self):
        header = source(PRIVATE_HEADERS[3])
        start = header.index("#define EXPECT_TRUE")
        self.assertEqual(HANDOFF_MACRO_DIGEST, digest_tokens(tokens(header[start:header.index("\n\n", start)])))
        records = {
            name: (owner, record) for owner, path in enumerate(HANDOFF_SOURCES)
            for name, record in function_records(source(path)).items()
        }
        allowed = {0: {2, 3}, 1: set(), 2: set(), 3: {1}}
        for name, (owner, record) in records.items():
            destinations = {
                records[word][0] for word in tokens(record.body)
                if word in records and records[word][0] != owner
            }
            self.assertLessEqual(destinations, allowed[owner], name)

    def test_handoff_fixture_inverse_preserves_original_mock_family_addresses_and_pure_doubles(self):
        caller = (
            "{ struct tty_handoff_test_fixture *fixture = tty_handoff_test_fixture(); "
            "const struct micros_process_handle process = {3, 7}; "
            "fixture->runtime_process_handle = process; "
            "EXPECT_TRUE(original_observe(&fixture->runtime_process, fixture->runtime_thread_handle)); return true; }"
        )
        expected = tokens(
            "{ const struct micros_process_handle process = {3, 7}; runtime_process_handle = process; "
            "EXPECT_TRUE(original_observe(&runtime_process, runtime_thread_handle)); return true; }"
        )
        self.assertEqual(expected, handoff_fixture_body_tokens("test_runtime_lifecycle_validation", tokens(caller)))
        for changed in (
            caller.replace("tty_handoff_test_fixture()", "reset_fixture()"),
            caller.replace("fixture->runtime_process_handle", "fixture->unknown_field"),
            caller.replace("&fixture->runtime_process", "&other.fixture->runtime_process"),
            caller.replace("return true;", "retain(fixture); return true;"),
            caller.replace("return true;", "copy = *fixture; return true;"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                handoff_fixture_body_tokens("test_runtime_lifecycle_validation", tokens(changed))
        pure = tokens("{ return registry != NULL && profile_id == TTY && operation == 3; }")
        self.assertEqual(pure, handoff_fixture_body_tokens("micros_privilege_profile_allows_kernel_operation", pure))
        signature = "bool tty_handoff_test_device_leaf_classification(void) { return true; }"
        self.assertEqual(tokens("static bool test_device_leaf_classification(void)"), original_signature_tokens(
            4, "test_device_leaf_classification", function_records(signature)["tty_handoff_test_device_leaf_classification"],
        ))

    def test_model_validation_has_one_typed_resident_comparison_phase_and_original_root_observations(self):
        records = function_records(source(MODEL_SOURCES[6]))
        self.assertIn(MODEL_VALIDATE_PHASE, set(records))
        self.assertEqual(
            tokens("static bool " + MODEL_VALIDATE_PHASE + "(" + MODEL_VALIDATE_PARAMETERS + ")"),
            tokens(records[MODEL_VALIDATE_PHASE].signature),
        )
        self.assertLessEqual(records["tty_model_validate"].lines, 120)
        self.assertLessEqual(records[MODEL_VALIDATE_PHASE].lines, 120)

    def test_model_comparison_inverse_preserves_pointer_initializers_first_failure_and_final_reference_order(self):
        caller = (
            "{ struct tty_model_validation_work work; work.terminal = &model->service.terminal; "
            "work.reference = &model->reference; MODEL_EXPECT(model, original_production_validity()); "
            "MODEL_EXPECT(model, original_callback_and_irq_observations()); "
            "if (!tty_model_compare_resident_buffers(model, &work)) { return false; } "
            "MODEL_EXPECT(model, original_request_completions()); MODEL_EXPECT(model, original_crlf()); "
            "MODEL_EXPECT(model, !model->bus.expect_lf || tty_model_reference_output_pending(work.reference)); return true; }"
        )
        phase = (
            "{ MODEL_EXPECT(model, work->terminal->input_count == work->reference->input_count); "
            "for (work->index = 0; work->index < INPUT_CAPACITY; ++work->index) { "
            "MODEL_EXPECT(model, work->terminal->input[work->index] == work->reference->input[work->index]); } "
            "MODEL_EXPECT(model, original_echo_and_write_comparison(work->terminal, work->reference)); return true; }"
        )
        expected = (
            "{ const struct micros_tty_state *terminal = &model->service.terminal; "
            "const struct model_reference *reference = &model->reference; size_t index; "
            "MODEL_EXPECT(model, original_production_validity()); "
            "MODEL_EXPECT(model, original_callback_and_irq_observations()); "
            "MODEL_EXPECT(model, terminal->input_count == reference->input_count); "
            "for (index = 0; index < INPUT_CAPACITY; ++index) { "
            "MODEL_EXPECT(model, terminal->input[index] == reference->input[index]); } "
            "MODEL_EXPECT(model, original_echo_and_write_comparison(terminal, reference)); "
            "MODEL_EXPECT(model, original_request_completions()); MODEL_EXPECT(model, original_crlf()); "
            "MODEL_EXPECT(model, !model->bus.expect_lf || tty_model_reference_output_pending(reference)); return true; }"
        )
        declarations = tuple(tokens(declaration) for declaration, _ in MODEL_VALIDATE_FIELDS)
        self.assertEqual(tokens(expected), model_validation_body_tokens(caller, phase, declarations))
        for changed in (
            caller.replace("struct tty_model_validation_work work;", "struct tty_model_validation_work work = {0};"),
            caller.replace("model, &work)", "model, &copy)"),
            caller.replace("model, &work)", "other, &work)"),
            caller.replace(
                "if (!tty_model_compare_resident_buffers(model, &work)) { return false; }",
                "MODEL_EXPECT(model, tty_model_compare_resident_buffers(model, &work));",
            ),
            caller.replace("return true; }", "retain(&work); return true; }"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                model_validation_body_tokens(changed, phase, declarations)
        with self.assertRaises(AssertionError):
            model_validation_body_tokens(caller, phase.replace("MODEL_EXPECT", "return false; MODEL_EXPECT", 1), declarations)
        with self.assertRaises(AssertionError):
            model_validation_body_tokens(caller, phase, (tokens("struct micros_tty_state *terminal;"), *declarations[1:]))
        self.assertNotEqual(
            tokens(expected), model_validation_body_tokens(caller.replace("&model->reference", "&copied.reference"), phase, declarations),
        )
        swapped = caller.replace(
            "MODEL_EXPECT(model, original_crlf()); "
            "MODEL_EXPECT(model, !model->bus.expect_lf || tty_model_reference_output_pending(work.reference));",
            "MODEL_EXPECT(model, !model->bus.expect_lf || tty_model_reference_output_pending(work.reference)); "
            "MODEL_EXPECT(model, original_crlf());",
        )
        self.assertNotEqual(tokens(expected), model_validation_body_tokens(swapped, phase, declarations))

    def test_model_originals_have_exact_task_owners_and_one_automatic_root(self):
        for owner, names in ORIGINAL_FUNCTIONS[3].items():
            records = function_records(source(MODEL_SOURCES[owner]))
            for name in names:
                expected = "main" if name == "main" else "tty_model_" + (name[6:] if name.startswith("model_") else name)
                self.assertIn(expected, set(records), MODEL_SOURCES[owner])
        root = function_records(source(MODEL_SOURCES[0]))["tty_model_test_replayable_mixed_transition_model"]
        self.assertEqual(1, tokens(root.body).count("tty_model"))

    def test_model_types_constants_macro_and_owner_directions_remain_original(self):
        header = source(PRIVATE_HEADERS[2])
        prefix = header[header.index("#include"):header.index("#define MODEL_EXPECT")]
        constants = tuple(file_scope_objects(source(MODEL_SOURCES[1])))
        self.assertEqual(2, len(constants))
        self.assertEqual(MODEL_PREFIX_DIGEST, digest_tokens(model_prefix_tokens(prefix, constants)))
        start = header.index("#define MODEL_EXPECT")
        macro = header[start:header.index("\n\n", start)]
        self.assertEqual(MODEL_MACRO_DIGEST, digest_tokens(
            tuple(normal_names(3).get(token, token) for token in tokens(macro)),
        ))
        for path in (MODEL_SOURCES[0], *MODEL_SOURCES[2:]):
            self.assertEqual((), tuple(file_scope_objects(source(path))), path)
        records = {
            name: (owner, record) for owner, path in enumerate(MODEL_SOURCES)
            for name, record in function_records(source(path)).items()
        }
        check_model_owner_dependencies(records)

    def test_model_graph_admits_only_original_reference_observation_without_reverse_edge(self):
        owners = {
            1: "bool tty_model_failure(void) { return false; }",
            2: (
                "bool tty_model_reference_output_pending(void) { return original_pending; } "
                "void tty_model_reference_receive_byte(void) { original_mutation(); }"
            ),
            6: (
                "bool tty_model_validate(void) { MODEL_EXPECT(model, original_crlf); "
                "MODEL_EXPECT(model, !expect_lf || tty_model_reference_output_pending()); return true; }"
            ),
        }
        records = lambda sources: {
            name: (owner, record) for owner, text in sources.items()
            for name, record in function_records(text).items()
        }
        check_model_owner_dependencies(records(owners))
        for changed in (
            {**owners, 6: owners[6].replace("tty_model_reference_output_pending()", "tty_model_reference_receive_byte()")},
            {**owners, 2: owners[2].replace("original_pending", "tty_model_validate()")},
            {**owners, 1: owners[1].replace("false", "tty_model_reference_output_pending()")},
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                check_model_owner_dependencies(records(changed))

    def test_model_readonly_type_and_signature_fixture_rejects_shadow_state_and_changed_types(self):
        prefix = "extern const micros_endpoint_t tty_model_tty_endpoint; extern const micros_endpoint_t tty_model_vfs_endpoint;"
        constants = (
            "const micros_endpoint_t tty_model_tty_endpoint = UINT32_C(0x00007003);",
            "const micros_endpoint_t tty_model_vfs_endpoint = UINT32_C(0x00009005);",
        )
        expected = tokens(
            "static const micros_endpoint_t tty_endpoint = UINT32_C(0x00007003);"
            "static const micros_endpoint_t vfs_endpoint = UINT32_C(0x00009005);"
        )
        self.assertEqual(expected, model_prefix_tokens(prefix, constants))
        with self.assertRaises(AssertionError):
            model_prefix_tokens(prefix.replace("extern const", "extern"), constants)
        with self.assertRaises(AssertionError):
            model_prefix_tokens(prefix, constants[:1])
        with self.assertRaises(AssertionError):
            model_prefix_tokens(prefix, (*constants, constants[0]))
        self.assertNotEqual(expected, model_prefix_tokens(prefix, (constants[0].replace("7003", "7004"), constants[1])))
        owner = "bool tty_model_failure(const char *expression, int line, const struct tty_model *model) { return false; }"
        signature = original_signature_tokens(3, "model_failure", function_records(owner)["tty_model_failure"])
        self.assertEqual(tokens("static bool model_failure(const char *expression, int line, const struct tty_model *model)"), signature)
        with self.assertRaises(AssertionError):
            original_signature_tokens(3, "model_failure", function_records("static " + owner)["tty_model_failure"])

    def test_write_publication_has_one_typed_void_phase_after_all_original_rejections(self):
        records = function_records(source(SERVICE_SOURCES[3]))
        self.assertIn(WRITE_PHASE, set(records))
        self.assertEqual(tokens("static void " + WRITE_PHASE + "(" + WRITE_PARAMETERS + ")"), tokens(records[WRITE_PHASE].signature))
        self.assertLessEqual(records["tty_service_handle_submit_write"].lines, 120)
        self.assertLessEqual(records[WRITE_PHASE].lines, 120)

    def test_write_inverse_preserves_busy_retry_preflight_copy_accept_and_publication_order(self):
        caller = (
            "{ struct tty_service_write_work work; work.error = original_submit(state, request); "
            "if (work.error != OK) { return work.error; } "
            "if (completion_active(state)) { return original_completion_busy(state); } "
            "if (resident_only(state)) { original_retry(state); return original_resident_busy(state); } "
            "work.copy_result = original_grant_copy(io, NULL, 0); original_invariant(state); "
            "work.copy_result = original_grant_copy(io, state->write, request->count); "
            "original_accept(state, &work.effects); "
            "tty_service_publish_write(state, request, action, &work); return work.error; }"
        )
        phase = (
            "{ state->last_id = request->id; original_completion(state, request); "
            "work->error = original_reply(action); "
            "if (work->error == OK) { action->effects = work->effects; action->event = COMPLETION; } }"
        )
        expected = (
            "{ enum micros_tty_service_copy_result copy_result; enum micros_tty_service_error error; "
            "struct micros_tty_effects effects; error = original_submit(state, request); "
            "if (error != OK) { return error; } "
            "if (completion_active(state)) { return original_completion_busy(state); } "
            "if (resident_only(state)) { original_retry(state); return original_resident_busy(state); } "
            "copy_result = original_grant_copy(io, NULL, 0); original_invariant(state); "
            "copy_result = original_grant_copy(io, state->write, request->count); "
            "original_accept(state, &effects); state->last_id = request->id; original_completion(state, request); "
            "error = original_reply(action); "
            "if (error == OK) { action->effects = effects; action->event = COMPLETION; } return error; }"
        )
        declarations = tuple(tokens(declaration) for declaration, _ in WRITE_FIELDS)
        self.assertEqual(tokens(expected), write_body_tokens(caller, phase, declarations))
        for changed in (
            caller.replace("struct tty_service_write_work work;", "struct tty_service_write_work work = {0};"),
            caller.replace("state, request, action, &work", "state, request, action, &copy"),
            caller.replace("state, request, action, &work", "state, other, action, &work"),
            caller.replace("return work.error; }", "retain(&work); return work.error; }"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                write_body_tokens(changed, phase, declarations)
        with self.assertRaises(AssertionError):
            write_body_tokens(caller, phase.replace("state->last_id", "return; state->last_id"), declarations)
        with self.assertRaises(AssertionError):
            write_body_tokens(caller, phase, (tokens("uint64_t copy_result;"), *declarations[1:]))
        for changed in (
            phase.replace("request->id", "0"),
            phase.replace("action->event = COMPLETION;", "action->event = READY;"),
            phase.replace("work->error == OK", "work->error != OK"),
        ):
            self.assertNotEqual(tokens(expected), write_body_tokens(caller, changed, declarations))
        binding = "tty_service_publish_write(state, request, action, &work);"
        reordered = caller.replace(binding, "").replace("original_accept(state, &work.effects);", binding + " original_accept(state, &work.effects);")
        self.assertNotEqual(tokens(expected), write_body_tokens(reordered, phase, declarations))

    def test_service_private_symbols_do_not_collide_with_retained_real_elf_neighbors(self):
        paths = (
            *SERVICE_SOURCES, "servers/tty/tty_service.c", "servers/tty/tty_control.c",
            "servers/tty/tty_core.c", "servers/tty/tty_uart.c",
        )
        locations = {}
        for path in paths:
            for name in function_records(source(path)):
                self.assertNotIn(name, locations, name + " also defined in " + locations.get(name, ""))
                locations[name] = path

    def test_service_private_directions_preserve_explicit_state_and_no_facade_cycle(self):
        records = {
            name: (owner, record) for owner, path in enumerate(SERVICE_SOURCES)
            for name, record in function_records(source(path)).items()
        }
        allowed = {0: {1, 2, 3, 4}, 1: set(), 2: {1, 4}, 3: {1, 4}, 4: {1}}
        for name, (owner, record) in records.items():
            destinations = {
                records[word][0] for word in tokens(record.body)
                if word in records and records[word][0] != owner
            }
            self.assertLessEqual(destinations, allowed[owner], name)

    def test_service_originals_have_exact_explicit_stateless_owners_and_single_magic_contract(self):
        private_names = {original: private for private, original in normal_names(2).items()}
        for owner, names in ORIGINAL_FUNCTIONS[2].items():
            records = function_records(source(SERVICE_SOURCES[owner]))
            for name in names:
                expected = private_names.get(name, name)
                self.assertIn(expected, set(records), SERVICE_SOURCES[owner])
        header = source(PRIVATE_HEADERS[1])
        start = header.index("#define MICROS_TTY_SERVICE_INITIALIZATION_MAGIC")
        end = header.index("\n\n", start)
        self.assertEqual(SERVICE_MAGIC_DIGEST, digest_tokens(tokens(header[start:end])))
        for path in SERVICE_SOURCES:
            self.assertEqual((), tuple(file_scope_objects(source(path))), path)

    def test_service_private_signature_fixture_preserves_original_full_types_and_no_state(self):
        owner = (
            "enum micros_tty_service_error tty_service_handle_cancel("
            "struct micros_tty_service_state *state, const struct micros_tty_request *request, "
            "struct micros_tty_service_reply_action *action) { return ORIGINAL_RESULT; }"
        )
        record = function_records(owner)["tty_service_handle_cancel"]
        expected = tokens(
            "static enum micros_tty_service_error handle_cancel(struct micros_tty_service_state *state, "
            "const struct micros_tty_request *request, struct micros_tty_service_reply_action *action)"
        )
        self.assertEqual(expected, original_signature_tokens(2, "handle_cancel", record))
        with self.assertRaises(AssertionError):
            original_signature_tokens(2, "handle_cancel", function_records("static " + owner)["tty_service_handle_cancel"])
        changed = function_records(owner.replace("const struct micros_tty_request *request", "struct micros_tty_request *request"))["tty_service_handle_cancel"]
        self.assertNotEqual(expected, original_signature_tokens(2, "handle_cancel", changed))

    def test_live_binding_has_typed_ordered_predicates_after_original_record_resolution(self):
        owner = source(RUNTIME_SOURCES[2])
        records = function_records(owner)
        self.assertIn(LIVE_PHASE, set(records))
        self.assertEqual(
            tokens("static void " + LIVE_PHASE + "(" + LIVE_PARAMETERS + ")"), tokens(records[LIVE_PHASE].signature),
        )
        self.assertLessEqual(records["tty_handoff_live_binding_matches"].lines, 120)
        self.assertLessEqual(records[LIVE_PHASE].lines, 120)

    def test_live_binding_inverse_keeps_resolution_before_two_predicates_and_original_selector(self):
        caller = (
            "{ struct tty_handoff_live_binding work; "
            "if (!original_resolve(registry, objects, &work.process, &work.thread, &work.endpoint)) { return false; } "
            "tty_handoff_classify_live_binding(context, binding, transition, &work); "
            "return original_phase_and_deadline(context, work.prepared, work.active); }"
        )
        phase = (
            "{ work->prepared = original_prepared(work->process, work->thread, work->endpoint, transition); "
            "work->active = original_active(context, binding, transition, work->process, work->thread, work->endpoint); }"
        )
        expected = (
            "{ const struct micros_process *process; const struct micros_thread *thread; "
            "const struct micros_endpoint_record *endpoint; bool prepared; bool active; "
            "if (!original_resolve(registry, objects, &process, &thread, &endpoint)) { return false; } "
            "prepared = original_prepared(process, thread, endpoint, transition); "
            "active = original_active(context, binding, transition, process, thread, endpoint); "
            "return original_phase_and_deadline(context, prepared, active); }"
        )
        declarations = tuple(tokens(declaration) for declaration, _ in LIVE_FIELDS)
        self.assertEqual(tokens(expected), live_binding_body_tokens(caller, phase, declarations))
        for changed in (
            caller.replace("struct tty_handoff_live_binding work;", "struct tty_handoff_live_binding work = {0};"),
            caller.replace("context, binding, transition, &work", "other, binding, transition, &work"),
            caller.replace("context, binding, transition, &work", "context, binding, transition, &copy"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                live_binding_body_tokens(changed, phase, declarations)
        with self.assertRaises(AssertionError):
            live_binding_body_tokens(caller, phase.replace("work->active =", "return; work->active ="), declarations)
        self.assertNotEqual(
            tokens(expected), live_binding_body_tokens(caller, phase.replace("original_prepared", "weakened_prepared"), declarations),
        )

    def test_runtime_state_leaf_keeps_actual_owner_directions_acyclic(self):
        records = {
            name: (owner, record) for owner, path in enumerate(RUNTIME_SOURCES)
            for name, record in function_records(source(path)).items()
        }
        allowed = {0: {1, 2, 3}, 1: set(), 2: {1}, 3: {1}}
        for name, (owner, record) in records.items():
            destinations = {
                records[word][0] for word in tokens(record.body)
                if word in records and records[word][0] != owner
            }
            self.assertLessEqual(destinations, allowed[owner], name)

    def test_runtime_context_has_only_two_original_fields_in_its_one_state_leaf(self):
        header = source(PRIVATE_HEADERS[0])
        expected = tokens("struct tty_handoff_context {" + "".join(RUNTIME_CONTEXT_FIELDS) + "};")
        self.assertIn(" ".join(expected), " ".join(tokens(header)))
        state = source(RUNTIME_SOURCES[1])
        self.assertEqual(
            ("static struct tty_handoff_context context_state;",), tuple(file_scope_objects(state)),
        )
        self.assertEqual(
            tokens("{ return &context_state; }"),
            tokens("{" + function_records(state)[RUNTIME_CONTEXT_GETTER].body + "}"),
        )
        for path in (RUNTIME_SOURCES[0], *RUNTIME_SOURCES[2:]):
            self.assertEqual((), tuple(file_scope_objects(source(path))), path)

    def test_runtime_context_inverse_keeps_original_object_bindings_lifetimes_and_pure_inputs(self):
        caller = (
            "{ struct tty_handoff_context *context = tty_handoff_context(); "
            "if (!binding_identity_matches(context, bootstrap)) { return INVARIANT; } "
            "copy_bytes(&context->tty_handoff_state, &candidate, sizeof(context->tty_handoff_state)); "
            "context->tty_bootstrap_state = bootstrap; return OK; }"
        )
        expected = (
            "{ if (!binding_identity_matches(bootstrap)) { return INVARIANT; } "
            "copy_bytes(&tty_handoff_state, &candidate, sizeof(tty_handoff_state)); "
            "tty_bootstrap_state = bootstrap; return OK; }"
        )
        original = "micros_tty_handoff_runtime_prepare"
        self.assertEqual(tokens(expected), runtime_context_body_tokens(original, tokens(caller)))
        for changed in (
            caller.replace("tty_handoff_context()", "reset_context()"),
            caller.replace("binding_identity_matches(context,", "binding_identity_matches(other,"),
            caller.replace("context->tty_handoff_state", "context->extra"),
            caller.replace("return OK;", "copy = *context; return OK;"),
            caller.replace("return OK;", "clear_bytes(context, sizeof(*context)); return OK;"),
            caller.replace("context->tty_bootstrap_state", "record.context->tty_bootstrap_state"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                runtime_context_body_tokens(original, tokens(changed))
        signature = tokens(
            "static bool binding_identity_matches(struct tty_handoff_context *context, "
            "const struct micros_bootstrap_control_state *bootstrap)"
        )
        self.assertEqual(
            tokens("static bool binding_identity_matches(const struct micros_bootstrap_control_state *bootstrap)"),
            runtime_context_signature_tokens(signature),
        )
        for changed in (
            "static bool binding_identity_matches(void *context, const struct micros_bootstrap_control_state *bootstrap)",
            "static bool binding_identity_matches(struct tty_handoff_context *other, const struct micros_bootstrap_control_state *bootstrap)",
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                runtime_context_signature_tokens(tokens(changed))
        pure = tokens("{ return left.slot == right.slot && left.generation == right.generation; }")
        self.assertEqual(pure, runtime_context_body_tokens("process_handles_equal", pure))

    def test_independent_recipe_fixture_preserves_all_eight_literal_consumers(self):
        expected = (
            "add_executable(micros before kernel/tty_handoff_runtime.c after)\n"
            "add_executable(tty_test kernel/tty_handoff_runtime.c tests/host/tty_handoff_test.c)\n"
            "add_executable(tty_service_test servers/tty/tty_service_core.c)\n"
            "add_executable(tty_model_test servers/tty/tty_service_core.c tests/host/tty_model_test.c)\n"
            "add_executable(tty_control_syscall_test tests/host/tty_control_syscall_test.c)\n"
            "if(TTY_IMAGE)\n"
            "micros_add_bootstrap_user(micros_tty_service servers/tty/tty_service_core.c)\nendif()\n"
        )
        current = (
            "".join(source_set_block(name, paths) + "\n\n" for name, paths in SOURCE_SETS.items())
            + "add_executable(micros before ${MICROS_TTY_HANDOFF_RUNTIME_SOURCES} after)\n"
            "add_executable(tty_test ${MICROS_TTY_HANDOFF_RUNTIME_SOURCES} ${MICROS_TTY_HANDOFF_HOST_TEST_SOURCES})\n"
            "add_executable(tty_service_test ${MICROS_TTY_SERVICE_CORE_SOURCES})\n"
            "add_executable(tty_model_test ${MICROS_TTY_SERVICE_CORE_SOURCES} ${MICROS_TTY_MODEL_TEST_SOURCES})\n"
            "add_executable(tty_control_syscall_test ${MICROS_TTY_CONTROL_SYSCALL_HOST_TEST_SOURCES})\n"
            "if(TTY_IMAGE)\n"
            "micros_add_bootstrap_user(micros_tty_service ${MICROS_TTY_SERVICE_CORE_SOURCES})\nendif()\n"
        )
        self.assertEqual(expected, tty_source_cmake_projection(current))
        for changed in (
            current.replace("kernel/tty_handoff_runtime_state.c", "kernel/unreviewed_state.c", 1),
            current.replace("${MICROS_TTY_MODEL_TEST_SOURCES}", "tests/host/tty_model_test.c", 1),
            current + "\nconsumer(${MICROS_TTY_HANDOFF_RUNTIME_SOURCES})\n",
            current.replace(source_set_block("MICROS_TTY_SERVICE_CORE_SOURCES", SERVICE_SOURCES) + "\n\n", "", 1),
        ):
            with self.subTest(changed=changed[-80:]), self.assertRaises(AssertionError):
                tty_source_cmake_projection(changed)
        self.assertNotEqual(
            expected, tty_source_cmake_projection(current.replace(" before ", " changed_before ")),
        )

    def test_independent_metadata_fixture_keeps_unrelated_rows_and_algorithms(self):
        expected = (
            "PATH_CLASSIFICATION_EQUIVALENTS = (('old', ('old_alias',)),)\n"
            "HOST_UNIT_ONLY_INPUTS = ('old_host',)\n"
            "def algorithm(value):\n    return value + 1\n"
        )
        current = (
            "PATH_CLASSIFICATION_EQUIVALENTS = " + repr((("old", ("old_alias",)), *alias_rows())) + "\n"
            "HOST_UNIT_ONLY_INPUTS = " + repr(("old_host", STRUCTURAL_TEST_PATH)) + "\n"
            "def algorithm(value):\n    return value + 1\n"
        )
        fingerprint = lambda text: ast_dump(ast.parse(text))
        self.assertEqual(fingerprint(expected), ast_dump(tty_planner_projection(ast.parse(current))))
        self.assertEqual(fingerprint(expected), ast_dump(tty_planner_projection(ast.parse(expected))))
        for changed in (
            current.replace("kernel/tty_handoff_runtime_state.c", "kernel/unknown_runtime.c"),
            current.replace(STRUCTURAL_TEST_PATH, "tests/host/other.py"),
            current.replace("'kernel/tty_handoff_runtime_state.c', ", ""),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                tty_planner_projection(ast.parse(changed))
        self.assertNotEqual(
            fingerprint(expected),
            ast_dump(tty_planner_projection(ast.parse(current.replace("value + 1", "value - 1")))),
        )

    def test_runtime_byte_primitives_have_their_sole_state_leaf_owner(self):
        facade = function_records(kernel_layout_source.read_path(ROOT / "kernel/tty_handoff_runtime.c"))
        for name in ("clear_bytes", "copy_bytes"):
            self.assertNotIn(name, set(facade))
        state = function_records(kernel_layout_source.read_path(ROOT / "kernel/tty_handoff_runtime_state.c"))
        self.assertIn("tty_handoff_clear_bytes", state)
        self.assertIn("tty_handoff_copy_bytes", state)

    def test_immutable_inventory_and_explicit_pending_work_are_complete(self):
        self.assertEqual(156, sum(len(values) for values in BASELINE_BODIES.values()))
        self.assertEqual(116, len(INITIAL_SYMBOL_OWNER_MIGRATION))
        self.assertEqual(7, len(LONG_FUNCTIONS))
        self.assertEqual(8, len(REQUIRED_TASKS))
        self.assertEqual(3, len(GENERATOR_FUNCTIONS))
        self.assertEqual(37, len(STABLE_HASHES))
        self.assertEqual((), SYMBOL_OWNER_MIGRATION)
        self.assertEqual((), FUNCTION_TASK_MIGRATION)

    def test_all_156_original_bodies_signatures_and_current_owners_remain_exact(self):
        for family, owners in ORIGINAL_FUNCTIONS.items():
            locations = original_locations(family)
            signatures = []
            for original in ORIGINAL_ORDER[family]:
                path, record = locations[original]
                final = next(owner for owner, names in owners.items() if original in names)
                current = 0 if (family, original) in SYMBOL_OWNER_MIGRATION else final
                with self.subTest(family=family, original=original):
                    self.assertEqual(FAMILY_PATHS[family][current], path)
                    self.assertEqual((), record.guards)
                    self.assertEqual(BASELINE_BODIES[family][original], original_body_digest(family, record))
                    signatures.append((original, original_signature_tokens(family, original, record)))
            self.assertEqual(BASELINE_SIGNATURES[family], digest_tokens(signatures))

    def test_only_original_pending_long_functions_exceed_the_signal(self):
        actual = {
            (family, normal_names(family).get(name, name))
            for family, paths in enumerate(FAMILY_PATHS) for path in paths
            for name, record in function_records(source(path)).items() if record.lines > 120
        }
        self.assertEqual(set(LONG_FUNCTIONS) & set(FUNCTION_TASK_MIGRATION), actual)

    def test_original_generator_neighbors_and_shared_inspector_remain_exact(self):
        generator = generator_module_projection(source(GENERATOR_PATH))
        self.assertEqual(GENERATOR_AST_DIGEST, digest_bytes(ast_dump(generator).encode()))
        functions = {node.name: node for node in generator.body if isinstance(node, ast.FunctionDef)}
        self.assertEqual(set(GENERATOR_FUNCTIONS), set(functions))
        for name, expected in GENERATOR_FUNCTIONS.items():
            self.assertEqual(expected, digest_bytes(ast_dump(functions[name]).encode()))
        for path, expected in STABLE_HASHES.items():
            from tests.host.test_qemu_runner_source_layout import restored_runner_source

            value = restored_runner_source().encode() if path == "tools/run_qemu_smoke.py" else kernel_layout_source.read_bytes_path(ROOT / path)
            self.assertEqual(expected, digest_bytes(value), path)
        self.assertEqual(SHARED_INSPECTOR_DIGEST, digest_bytes(kernel_layout_source.read_bytes_path(ROOT / "tests/host/c_source_records.py")))

    def test_exact_source_sets_private_headers_consumers_and_recipe_inverse(self):
        self.assertEqual(26, sum(map(len, FAMILY_PATHS)))
        cmake = source("CMakeLists.txt")
        for paths in FAMILY_PATHS:
            for path in paths:
                self.assertTrue(kernel_layout_source.is_file_path(ROOT / path), path)
        for name, paths in SOURCE_SETS.items():
            self.assertEqual((source_set_block(name, paths),), cmake_commands(cmake, "set", name))
        for header in PRIVATE_HEADERS:
            self.assertTrue(kernel_layout_source.is_file_path(ROOT / header))
            self.assertEqual({}, function_records(source(header)))
        self.assertEqual(BASE_CMAKE_DIGEST, digest_bytes(restored_cmake(cmake).encode()))
        self.assertEqual(
            digest_bytes(cmake.encode()),
            kernel_layout_source.original_cmake_recipe_digest(
                validation_context.RECIPE_DIGESTS["CMakeLists.txt"]
            ),
        )
        for target, variable in (
            ("micros", "MICROS_TTY_HANDOFF_RUNTIME_SOURCES"), ("tty_test", "MICROS_TTY_HANDOFF_RUNTIME_SOURCES"),
            ("tty_service_test", "MICROS_TTY_SERVICE_CORE_SOURCES"), ("tty_model_test", "MICROS_TTY_SERVICE_CORE_SOURCES"),
            ("tty_model_test", "MICROS_TTY_MODEL_TEST_SOURCES"), ("tty_test", "MICROS_TTY_HANDOFF_HOST_TEST_SOURCES"),
            ("tty_control_syscall_test", "MICROS_TTY_CONTROL_SYSCALL_HOST_TEST_SOURCES"),
        ):
            self.assertEqual(1, cmake_commands(cmake, "add_executable", target)[0].count("${" + variable + "}"))
        self.assertEqual(1, cmake_commands(cmake, "micros_add_bootstrap_user", "micros_tty_service")[0].count("${MICROS_TTY_SERVICE_CORE_SOURCES}"))

    def test_only_declared_tool_metadata_and_one_recipe_pin_change(self):
        planner = tty_planner_projection(ast.parse(source("tools/validation_plan.py")))
        self.assertEqual(PLANNER_AST_DIGEST, digest_bytes(ast_dump(planner).encode()))
        context = context_ast_without_recipe_pin(source("tools/validation_context.py"))
        self.assertEqual(CONTEXT_AST_DIGEST, digest_bytes(ast_dump(context).encode()))

    def test_each_alias_retains_all_four_independent_validation_dimensions(self):
        for representative, paths in alias_rows():
            expected = (
                validation_plan.requires_full_tier([representative]),
                validation_plan.affected_workflows([representative]),
                validation_plan.affected_slow_models([representative]),
                validation_plan.affected_image_workflows([representative]),
            )
            for path in paths:
                self.assertEqual(expected, (
                    validation_plan.requires_full_tier([path]), validation_plan.affected_workflows([path]),
                    validation_plan.affected_slow_models([path]), validation_plan.affected_image_workflows([path]),
                ), path)
        self.assertEqual((False, [], [], []), (
            validation_plan.requires_full_tier([STRUCTURAL_TEST_PATH]),
            validation_plan.affected_workflows([STRUCTURAL_TEST_PATH]),
            validation_plan.affected_slow_models([STRUCTURAL_TEST_PATH]),
            validation_plan.affected_image_workflows([STRUCTURAL_TEST_PATH]),
        ))


if __name__ == "__main__":
    unittest.main()
