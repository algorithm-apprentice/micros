#ifndef MICROS_KERNEL_UART_CONSOLE_CORE_H
#define MICROS_KERNEL_UART_CONSOLE_CORE_H

#include <stdbool.h>
#include <stdint.h>

#define MICROS_UART_CONSOLE_INITIALIZATION_MAGIC \
    UINT64_C(0x554152544f574e52)

enum micros_uart_console_owner {
    MICROS_UART_CONSOLE_OWNER_EARLY = 1,
    MICROS_UART_CONSOLE_OWNER_TTY,
    MICROS_UART_CONSOLE_OWNER_PANIC,
};

enum micros_uart_console_error {
    MICROS_UART_CONSOLE_OK = 0,
    MICROS_UART_CONSOLE_ERROR_ARGUMENT,
    MICROS_UART_CONSOLE_ERROR_STATE,
    MICROS_UART_CONSOLE_ERROR_INVARIANT,
};

struct micros_uart_console_state {
    uint64_t initialization_magic;
    enum micros_uart_console_owner owner;
    bool quiesced;
};

enum micros_uart_console_error micros_uart_console_initialize(
    struct micros_uart_console_state *state
);

enum micros_uart_console_error micros_uart_console_validate(
    const struct micros_uart_console_state *state
);

enum micros_uart_console_error
micros_uart_console_preflight_begin(
    const struct micros_uart_console_state *state
);

enum micros_uart_console_error micros_uart_console_quiesce(
    struct micros_uart_console_state *state
);

enum micros_uart_console_error micros_uart_console_commit_handoff(
    struct micros_uart_console_state *state
);

enum micros_uart_console_error micros_uart_console_panic(
    struct micros_uart_console_state *state
);

bool micros_uart_console_output_allowed(
    const struct micros_uart_console_state *state
);

#endif
