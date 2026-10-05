#include "arch/riscv64/platform.h"

enum {
    UART0_BASE = 0x10000000,
    UART_THR = 0,
    UART_IER = 1,
    UART_LCR = 3,
    UART_LSR = 5,
    UART_LCR_8N1 = 3,
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

void uart_write_hex64(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;

    uart_write("0x");
    for (shift = 60; shift >= 0; shift -= 4) {
        uint8_t digit = (uint8_t)((value >> (unsigned int)shift) & 0xfU);

        uart_write_character(digits[digit]);
    }
}

void uart_panic_seize(void)
{
    uart0[UART_LCR] = UART_LCR_8N1;
    uart0[UART_IER] = 0;
}

void uart_flush(void)
{
    while ((uart0[UART_LSR] & UART_LSR_TRANSMITTER_EMPTY) == 0) {
    }
}
