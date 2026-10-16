from __future__ import annotations

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
from tests.host.test_pm_service_source_layout import static_local_storage
from tests.host.test_vm_service_source_layout import c_declaration_text, file_scope_objects
from tools import validation_plan
from tests.host import kernel_layout_source


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
FUNCTION_REVIEW_LINE_LIMIT = 120

SOURCE_SETS = {
    "MICROS_IPC_CORE_SOURCES": (
        "kernel/ipc_common.c",
        "kernel/ipc_queue.c",
        "kernel/ipc_completion.c",
        "kernel/ipc_tokens.c",
        "kernel/ipc_notifications.c",
        "kernel/ipc_deadlock.c",
        "kernel/ipc_rendezvous.c",
        "kernel/ipc_close.c",
        "kernel/ipc_reply.c",
        "kernel/ipc.c",
    ),
    "MICROS_IPC_SYSCALL_SOURCES": (
        "kernel/ipc_syscall_result.c",
        "kernel/ipc_syscall_request.c",
        "kernel/ipc_syscall.c",
    ),
    "MICROS_IPC_OPERATION_HOST_TEST_SOURCES": (
        "tests/host/ipc_operation_test.c",
        "tests/host/ipc_operation_test_fixture.c",
        "tests/host/ipc_send_operation_test.c",
        "tests/host/ipc_receive_operation_test.c",
    ),
    "MICROS_IPC_CALL_HOST_TEST_SOURCES": (
        "tests/host/ipc_call_test.c",
        "tests/host/ipc_call_test_fixture.c",
        "tests/host/ipc_call_delivery_test.c",
        "tests/host/ipc_call_validation_test.c",
    ),
    "MICROS_IPC_REPLY_HOST_TEST_SOURCES": (
        "tests/host/ipc_reply_test.c",
        "tests/host/ipc_reply_test_fixture.c",
        "tests/host/ipc_reply_authority_test.c",
        "tests/host/ipc_reply_validation_test.c",
        "tests/host/ipc_reply_receive_test.c",
    ),
    "MICROS_IPC_NOTIFY_HOST_TEST_SOURCES": (
        "tests/host/ipc_notify_test.c",
        "tests/host/ipc_notify_test_fixture.c",
        "tests/host/ipc_notify_delivery_test.c",
        "tests/host/ipc_kernel_notify_test.c",
        "tests/host/ipc_notify_validation_test.c",
        "tests/host/ipc_notify_model_test.c",
    ),
    "MICROS_IPC_CLOSE_HOST_TEST_SOURCES": (
        "tests/host/ipc_close_test.c",
        "tests/host/ipc_close_test_fixture.c",
        "tests/host/ipc_close_cancellation_test.c",
        "tests/host/ipc_close_validation_test.c",
        "tests/host/ipc_close_model_test.c",
    ),
    "MICROS_IPC_MODEL_HOST_TEST_SOURCES": (
        "tests/host/ipc_model_test.c",
        "tests/host/ipc_model_test_common.c",
        "tests/host/ipc_model_test_fixture.c",
        "tests/host/ipc_model_test_trace.c",
        "tests/host/ipc_model_reference_authority.c",
        "tests/host/ipc_model_reference_scheduler.c",
        "tests/host/ipc_model_reference_ipc_state.c",
        "tests/host/ipc_model_reference_rendezvous.c",
        "tests/host/ipc_model_reference_notifications.c",
        "tests/host/ipc_model_reference_lifecycle.c",
        "tests/host/ipc_model_reference.c",
        "tests/host/ipc_model_test_production.c",
        "tests/host/ipc_model_test_step.c",
        "tests/host/ipc_model_test_actions.c",
        "tests/host/ipc_model_test_prelude.c",
    ),
    "MICROS_IPC_QEMU_TEST_SOURCES": (
        "kernel/ipc_test.c",
        "kernel/ipc_test_fixture.c",
        "kernel/ipc_test_setup.c",
        "kernel/ipc_test_scenarios.c",
        "kernel/ipc_test_report.c",
    ),
    "MICROS_IPC_ECALL_QEMU_TEST_SOURCES": (
        "kernel/ipc_ecall_test.c",
        "kernel/ipc_ecall_test_fixture.c",
        "kernel/ipc_ecall_test_setup.c",
        "kernel/ipc_ecall_test_report.c",
    ),
    "MICROS_IPC_SYSCALL_QEMU_TEST_SOURCES": (
        "kernel/ipc_syscall_test.c",
        "kernel/ipc_syscall_test_fixture.c",
        "kernel/ipc_syscall_test_setup.c",
        "kernel/ipc_syscall_test_commands.c",
        "kernel/ipc_syscall_test_scenarios.c",
        "kernel/ipc_syscall_test_hooks.c",
        "kernel/ipc_syscall_test_report.c",
    ),
}

PRIVATE_HEADERS = {
    "MICROS_IPC_CORE_SOURCES": "kernel/ipc_internal.h",
    "MICROS_IPC_SYSCALL_SOURCES": "kernel/ipc_syscall_internal.h",
    "MICROS_IPC_OPERATION_HOST_TEST_SOURCES": "tests/host/ipc_operation_test_internal.h",
    "MICROS_IPC_CALL_HOST_TEST_SOURCES": "tests/host/ipc_call_test_internal.h",
    "MICROS_IPC_REPLY_HOST_TEST_SOURCES": "tests/host/ipc_reply_test_internal.h",
    "MICROS_IPC_NOTIFY_HOST_TEST_SOURCES": "tests/host/ipc_notify_test_internal.h",
    "MICROS_IPC_CLOSE_HOST_TEST_SOURCES": "tests/host/ipc_close_test_internal.h",
    "MICROS_IPC_MODEL_HOST_TEST_SOURCES": "tests/host/ipc_model_test_internal.h",
    "MICROS_IPC_QEMU_TEST_SOURCES": "kernel/ipc_test_internal.h",
    "MICROS_IPC_ECALL_QEMU_TEST_SOURCES": "kernel/ipc_ecall_test_internal.h",
    "MICROS_IPC_SYSCALL_QEMU_TEST_SOURCES": "kernel/ipc_syscall_test_internal.h",
}

STABLE_HASHES = {
    "include/micros/ipc.h":
        "14499e75881a01d26113e23e674b1ffb0df34633023d2bda2098d3e9acb89f1c",
    "include/micros/ipc_core.h":
        "095402661724045179afe0316865ce751cda4e7a61b7c451234ca2ac5b2cef6a",
    "include/micros/ipc_abi.h":
        "ddf21e7cf2752908e7297a051e0c74bc8db0a37c8f3487413147961fca4eb5c3",
    "include/micros/ipc_runtime.h":
        "995cabdf5ffd3faf64a39617c010dc0879f06e93e57039f506568e7209172fc2",
    "include/micros/ipc_buffer.h":
        "cae685218606732d41340f88978ae36bcc857946e6328f2fc5431e2ba1e79928",
    "kernel/ipc_syscall.h":
        "ef87f9ebe808d7b3ce5c0f6d997ded40d4aca26b7f90271959b3daca3f655406",
    "kernel/ipc_syscall_test.h":
        "c918d24e10ea7e1aae10d20e6bc9dfba446df4e0833353d935e7a6e15e4c189d",
    "kernel/ipc_ecall_test.h":
        "ff45ddc386e705731722942c17183da09afad743caafc5231fc4f44a12622e43",
    "kernel/ipc_syscall_panic_test.h":
        "3035da2b9b5a6e0501078fb78b96fd8c048c8623c1ba598fe35f6257a8dcd6e5",
    "kernel/ipc_runtime_internal.h":
        "da59e3c038efd7214ab914041ee06ae77c74bc350b60eead84f84e09d422eb6f",
    "kernel/ipc_buffer_internal.h":
        "91291137fd7977c7004fcfd18412de3edad489d1d2124bfd182988b5ccf80f1d",
    "kernel/ipc_abi.c":
        "a2031d2f993f92b564cd170ece512252b5bb708eae94857374061bbc2f8482e7",
    "kernel/ipc_buffer.c":
        "233710a139fb8c6ff551d99b4eb83bdddb708ebe451a911096c1b98f5281e01b",
    "kernel/ipc_runtime.c":
        "c602cbf941bde404a27d88e4b19391d7c2ffa795f1dc730ae51b79e759b8ac41",
    "kernel/ipc_syscall_panic_test.c":
        "88e95b6ee296cdde729c3c78f71e089ee61b96ca2d37138feb30388c7948bdf6",
    "arch/riscv64/ipc_syscall_test.S":
        "3988c75ce3715778e55d6ddecc109e3b42d6ded7896fca122eb61c5f809a93ef",
    "arch/riscv64/ipc_ecall_test.S":
        "024231cb6d2ce2007eb895cdc69b199c5a7bc506996c022754f50a53275bafcd",
    "arch/riscv64/ipc_syscall_panic_test.S":
        "8c9b737286f94189e511e445664f85ff6fc02b0056c2b6bd3b2c4458a425e559",
    "tests/host/ipc_abi_test.c":
        "ef789f42a902c9de72c0effdb2bc0e964d82e490ab50b9e41011827084d5dd40",
    "tests/host/ipc_state_test.c":
        "5dcc08e8033b17aede4bf316590d7edd88bb30490d7b36de0a93239074f9d9d2",
    "tests/host/ipc_queue_test.c":
        "9cb508b99fd6a6b3950dac2e42802810d26a77f1e04d4a6d25ed15c4bca5fb0c",
    "tests/host/ipc_queue_mutation_test.c":
        "ae77a256ef1498dec2a1c91491b63a531039c6146bece6262c607c517a561e72",
    "tests/host/ipc_delivery_test.c":
        "348821fb05b57ec8250f3d126ec46178fd7cd082fd143ec3a7b15485db5b9651",
    "tests/host/ipc_deadlock_test.c":
        "04e0c132e40ed8ac59a3658ef6e22118f25879706e85a9c453cb37cadd8a1ad3",
    "tests/host/ipc_current_guard_test.c":
        "b023a05e9f0bd003383aa8e85901ec5215163136b9caf88c329ef9b2e8023220",
    "tests/host/ipc_model_main.c":
        "451170f1a93a19ac85d347121257d89817de1ff2e928990d514fc3b719a813f3",
}

