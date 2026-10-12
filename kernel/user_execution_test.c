#include "micros/user_execution.h"

#include <stdbool.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "kernel/user_execution_test_internal.h"
#include "micros/scheduler.h"

_Noreturn void micros_user_execution_test_enter_production(
    uint64_t thread_slot,
    uint64_t thread_generation
)
{
    struct micros_thread_handle thread = {
        (uint16_t)thread_slot,
        (uint32_t)thread_generation,
    };

    micros_scheduler_test_enter_without_timer(thread);
}

static bool supervisor_return_is_valid(
    const struct micros_user_execution_test_context *context
)
{
    return (
        context->test_state
            == MICROS_USER_EXECUTION_TEST_SUPERVISOR
        && (
            micros_user_execution_test_immediate_status
            & MICROS_RISCV_SSTATUS_SIE
        ) == 0
        && micros_user_execution_test_bytes_equal(
            micros_user_execution_test_saved_state,
            micros_user_execution_test_restored_state,
            sizeof(uint64_t) * 17
        )
    );
}

static _Noreturn void publish_lifecycle_result(bool passed)
{
    if (passed) {
        uart_write(
            "MICROS_USER_EXECUTION_TEST_PASS "
            "mode=entered faults=isolated context=preserved "
            "stack=owned return=resumed\n"
        );
        uart_flush();
        (void)sbi_system_reset(
            SBI_RESET_TYPE_SHUTDOWN,
            SBI_RESET_REASON_NONE
        );
    }
    uart_write("MICROS_TEST_FAILURE user-execution-test\n");
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_SYSTEM_FAILURE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}

static _Noreturn void fail_setup(uintptr_t saved_status)
{
    riscv_irq_restore(saved_status);
    uart_write("MICROS_TEST_FAILURE user-execution-setup\n");
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_SYSTEM_FAILURE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}

_Noreturn void micros_user_execution_runtime_run_self_test(void)
{
    struct micros_user_execution_test_context *context =
        micros_user_execution_test_context();
    uintptr_t saved_status;

    saved_status = riscv_irq_save();
    if (
        !micros_user_execution_test_acquire(context)
        || !micros_user_execution_test_create_fixture(context)
        || !micros_user_execution_test_install_payload(context)
        || !micros_user_execution_test_initialize_prepare_state(
            context
        )
        || !micros_user_execution_test_reject_invalid_preparations(
            context
        )
        || !micros_user_execution_test_prepare_threads(context)
    ) {
        fail_setup(saved_status);
    }
    context->test_state = MICROS_USER_EXECUTION_TEST_KERNEL_FAULT;
    micros_user_execution_test_enter(
        context->test_thread.slot,
        context->test_thread.generation
    );
    if (!supervisor_return_is_valid(context)) {
        fail_setup(saved_status);
    }
    publish_lifecycle_result(
        micros_user_execution_test_finish(context)
    );
}
