#include "micros/panic.h"

#include <stdint.h>

#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"

#ifndef MICROS_VERSION
#error "MICROS_VERSION must be defined by the build"
#endif

_Noreturn void micros_panic_report(
    uintptr_t hart_id,
    const char *reason,
    const char *file,
    uint32_t line,
    const struct micros_trap_frame *trap_frame,
    const struct micros_panic_machine_context *context
)
{
    uart_panic_seize();

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
