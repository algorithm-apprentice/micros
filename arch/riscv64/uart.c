#include "arch/riscv64/platform.h"

#include "arch/riscv64/interrupt.h"
#include "kernel/tty/uart_console_core.h"
#include "micros/panic.h"
#include "kernel/diagnostic/diagnostic_history.h"
#ifdef MICROS_BUILD_TTY_SERVICE_TEST
#include "tests/qemu/diagnostic_history_kernel.h"
#endif

enum {
    UART_THR = 0,
    UART_DLL = 0,
    UART_IER = 1,
    UART_DLM = 1,
    UART_LCR = 3,
    UART_LSR = 5,
    UART_LCR_8N1 = 3,
    UART_LCR_DLAB = 1 << 7,
    UART_DIVISOR_LSB = 2,
    UART_DIVISOR_MSB = 0,
    UART_LSR_THR_EMPTY = 1 << 5,
    UART_LSR_TRANSMITTER_EMPTY = 1 << 6,
};

static volatile uint8_t *const uart0 =
    (volatile uint8_t *)(uintptr_t)MICROS_RISCV_UART0_BASE;

static struct micros_uart_console_state uart_console_state = {
    .initialization_magic =
        MICROS_UART_CONSOLE_INITIALIZATION_MAGIC,
    .owner = MICROS_UART_CONSOLE_OWNER_EARLY,
};

static void uart_write_character(char character)
{
    while ((uart0[UART_LSR] & UART_LSR_THR_EMPTY) == 0) {
    }
    uart0[UART_THR] = (uint8_t)character;
#ifdef MICROS_BUILD_TTY_SERVICE_TEST
    micros_diagnostic_test_observe_thr();
#endif
}

static void uart_write_direct(const char *text)
{
    if (!micros_uart_console_output_allowed(&uart_console_state)) {
        MICROS_PANIC(0, "console-owner-violation");
    }
    while (*text != '\0') {
        if (*text == '\n') {
            uart_write_character('\r');
        }
        uart_write_character(*text);
        ++text;
    }
}

static void uart_write_hex64_direct(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;

    uart_write("0x");
    for (shift = 60; shift >= 0; shift -= 4) {
        uint8_t digit = (uint8_t)((value >> (unsigned int)shift) & 0xfU);

        uart_write_character(digits[digit]);
    }
}

static bool uart_panic_mode(void)
{
    return uart_console_state.owner == MICROS_UART_CONSOLE_OWNER_PANIC;
}

static void uart_write_logical_byte(uint8_t byte)
{
    if (!uart_console_state.quiesced) {
        if (byte == (uint8_t)'\n') {
            uart_write_character('\r');
        }
        uart_write_character((char)byte);
    }
    micros_diagnostic_record_byte(byte);
}

static void uart_write_record(const char *text)
{
    uintptr_t saved_status = riscv_irq_save();

    if (
        micros_uart_console_validate(&uart_console_state) != MICROS_UART_CONSOLE_OK
        || uart_console_state.owner == MICROS_UART_CONSOLE_OWNER_PANIC
        || !micros_diagnostic_history_valid(micros_diagnostic_history_state())
    ) {
        MICROS_PANIC(0, "diagnostic-console-invariant");
    }
    while (*text != '\0') {
        uart_write_logical_byte((uint8_t)*text);
        ++text;
    }
    micros_diagnostic_record_end();
    riscv_irq_restore(saved_status);
}

static void uart_write_hex64_record(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    uintptr_t saved_status = riscv_irq_save();
    int shift;

    if (
        micros_uart_console_validate(&uart_console_state) != MICROS_UART_CONSOLE_OK
        || uart_console_state.owner == MICROS_UART_CONSOLE_OWNER_PANIC
        || !micros_diagnostic_history_valid(micros_diagnostic_history_state())
    ) {
        MICROS_PANIC(0, "diagnostic-console-invariant");
    }
    uart_write_logical_byte((uint8_t)'0');
    uart_write_logical_byte((uint8_t)'x');
    for (shift = 60; shift >= 0; shift -= 4) {
        uint8_t digit = (uint8_t)((value >> (unsigned int)shift) & 0xfU);

        uart_write_logical_byte((uint8_t)digits[digit]);
    }
    micros_diagnostic_record_end();
    riscv_irq_restore(saved_status);
}

