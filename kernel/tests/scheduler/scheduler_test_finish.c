#include "kernel/tests/scheduler/scheduler_test_internal.h"
#include "micros/stack_observation.h"
#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

static bool accumulated_evidence_is_complete(
    struct scheduler_test_context *fixture,
    const struct micros_frame_ownership *ledger,
    const struct micros_kernel_objects *objects
)
{
    if (
        !fixture->supervisor_returned
        || !fixture->start_boundary_observed
        || !fixture->start_completion_observed
        || !fixture->message_completion_deferred
        || !fixture->message_completion_observed
        || !fixture->captured_completion_observed
        || !fixture->error_completion_observed
        || !fixture->idle_completion_observed
        || fixture->timer_trap_count == 0
        || fixture->switch_count < 6
        || !fixture->idle_requested
        || !fixture->spurious_idle_bypassed
        || !fixture->idle_woke_thread
        || !fixture->user_external_started
        || !fixture->user_external_observed
        || !fixture->user_external_return_observed
        || !fixture->supervisor_external_observed
        || !fixture->idle_external_started
        || !fixture->idle_external_observed
        || !fixture->idle_external_completion_observed
        || fixture->selector_entry_count
            != fixture->selector_entries_before_spurious + 2
        || ledger == NULL
        || objects == NULL
        || fixture->previous_counter[0] == 0
        || fixture->previous_counter[1] == 0
        || fixture->ecall_count[0] == 0
        || fixture->ecall_count[1] == 0
        || objects->harts[0].current_thread.generation != 0
        || objects->harts[0].accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_KERNEL
        || objects->harts[0].timer.active
        || (
            micros_scheduler_test_immediate_status
            & MICROS_RISCV_SSTATUS_SIE
        ) != 0
        || !scheduler_test_bytes_equal(
            micros_scheduler_test_saved_state,
            micros_scheduler_test_restored_state,
            sizeof(uint64_t) * 17
        )
    ) {
        return false;
    }
    return true;
}

static bool release_test_process(
    struct scheduler_test_context *fixture,
    struct micros_kernel_objects *objects,
    size_t index
)
{
    uint64_t released;

    if (
        micros_thread_scheduler_remove(
            objects,
            fixture->threads[index]
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_user_execution_detach(fixture->threads[index])
            != MICROS_USER_EXECUTION_OK
        || micros_thread_release(objects, fixture->threads[index])
            != MICROS_KERNEL_OBJECT_OK
        || micros_ipc_endpoint_close(
            fixture->registry,
            objects,
            fixture->endpoints[index]
        ) != MICROS_IPC_OK
        || micros_user_address_space_release_page(
            fixture->processes[index],
            TEST_CODE_VIRTUAL_ADDRESS,
            &released
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || released != fixture->code_physical[index]
        || micros_user_address_space_release_page(
            fixture->processes[index],
            TEST_COUNTER_VIRTUAL_ADDRESS,
            &released
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || released != fixture->counter_physical[index]
        || micros_user_address_space_release_page(
            fixture->processes[index],
            TEST_STACK_VIRTUAL_ADDRESS,
            &released
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || released != fixture->stack_physical[index]
        || micros_user_address_space_destroy(fixture->processes[index])
            != MICROS_USER_ADDRESS_SPACE_OK
        || micros_frame_ownership_runtime_release_process(
            objects,
            fixture->processes[index]
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    return true;
}

static bool cleanup_balance_matches(
    struct scheduler_test_context *fixture,
    const struct micros_frame_ownership *ledger,
    struct micros_kernel_objects *objects
)
{
    if (
        ledger->owned_frame_count != fixture->baseline_owned
        || ledger->allocator->free_frame_count != fixture->baseline_free
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
        || micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        return false;
    }
    return true;
}

static void report_scheduler_success(void)
{
    uart_write(
        "MICROS_IPC_RETURN completions=shared "
        "paths=start,user,captured,idle buffers=bounded\n"
    );
    uart_write(
        "MICROS_SCHEDULER_TEST_PASS "
        "queues=minix-priority current=reachable "
        "accounting=separate switches=alternating "
        "idle=resumed registers=preserved\n"
    );
    uart_write(
        "MICROS_TTY_TRAP_TEST_PASS "
        "user=cause9-scheduled supervisor=cause9-direct "
        "idle=cause9-selected\n"
    );
    uart_flush(); MICROS_TEST_STACK_REPORT();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_NONE
    );
}

_Noreturn void scheduler_test_finish_test(struct scheduler_test_context *fixture)
{
    const struct micros_frame_ownership *ledger =
        micros_frame_ownership_runtime_ledger();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    size_t index;

    if (!scheduler_test_run_supervisor_external_test(fixture)) {
        goto failure;
    }
    if (!accumulated_evidence_is_complete(fixture, ledger, objects)) {
        goto failure;
    }
    for (index = 0; index < 2; ++index) {
        if (!release_test_process(fixture, objects, index)) {
            goto failure;
        }
    }
    if (!cleanup_balance_matches(fixture, ledger, objects)) {
        goto failure;
    }
    report_scheduler_success();

failure:
    uart_write("MICROS_TEST_FAILURE scheduler-test\n");
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_SYSTEM_FAILURE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}
