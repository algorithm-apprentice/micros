from __future__ import annotations

import hashlib
from pathlib import Path
import re
import unittest

from tests.host.test_frame_ownership_source_layout import (
    c_function_definitions,
    cmake_command,
)
from tests.host.test_vm_service_source_layout import c_declaration_text, file_scope_objects
from tools import validation_plan


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
SYMBOL_OWNER_MIGRATION = tuple(
    entry for entry in INITIAL_SYMBOL_OWNER_MIGRATION
    if entry[1] not in {
        "kernel/ipc_common.c",
        "kernel/ipc_completion.c",
        "kernel/ipc_notifications.c",
        "kernel/ipc_rendezvous.c",
        "kernel/ipc_close.c",
        "kernel/ipc_reply.c",
        "tests/host/ipc_send_operation_test.c",
        "tests/host/ipc_receive_operation_test.c",
        "tests/host/ipc_call_delivery_test.c",
        "tests/host/ipc_call_validation_test.c",
        "tests/host/ipc_reply_authority_test.c",
        "tests/host/ipc_reply_validation_test.c",
        "tests/host/ipc_reply_receive_test.c",
        "tests/host/ipc_notify_delivery_test.c",
        "tests/host/ipc_kernel_notify_test.c",
        "tests/host/ipc_notify_validation_test.c",
        "tests/host/ipc_notify_model_test.c",
        "tests/host/ipc_close_cancellation_test.c",
        "tests/host/ipc_close_validation_test.c",
        "tests/host/ipc_close_model_test.c",
    }
)

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
LONG_FUNCTION_MIGRATION = tuple(
    entry for entry in INITIAL_LONG_FUNCTION_MIGRATION
    if entry[0] != "tests/host/ipc_model_test.c" and entry[1] not in {
        "micros_ipc_notify",
        "sender_enqueue",
        "micros_ipc_receiver_commit_delivery",
        "sender_commit_delivery",
        "micros_ipc_reply_receive",
        "micros_ipc_handle_captured_user_ecall",
        "test_receive_specific_and_any_fifo",
        "test_queued_calls_bind_exact_threads_and_deliver_fifo",
        "test_call_rejections_are_atomic",
        "setup_reply_fixture",
        "test_reply_requires_request_return",
        "test_reply_routes_exact_tokens_without_send_target",
        "test_reply_rejections_are_atomic",
        "test_reply_receive_blocks_and_later_receives",
        "test_reply_receive_selects_specific_sender_atomically",
        "setup_notify_fixture",
        "test_kernel_notify_prepare_commit_is_retained",
        "test_kernel_notify_validation_and_close_rules",
        "test_receive_prefers_lowest_notification_before_sender",
        "test_reply_receive_prefers_pending_notification",
        "test_notification_validators_and_close_preflight",
        "test_seeded_notify_model",
        "test_close_cancels_complete_transaction",
        "test_successful_staged_deliveries_block_close_atomically",
        "test_close_failures_are_atomic",
        "test_seeded_close_scenarios",
    }
)

FILE_SCOPE_STORAGE_HASHES = {
    "tests/host/ipc_operation_test_fixture.c": "3bbffce409df296cc5ccbc908c8034b84d730d32db201c73f6fdfe128ba065fc",
    "tests/host/ipc_call_test_fixture.c": "3c4205f69f3a21baaeb3e7fada651741446007de252d5d1f5d932e11f2ccbe87",
    "tests/host/ipc_reply_test_fixture.c": "ac463d588ed52d441e693e6f98821b4fbaa84e1ff9a32cb5adbed247e8516ebe",
    "tests/host/ipc_notify_test_fixture.c": "df17fd7674262222781d53f2d3e27a16e331243280131154eb7bfc0e57f78de3",
    "tests/host/ipc_notify_model_test.c": "81f85e33023553fbd2c1b334f5b19342f95962e4ec3c9ddac9982aed6f4a019c",
    "tests/host/ipc_close_test_fixture.c": "38ebf808fef614fc64d51d3057065bee7906afa47d7df17c08e404c0adb1090f",
    "tests/host/ipc_close_model_test.c": "7b2491687b616a4100153002b72e99b1ea7b58b7ba95d4035f3828a713ce477f",
    "tests/host/ipc_model_test_fixture.c": "e833e18b6805722b07da8602feecdf1fbb9bfbd743a123d22da7719df4f6617a",
    "kernel/ipc_test.c": "b0ef5316ebac2b81c2ef1982af6c264bb833355211c27953405971b983c33f28",
    "kernel/ipc_ecall_test.c": "d8b668741da95028e58573d42953fb94c12c3efc32132c2f41054e50a81fdbff",
    "kernel/ipc_syscall_test.c": "edd0f929402275dc494bc18e5d3f585825529f43b45fec956e3394118f654523",
}


