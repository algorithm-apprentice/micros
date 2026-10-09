#ifndef MICROS_SERVERS_TTY_UART_H
#define MICROS_SERVERS_TTY_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "servers/tty/tty_core.h"

enum {
    MICROS_TTY_UART_REGISTER_RBR_THR_DLL = 0,
    MICROS_TTY_UART_REGISTER_IER_DLM = 1,
    MICROS_TTY_UART_REGISTER_IIR_FCR = 2,
    MICROS_TTY_UART_REGISTER_LCR = 3,
    MICROS_TTY_UART_REGISTER_MCR = 4,
    MICROS_TTY_UART_REGISTER_LSR = 5,
    MICROS_TTY_UART_REGISTER_MSR = 6,
    MICROS_TTY_UART_FIFO_CAPACITY = 16,
};

enum micros_tty_uart_error {
    MICROS_TTY_UART_OK = 0,
    MICROS_TTY_UART_ERROR_ARGUMENT,
    MICROS_TTY_UART_ERROR_STATE,
    MICROS_TTY_UART_ERROR_INVARIANT,
};

struct micros_tty_uart_bus {
    uint8_t (*read)(void *context, uint8_t offset);
    void (*write)(void *context, uint8_t offset, uint8_t value);
    void *context;
};

struct micros_tty_uart_state {
    uint64_t initialization_magic;
    uint8_t interrupt_enable;
};

struct micros_tty_uart_drain_effects {
    bool writable;
};

enum micros_tty_uart_error micros_tty_uart_initialize(
    struct micros_tty_uart_state *uart,
    const struct micros_tty_uart_bus *bus,
    const struct micros_tty_state *terminal
);

enum micros_tty_uart_error micros_tty_uart_validate(
    const struct micros_tty_uart_state *uart,
    const struct micros_tty_state *terminal
);

enum micros_tty_uart_error micros_tty_uart_apply_effects(
    struct micros_tty_uart_state *uart,
    const struct micros_tty_uart_bus *bus,
    const struct micros_tty_state *terminal,
    const struct micros_tty_effects *effects
);

enum micros_tty_uart_error micros_tty_uart_drain(
    struct micros_tty_uart_state *uart,
    const struct micros_tty_uart_bus *bus,
    struct micros_tty_state *terminal,
    struct micros_tty_uart_drain_effects *effects
);

#endif
