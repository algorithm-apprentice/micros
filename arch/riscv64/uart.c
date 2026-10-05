#include "arch/riscv64/platform.h"

enum {
    UART0_BASE = 0x10000000,
    UART_THR = 0,
    UART_LSR = 5,
    UART_LSR_THR_EMPTY = 1 << 5,
    UART_LSR_TRANSMITTER_EMPTY = 1 << 6,
};

static volatile uint8_t *const uart0 =
    (volatile uint8_t *)(uintptr_t)UART0_BASE;

static void uart_write_character(char character)
{
    while ((uart0[UART_LSR] & UART_LSR_THR_EMPTY) == 0) {
    }
    uart0[UART_THR] = (uint8_t)character;
}

void uart_write(const char *text)
{
    while (*text != '\0') {
        if (*text == '\n') {
            uart_write_character('\r');
        }
        uart_write_character(*text);
        ++text;
    }
}

void uart_flush(void)
{
    while ((uart0[UART_LSR] & UART_LSR_TRANSMITTER_EMPTY) == 0) {
    }
}