def all_sources() -> tuple[str, ...]:
    return tuple(path for paths in SOURCE_SETS.values() for path in paths)


class KernelIpcSourceLayoutTest(unittest.TestCase):
    def test_complete_accepted_source_and_private_header_inventory_exists(self) -> None:
        self.assertEqual(68, len(all_sources()))
        self.assertEqual(11, len(PRIVATE_HEADERS))
        self.assertEqual(
            [],
            [path for path in (*all_sources(), *PRIVATE_HEADERS.values())
             if not (REPOSITORY_ROOT / path).is_file()],
        )

    def test_public_support_and_assembly_surfaces_are_unchanged(self) -> None:
        self.assertEqual(
            STABLE_HASHES,
            {path: hashlib.sha256((REPOSITORY_ROOT / path).read_bytes()).hexdigest()
             for path in STABLE_HASHES},
        )

    def test_private_headers_have_no_function_or_storage_definitions(self) -> None:
        for path in PRIVATE_HEADERS.values():
            text = (REPOSITORY_ROOT / path).read_text()
            self.assertEqual({}, c_function_definitions(text), path)
            self.assertEqual((), file_scope_objects(text), path)
            self.assertNotRegex(c_declaration_text(text), r"\bstatic\b", path)

    def test_current_file_scope_storage_has_exact_approved_owners(self) -> None:
        observed = {}
        for path in all_sources():
            declarations = file_scope_objects((REPOSITORY_ROOT / path).read_text())
            if declarations:
                observed[path] = hashlib.sha256("\n".join(declarations).encode()).hexdigest()
        self.assertEqual(FILE_SCOPE_STORAGE_HASHES, observed)

    def test_source_sets_and_current_consumers_are_exact(self) -> None:
        cmake = (REPOSITORY_ROOT / "CMakeLists.txt").read_text()
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
        for name, count in {
            "MICROS_IPC_CORE_SOURCES": 5, "MICROS_IPC_SYSCALL_SOURCES": 1,
            "MICROS_IPC_OPERATION_HOST_TEST_SOURCES": 1, "MICROS_IPC_CALL_HOST_TEST_SOURCES": 1,
            "MICROS_IPC_REPLY_HOST_TEST_SOURCES": 1, "MICROS_IPC_NOTIFY_HOST_TEST_SOURCES": 2,
            "MICROS_IPC_CLOSE_HOST_TEST_SOURCES": 2, "MICROS_IPC_MODEL_HOST_TEST_SOURCES": 1,
            "MICROS_IPC_QEMU_TEST_SOURCES": 1, "MICROS_IPC_ECALL_QEMU_TEST_SOURCES": 1,
            "MICROS_IPC_SYSCALL_QEMU_TEST_SOURCES": 1,
        }.items():
            self.assertEqual(count, cmake.count(f"${{{name}}}"), name)

    def test_exact_public_case_and_hook_owner_migration(self) -> None:
        self.assertEqual(70, len(INITIAL_SYMBOL_OWNER_MIGRATION))
        self.assertEqual(len(SYMBOL_OWNER_MIGRATION), len(set(SYMBOL_OWNER_MIGRATION)))
        self.assertLessEqual(set(SYMBOL_OWNER_MIGRATION), set(INITIAL_SYMBOL_OWNER_MIGRATION))
        current = {name: owner for owner, _, name in SYMBOL_OWNER_MIGRATION}
        definitions = {}
        for path in all_sources():
            if (REPOSITORY_ROOT / path).is_file():
                for name in c_function_definitions((REPOSITORY_ROOT / path).read_text()):
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
            for path in all_sources() if (REPOSITORY_ROOT / path).is_file()
            for name, size in c_function_definitions((REPOSITORY_ROOT / path).read_text()).items()
            if size > FUNCTION_REVIEW_LINE_LIMIT
        }
        self.assertLessEqual(set(LONG_FUNCTION_MIGRATION), set(INITIAL_LONG_FUNCTION_MIGRATION))
        self.assertEqual(set(LONG_FUNCTION_MIGRATION), oversized)


if __name__ == "__main__":
    unittest.main()
