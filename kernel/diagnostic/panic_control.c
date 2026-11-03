#include "kernel/diagnostic/panic_control.h"

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "kernel/diagnostic/panic_control_core.h"

static struct micros_panic_control panic_control;

bool micros_panic_claim_first(void)
{
    enum micros_panic_enter_result result;

    (void)riscv_irq_save();
    riscv_external_interrupt_disable();
    result = micros_panic_control_enter(&panic_control);
    if (result == MICROS_PANIC_ENTRY_INVARIANT) {
        micros_panic_halt();
    }
    return result == MICROS_PANIC_FIRST;
}

bool micros_panic_claim_reset(void)
{
    enum micros_panic_reset_result result;

    (void)riscv_irq_save();
    riscv_external_interrupt_disable();
    result = micros_panic_control_request_reset(&panic_control);
    if (result == MICROS_PANIC_RESET_INVARIANT) {
        micros_panic_halt();
    }
    return result == MICROS_PANIC_ISSUE;
}

_Noreturn void micros_panic_terminate_no_console(void)
{
    (void)riscv_irq_save();
    riscv_external_interrupt_disable();
    (void)sbi_system_reset(SBI_RESET_TYPE_SHUTDOWN, SBI_RESET_REASON_SYSTEM_FAILURE);
    micros_panic_halt();
}

_Noreturn void micros_panic_halt(void)
{
    for (;;) {
        __asm__ volatile("wfi");
    }
}