void uart_write(const char *text)
{
    if (uart_panic_mode()) {
        uart_write_direct(text);
        return;
    }
    uart_write_record(text);
}

void uart_write_hex64(uint64_t value)
{
    if (uart_panic_mode()) {
        uart_write_hex64_direct(value);
        return;
    }
    uart_write_hex64_record(value);
}

bool uart_console_panic_is_active(void)
{
    bool active = (
        micros_uart_console_validate(&uart_console_state)
            == MICROS_UART_CONSOLE_OK
        && uart_console_state.owner
            == MICROS_UART_CONSOLE_OWNER_PANIC
        && uart_console_state.quiesced
        && !riscv_irq_is_enabled()
        && !riscv_external_interrupt_is_enabled()
        && uart0[UART_LCR] == UART_LCR_8N1
        && uart0[UART_IER] == 0
    );

    if (!active) {
        return false;
    }
    uart0[UART_LCR] = UART_LCR_8N1 | UART_LCR_DLAB;
    riscv_mmio_fence();
    active = (
        uart0[UART_DLL] == UART_DIVISOR_LSB
        && uart0[UART_DLM] == UART_DIVISOR_MSB
    );
    uart0[UART_LCR] = UART_LCR_8N1;
    riscv_mmio_fence();
    return active;
}

void uart_panic_seize(void)
{
    (void)riscv_irq_save();
    riscv_external_interrupt_disable();
    if (
        micros_uart_console_panic(&uart_console_state)
        != MICROS_UART_CONSOLE_OK
    ) {
        for (;;) {
        }
    }
    uart0[UART_LCR] = UART_LCR_8N1;
    uart0[UART_IER] = 0;
    uart0[UART_LCR] = UART_LCR_8N1 | UART_LCR_DLAB;
    uart0[UART_DLL] = UART_DIVISOR_LSB;
    uart0[UART_DLM] = UART_DIVISOR_MSB;
    uart0[UART_LCR] = UART_LCR_8N1;
    riscv_mmio_fence();
}

void uart_flush(void)
{
    while ((uart0[UART_LSR] & UART_LSR_TRANSMITTER_EMPTY) == 0) {
    }
}

bool uart_console_begin_preflight(void)
{
    return (
        micros_uart_console_preflight_begin(&uart_console_state)
        == MICROS_UART_CONSOLE_OK
    );
}

void uart_console_begin_quiesce_prevalidated(void)
{
    uart_flush();
    uart0[UART_IER] = 0;
    riscv_mmio_fence();
    if (
        micros_uart_console_quiesce(&uart_console_state)
        != MICROS_UART_CONSOLE_OK
    ) {
        MICROS_PANIC(0, "console-quiesce-invariant");
    }
    micros_diagnostic_handoff();
}

void uart_console_handoff_commit_prevalidated(void)
{
    if (
        micros_uart_console_commit_handoff(&uart_console_state)
        != MICROS_UART_CONSOLE_OK
    ) {
        MICROS_PANIC(0, "console-handoff-invariant");
    }
}

bool uart_console_handoff_is_active(void)
{
    return (
        micros_uart_console_validate(&uart_console_state)
            == MICROS_UART_CONSOLE_OK
        && uart_console_state.owner
            == MICROS_UART_CONSOLE_OWNER_TTY
        && uart_console_state.quiesced
    );
}

bool uart_console_handoff_is_quiesced(void)
{
    return (
        uart_console_handoff_is_active()
        && uart0[UART_IER] == 0
    );
}