FINAL_OWNER_GROUPS = {
    "kernel/ipc_common.c": ("micros_ipc_thread_has_staged_kernel_notification",),
    "kernel/ipc_completion.c": ("micros_ipc_stage_no_message_completion",),
    "kernel/ipc_notifications.c": (
        "micros_ipc_notify", "micros_ipc_inject_kernel_notification",
        "micros_ipc_prepare_kernel_notification",
        "micros_ipc_commit_kernel_notification_prevalidated",
    ),
    "kernel/ipc_rendezvous.c": (
        "micros_ipc_sender_enqueue", "micros_ipc_receiver_enqueue",
        "micros_ipc_sender_commit_delivery", "micros_ipc_receiver_commit_delivery",
    ),
    "kernel/ipc_close.c": ("micros_ipc_endpoint_close",),
    "kernel/ipc_reply.c": (
        "micros_ipc_reply", "micros_ipc_reply_expected_caller", "micros_ipc_reply_receive",
    ),
    "tests/host/ipc_send_operation_test.c": (
        "test_authorized_send_blocks_with_snapshot",
        "test_authorized_send_matches_first_receiver", "test_send_rejections_are_atomic",
    ),
    "tests/host/ipc_receive_operation_test.c": (
        "test_authorized_receive_blocks", "test_receive_specific_and_any_fifo",
        "test_receive_rejections_are_atomic",
    ),
    "tests/host/ipc_call_delivery_test.c": (
        "test_call_delivers_to_waiting_receiver", "test_call_preserves_independent_receiver_flags",
        "test_queued_calls_bind_exact_threads_and_deliver_fifo",
    ),
    "tests/host/ipc_call_validation_test.c": (
        "test_low_level_enqueue_rejects_reply_tokens", "test_token_exhaustion_is_preflighted",
        "test_call_rejections_are_atomic", "test_staged_call_token_binding_is_validated",
        "test_allocator_validator_rejects_unissued_token",
    ),
    "tests/host/ipc_reply_authority_test.c": (
        "test_staged_call_tokens_are_unique", "test_reply_requires_request_return",
        "test_reply_routes_exact_tokens_without_send_target",
        "test_reply_preserves_independent_caller_flags",
    ),
    "tests/host/ipc_reply_validation_test.c": (
        "test_reply_rejections_are_atomic", "test_reply_scheduler_failure_preserves_token",
        "test_reply_rejects_source_only_caller",
    ),
    "tests/host/ipc_reply_receive_test.c": (
        "test_reply_receive_blocks_and_later_receives",
        "test_reply_receive_selects_specific_sender_atomically",
        "test_reply_receive_requires_combined_operation",
        "test_reply_receive_rejections_are_atomic",
        "test_reply_receive_scheduler_failures_are_atomic",
    ),
    "tests/host/ipc_notify_delivery_test.c": (
        "test_notify_coalesces_and_never_blocks", "test_notify_wakes_first_matching_receiver",
        "test_call_reply_wait_does_not_consume_notification",
        "test_receive_prefers_lowest_notification_before_sender",
        "test_reply_receive_prefers_pending_notification",
    ),
    "tests/host/ipc_kernel_notify_test.c": (
        "test_kernel_notify_coalesces_and_any_receives",
        "test_kernel_notify_wakes_only_any_receiver",
        "test_kernel_notify_prepare_commit_is_retained",
        "test_staged_kernel_notification_is_detected",
        "test_kernel_notify_rejections_are_atomic",
        "test_kernel_notify_validation_and_close_rules",
    ),
    "tests/host/ipc_notify_validation_test.c": (
        "test_reply_receive_pending_failure_is_atomic",
        "test_notification_validators_and_close_preflight",
        "test_notify_rejections_preserve_state", "test_notify_scheduler_failure_is_atomic",
    ),
    "tests/host/ipc_notify_model_test.c": ("test_seeded_notify_model",),
    "tests/host/ipc_close_cancellation_test.c": (
        "test_close_cancels_complete_transaction",
        "test_cascading_close_discards_owned_dead_endpoint_result",
        "test_source_only_close_cancels_queued_sender",
    ),
    "tests/host/ipc_close_validation_test.c": (
        "test_source_only_notification_dependency_blocks_strict_close",
        "test_successful_staged_deliveries_block_close_atomically",
        "test_close_failures_are_atomic",
    ),
    "tests/host/ipc_close_model_test.c": ("test_seeded_close_scenarios",),
    "kernel/ipc_syscall_test_hooks.c": (
        "micros_ipc_syscall_test_before_ecall", "micros_ipc_syscall_test_after_return",
        "micros_ipc_syscall_test_before_timer_return", "micros_ipc_syscall_test_after_timer_return",
    ),
    "kernel/ipc_syscall_test_report.c": ("micros_ipc_syscall_test_finish",),
    "kernel/ipc_ecall_test_report.c": (
        "micros_ipc_ecall_core_test_finish", "micros_ipc_ecall_core_test_finish_trap",
    ),
}


def original_owner(final_owner: str) -> str:
    for prefix, baseline in (
        ("kernel/ipc_syscall_test", "kernel/ipc_syscall_test.c"),
        ("kernel/ipc_ecall_test", "kernel/ipc_ecall_test.c"),
        ("kernel/", "kernel/ipc.c"),
        ("tests/host/ipc_call_", "tests/host/ipc_call_test.c"),
        ("tests/host/ipc_reply_", "tests/host/ipc_reply_test.c"),
        ("tests/host/ipc_notify_", "tests/host/ipc_notify_test.c"),
        ("tests/host/ipc_kernel_notify_", "tests/host/ipc_notify_test.c"),
        ("tests/host/ipc_close_", "tests/host/ipc_close_test.c"),
        ("tests/host/ipc_send_operation_", "tests/host/ipc_operation_test.c"),
        ("tests/host/ipc_receive_operation_", "tests/host/ipc_operation_test.c"),
    ):
        if final_owner.startswith(prefix):
            return baseline
    raise AssertionError(f"unclassified IPC owner {final_owner}")


INITIAL_SYMBOL_OWNER_MIGRATION = tuple(
    (original_owner(owner), owner, name)
    for owner, names in FINAL_OWNER_GROUPS.items()
    for name in names
)
SYMBOL_OWNER_MIGRATION = ()

