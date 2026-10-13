#include "ipc_syscall_test_internal.h"

static bool hold_syscall_fixture_threads(
    struct ipc_syscall_test_context *fixture,
    struct micros_kernel_objects *objects,
    uint64_t *failure_stage
)
{
    size_t index;

    for (index = 0; index < IPC_SYSCALL_PROCESS_COUNT; ++index) {
        if (
            (
                objects->threads[fixture->threads[index].slot].runtime_flags
                & MICROS_THREAD_RTS_INACTIVE
            ) == 0
            && micros_thread_runtime_flags_set(
                objects,
                fixture->threads[index],
                MICROS_THREAD_RTS_INACTIVE
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            (*failure_stage) = UINT64_C(0x80) + index;
            return false;
        }
    }
    return true;
}

static bool close_syscall_fixture_endpoints(
    struct ipc_syscall_test_context *fixture,
    struct micros_kernel_objects *objects,
    uint64_t *failure_stage
)
{
    size_t index;

    for (index = 0; index < IPC_SYSCALL_PROCESS_COUNT; ++index) {
        enum micros_ipc_error close_error =
            micros_ipc_endpoint_close(
                fixture->registry,
                objects,
                fixture->endpoints[index]
            );

        if (close_error != MICROS_IPC_OK) {
            const struct micros_thread *thread =
                &objects->threads[fixture->threads[index].slot];
            const struct micros_endpoint_record *endpoint =
                &fixture->registry->endpoints[fixture->processes[index].slot];

            uart_write("MICROS_TEST_FAILURE close flags=");
            uart_write_hex64(thread->runtime_flags);
            uart_write(" kind=");
            uart_write_hex64(thread->ipc_queue_kind);
            uart_write(" source=");
            uart_write_hex64(thread->ipc_receive_source);
            uart_write(" buffer=");
            uart_write_hex64(thread->ipc_receive_buffer);
            uart_write(" delivery=");
            uart_write_hex64(thread->ipc_delivery_pending);
            uart_write(" result=");
            uart_write_hex64(thread->ipc_staged_result);
            uart_write(" notifications=");
            uart_write_hex64(
                endpoint->pending_notification_sources
            );
            uart_write(" kernel-events=");
            uart_write_hex64(endpoint->pending_kernel_events);
            uart_write("\n");
            uart_flush();
            (*failure_stage) =
                UINT64_C(0x100)
                + index * UINT64_C(0x10)
                + close_error;
            return false;
        }
    }
    return true;
}

static bool cleanup_syscall_fixture_processes(
    struct ipc_syscall_test_context *fixture,
    struct micros_kernel_objects *objects,
    uint64_t *failure_stage
)
{
    size_t index;

    (*failure_stage) = 2;
    for (index = 0; index < IPC_SYSCALL_PROCESS_COUNT; ++index) {
        if (!ipc_syscall_test_cleanup_process(fixture, objects, index)) {
            (*failure_stage) = UINT64_C(0x200) + index;
            return false;
        }
    }
    return true;
}

_Noreturn void micros_ipc_syscall_test_finish(void)
{
    struct ipc_syscall_test_context *fixture = ipc_syscall_test_context_get();
    const struct micros_frame_ownership *ledger =
        micros_frame_ownership_runtime_ledger();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    uint64_t failure_stage = 1;

    if (
        ledger == NULL
        || objects == NULL
        || fixture->registry == NULL
    ) {
        goto failure;
    }
    if (
        !hold_syscall_fixture_threads(fixture, objects, &failure_stage)
        || !close_syscall_fixture_endpoints(fixture, objects, &failure_stage)
        || !cleanup_syscall_fixture_processes(fixture, objects, &failure_stage)
    ) {
        goto failure;
    }
    failure_stage = 3;
    if (
        objects->live_process_count != fixture->baseline_processes
        || objects->live_thread_count != fixture->baseline_threads
        || objects->registered_hart_count != fixture->baseline_harts
        || ledger->owned_frame_count != fixture->baseline_owned
        || ledger->allocator->free_frame_count != fixture->baseline_free
    ) {
        goto failure;
    }
    failure_stage = 4;
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
        "MICROS_IPC_SYSCALL_TEST_PASS "
        "spaces=three operations=six blocking=validated "
        "calls=tokenized notifications=coalesced "
        "errors=stable completion=deferred "
        "registers=preserved cleanup=complete\n"
    );
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_NONE
    );

failure:
    uart_write("MICROS_TEST_FAILURE ipc-syscall-test stage=");
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

_Noreturn void ipc_syscall_test_setup_failure(uintptr_t saved_status)
{
    riscv_irq_restore(saved_status);
    uart_write("MICROS_TEST_FAILURE ipc-syscall-setup\n");
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_SYSTEM_FAILURE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}
