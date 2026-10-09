#include "kernel/uart_console_core.h"

#include <stddef.h>

static void clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

enum micros_uart_console_error micros_uart_console_validate(
    const struct micros_uart_console_state *state
)
{
    if (state == NULL) {
        return MICROS_UART_CONSOLE_ERROR_ARGUMENT;
    }
    if (
        state->initialization_magic
            != MICROS_UART_CONSOLE_INITIALIZATION_MAGIC
    ) {
        return MICROS_UART_CONSOLE_ERROR_INVARIANT;
    }
    switch (state->owner) {
    case MICROS_UART_CONSOLE_OWNER_EARLY:
        return MICROS_UART_CONSOLE_OK;
    case MICROS_UART_CONSOLE_OWNER_TTY:
    case MICROS_UART_CONSOLE_OWNER_PANIC:
        return state->quiesced
            ? MICROS_UART_CONSOLE_OK
            : MICROS_UART_CONSOLE_ERROR_INVARIANT;
    }
    return MICROS_UART_CONSOLE_ERROR_INVARIANT;
}

enum micros_uart_console_error micros_uart_console_initialize(
    struct micros_uart_console_state *state
)
{
    if (state == NULL) {
        return MICROS_UART_CONSOLE_ERROR_ARGUMENT;
    }
    clear_bytes(state, sizeof(*state));
    state->initialization_magic =
        MICROS_UART_CONSOLE_INITIALIZATION_MAGIC;
    state->owner = MICROS_UART_CONSOLE_OWNER_EARLY;
    return MICROS_UART_CONSOLE_OK;
}

enum micros_uart_console_error
micros_uart_console_preflight_begin(
    const struct micros_uart_console_state *state
)
{
    enum micros_uart_console_error error =
        micros_uart_console_validate(state);

    if (error != MICROS_UART_CONSOLE_OK) {
        return error;
    }
    return (
        state->owner == MICROS_UART_CONSOLE_OWNER_EARLY
        && !state->quiesced
    )
        ? MICROS_UART_CONSOLE_OK
        : MICROS_UART_CONSOLE_ERROR_STATE;
}

enum micros_uart_console_error micros_uart_console_quiesce(
    struct micros_uart_console_state *state
)
{
    enum micros_uart_console_error error =
        micros_uart_console_preflight_begin(state);

    if (error != MICROS_UART_CONSOLE_OK) {
        return error;
    }
    state->quiesced = true;
    return MICROS_UART_CONSOLE_OK;
}

enum micros_uart_console_error micros_uart_console_commit_handoff(
    struct micros_uart_console_state *state
)
{
    enum micros_uart_console_error error =
        micros_uart_console_validate(state);

    if (error != MICROS_UART_CONSOLE_OK) {
        return error;
    }
    if (
        state->owner != MICROS_UART_CONSOLE_OWNER_EARLY
        || !state->quiesced
    ) {
        return MICROS_UART_CONSOLE_ERROR_STATE;
    }
    state->owner = MICROS_UART_CONSOLE_OWNER_TTY;
    return MICROS_UART_CONSOLE_OK;
}

enum micros_uart_console_error micros_uart_console_panic(
    struct micros_uart_console_state *state
)
{
    if (state == NULL) {
        return MICROS_UART_CONSOLE_ERROR_ARGUMENT;
    }
    if (
        state->initialization_magic
            != MICROS_UART_CONSOLE_INITIALIZATION_MAGIC
    ) {
        return MICROS_UART_CONSOLE_ERROR_INVARIANT;
    }
    state->owner = MICROS_UART_CONSOLE_OWNER_PANIC;
    state->quiesced = true;
    return MICROS_UART_CONSOLE_OK;
}

bool micros_uart_console_output_allowed(
    const struct micros_uart_console_state *state
)
{
    return (
        micros_uart_console_validate(state)
            == MICROS_UART_CONSOLE_OK
        && state->owner != MICROS_UART_CONSOLE_OWNER_TTY
    );
}