INITIAL_LONG_FUNCTION_MIGRATION = (
    ("kernel/ipc.c", "micros_ipc_notify"),
    ("kernel/ipc.c", "sender_enqueue"),
    ("kernel/ipc.c", "micros_ipc_receiver_commit_delivery"),
    ("kernel/ipc.c", "sender_commit_delivery"),
    ("kernel/ipc.c", "micros_ipc_reply_receive"),
    ("kernel/ipc_syscall.c", "micros_ipc_handle_captured_user_ecall"),
    ("kernel/ipc_test.c", "micros_ipc_runtime_run_self_test"),
    ("kernel/ipc_syscall_test.c", "transition_after_return"),
    ("kernel/ipc_syscall_test.c", "micros_ipc_syscall_test_finish"),
    ("kernel/ipc_syscall_test.c", "micros_ipc_syscall_runtime_run_self_test"),
    ("kernel/ipc_ecall_test.c", "micros_ipc_ecall_core_test_finish"),
    ("kernel/ipc_ecall_test.c", "micros_ipc_ecall_core_runtime_run_self_test"),
    ("tests/host/ipc_model_test.c", "model_matches_production"),
    ("tests/host/ipc_model_test.c", "setup_model"),
    ("tests/host/ipc_model_test.c", "model_send"),
    ("tests/host/ipc_model_test.c", "model_receive"),
    ("tests/host/ipc_model_test.c", "model_call"),
    ("tests/host/ipc_model_test.c", "model_reply_receive"),
    ("tests/host/ipc_model_test.c", "model_close_endpoint"),
    ("tests/host/ipc_model_test.c", "production_reuse_process"),
    ("tests/host/ipc_model_test.c", "run_model_prelude"),
    ("tests/host/ipc_model_test.c", "build_random_action"),
    ("tests/host/ipc_notify_test.c", "setup_notify_fixture"),
    ("tests/host/ipc_notify_test.c", "test_kernel_notify_prepare_commit_is_retained"),
    ("tests/host/ipc_notify_test.c", "test_kernel_notify_validation_and_close_rules"),
    ("tests/host/ipc_notify_test.c", "test_receive_prefers_lowest_notification_before_sender"),
    ("tests/host/ipc_notify_test.c", "test_reply_receive_prefers_pending_notification"),
    ("tests/host/ipc_notify_test.c", "test_notification_validators_and_close_preflight"),
    ("tests/host/ipc_notify_test.c", "test_seeded_notify_model"),
    ("tests/host/ipc_reply_test.c", "setup_reply_fixture"),
    ("tests/host/ipc_reply_test.c", "test_reply_requires_request_return"),
    ("tests/host/ipc_reply_test.c", "test_reply_routes_exact_tokens_without_send_target"),
    ("tests/host/ipc_reply_test.c", "test_reply_rejections_are_atomic"),
    ("tests/host/ipc_reply_test.c", "test_reply_receive_blocks_and_later_receives"),
    ("tests/host/ipc_reply_test.c", "test_reply_receive_selects_specific_sender_atomically"),
    ("tests/host/ipc_close_test.c", "test_close_cancels_complete_transaction"),
    ("tests/host/ipc_close_test.c", "test_successful_staged_deliveries_block_close_atomically"),
    ("tests/host/ipc_close_test.c", "test_close_failures_are_atomic"),
    ("tests/host/ipc_close_test.c", "test_seeded_close_scenarios"),
    ("tests/host/ipc_call_test.c", "test_queued_calls_bind_exact_threads_and_deliver_fifo"),
    ("tests/host/ipc_call_test.c", "test_call_rejections_are_atomic"),
    ("tests/host/ipc_operation_test.c", "test_receive_specific_and_any_fifo"),
)
LONG_FUNCTION_MIGRATION = ()

FILE_SCOPE_STORAGE_HASHES = {
    "tests/host/ipc_operation_test_fixture.c": "3bbffce409df296cc5ccbc908c8034b84d730d32db201c73f6fdfe128ba065fc",
    "tests/host/ipc_call_test_fixture.c": "3c4205f69f3a21baaeb3e7fada651741446007de252d5d1f5d932e11f2ccbe87",
    "tests/host/ipc_reply_test_fixture.c": "ac463d588ed52d441e693e6f98821b4fbaa84e1ff9a32cb5adbed247e8516ebe",
    "tests/host/ipc_notify_test_fixture.c": "df17fd7674262222781d53f2d3e27a16e331243280131154eb7bfc0e57f78de3",
    "tests/host/ipc_notify_model_test.c": "81f85e33023553fbd2c1b334f5b19342f95962e4ec3c9ddac9982aed6f4a019c",
    "tests/host/ipc_close_test_fixture.c": "38ebf808fef614fc64d51d3057065bee7906afa47d7df17c08e404c0adb1090f",
    "tests/host/ipc_close_model_test.c": "7b2491687b616a4100153002b72e99b1ea7b58b7ba95d4035f3828a713ce477f",
    "tests/host/ipc_model_test_fixture.c": "e833e18b6805722b07da8602feecdf1fbb9bfbd743a123d22da7719df4f6617a",
    "kernel/ipc_test_fixture.c": "2b411170546bf55f679f74f6511fd8a246d60908bcd4ad49725993bf48785fb3",
    "kernel/ipc_ecall_test_fixture.c": "ba1f713e71c720adef6be2bab0ff295d2f662c7c3e19d742285cc43a4817338f",
    "kernel/ipc_syscall_test_fixture.c": "d9c0441b0feae0c995a2c314fea1ad95a2ebcca2484771aac49db8eb1e63505d",
    "kernel/ipc_runtime.c": "a28d66d59b3df567ad3c4aaafcb76818c4113a9f1b73045b147529d2935483f6",
    "kernel/ipc_syscall_panic_test.c": "f98ed6132002a63a0820f802e326ae0fb5a6a62ecb4522de2905b7785e19af12",
    "tests/host/ipc_current_guard_test.c": "ac62816bb31f94100e9621b556ad3d2197bb867e9bad19277b906d35ef3d227d",
    "tests/host/ipc_deadlock_test.c": "bf2037bfafce9aec3eb14262b38df3d29e0d2ce8aa058cf950e37520990e179e",
    "tests/host/ipc_delivery_test.c": "3f1db9b29d98185b30d55951455ffe0698586630f545948b3f430b6f1702d3e6",
    "tests/host/ipc_queue_mutation_test.c": "bad57c32245122923c9043ea1c524d8c61a183a18c9f30d1fb71551fc9227c14",
    "tests/host/ipc_queue_test.c": "dc27f83b4fdaeb64f41ce2a512f507f3f1e7a6b49035874cc9ac0c204c965554",
    "tests/host/ipc_state_test.c": "eb48f692ebe5a44651cd96275d6f0a2b7fd1bd6464311eed80aaa917666493db",
}

STATIC_LOCAL_STORAGE_HASHES = {
    "kernel/ipc_syscall_panic_test.c": "0e718043238871201de2a82adbced676483aff9bd2c465574a92bc6b1ac64d1d",
    "kernel/ipc_syscall_test_setup.c": "c194166d283943b75bef7f4d9ab5d5b9ac6cfa99a8f0c0d23ff0008940604817",
    "kernel/ipc_test_setup.c": "ae036692132986f9e7f86fc1adc689c60a8b2026dabce157fecf06baf7da3ee8",
    "tests/host/ipc_abi_test.c": "005c298eecc6b726dc1b79b48319c08e59ae69bc6a401aec8d78523e0ae5ea85",
    "tests/host/ipc_call_test.c": "bb9e084c5fb7e007efb0d9884a8f224e312d1bf5dbcf4d582ea00e3d90b427fa",
    "tests/host/ipc_call_test_fixture.c": "c6e0753bd28aad9030d7764130256893975fa653212005dde4a58cac323de610",
    "tests/host/ipc_close_test.c": "bb9e084c5fb7e007efb0d9884a8f224e312d1bf5dbcf4d582ea00e3d90b427fa",
    "tests/host/ipc_close_test_fixture.c": "cdb0f978c635b94844a5caaa49745e8cd25d6a30dcbfc59e233a5500e6994675",
    "tests/host/ipc_deadlock_test.c": "81a471e7bb3abc84119a88736be5aeca26abf0ee915626a90b130a595e803f70",
    "tests/host/ipc_delivery_test.c": "114b9c95f5ff2d1d01cda4dabfffcdfafebc2538e0aeff6deb5b3eb0e6e64e59",
    "tests/host/ipc_model_main.c": "bb9e084c5fb7e007efb0d9884a8f224e312d1bf5dbcf4d582ea00e3d90b427fa",
    "tests/host/ipc_model_test_common.c": "4759c23d789f5bdb2180b13fa5ebe3834771fa4041ce5373ebea401bce9163cc",
    "tests/host/ipc_model_test_fixture.c": "40d65fffbf05e33d23385c73961816f129e5ee4f13a1d4e8607130f2bf7775c9",
    "tests/host/ipc_model_test_trace.c": "4be320640326f80dc17b6e248509c24aab8fd36e69557b5f485e6c476aca93cb",
    "tests/host/ipc_notify_model_test.c": "59f222e67b0e2b00b40a609ea074abf7365bea31457708051f8b9ed66c5a986a",
    "tests/host/ipc_notify_test.c": "bb9e084c5fb7e007efb0d9884a8f224e312d1bf5dbcf4d582ea00e3d90b427fa",
    "tests/host/ipc_notify_test_fixture.c": "7be2e009c97bee318fe8c6cc5369c7b3d086f2eae39cff07601f6274b1169164",
    "tests/host/ipc_operation_test_fixture.c": "c3b7ab53d6786bece30a56f5ac4956cbb3b1b01e4b6ed122b028b79a68eab884",
    "tests/host/ipc_queue_mutation_test.c": "114b9c95f5ff2d1d01cda4dabfffcdfafebc2538e0aeff6deb5b3eb0e6e64e59",
    "tests/host/ipc_queue_test.c": "4858b87307376f66e423ed06fc7ef7aca3a59a881ee975e95ebf6da59b1ffcc2",
    "tests/host/ipc_reply_test.c": "bb9e084c5fb7e007efb0d9884a8f224e312d1bf5dbcf4d582ea00e3d90b427fa",
    "tests/host/ipc_reply_test_fixture.c": "9904aab207d803377fb1d42473e7a659c0e7512a8e6f94cf38b11dc38f4797a2",
    "tests/host/ipc_state_test.c": "6a29ff4af8e134e8d8e3bc6c104a77a69b49fcf87960d85ca680e4a3aadd90c0",
}

