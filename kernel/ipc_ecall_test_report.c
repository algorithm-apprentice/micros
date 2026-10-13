#include "ipc_ecall_test_internal.h"

bool micros_ipc_ecall_core_test_finish_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct ipc_ecall_test_context *fixture = ipc_ecall_test_context_get();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    const struct micros_thread *thread;
    const struct micros_endpoint_record *server;
    const uint64_t control =
        *(const volatile uint64_t *)(uintptr_t)(
            fixture->data_physical + IPC_ECALL_TEST_CONTROL_OFFSET
        );
    const uint64_t control_mask =
        MICROS_RISCV_SSTATUS_SIE
        | MICROS_RISCV_SSTATUS_SPIE
        | MICROS_RISCV_SSTATUS_SPP
        | MICROS_RISCV_SSTATUS_SUM
        | IPC_ECALL_TEST_SSTATUS_UBE
        | IPC_ECALL_TEST_SSTATUS_VS
        | IPC_ECALL_TEST_SSTATUS_FS
        | IPC_ECALL_TEST_SSTATUS_XS
        | IPC_ECALL_TEST_SSTATUS_MXR
        | IPC_ECALL_TEST_SSTATUS_SD;

    if (
        objects == NULL
        || hart == NULL
        || frame == NULL
        || fixture->registry == NULL
        || control != 3
        || frame->a4 != IPC_ECALL_TEST_A4
        || frame->a5 != IPC_ECALL_TEST_A5
        || frame->a6 != IPC_ECALL_TEST_A6
        || frame->a7 != UINT64_MAX
        || frame->sepc
            != ipc_ecall_test_user_address_of(
                micros_ipc_ecall_test_payload_done
            )
    ) {
        return false;
    }
    thread = &objects->threads[fixture->client_thread.slot];
    server = &fixture->registry->endpoints[fixture->processes[IPC_ECALL_SERVER].slot];
    if (
        !ipc_ecall_test_thread_ipc_state_is_clear(thread)
        || server->pending_events[fixture->processes[IPC_ECALL_CLIENT].slot]
            != UINT64_C(5)
        || server->pending_kernel_events != 0
        || (
            server->pending_notification_sources
            & (UINT64_C(1) << fixture->processes[IPC_ECALL_CLIENT].slot)
        ) == 0
        || micros_scheduler_test_prepare_supervisor_return(
            hart,
            frame
        ) != MICROS_SCHEDULER_OK
    ) {
        return false;
    }
    frame->sp = micros_ipc_ecall_test_saved_sp;
    frame->sepc =
        (uintptr_t)micros_ipc_ecall_test_supervisor_resume;
    frame->sstatus &= ~control_mask;
    frame->sstatus |= MICROS_RISCV_SSTATUS_SPP;
    return true;
}

static bool detach_ecall_client_execution(
    struct ipc_ecall_test_context *fixture,
    struct micros_kernel_objects *objects,
    uint64_t *failure_stage
)
{
    (*failure_stage) = 2;
    if (
        micros_thread_scheduler_remove(
            objects,
            fixture->client_thread
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_user_execution_detach(fixture->client_thread)
            != MICROS_USER_EXECUTION_OK
        || micros_thread_release(objects, fixture->client_thread)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    return true;
}

static bool release_ecall_client_image_and_process(
    struct ipc_ecall_test_context *fixture,
    struct micros_kernel_objects *objects,
    uint64_t *failure_stage
)
{
    uint64_t released;

    (*failure_stage) = 3;
    if (
        micros_user_address_space_release_page(
            fixture->processes[IPC_ECALL_CLIENT],
            IPC_ECALL_TEST_CODE_VIRTUAL_ADDRESS,
            &released
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || released != fixture->code_physical
        || micros_user_address_space_release_page(
            fixture->processes[IPC_ECALL_CLIENT],
            IPC_ECALL_TEST_DATA_VIRTUAL_ADDRESS,
            &released
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || released != fixture->data_physical
        || micros_user_address_space_release_page(
            fixture->processes[IPC_ECALL_CLIENT],
            IPC_ECALL_TEST_STACK_VIRTUAL_ADDRESS,
            &released
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || released != fixture->stack_physical
        || micros_user_address_space_destroy(
            fixture->processes[IPC_ECALL_CLIENT]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_frame_ownership_runtime_release_process(
            objects,
            fixture->processes[IPC_ECALL_CLIENT]
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    return true;
}

_Noreturn void micros_ipc_ecall_core_test_finish(void)
{
    struct ipc_ecall_test_context *fixture = ipc_ecall_test_context_get();
    const struct micros_frame_ownership *ledger =
        micros_frame_ownership_runtime_ledger();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    uint64_t failure_stage = 1;

    if (
        ledger == NULL
        || objects == NULL
        || fixture->registry == NULL
        || micros_ipc_endpoint_close(
            fixture->registry,
            objects,
            fixture->endpoints[IPC_ECALL_CLIENT]
        ) != MICROS_IPC_OK
        || micros_ipc_endpoint_close(
            fixture->registry,
            objects,
            fixture->endpoints[IPC_ECALL_SERVER]
        ) != MICROS_IPC_OK
    ) {
        goto failure;
    }
    if (
        !detach_ecall_client_execution(fixture, objects, &failure_stage)
        || !release_ecall_client_image_and_process(fixture, objects, &failure_stage)
    ) {
        goto failure;
    }
    failure_stage = 4;
    if (
        micros_frame_ownership_runtime_release_process(
            objects,
            fixture->processes[IPC_ECALL_SERVER]
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        goto failure;
    }
    failure_stage = 5;
    if (
        objects->live_process_count != fixture->baseline_processes
        || objects->live_thread_count != fixture->baseline_threads
        || objects->registered_hart_count != fixture->baseline_harts
        || ledger->owned_frame_count != fixture->baseline_owned
        || ledger->allocator->free_frame_count != fixture->baseline_free
    ) {
        goto failure;
    }
    failure_stage = 6;
    if (
        micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
        || micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        goto failure;
    }
    uart_write(
        "MICROS_IPC_ECALL_CORE_TEST_PASS "
        "dispatch=production guard=transactional "
        "completion=returned errors=stable registers=preserved\n"
    );
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_NONE
    );

failure:
    uart_write("MICROS_TEST_FAILURE ipc-ecall-core stage=");
    uart_write_hex64(failure_stage);
    uart_write("\n");
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_SYSTEM_FAILURE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}
