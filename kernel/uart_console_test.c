#include "kernel/uart_console_test.h"

#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "micros/panic.h"
#include "micros/tty.h"

#define UART_INTERRUPT_ENABLE_OFFSET UINT64_C(1)
#define UART_INTERRUPT_ENABLE_TEST_VALUE UINT8_C(0x03)

static volatile uint8_t *uart_registers(void)
{
    return (volatile uint8_t *)(uintptr_t)
        MICROS_TTY_UART_PHYSICAL_BASE;
}

_Noreturn void micros_uart_console_runtime_run_self_test(
    uintptr_t hart_id
)
{
    volatile uint8_t *registers = uart_registers();

    if (!uart_console_begin_preflight()) {
        MICROS_PANIC(hart_id, "uart-console-preflight");
    }
    uart_write("MICROS_UART_CONSOLE_TEST begin=early\n");
    uart_flush();
    registers[UART_INTERRUPT_ENABLE_OFFSET] =
        UART_INTERRUPT_ENABLE_TEST_VALUE;
    if (
        registers[UART_INTERRUPT_ENABLE_OFFSET]
            != UART_INTERRUPT_ENABLE_TEST_VALUE
    ) {
        MICROS_PANIC(hart_id, "uart-console-ier-write");
    }

    uart_console_begin_quiesce_prevalidated();
    if (registers[UART_INTERRUPT_ENABLE_OFFSET] != 0) {
        MICROS_PANIC(hart_id, "uart-console-ier-disable");
    }
    uart_console_handoff_commit_prevalidated();
    if (!uart_console_handoff_is_quiesced()) {
        MICROS_PANIC(hart_id, "uart-console-handoff");
    }

    registers[UART_INTERRUPT_ENABLE_OFFSET] = UINT8_C(0x01);
    riscv_mmio_fence();
    if (
        !uart_console_handoff_is_active()
        || uart_console_handoff_is_quiesced()
    ) {
        MICROS_PANIC(hart_id, "uart-console-owned-programming");
    }
    riscv_external_interrupt_enable();
    if (!riscv_external_interrupt_is_enabled()) {
        MICROS_PANIC(hart_id, "uart-console-seie-enable");
    }
    uart_panic_seize();
    if (!uart_console_panic_is_active()) {
        MICROS_PANIC(hart_id, "uart-console-panic-seize");
    }
    uart_write(
        "MICROS_UART_CONSOLE_TEST_PASS "
        "quiesce=drained ier=disabled ownership=tty panic=seized\n"
    );
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_NONE
    );
    MICROS_PANIC(hart_id, "uart-console-reset-returned");
}