PRIVATE_SCHEMA_HASHES = {
    "kernel/ipc_internal.h": ("e3fa2a333bff704697065d5ef4cea6536010c973b4a28d2bd424d41f7cedeab3", "6bbd1f0488f1bebd96b63f02740851b2e193cad3d177cbe9f41cfd7ced7d742d"),
    "kernel/ipc_syscall_internal.h": ("403bf47b79ad8d5cc4416b52c0c463f2544ca4341ef368e39fa8f3c244636a3d", "12f0223649be6359fb8f575579fe479247400fc6a7d7de39a1b57252a0c0e99f"),
    "tests/host/ipc_operation_test_internal.h": ("3a3a8abad0960da078eb647e1b6361e5aa95127ea1d8dfac15665278e3a742cf", "b491d4c17ba17bf356a4068c21809d29c74c2ab47d5752edf6618389fe251ca7"),
    "tests/host/ipc_call_test_internal.h": ("9bb4c674dd88bbd660490f80fda08658ab1bb136813a0f30187652374e813507", "82cbdcc4b3fbfa218724212a2c045c4e2c2e76cfb7c00a7cf852bf326596462e"),
    "tests/host/ipc_reply_test_internal.h": ("9ab918f6a506030e9df437a3e2c0b344b7efb04b9a83b097574d7e993a04c770", "998e78fb3393adeebb76e8a54d299253a07c285bc0c6855b30e124f8c9b55dc2"),
    "tests/host/ipc_notify_test_internal.h": ("a7a70bc7eb128f73eae00c5d9c2ceb4616cd3a0e88d48f319b9e8c52b2b2c681", "090adae619c28e572a672fe1a0f4aae8b270d99bf07daa5d246d4e2ed161e2ec"),
    "tests/host/ipc_close_test_internal.h": ("e22c18f20f01cbbf1daa3377bf9d8a2f2d0c8d8d7f8f5f76c52822d2c7e5026a", "e8c05862a6234982070b1a81735332786bee5442ae662c6d95b7b3f8d94b5d36"),
    "tests/host/ipc_model_test_internal.h": ("4f5507b4061ac5ed097863f9fb778431cbceccc0e6f5a82fc96853b024e3fceb", "93a94a4780e0972f7f06007566bf23cf3f92ecb89dd4497a8a081ecb33187c35"),
    "kernel/ipc_test_internal.h": ("c0a25937e460707dd2b716e648c2b8dd47124620a71b8af9dd2d5733a69afd0f", "2a3d5db001253ac6c918229bbbe758cb104c37477030a063b8054e65ccd9ec7c"),
    "kernel/ipc_ecall_test_internal.h": ("0225c966ae7c71d7d967a6f079cdb77b3fda30d06f087f55fde9cfae1ac83829", "3a5164ceb2be4c700554a896fcceea4ccdaba2d6ba1eb1a2df7a663d6942c1ee"),
    "kernel/ipc_syscall_test_internal.h": ("a342d2597fedc247ae26a50ef279ffd67fefafa8ba2349201f5c85a50b4876a8", "7d9a1511ddfab5a7ee4abff1e420b998d08dddd84872da4799110764a33c34cd"),
}

DECLARATION_ONLY_HEADERS = {
    *PRIVATE_HEADERS.values(), *(path for path in STABLE_HASHES if path.endswith(".h")),
    "include/micros/endpoint.h", "include/micros/scheduler_core.h",
    "include/micros/kernel_objects.h", "include/micros/kernel_object_runtime.h",
    "include/micros/scheduler.h", "kernel/syscall.h",
    "kernel/endpoint_internal.h", "kernel/scheduler_core_internal.h",
    "kernel/bootstrap_image.h", "kernel/bootstrap_runtime.h",
    "kernel/kernel_object_runtime_internal.h",
}

RETAINED_OWNER_GROUPS = {
    "kernel/ipc.c": ("micros_ipc_send", "micros_ipc_receive", "micros_ipc_call"),
    "kernel/ipc_syscall.c": ("micros_ipc_handle_captured_user_ecall",),
    "tests/host/ipc_operation_test.c": ("micros_ipc_operation_test_run",),
    "tests/host/ipc_call_test.c": ("micros_ipc_call_test_run",),
    "tests/host/ipc_reply_test.c": ("micros_ipc_reply_test_run",),
    "tests/host/ipc_notify_test.c": ("micros_ipc_notify_test_run", "micros_ipc_notify_model_test_run"),
    "tests/host/ipc_close_test.c": ("micros_ipc_close_test_run", "micros_ipc_close_model_test_run"),
    "tests/host/ipc_model_test.c": ("model_random_next", "coverage_is_complete", "micros_ipc_model_test_run"),
    "kernel/ipc_test.c": ("micros_ipc_runtime_run_self_test",),
    "kernel/ipc_ecall_test.c": ("micros_ipc_ecall_core_runtime_run_self_test",),
    "kernel/ipc_syscall_test.c": ("micros_ipc_syscall_runtime_run_self_test",),
}

CORE_HELPER_OWNER_GROUPS = {
    "kernel/ipc_common.c": (
        "ipc_copy_bytes", "ipc_clear_bytes", "ipc_thread_handle_is_zero",
        "ipc_thread_handles_equal", "ipc_process_handles_equal", "ipc_thread_is_current",
        "ipc_endpoint_error_to_ipc", "ipc_scheduler_error_to_ipc",
        "ipc_canonicalize_message", "ipc_clear_message", "thread_is_held_and_clear",
        "ipc_resolve_held_thread", "ipc_resolve_operation_thread",
    ),
    "kernel/ipc_queue.c": (
        "ipc_resolve_queue_tail", "ipc_receiver_is_ready_for", "ipc_sender_is_ready_for",
        "ipc_append_thread", "ipc_find_matching_sender", "ipc_find_matching_receiver_const",
        "ipc_find_matching_receiver", "ipc_unlink_thread",
    ),
    "kernel/ipc_completion.c": ("ipc_stage_no_message_completion", "ipc_clear_delivered_sender", "ipc_stage_delivery"),
    "kernel/ipc_tokens.c": ("ipc_reply_token_is_available", "resolve_reply_waiter", "reply_request_is_pending", "ipc_preflight_reply", "ipc_commit_reply_delivery"),
    "kernel/ipc_notifications.c": ("canonicalize_notification", "ipc_find_pending_notification", "ipc_consume_pending_notification"),
    "kernel/ipc_deadlock.c": ("resolve_dependency_endpoint", "resolve_sole_live_thread", "blocked_dependency", "ipc_preflight_deadlock"),
    "kernel/ipc_rendezvous.c": ("ipc_sender_enqueue", "ipc_sender_commit_delivery"),
    "kernel/ipc_close.c": (
        "thread_has_staged_reference", "thread_has_endpoint_reference", "add_close_transition",
        "preflight_close_threads", "preflight_close_queue", "preflight_close_queues",
        "preflight_close_notifications", "preflight_endpoint_close", "commit_close_queue",
        "clear_thread_ipc_state_for_close", "stage_dead_endpoint_for_close", "commit_endpoint_close",
    ),
}

CASE_LABELS = {
    "call": (
        ("call delivers to waiting receiver", "test_call_delivers_to_waiting_receiver"),
        ("call preserves independent receiver flags", "test_call_preserves_independent_receiver_flags"),
        ("queued calls bind exact threads and deliver FIFO", "test_queued_calls_bind_exact_threads_and_deliver_fifo"),
        ("low-level enqueue rejects reply tokens", "test_low_level_enqueue_rejects_reply_tokens"),
        ("token exhaustion is preflighted", "test_token_exhaustion_is_preflighted"),
        ("call rejections are atomic", "test_call_rejections_are_atomic"),
        ("staged call token binding is validated", "test_staged_call_token_binding_is_validated"),
        ("allocator validator rejects unissued token", "test_allocator_validator_rejects_unissued_token"),
    ),
    "reply": (
        ("staged call tokens are unique", "test_staged_call_tokens_are_unique"),
        ("reply requires request return", "test_reply_requires_request_return"),
        ("reply routes exact tokens without send target", "test_reply_routes_exact_tokens_without_send_target"),
        ("reply preserves independent caller flags", "test_reply_preserves_independent_caller_flags"),
        ("reply rejections are atomic", "test_reply_rejections_are_atomic"),
        ("reply scheduler failure preserves token", "test_reply_scheduler_failure_preserves_token"),
        ("reply rejects source-only caller", "test_reply_rejects_source_only_caller"),
        ("reply receive blocks and later receives", "test_reply_receive_blocks_and_later_receives"),
        ("reply receive selects specific sender atomically", "test_reply_receive_selects_specific_sender_atomically"),
        ("reply receive requires combined operation", "test_reply_receive_requires_combined_operation"),
        ("reply receive rejections are atomic", "test_reply_receive_rejections_are_atomic"),
        ("reply receive scheduler failures are atomic", "test_reply_receive_scheduler_failures_are_atomic"),
    ),
    "notify": (
        ("notify coalesces and never blocks", "test_notify_coalesces_and_never_blocks"),
        ("kernel notify coalesces and any receives", "test_kernel_notify_coalesces_and_any_receives"),
        ("kernel notify wakes only any receiver", "test_kernel_notify_wakes_only_any_receiver"),
        ("kernel notify prepare commit is retained", "test_kernel_notify_prepare_commit_is_retained"),
        ("staged kernel notification is detected", "test_staged_kernel_notification_is_detected"),
        ("kernel notify rejections are atomic", "test_kernel_notify_rejections_are_atomic"),
        ("kernel notify validation and close rules", "test_kernel_notify_validation_and_close_rules"),
        ("notify wakes first matching receiver", "test_notify_wakes_first_matching_receiver"),
        ("call reply wait excludes notification", "test_call_reply_wait_does_not_consume_notification"),
        ("receive prefers lowest notification before sender", "test_receive_prefers_lowest_notification_before_sender"),
        ("reply receive prefers pending notification", "test_reply_receive_prefers_pending_notification"),
        ("reply receive pending failure is atomic", "test_reply_receive_pending_failure_is_atomic"),
        ("notification validators and close preflight", "test_notification_validators_and_close_preflight"),
        ("notify rejections preserve state", "test_notify_rejections_preserve_state"),
        ("notify scheduler failure is atomic", "test_notify_scheduler_failure_is_atomic"),
    ),
    "close": (
        ("complete cancellation transaction", "test_close_cancels_complete_transaction"),
        ("cascading close discards owned dead-endpoint result", "test_cascading_close_discards_owned_dead_endpoint_result"),
        ("source-only close cancels queued sender", "test_source_only_close_cancels_queued_sender"),
        ("source-only notification dependency blocks strict close", "test_source_only_notification_dependency_blocks_strict_close"),
        ("successful staged deliveries block close atomically", "test_successful_staged_deliveries_block_close_atomically"),
        ("stale corrupt and late failures are atomic", "test_close_failures_are_atomic"),
    ),
}

