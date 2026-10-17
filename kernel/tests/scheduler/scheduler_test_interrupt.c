#include "kernel/tests/scheduler/scheduler_test_internal.h"

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/platform/plic.h"
#include "micros/kernel_object_runtime.h"

static volatile uint8_t *test_uart_interrupt_enable(void)
{
    return (volatile uint8_t *)TEST_UART_INTERRUPT_ENABLE;
}

bool scheduler_test_arm_external_interrupt(
    struct scheduler_test_context *fixture,
    enum test_external_origin origin
)
{
    struct micros_plic_tty_enable_plan plan;

    if (
        origin == TEST_EXTERNAL_NONE
        || fixture->external_origin != TEST_EXTERNAL_NONE
        || !micros_plic_prepare_tty_enable(&plan)
    ) {
        return false;
    }
    fixture->external_origin = origin;
    micros_plic_commit_tty_prepare_prevalidated(&plan);
    micros_plic_commit_tty_enable_prevalidated(&plan);
    riscv_external_interrupt_enable();
    *test_uart_interrupt_enable() =
        TEST_UART_INTERRUPT_TX_EMPTY;
    riscv_mmio_fence();
    return (
        micros_plic_validate(MICROS_PLIC_ENABLED)
        && riscv_external_interrupt_is_enabled()
    );
}

bool scheduler_test_run_supervisor_external_test(struct scheduler_test_context *fixture)
{
    uintptr_t saved_status;
    uint32_t attempt;

    if (
        !fixture->user_external_return_observed
        || !scheduler_test_arm_external_interrupt(fixture, TEST_EXTERNAL_SUPERVISOR)
    ) {
        return false;
    }
    saved_status = riscv_irq_save();
    riscv_irq_restore(MICROS_RISCV_SSTATUS_SIE);
    for (
        attempt = 0;
        attempt < TEST_EXTERNAL_ATTEMPTS
            && !fixture->supervisor_external_observed;
        ++attempt
    ) {
        __asm__ volatile("" : : : "memory");
    }
    (void)riscv_irq_save();
    riscv_irq_restore(saved_status);
    return (
        fixture->supervisor_external_observed
        && fixture->external_origin == TEST_EXTERNAL_NONE
        && !riscv_external_interrupt_is_enabled()
        && micros_plic_validate(MICROS_PLIC_DISABLED)
    );
}

bool micros_scheduler_test_handle_external_interrupt(
    struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    struct scheduler_test_context *fixture =
        scheduler_test_state();
    enum test_external_origin origin = fixture->external_origin;
    bool supervisor_origin;
    uint32_t source = 0;

    if (origin == TEST_EXTERNAL_NONE) {
        return false;
    }
    supervisor_origin = (
        frame != NULL
        && (frame->sstatus & MICROS_RISCV_SSTATUS_SPP) != 0
    );
    if (
        hart == NULL
        || frame == NULL
        || (
            origin == TEST_EXTERNAL_USER
            && supervisor_origin
        )
        || (
            (
                origin == TEST_EXTERNAL_SUPERVISOR
                || origin == TEST_EXTERNAL_IDLE
            )
            && !supervisor_origin
        )
        || (
            origin == TEST_EXTERNAL_IDLE
            && (
                !fixture->idle_external_started
                || fixture->idle_external_observed
                || hart->accounting_owner
                    != MICROS_SCHEDULER_ACCOUNTING_KERNEL
                || hart->current_thread.generation != 0
                || fixture->selector_entry_count
                    != fixture->selector_entries_before_spurious + 1
            )
        )
        || !micros_plic_claim(&source)
        || source != MICROS_TTY_UART_IRQ_SOURCE
    ) {
        return false;
    }
    *test_uart_interrupt_enable() = 0;
    riscv_mmio_fence();
    riscv_external_interrupt_disable();
    if (
        !micros_plic_complete(MICROS_TTY_UART_IRQ_SOURCE)
        || !micros_plic_disable_tty()
    ) {
        return false;
    }
    fixture->external_origin = TEST_EXTERNAL_NONE;
    if (origin == TEST_EXTERNAL_USER) {
        fixture->user_external_observed = true;
    } else if (origin == TEST_EXTERNAL_SUPERVISOR) {
        fixture->supervisor_external_observed = true;
    } else {
        struct micros_kernel_objects *objects =
            micros_kernel_object_runtime_test_registry();

        if (objects == NULL) {
            return false;
        }
        objects->threads[fixture->threads[0].slot].user_context.sepc =
            scheduler_test_user_address_of(micros_scheduler_payload_ecall);
        objects->threads[fixture->threads[0].slot].user_context.s1 = 0;
        if (
            !scheduler_test_expect_no_message_completion(
                fixture,
                objects,
                0,
                MICROS_IPC_OK,
                TEST_COMPLETION_EXTERNAL_IDLE
            )
            || micros_thread_runtime_flags_unset(
                objects,
                fixture->threads[0],
                MICROS_THREAD_RTS_INACTIVE
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
        fixture->idle_external_observed = true;
    }
    return true;
}

void micros_scheduler_test_note_selector_entry(void)
{
    struct scheduler_test_context *fixture =
        scheduler_test_state();
    ++fixture->selector_entry_count;
}

bool micros_scheduler_test_handle_idle_timer(struct micros_hart *hart)
{
    struct scheduler_test_context *fixture =
        scheduler_test_state();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();

    if (
        objects == NULL
        || hart == NULL
        || !fixture->idle_requested
        || fixture->idle_woke_thread
        || micros_scheduler_test_idle_bypass_observed != 1
        || fixture->selector_entry_count != fixture->selector_entries_before_spurious
        || !hart->reschedule_pending
        || hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_KERNEL
    ) {
        return false;
    }
    fixture->spurious_idle_bypassed = true;
    objects->threads[fixture->threads[0].slot].user_context.sepc =
        scheduler_test_user_address_of(micros_scheduler_payload_ecall);
    objects->threads[fixture->threads[0].slot].user_context.s1 = 0;
    if (
        !scheduler_test_expect_no_message_completion(
            fixture,
            objects,
            0,
            MICROS_IPC_OK,
            TEST_COMPLETION_IDLE
        )
        || micros_thread_runtime_flags_unset(
            objects,
            fixture->threads[0],
            MICROS_THREAD_RTS_INACTIVE
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    fixture->idle_woke_thread = true;
    return true;
}
