#include "ipc_syscall_test_internal.h"

bool micros_ipc_syscall_test_before_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct ipc_syscall_test_context *fixture = ipc_syscall_test_context_get();
    size_t thread_index = ipc_syscall_test_current_thread_index(fixture, hart);
    struct ipc_thread_script *script;

    if (
        frame == NULL
        || thread_index >= IPC_SYSCALL_PROCESS_COUNT
    ) {
        return ipc_syscall_test_test_mismatch(
            1,
            thread_index,
            IPC_COMMAND_NONE
        );
    }
    script = &fixture->scripts[thread_index];
    if (
        !script->armed
        || script->awaiting
        || fixture->scenario_phase
            >= sizeof(ipc_syscall_test_scenario_commands)
                / sizeof(ipc_syscall_test_scenario_commands[0])
        || ipc_syscall_test_scenario_threads[fixture->scenario_phase] != thread_index
        || ipc_syscall_test_scenario_commands[fixture->scenario_phase] != script->command
        || frame->sepc
            != ipc_syscall_test_user_address_of(
                micros_ipc_syscall_test_payload_ecall
            )
    ) {
        return ipc_syscall_test_test_mismatch(
            2,
            thread_index,
            script->command
        );
    }
    if (fixture->scenario_phase == 1) {
        const struct micros_thread *client =
            &micros_kernel_object_runtime_test_registry()
                ->threads[fixture->threads[IPC_SYSCALL_CLIENT].slot];

        if (
            client->runtime_flags != MICROS_THREAD_RTS_IPC_SEND
            || client->ipc_queue_kind != MICROS_IPC_QUEUE_SENDER
        ) {
            return ipc_syscall_test_test_mismatch(
                UINT64_C(0x21),
                thread_index,
                script->command
            );
        }
    }
    if (
        script->command == IPC_COMMAND_PEER_NOTIFY_ONE
        || script->command == IPC_COMMAND_PEER_NOTIFY_TWO
    ) {
        const struct micros_thread *server =
            &micros_kernel_object_runtime_test_registry()
                ->threads[fixture->threads[IPC_SYSCALL_SERVER].slot];

        if (
            server->runtime_flags != MICROS_THREAD_RTS_IPC_REPLY
            || server->ipc_reply_token == 0
            || server->ipc_delivery_pending
        ) {
            return ipc_syscall_test_test_mismatch(
                UINT64_C(0x22),
                thread_index,
                script->command
            );
        }
    }
    if (
        ipc_syscall_test_command_has_stable_error(script->command)
        && !ipc_syscall_test_snapshot_negative_state(
            fixture,
            micros_kernel_object_runtime_test_registry(),
            script->command
        )
    ) {
        return ipc_syscall_test_test_mismatch(
            UINT64_C(0x23),
            thread_index,
            script->command
        );
    }
    ++fixture->scenario_phase;
    ipc_syscall_test_copy_bytes(
        &script->captured,
        (const struct micros_user_context *)frame,
        sizeof(script->captured)
    );
    script->armed = false;
    script->awaiting = true;
    return true;
}

static bool validate_return(
    struct ipc_syscall_test_context *fixture,
    size_t thread_index,
    const struct micros_trap_frame *frame
)
{
    const struct ipc_thread_script *script = &fixture->scripts[thread_index];
    struct micros_user_context expected;

    ipc_syscall_test_copy_bytes(&expected, &script->captured, sizeof(expected));
    expected.a0 = script->expected_result;
    expected.sepc =
        ipc_syscall_test_user_address_of(
            micros_ipc_syscall_test_payload_after_ecall
        );

    return (
        ipc_syscall_test_bytes_equal(
            frame,
            &expected,
            sizeof(expected)
        )
    );
}

static void report_return_mismatch(
    struct ipc_syscall_test_context *fixture,
    size_t thread_index,
    const struct micros_trap_frame *frame
)
{
    const struct ipc_thread_script *script = &fixture->scripts[thread_index];

    uart_write("MICROS_TEST_FAILURE ipc-return expected=");
    uart_write_hex64(script->expected_result);
    uart_write(" actual=");
    uart_write_hex64(frame->a0);
    uart_write(" sepc=");
    uart_write_hex64(frame->sepc);
    uart_write(" a1=");
    uart_write_hex64(frame->a1);
    uart_write(" a7=");
    uart_write_hex64(frame->a7);
    uart_write("\n");
    uart_flush();
}

bool micros_ipc_syscall_test_after_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct ipc_syscall_test_context *fixture = ipc_syscall_test_context_get();
    size_t thread_index = ipc_syscall_test_current_thread_index(fixture, hart);
    struct ipc_thread_script *script;
    enum ipc_syscall_command completed;

    if (
        frame == NULL
        || thread_index >= IPC_SYSCALL_PROCESS_COUNT
    ) {
        return ipc_syscall_test_test_mismatch(
            3,
            thread_index,
            IPC_COMMAND_NONE
        );
    }
    script = &fixture->scripts[thread_index];
    if (script->armed && !script->awaiting) {
        return true;
    }
    if (
        !script->awaiting
        || !validate_return(fixture, thread_index, frame)
    ) {
        if (script->awaiting) {
            report_return_mismatch(fixture, thread_index, frame);
        }
        return ipc_syscall_test_test_mismatch(
            4,
            thread_index,
            script->command
        );
    }
    completed = script->command;
    if (
        ipc_syscall_test_command_has_stable_error(completed)
        && !ipc_syscall_test_negative_state_matches(
            fixture,
            micros_kernel_object_runtime_test_registry(),
            completed
        )
    ) {
        return ipc_syscall_test_test_mismatch(
            UINT64_C(0x24),
            thread_index,
            completed
        );
    }
    script->awaiting = false;
    script->command = IPC_COMMAND_NONE;
    if (
        !ipc_syscall_test_transition_after_return(
            fixture,
            hart,
            frame,
            thread_index,
            completed
        )
    ) {
        return ipc_syscall_test_test_mismatch(5, thread_index, completed);
    }
    return true;
}