SYSCALL_SCRIPT = (
    ("CLIENT", "CLIENT_SEND_ONE"),
    ("SERVER", "SERVER_RECEIVE_SEND_ONE"),
    ("SERVER", "SERVER_WAIT_CALL_ONE"),
    ("CLIENT", "CLIENT_CALL_ONE"),
    ("SERVER", "SERVER_REPLY_ONE"),
    ("SERVER", "SERVER_WAIT_CALL_TWO"),
    ("CLIENT", "CLIENT_CALL_TWO"),
    ("SERVER", "SERVER_REPLY_RECEIVE_TWO"),
    ("CLIENT", "CLIENT_WAIT_SERVER_CALL"),
    ("PEER", "PEER_SEND"),
    ("SERVER", "SERVER_CALL_CLIENT"),
    ("PEER", "PEER_NOTIFY_ONE"),
    ("PEER", "PEER_NOTIFY_TWO"),
    ("PEER", "PEER_BLOCK"),
    ("CLIENT", "CLIENT_REPLY_THREE"),
    ("SERVER", "SERVER_RECEIVE_NOTIFY"),
    ("SERVER", "SERVER_WAKE_PEER"),
    ("SERVER", "SERVER_BLOCK_FINAL"),
    ("CLIENT", "CLIENT_ERROR_UNAUTHORIZED"),
    ("CLIENT", "CLIENT_ERROR_ARGUMENT"),
    ("CLIENT", "CLIENT_ERROR_FAULT"),
)

COMMON = "kernel/ipc_common.c"
QUEUE = "kernel/ipc_queue.c"
COMPLETION = "kernel/ipc_completion.c"
TOKENS = "kernel/ipc_tokens.c"
NOTIFICATIONS = "kernel/ipc_notifications.c"
DEADLOCK = "kernel/ipc_deadlock.c"
MODEL_COMMON = "tests/host/ipc_model_test_common.c"
MODEL_FIXTURE = "tests/host/ipc_model_test_fixture.c"
MODEL_REFERENCE = "tests/host/ipc_model_reference.c"
MODEL_PRIMITIVES = (
    "tests/host/ipc_model_reference_authority.c",
    "tests/host/ipc_model_reference_scheduler.c",
    "tests/host/ipc_model_reference_ipc_state.c",
)
MODEL_OPERATIONS = (
    "tests/host/ipc_model_reference_rendezvous.c",
    "tests/host/ipc_model_reference_notifications.c",
    "tests/host/ipc_model_reference_lifecycle.c",
)

PERMITTED_OWNER_DEPENDENCIES = {
    COMMON: (),
    QUEUE: (COMMON,),
    COMPLETION: (COMMON,),
    TOKENS: (COMMON, COMPLETION),
    NOTIFICATIONS: (COMMON, QUEUE, COMPLETION),
    DEADLOCK: (COMMON,),
    "kernel/ipc_rendezvous.c": (COMMON, QUEUE, COMPLETION, TOKENS, NOTIFICATIONS),
    "kernel/ipc_close.c": (COMMON,),
    "kernel/ipc_reply.c": (COMMON, QUEUE, COMPLETION, TOKENS, NOTIFICATIONS, DEADLOCK),
    "kernel/ipc.c": (COMMON, DEADLOCK, "kernel/ipc_rendezvous.c"),
    "kernel/ipc_syscall_result.c": (),
    "kernel/ipc_syscall_request.c": ("kernel/ipc_syscall_result.c", *SOURCE_SETS["MICROS_IPC_CORE_SOURCES"]),
    "kernel/ipc_syscall.c": (
        "kernel/ipc_syscall_result.c", "kernel/ipc_syscall_request.c",
        *SOURCE_SETS["MICROS_IPC_CORE_SOURCES"],
    ),
    MODEL_COMMON: (),
    **{path: (MODEL_COMMON,) for path in MODEL_PRIMITIVES},
    **{path: (MODEL_COMMON, *MODEL_PRIMITIVES) for path in MODEL_OPERATIONS},
    MODEL_REFERENCE: (MODEL_COMMON, *MODEL_PRIMITIVES, *MODEL_OPERATIONS),
    MODEL_FIXTURE: (MODEL_COMMON, MODEL_REFERENCE),
    "tests/host/ipc_model_test_trace.c": (MODEL_COMMON,),
    "tests/host/ipc_model_test_production.c": (MODEL_COMMON, *SOURCE_SETS["MICROS_IPC_CORE_SOURCES"]),
    "tests/host/ipc_model_test_step.c": (
        MODEL_REFERENCE, "tests/host/ipc_model_test_production.c", "tests/host/ipc_model_test_trace.c",
    ),
    "tests/host/ipc_model_test_actions.c": (
        MODEL_COMMON, MODEL_FIXTURE, "tests/host/ipc_model_reference_authority.c",
    ),
    "tests/host/ipc_model_test_prelude.c": (MODEL_COMMON, "tests/host/ipc_model_test_actions.c", "tests/host/ipc_model_test_step.c"),
    "tests/host/ipc_model_test.c": (
        MODEL_COMMON, MODEL_FIXTURE, "tests/host/ipc_model_test_prelude.c",
        "tests/host/ipc_model_test_actions.c", "tests/host/ipc_model_test_step.c",
        "tests/host/ipc_model_test_trace.c",
    ),
}
for family in ("OPERATION", "CALL", "REPLY", "NOTIFY", "CLOSE"):
    paths = SOURCE_SETS[f"MICROS_IPC_{family}_HOST_TEST_SOURCES"]
    for path in paths:
        PERMITTED_OWNER_DEPENDENCIES[path] = (
            (*paths, *SOURCE_SETS["MICROS_IPC_CORE_SOURCES"]) if path == paths[0]
            else (*SOURCE_SETS["MICROS_IPC_CORE_SOURCES"],)
            if path == paths[1] else (paths[1], *SOURCE_SETS["MICROS_IPC_CORE_SOURCES"])
        )
for group in ("MICROS_IPC_QEMU_TEST_SOURCES", "MICROS_IPC_ECALL_QEMU_TEST_SOURCES", "MICROS_IPC_SYSCALL_QEMU_TEST_SOURCES"):
    paths = SOURCE_SETS[group]
    facade, fixture, setup = paths[:3]
    for path in paths:
        suffix = Path(path).stem
        dependencies = (
            paths if path == facade and group == "MICROS_IPC_QEMU_TEST_SOURCES"
            else (fixture, setup) if path == facade
            else () if path == fixture
            else (fixture, "kernel/ipc_syscall_test_commands.c")
            if group == "MICROS_IPC_SYSCALL_QEMU_TEST_SOURCES" and path == setup
            else (fixture, "kernel/ipc_syscall_test_scenarios.c") if suffix.endswith("_hooks")
            else (fixture, setup) if suffix.endswith("_report")
            else (fixture, "kernel/ipc_syscall_test_commands.c")
            if group == "MICROS_IPC_SYSCALL_QEMU_TEST_SOURCES" and suffix.endswith("_scenarios")
            else (fixture,)
        )
        PERMITTED_OWNER_DEPENDENCIES[path] = (*dependencies, *SOURCE_SETS["MICROS_IPC_CORE_SOURCES"])


