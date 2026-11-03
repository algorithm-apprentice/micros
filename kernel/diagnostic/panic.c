#include "micros/panic.h"

#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "tests/qemu/terminal_failure_kernel.h"
#include "kernel/diagnostic/panic_control.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/platform/plic.h"
#include "kernel/tty/tty_handoff_runtime.h"
#include "kernel/diagnostic/diagnostic_history.h"

#ifndef MICROS_VERSION
#error "MICROS_VERSION must be defined by the build"
#endif

static bool panic_seizure_failure_emitted;

void micros_panic_seize(void)
{
    bool plic_disabled;
    bool tty_panicked;

    (void)riscv_irq_save();
    riscv_external_interrupt_disable();
    plic_disabled = micros_plic_panic_disable();
    tty_panicked = (
        micros_tty_handoff_runtime_panic()
            == MICROS_TTY_HANDOFF_OK
    );
    if (uart_panic_seize()) {
        uart_write("MICROS_CONSOLE_METADATA_REPAIRED\n");
    }
    if (
        (!plic_disabled || !tty_panicked)
        && !panic_seizure_failure_emitted
    ) {
        uart_write("MICROS_PANIC_SEIZURE_FAILURE plic=");
        uart_write(plic_disabled ? "disabled" : "failed");
        uart_write(" tty=");
        uart_write(tty_panicked ? "panic" : "failed");
        uart_write("\n");
        uart_flush();
        panic_seizure_failure_emitted = true;
    }
}

static _Noreturn void panic_report_claimed(
    uintptr_t hart_id,
    const char *reason,
    const char *file,
    uint32_t line,
    const struct micros_trap_frame *trap_frame,
    const struct micros_panic_machine_context *context
)
{
#ifdef MICROS_BUILD_TERMINAL_FAILURE_TEST
    micros_terminal_test_report(context);
#endif
    micros_panic_seize();
#ifdef MICROS_BUILD_TERMINAL_FAILURE_TEST
    micros_terminal_test_after_seize(hart_id);
#endif

    uart_write("MICROS_PANIC reason=");
    uart_write(reason);
    uart_write("\n");

    uart_write("MICROS_PANIC_BUILD version=" MICROS_VERSION "\n");

    uart_write("MICROS_PANIC_SOURCE file=");
    uart_write(file);
    uart_write(" line=");
    uart_write_hex64(line);
    uart_write("\n");

    uart_write("MICROS_PANIC_HART mode=S id=");
    uart_write_hex64(hart_id);
    uart_write("\n");

    uart_write("MICROS_PANIC_MACHINE sstatus=");
    uart_write_hex64(context->sstatus);
    uart_write(" scause=");
    uart_write_hex64(context->scause);
    uart_write(" stval=");
    uart_write_hex64(context->stval);
    uart_write(" sepc=");
    uart_write_hex64(context->sepc);
    uart_write(" ra=");
    uart_write_hex64(context->ra);
    uart_write(" sp=");
    uart_write_hex64(context->sp);
    uart_write("\n");

    if (trap_frame != NULL) {
        uart_write("MICROS_TRAP_CONTEXT origin=");
        if (
            (trap_frame->sstatus & MICROS_RISCV_SSTATUS_SPP) != 0
        ) {
            uart_write("S");
        } else {
            uart_write("U");
        }
        uart_write(" sstatus=");
        uart_write_hex64(trap_frame->sstatus);
        uart_write(" scause=");
        uart_write_hex64(trap_frame->scause);
        uart_write(" stval=");
        uart_write_hex64(trap_frame->stval);
        uart_write(" sepc=");
        uart_write_hex64(trap_frame->sepc);
        uart_write(" ra=");
        uart_write_hex64(trap_frame->ra);
        uart_write(" sp=");
        uart_write_hex64(trap_frame->sp);
        uart_write("\n");
    }

    micros_diagnostic_panic_report();
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_SYSTEM_FAILURE
    );

    uart_write("MICROS_TEST_FAILURE sbi-system-reset-returned\n");
    uart_flush();
    for (;;) {
        __asm__ volatile("wfi");
    }
}

_Noreturn void micros_panic_report(
    uintptr_t hart_id,
    const char *reason,
    const char *file,
    uint32_t line,
    const struct micros_trap_frame *trap_frame,
    const struct micros_panic_machine_context *context
)
{
    if (!micros_panic_claim_first()) {
        micros_panic_terminate_no_console();
    }
    panic_report_claimed(hart_id, reason, file, line, trap_frame, context);
}