static bool prepare_supervisor_exit(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    const uint64_t control_mask =
        MICROS_RISCV_SSTATUS_SIE
        | MICROS_RISCV_SSTATUS_SPIE
        | MICROS_RISCV_SSTATUS_SPP
        | MICROS_RISCV_SSTATUS_SUM
        | IPC_SYSCALL_TEST_SSTATUS_UBE
        | IPC_SYSCALL_TEST_SSTATUS_VS
        | IPC_SYSCALL_TEST_SSTATUS_FS
        | IPC_SYSCALL_TEST_SSTATUS_XS
        | IPC_SYSCALL_TEST_SSTATUS_MXR
        | IPC_SYSCALL_TEST_SSTATUS_SD;

    if (
        objects == NULL
        || micros_scheduler_account_user_trap(
            objects,
            micros_kernel_object_runtime_boot_hart_handle(),
            riscv_read_time()
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_scheduler_test_prepare_supervisor_return(
            hart,
            frame
        ) != MICROS_SCHEDULER_OK
    ) {
        return false;
    }
    frame->sp = micros_ipc_syscall_test_saved_sp;
    frame->sepc =
        (uintptr_t)micros_ipc_syscall_test_supervisor_resume;
    frame->sstatus &= ~control_mask;
    frame->sstatus |= MICROS_RISCV_SSTATUS_SPP;
    return true;
}

bool micros_ipc_syscall_test_before_timer_return(
    struct micros_hart *hart
)
{
    struct ipc_syscall_test_context *fixture = ipc_syscall_test_context_get();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    enum micros_kernel_object_error error;

    if (
        !fixture->timer_completion_started
        || objects == NULL
        || hart == NULL
        || ipc_syscall_test_current_thread_index(fixture, hart) != IPC_SYSCALL_CLIENT
        || !objects->threads[
            fixture->threads[IPC_SYSCALL_CLIENT].slot
        ].ipc_delivery_pending
    ) {
        return ipc_syscall_test_test_mismatch(
            UINT64_C(0xf40),
            ipc_syscall_test_current_thread_index(fixture, hart),
            IPC_COMMAND_NONE
        );
    }
    error = micros_thread_install_policy(
        objects,
        fixture->threads[IPC_SYSCALL_PEER],
        IPC_SYSCALL_TEST_PEER_PRIORITY,
        UINT64_MAX
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return ipc_syscall_test_test_mismatch(
            UINT64_C(0xf50) + (uint64_t)error,
            IPC_SYSCALL_PEER,
            IPC_COMMAND_NONE
        );
    }
    return true;
}

bool micros_ipc_syscall_test_after_timer_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct ipc_syscall_test_context *fixture = ipc_syscall_test_context_get();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    const struct micros_thread *client;
    struct micros_user_context peer_expected;

    if (
        !fixture->timer_completion_started
        || objects == NULL
        || hart == NULL
        || frame == NULL
        || ipc_syscall_test_current_thread_index(fixture, hart) != IPC_SYSCALL_PEER
    ) {
        return false;
    }
    client = &objects->threads[fixture->threads[IPC_SYSCALL_CLIENT].slot];
    ipc_syscall_test_copy_bytes(
        &peer_expected,
        &fixture->scripts[IPC_SYSCALL_PEER].captured,
        sizeof(peer_expected)
    );
    peer_expected.a0 = MICROS_IPC_ABI_OK;
    peer_expected.sepc =
        ipc_syscall_test_user_address_of(
            micros_ipc_syscall_test_payload_spin
        );
    if (
        !client->ipc_delivery_pending
        || client->ipc_staged_result
            != MICROS_IPC_ERROR_DEAD_ENDPOINT
        || !ipc_syscall_test_bytes_equal(
            frame,
            &peer_expected,
            sizeof(peer_expected)
        )
        || !ipc_syscall_test_message_matches(
            fixture,
            IPC_SYSCALL_PEER,
            IPC_SYSCALL_TEST_INBOUND_ADDRESS,
            fixture->endpoints[IPC_SYSCALL_SERVER],
            UINT32_C(0x1801),
            UINT8_C(0x90),
            IPC_TOKEN_ZERO,
            NULL
        )
        || !micros_thread_ipc_state_is_clear(
            &objects->threads[fixture->threads[IPC_SYSCALL_PEER].slot]
        )
        || micros_scheduler_account_user_trap(
            objects,
            micros_kernel_object_runtime_boot_hart_handle(),
            riscv_read_time()
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_thread_scheduler_hold(
            objects,
            fixture->threads[IPC_SYSCALL_PEER]
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_scheduler_select_user_return(hart, frame)
            != MICROS_SCHEDULER_OK
        || ipc_syscall_test_current_thread_index(fixture, hart) != IPC_SYSCALL_CLIENT
        || !ipc_syscall_test_bytes_equal(
            frame,
            &fixture->timer_expected_context,
            sizeof(fixture->timer_expected_context)
        )
        || !micros_thread_ipc_state_is_clear(
            &objects->threads[fixture->threads[IPC_SYSCALL_CLIENT].slot]
        )
        || !prepare_supervisor_exit(hart, frame)
    ) {
        return false;
    }
    fixture->timer_completion_started = false;
    return true;
}