def all_sources() -> tuple[str, ...]:
    return tuple(path for paths in SOURCE_SETS.values() for path in paths)


def accepted_family_paths() -> tuple[str, ...]:
    return tuple(sorted({*all_sources(), *PRIVATE_HEADERS.values(), *STABLE_HASHES}))


def discovered_family_paths() -> tuple[str, ...]:
    return tuple(sorted({
        str(path.relative_to(REPOSITORY_ROOT))
        for pattern in ("kernel/ipc*.[ch]", "tests/host/ipc*.[ch]",
                        "include/micros/ipc*.h", "arch/riscv64/ipc*.S")
        for path in kernel_layout_source.glob_paths(REPOSITORY_ROOT, pattern)
    }))


def compiled_source_header_closure() -> tuple[str, ...]:
    return tuple(sorted({
        dependency for path in accepted_family_paths() if path.endswith((".c", ".h"))
        for dependency in vm_layout.local_include_closure(path)
    }))


def storage_inventory(roots: tuple[str, ...]) -> dict[str, tuple[str, ...]]:
    inventory = {}
    for path in sorted({dependency for root in roots for dependency in vm_layout.local_include_closure(root)}):
        objects = file_scope_objects(kernel_layout_source.read_path(REPOSITORY_ROOT / path))
        if objects:
            inventory[path] = objects
    return inventory


def owner_dependency_text(relative: str) -> str:
    selected = []
    for dependency in vm_layout.local_include_closure(relative):
        text = kernel_layout_source.read_path(REPOSITORY_ROOT / dependency)
        if dependency in DECLARATION_ONLY_HEADERS and dependency != relative:
            text = macro_definition_text(text)
        selected.append(re.sub(
            r"\b(?:struct|union|enum)\s+[A-Za-z_][A-Za-z0-9_]*", "", c_lexical_text(text),
        ))
    return "\n".join(selected)


class KernelIpcSourceLayoutTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.definitions = {
            path: c_function_definitions(kernel_layout_source.read_path(REPOSITORY_ROOT / path))
            for path in all_sources()
        }

    def test_recursive_header_scan_keeps_hidden_calls_and_storage(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            (root / "owner.c").write_text('#include "forward.h"\nvoid owner(void) {}\n')
            (root / "forward.h").write_text('#include "nested.h"\n')
            (root / "nested.h").write_text(
                "struct micros_ipc_reply;\n"
                "#define HIDDEN_CALL micros_ipc_endpoint_close\n"
                "static inline void hidden(void) { micros_ipc_reply(0); }\n"
                "static struct ipc_model_test_context duplicate = {0};\n",
            )
            with patch(f"{__name__}.REPOSITORY_ROOT", root), \
                    patch.object(vm_layout, "REPOSITORY_ROOT", root):
                dependency = owner_dependency_text("owner.c")
                storage = storage_inventory(("owner.c",))
            self.assertIn("micros_ipc_reply(0)", dependency)
            self.assertIn("micros_ipc_endpoint_close", dependency)
            self.assertNotIn("struct micros_ipc_reply", dependency)
            self.assertEqual({"nested.h"}, set(storage))

    def test_context_shape_rejects_arrays_aliases_and_codeclarations(self) -> None:
        scalar = r"static\s+struct\s+ipc_model_test_context\s+model_context\s*;"
        for declaration in (
            "static struct ipc_model_test_context model_context[2];",
            "static model_alias model_context;",
            "extern struct ipc_model_test_context model_context = {0};",
        ):
            self.assertEqual(1, len(file_scope_objects(declaration)))
            self.assertIsNone(re.fullmatch(scalar, declaration))
        for declaration in (
            "static struct ipc_model_test_context model_context, hidden;",
            "static struct ipc_model_test_context model_context = {0}, hidden = {0};",
            "static struct ipc_model_test_context model_context[2], hidden[2];",
        ):
            with self.assertRaisesRegex(AssertionError, "multiple objects"):
                file_scope_objects(declaration)

    def test_complete_accepted_source_and_private_header_inventory_exists(self) -> None:
        self.assertEqual(68, len(all_sources()))
        self.assertEqual(11, len(PRIVATE_HEADERS))
        self.assertEqual(
            [],
            [path for path in (*all_sources(), *PRIVATE_HEADERS.values())
             if not kernel_layout_source.is_file_path(REPOSITORY_ROOT / path)],
        )
        self.assertEqual(accepted_family_paths(), discovered_family_paths())

    def test_public_support_and_assembly_surfaces_are_unchanged(self) -> None:
        self.assertEqual(
            STABLE_HASHES,
            {path: hashlib.sha256(kernel_layout_source.read_bytes_path(REPOSITORY_ROOT / path)).hexdigest()
             for path in STABLE_HASHES},
        )

    def test_private_headers_have_no_function_or_storage_definitions(self) -> None:
        for path in PRIVATE_HEADERS.values():
            text = kernel_layout_source.read_path(REPOSITORY_ROOT / path)
            self.assertEqual({}, c_function_definitions(text), path)
            self.assertEqual((), file_scope_objects(text), path)
            self.assertNotRegex(c_declaration_text(text), r"\bstatic\b", path)
            expected_shape, expected_macros = PRIVATE_SCHEMA_HASHES[path]
            self.assertEqual(
                expected_shape, hashlib.sha256("".join(c_declaration_text(text).split()).encode()).hexdigest(),
                (path, "private types, members, extents and declarations"),
            )
            self.assertEqual(
                expected_macros, hashlib.sha256("".join(macro_definition_text(text).split()).encode()).hexdigest(),
                (path, "literal constants and assertion macros"),
            )

    def test_current_file_scope_storage_has_exact_approved_owners(self) -> None:
        observed = {
            path: hashlib.sha256("\n".join(declarations).encode()).hexdigest()
            for path, declarations in storage_inventory(
                tuple(path for path in accepted_family_paths() if path.endswith((".c", ".h"))),
            ).items()
        }
        self.assertEqual(FILE_SCOPE_STORAGE_HASHES, observed)

    def test_static_local_storage_and_macros_cannot_add_hidden_state(self) -> None:
        observed = {}
        for path in compiled_source_header_closure():
            text = kernel_layout_source.read_path(REPOSITORY_ROOT / path)
            local = static_local_storage(text)
            if local:
                observed[path] = hashlib.sha256("\n".join(local).encode()).hexdigest()
            self.assertNotRegex(c_lexical_text(macro_definition_text(text)), r"\bstatic\b", path)
        self.assertEqual(STATIC_LOCAL_STORAGE_HASHES, observed)

    def test_recursive_owner_direction_and_private_imports_are_bounded(self) -> None:
        definitions = {
            path: set(self.definitions[path])
            for path in all_sources()
        }
        symbols = {}
        for path, names in definitions.items():
            for name in names:
                symbols.setdefault(name, set()).add(path)
        private_core = set(re.findall(
            r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(",
            c_declaration_text(kernel_layout_source.read_path(REPOSITORY_ROOT / "kernel/ipc_internal.h")),
        ))
        for path in all_sources():
            text = owner_dependency_text(path)
            closure = vm_layout.local_include_closure(path)
            allowed = {path, *PERMITTED_OWNER_DEPENDENCIES[path]}
            violations = {
                name: sorted(owners) for name, owners in symbols.items()
                if name not in definitions[path] and not owners & allowed
                and re.search(rf"\b{re.escape(name)}\b", text)
            }
            self.assertEqual({}, violations, path)
            if path not in SOURCE_SETS["MICROS_IPC_CORE_SOURCES"]:
                self.assertNotIn("kernel/ipc_internal.h", closure, path)
                for name in private_core:
                    self.assertNotRegex(text, rf"\b{re.escape(name)}\b", (path, name))
            if path.startswith("kernel/") and "_test" not in path:
                self.assertFalse(
                    set(closure) & (set(PRIVATE_HEADERS.values()) - {
                        "kernel/ipc_internal.h", "kernel/ipc_syscall_internal.h",
                    }),
                    path,
                )
        for path in compiled_source_header_closure():
            self.assertNotRegex(
                kernel_layout_source.read_path(REPOSITORY_ROOT / path),
                r'(?m)^\s*#\s*include\s*[<"][^">]+\.(?:c|py)[">]', path,
            )

    def test_action_selection_rejects_reference_mutators(self) -> None:
        actions_path = "tests/host/ipc_model_test_actions.c"
        actions_dependency = owner_dependency_text(actions_path)
        for call in (
            "ipc_model_hold_thread(0, 0);",
            "ipc_model_drain_thread(0, 0);",
            "ipc_model_stage_delivery(0, 0, 0);",
            "ipc_model_ipc_append(0, 0, 0, 0);",
        ):
            with self.subTest(call=call):
                def dependency_with_mutator(path: str) -> str:
                    return f"{actions_dependency}\n{call}\n" if path == actions_path else ""

                with patch(f"{__name__}.owner_dependency_text",
                           side_effect=dependency_with_mutator):
                    with self.assertRaisesRegex(AssertionError, call.split("(")[0]):
                        self.test_recursive_owner_direction_and_private_imports_are_bounded()

    def test_reference_decisions_and_action_selection_do_not_execute_production(self) -> None:
        for path in (*MODEL_PRIMITIVES, *MODEL_OPERATIONS):
            text = owner_dependency_text(path)
            self.assertNotRegex(text, r"\bmicros_[A-Za-z0-9_]+\s*\(", path)
        actions = owner_dependency_text("tests/host/ipc_model_test_actions.c")
        for name in (
            "ipc_model_send", "ipc_model_receive", "ipc_model_call", "ipc_model_reply",
            "ipc_model_reply_receive", "ipc_model_notify", "ipc_model_kernel_notify",
            "ipc_model_close_reuse", "ipc_model_apply_transitions", "ipc_model_execute_step",
            "ipc_model_production_execute_action",
        ):
            self.assertNotRegex(actions, rf"\b{name}\b")
        compare = owner_dependency_text(MODEL_REFERENCE)
        self.assertEqual(
            {"micros_kernel_objects_validate", "micros_scheduler_core_validate",
             "micros_endpoint_registry_validate_objects"},
            set(re.findall(r"\b(micros_[A-Za-z0-9_]+)\s*\(", compare)),
        )

    def test_retained_helpers_and_facade_owners_are_exact(self) -> None:
        definitions = self.definitions
        for groups in (RETAINED_OWNER_GROUPS, CORE_HELPER_OWNER_GROUPS):
            for path, names in groups.items():
                observed = definitions[path]
                self.assertLessEqual(set(names), set(observed), path)
        for path, expected in RETAINED_OWNER_GROUPS.items():
            observed = tuple(definitions[path])
            if path != "kernel/ipc_syscall.c":
                self.assertEqual(expected, observed, path)
        for path in PRIVATE_HEADERS.values():
            names = re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(",
                               c_declaration_text(kernel_layout_source.read_path(REPOSITORY_ROOT / path)))
            for name in names:
                if name != "_Alignas":
                    owners = [source for source in all_sources()
                              if name in definitions[source]]
                    self.assertEqual(1, len(owners), (path, name, owners))

    def test_native_case_labels_order_and_existing_executions_are_retained(self) -> None:
        for family, expected in CASE_LABELS.items():
            path = REPOSITORY_ROOT / f"tests/host/ipc_{family}_test.c"
            observed = tuple(re.findall(r'\{\s*"([^"\n]+)"\s*,\s*(test_[A-Za-z_]+)', kernel_layout_source.read_path(path)))
            self.assertEqual(expected, observed, family)
        operation = kernel_layout_source.read_path(REPOSITORY_ROOT / "tests/host/ipc_operation_test.c")
        self.assertEqual(
            (
                "test_authorized_send_blocks_with_snapshot", "test_authorized_send_matches_first_receiver",
                "test_send_rejections_are_atomic", "test_authorized_receive_blocks",
                "test_receive_specific_and_any_fifo", "test_receive_rejections_are_atomic",
            ),
            tuple(re.findall(r"\b(test_[A-Za-z_]+)\s*\(", operation)),
        )
        cmake = kernel_layout_source.read_path(REPOSITORY_ROOT / "CMakeLists.txt")
        self.assertEqual(1, cmake.count("NAME endpoint-profile"))
        self.assertEqual(1, cmake.count("NAME ipc-model"))

    def test_acceptance_model_seed_draw_trace_and_comparison_order_are_retained(self) -> None:
        runner = kernel_layout_source.read_path(REPOSITORY_ROOT / "tests/host/ipc_model_test.c")
        actions = kernel_layout_source.read_path(REPOSITORY_ROOT / "tests/host/ipc_model_test_actions.c")
        prelude = c_declaration_text(kernel_layout_source.read_path(REPOSITORY_ROOT / "tests/host/ipc_model_test_prelude.c"))
        step = kernel_layout_source.read_path(REPOSITORY_ROOT / "tests/host/ipc_model_test_step.c")
        trace = kernel_layout_source.read_path(REPOSITORY_ROOT / "tests/host/ipc_model_test_trace.c")
        fixture = kernel_layout_source.read_path(REPOSITORY_ROOT / MODEL_FIXTURE)
        self.assertIn("UINT32_C(0x30accee5)", runner)
        self.assertIn("struct model_state model;", runner)
        self.assertIn("struct model_coverage coverage;", runner)
        self.assertIn("while (step < MODEL_STEPS)", runner)
        self.assertEqual(2, len(re.findall(r"\bmodel_random_next\s*\(", runner)))
        self.assertEqual(93, len(re.findall(r"\bRUN\s*\(", prelude)))
        self.assertEqual(tuple(range(13)), tuple(int(value) for value in re.findall(r"\bcase\s+(\d+):", actions)))
        self.assertIn("return selected ? selected_action : recovery_action(fixture, model, random, step)", actions)
        ordered = (
            "ipc_model_reference_execute_action", "ipc_model_production_execute_action",
            "ipc_model_record_trace", "update_coverage(coverage", "*result = expected",
            "if (expected != actual)", "ipc_model_matches_production",
        )
        self.assertEqual(sorted(step.index(name) for name in ordered),
                         [step.index(name) for name in ordered])
        self.assertIn("UINT64_C(0x30accee57ace0001)", fixture)
        self.assertIn("(fixture->model_trace_hash << 7) | (fixture->model_trace_hash >> 57)", trace)
        for fragment in (
            "((uint64_t)step << 32)", "((uint64_t)action->operation << 56)",
            "((uint64_t)action->thread_slot << 16)", "((uint64_t)action->process_slot << 8)",
            "^ action->endpoint", "^ action->source", "^ action->value", "^ (uint64_t)result",
        ):
            self.assertIn(fragment, trace)
        compare = kernel_layout_source.read_path(REPOSITORY_ROOT / MODEL_REFERENCE)
        body = compare[compare.index("bool ipc_model_matches_production("):]
        comparisons = (
            "compare_acceptance_aggregates", "compare_acceptance_processes",
            "compare_acceptance_endpoints_and_events", "compare_acceptance_threads",
            "compare_acceptance_hart_and_ready_queues", "compare_acceptance_public_invariants",
        )
        self.assertEqual(sorted(body.index(name) for name in comparisons),
                         [body.index(name) for name in comparisons])

    def test_small_models_keep_original_counts_draws_and_forced_prefix(self) -> None:
        notify = kernel_layout_source.read_path(REPOSITORY_ROOT / "tests/host/ipc_notify_model_test.c")
        close = kernel_layout_source.read_path(REPOSITORY_ROOT / "tests/host/ipc_close_model_test.c")
        self.assertIn("UINT32_C(0x30c0a1e5)", notify)
        self.assertIn("state.step < NOTIFY_MODEL_STEPS", notify)
        self.assertEqual(3, len(re.findall(r"\bnotify_model_next_random\s*\(", notify)))
        self.assertLess(notify.index("notify_model_next_random(&state->random_state)"),
                        notify.index("state->operation = scripted_operations"))
        self.assertLess(notify.index("notify_model_next_random(&state->random_state)",
                                     notify.index("notify_model_next_random(&state->random_state)") + 1),
                        notify.index("state->operation = scripted_operations"))
        self.assertIn("UINT32_C(0x30c105e1)", close)
        self.assertIn("case_index < CLOSE_SCENARIO_CASE_COUNT", close)
        self.assertEqual(4, len(re.findall(r"\bclose_scenario_next_random\s*\(", close)))
        select = close.index("select_close_model_scenario(&random_state, &scenario)")
        record = close.index("close_scenario_record(fixture, case_index, scenario.operation)", select)
        setup = close.index("ipc_close_test_setup_fixture(fixture, 3, 1)", record)
        self.assertLess(select, record)
        self.assertLess(record, setup)

    def test_target_script_capture_and_evidence_identities_remain_exact(self) -> None:
        fixture = kernel_layout_source.read_path(REPOSITORY_ROOT / "kernel/ipc_syscall_test_fixture.c")
        threads = re.search(r"ipc_syscall_test_scenario_threads\[21\]\s*=\s*\{(.*?)\};", fixture, re.DOTALL)
        commands = re.search(r"ipc_syscall_test_scenario_commands\[21\]\s*=\s*\{(.*?)\};", fixture, re.DOTALL)
        self.assertIsNotNone(threads)
        self.assertIsNotNone(commands)
        self.assertEqual(
            SYSCALL_SCRIPT,
            tuple(zip(re.findall(r"\bIPC_SYSCALL_(CLIENT|SERVER|PEER)\b", threads.group(1)),
                      re.findall(r"\bIPC_COMMAND_([A-Z_]+)\b", commands.group(1)))),
        )
        self.assertIn("_Static_assert(", fixture)
        for family, entry in (("ecall", "micros_scheduler_test_enter_without_timer"),
                              ("syscall", "micros_scheduler_start")):
            facade = kernel_layout_source.read_path(REPOSITORY_ROOT / f"kernel/ipc_{family}_test.c")
            self.assertEqual(1, facade.count('"mv %0, sp'))
            self.assertLess(facade.index('"mv %0, sp'), facade.index(entry))
            for path in SOURCE_SETS[
                "MICROS_IPC_ECALL_QEMU_TEST_SOURCES" if family == "ecall"
                else "MICROS_IPC_SYSCALL_QEMU_TEST_SOURCES"
            ][1:]:
                self.assertNotIn('"mv %0, sp', kernel_layout_source.read_path(REPOSITORY_ROOT / path), path)
            owner = kernel_layout_source.read_path(REPOSITORY_ROOT / f"kernel/ipc_{family}_test_fixture.c")
            for register in ("sp", "gp", "tp"):
                self.assertEqual(1, owner.count(f"uintptr_t micros_ipc_{family}_test_saved_{register};"))
        cmake = kernel_layout_source.read_path(REPOSITORY_ROOT / "CMakeLists.txt")
        markers = {
            "test-qemu-ipc": "MICROS_IPC_TEST_PASS endpoints=generation-safe queues=blocking calls=tokenized notifications=coalesced kernel-events=injected deadlock=rejected",
            "test-qemu-ipc-ecall-core": "MICROS_IPC_ECALL_CORE_TEST_PASS dispatch=production guard=transactional completion=returned errors=stable registers=preserved",
            "test-qemu-ipc-syscall": "MICROS_IPC_SYSCALL_TEST_PASS spaces=three operations=six blocking=validated calls=tokenized notifications=coalesced errors=stable completion=deferred registers=preserved cleanup=complete",
            "test-qemu-ipc-syscall-panic": "MICROS_PANIC reason=invalid-bootstrap-ipc-buffer",
        }
        for target, marker in markers.items():
            block = cmake_command(cmake, "add_custom_target", target)
            self.assertIn(marker, block)
            self.assertRegex(block, r"--timeout\s+8\b")
            self.assertEqual(1, cmake.count(marker))
        negative = kernel_layout_source.read_path(REPOSITORY_ROOT / "kernel/ipc_syscall_test_fixture.c")
        self.assertEqual(
            {"accounting_owner", "accounting_started_at", "kernel_counter_ticks"},
            set(re.findall(r"negative_objects_candidate\.harts\s*\[[^]]+\]\s*\.\s*(\w+)\s*=", negative)),
        )
        self.assertIn("].user_context", negative)
        self.assertIn("].accounted_thread", negative)
        hooks = kernel_layout_source.read_path(REPOSITORY_ROOT / "kernel/ipc_syscall_test_hooks.c")
        self.assertIn("fixture->timer_completion_started", hooks)
        self.assertIn("fixture->timer_expected_context", hooks)

    def test_source_sets_and_current_consumers_are_exact(self) -> None:
        cmake = kernel_layout_source.read_path(REPOSITORY_ROOT / "CMakeLists.txt")
        for name, expected in SOURCE_SETS.items():
            block = cmake_command(cmake, "set", name)
            self.assertEqual(
                expected,
                tuple(re.findall(r"(?:kernel|tests/host)/[A-Za-z0-9_]+\.c", block)),
                name,
            )
            for path in expected:
                self.assertEqual(1, cmake.count(path), path)
        for target, required in {
            "endpoint_test": (
                "MICROS_IPC_CORE_SOURCES", "MICROS_IPC_OPERATION_HOST_TEST_SOURCES",
                "MICROS_IPC_CALL_HOST_TEST_SOURCES", "MICROS_IPC_REPLY_HOST_TEST_SOURCES",
                "MICROS_IPC_NOTIFY_HOST_TEST_SOURCES", "MICROS_IPC_CLOSE_HOST_TEST_SOURCES",
            ),
            "ipc_model_test": (
                "MICROS_IPC_CORE_SOURCES", "MICROS_IPC_MODEL_HOST_TEST_SOURCES",
                "MICROS_IPC_NOTIFY_HOST_TEST_SOURCES", "MICROS_IPC_CLOSE_HOST_TEST_SOURCES",
            ),
            "bootstrap_control_test": ("MICROS_IPC_CORE_SOURCES",),
            "pm_test": ("MICROS_IPC_CORE_SOURCES",),
            "micros": ("MICROS_IPC_CORE_SOURCES", "MICROS_IPC_SYSCALL_SOURCES"),
        }.items():
            block = cmake_command(cmake, "add_executable", target)
            for name in SOURCE_SETS:
                self.assertEqual(int(name in required), block.count(f"${{{name}}}"),
                                 (target, name))
            for source in all_sources():
                self.assertNotIn(source, block, target)
        for name, count in {
            "MICROS_IPC_CORE_SOURCES": 5, "MICROS_IPC_SYSCALL_SOURCES": 1,
            "MICROS_IPC_OPERATION_HOST_TEST_SOURCES": 1, "MICROS_IPC_CALL_HOST_TEST_SOURCES": 1,
            "MICROS_IPC_REPLY_HOST_TEST_SOURCES": 1, "MICROS_IPC_NOTIFY_HOST_TEST_SOURCES": 2,
            "MICROS_IPC_CLOSE_HOST_TEST_SOURCES": 2, "MICROS_IPC_MODEL_HOST_TEST_SOURCES": 1,
            "MICROS_IPC_QEMU_TEST_SOURCES": 1, "MICROS_IPC_ECALL_QEMU_TEST_SOURCES": 1,
            "MICROS_IPC_SYSCALL_QEMU_TEST_SOURCES": 1,
        }.items():
            self.assertEqual(count, cmake.count(f"${{{name}}}"), name)
        for flag, source_set, assembly in (
            ("MICROS_BUILD_IPC_TEST", "MICROS_IPC_QEMU_TEST_SOURCES", None),
            ("MICROS_BUILD_IPC_ECALL_CORE_TEST", "MICROS_IPC_ECALL_QEMU_TEST_SOURCES", "arch/riscv64/ipc_ecall_test.S"),
            ("MICROS_BUILD_IPC_SYSCALL_TEST", "MICROS_IPC_SYSCALL_QEMU_TEST_SOURCES", "arch/riscv64/ipc_syscall_test.S"),
        ):
            sources = [block for block in cmake_if_blocks(cmake, flag) if "target_sources(" in block]
            self.assertEqual(1, len(sources), flag)
            self.assertEqual(1, sources[0].count("${" + source_set + "}"))
            if assembly:
                self.assertEqual(1, sources[0].count(assembly))

    def test_exact_public_case_and_hook_owner_migration(self) -> None:
        self.assertEqual(70, len(INITIAL_SYMBOL_OWNER_MIGRATION))
        self.assertEqual(len(SYMBOL_OWNER_MIGRATION), len(set(SYMBOL_OWNER_MIGRATION)))
        self.assertLessEqual(set(SYMBOL_OWNER_MIGRATION), set(INITIAL_SYMBOL_OWNER_MIGRATION))
        self.assertEqual((), SYMBOL_OWNER_MIGRATION)
        current = {name: owner for owner, _, name in SYMBOL_OWNER_MIGRATION}
        definitions = {}
        for path in all_sources():
            if kernel_layout_source.is_file_path(REPOSITORY_ROOT / path):
                for name in self.definitions[path]:
                    definitions.setdefault(name, []).append(path)
        for owner, names in FINAL_OWNER_GROUPS.items():
            for name in names:
                self.assertEqual([current.get(name, owner)], definitions.get(name, []), name)

    def test_new_paths_inherit_complete_original_classification(self) -> None:
        baselines = {
            name: paths[-1] if name in {"MICROS_IPC_CORE_SOURCES", "MICROS_IPC_SYSCALL_SOURCES"}
            else paths[0]
            for name, paths in SOURCE_SETS.items()
        }
        observed = dict(validation_plan.PATH_CLASSIFICATION_EQUIVALENTS)
        for name, baseline in baselines.items():
            self.assertEqual(
                tuple(path for path in (*SOURCE_SETS[name], PRIVATE_HEADERS[name])
                      if path != baseline),
                observed.get(baseline),
                baseline,
            )
            for path in observed[baseline]:
                for tier in ("fast", "pr"):
                    self.assertEqual(
                        validation_plan.plan([baseline], tier, "main"),
                        validation_plan.plan([path], tier, "main"),
                        path,
                    )
        self.assertIn(
            "tests/host/test_kernel_ipc_source_layout.py",
            validation_plan.HOST_UNIT_ONLY_INPUTS,
        )

    def test_long_migration_matches_every_actual_signal(self) -> None:
        self.assertEqual(42, len(INITIAL_LONG_FUNCTION_MIGRATION))
        oversized = {
            (path, name)
            for path in all_sources() if kernel_layout_source.is_file_path(REPOSITORY_ROOT / path)
            for name, size in self.definitions[path].items()
            if size > FUNCTION_REVIEW_LINE_LIMIT
        }
        self.assertLessEqual(set(LONG_FUNCTION_MIGRATION), set(INITIAL_LONG_FUNCTION_MIGRATION))
        self.assertEqual(set(LONG_FUNCTION_MIGRATION), oversized)
        self.assertEqual((), LONG_FUNCTION_MIGRATION)


if __name__ == "__main__":
    unittest.main()
