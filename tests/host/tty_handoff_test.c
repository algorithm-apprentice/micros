#include "kernel/tty_handoff_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

bool micros_tty_handoff_test_run(void);

#define EXPECT_TRUE(expression) \
    do { \
        if (!(expression)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %s\n", \
                __FILE__, \
                __LINE__, \
                #expression \
            ); \
            return false; \
        } \
    } while (false)

static bool test_complete_handoff(void)
{
    struct micros_tty_handoff state;

    EXPECT_TRUE(
        micros_tty_handoff_initialize(&state)
            == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_validate(&state)
            == MICROS_TTY_HANDOFF_OK
        && state.console_phase == MICROS_TTY_CONSOLE_EARLY
        && state.route_phase == MICROS_TTY_ROUTE_DISABLED
        && !state.deadline_armed
    );
    EXPECT_TRUE(
        micros_tty_handoff_begin(&state, 100, 50)
            == MICROS_TTY_HANDOFF_OK
        && state.console_phase
            == MICROS_TTY_CONSOLE_MAP_REQUESTED
        && state.deadline_armed
        && state.deadline == 150
        && !micros_tty_handoff_deadline_expired(&state, 149)
        && micros_tty_handoff_deadline_expired(&state, 150)
        && micros_tty_handoff_mark_mapped(&state)
            == MICROS_TTY_HANDOFF_OK
        && state.console_phase == MICROS_TTY_CONSOLE_MAPPED
        && state.deadline == 150
        && micros_tty_handoff_release(&state)
            == MICROS_TTY_HANDOFF_OK
        && state.console_phase == MICROS_TTY_CONSOLE_STARTING
        && state.deadline == 150
        && micros_tty_handoff_commit(&state)
            == MICROS_TTY_HANDOFF_OK
        && state.console_phase == MICROS_TTY_CONSOLE_OWNED
        && state.route_phase == MICROS_TTY_ROUTE_IDLE
        && state.deadline == 150
        && micros_tty_handoff_claim(
            &state,
            MICROS_TTY_UART_IRQ_SOURCE
        ) == MICROS_TTY_HANDOFF_OK
        && state.route_phase == MICROS_TTY_ROUTE_IN_SERVICE
        && state.claimed_source == MICROS_TTY_UART_IRQ_SOURCE
        && micros_tty_handoff_accept_ready(&state, 149)
            == MICROS_TTY_HANDOFF_OK
        && !state.deadline_armed
        && state.deadline == 0
        && micros_tty_handoff_complete(
            &state,
            MICROS_TTY_UART_IRQ_SOURCE
        ) == MICROS_TTY_HANDOFF_OK
        && state.route_phase == MICROS_TTY_ROUTE_IDLE
        && state.claimed_source == 0
        && micros_tty_handoff_validate(&state)
            == MICROS_TTY_HANDOFF_OK
    );
    return true;
}

static bool test_failure_preservation(void)
{
    struct micros_tty_handoff state;
    struct micros_tty_handoff snapshot;

    EXPECT_TRUE(
        micros_tty_handoff_initialize(&state)
            == MICROS_TTY_HANDOFF_OK
    );
    snapshot = state;
    EXPECT_TRUE(
        micros_tty_handoff_mark_mapped(&state)
            == MICROS_TTY_HANDOFF_ERROR_STATE
        && memcmp(&state, &snapshot, sizeof(state)) == 0
        && micros_tty_handoff_begin(&state, UINT64_MAX, 1)
            == MICROS_TTY_HANDOFF_ERROR_RANGE
        && memcmp(&state, &snapshot, sizeof(state)) == 0
        && micros_tty_handoff_begin(&state, 1, 0)
            == MICROS_TTY_HANDOFF_ERROR_ARGUMENT
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );
    EXPECT_TRUE(
        micros_tty_handoff_begin(&state, 1, 10)
            == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_mark_mapped(&state)
            == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_release(&state)
            == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_commit(&state)
            == MICROS_TTY_HANDOFF_OK
    );
    snapshot = state;
    EXPECT_TRUE(
        micros_tty_handoff_claim(&state, 9)
            == MICROS_TTY_HANDOFF_ERROR_ARGUMENT
        && memcmp(&state, &snapshot, sizeof(state)) == 0
        && micros_tty_handoff_complete(
            &state,
            MICROS_TTY_UART_IRQ_SOURCE
        ) == MICROS_TTY_HANDOFF_ERROR_STATE
        && memcmp(&state, &snapshot, sizeof(state)) == 0
        && micros_tty_handoff_accept_ready(&state, 11)
            == MICROS_TTY_HANDOFF_ERROR_STATE
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );
    EXPECT_TRUE(
        micros_tty_handoff_claim(
            &state,
            MICROS_TTY_UART_IRQ_SOURCE
        ) == MICROS_TTY_HANDOFF_OK
    );
    snapshot = state;
    EXPECT_TRUE(
        micros_tty_handoff_claim(
            &state,
            MICROS_TTY_UART_IRQ_SOURCE
        ) == MICROS_TTY_HANDOFF_ERROR_STATE
        && memcmp(&state, &snapshot, sizeof(state)) == 0
        && micros_tty_handoff_complete(&state, 9)
            == MICROS_TTY_HANDOFF_ERROR_ARGUMENT
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );
    return true;
}

static bool advance_to_phase(
    enum micros_tty_console_phase phase,
    struct micros_tty_handoff *state
)
{
    if (
        micros_tty_handoff_initialize(state)
            != MICROS_TTY_HANDOFF_OK
        || phase == MICROS_TTY_CONSOLE_EARLY
    ) {
        return phase == MICROS_TTY_CONSOLE_EARLY;
    }
    if (
        micros_tty_handoff_begin(state, 10, 10)
            != MICROS_TTY_HANDOFF_OK
        || phase == MICROS_TTY_CONSOLE_MAP_REQUESTED
    ) {
        return phase == MICROS_TTY_CONSOLE_MAP_REQUESTED;
    }
    if (
        micros_tty_handoff_mark_mapped(state)
            != MICROS_TTY_HANDOFF_OK
        || phase == MICROS_TTY_CONSOLE_MAPPED
    ) {
        return phase == MICROS_TTY_CONSOLE_MAPPED;
    }
    if (
        micros_tty_handoff_release(state)
            != MICROS_TTY_HANDOFF_OK
        || phase == MICROS_TTY_CONSOLE_STARTING
    ) {
        return phase == MICROS_TTY_CONSOLE_STARTING;
    }
    return (
        micros_tty_handoff_commit(state) == MICROS_TTY_HANDOFF_OK
        && phase == MICROS_TTY_CONSOLE_OWNED
    );
}

static bool test_terminal_panic(void)
{
    const enum micros_tty_console_phase phases[] = {
        MICROS_TTY_CONSOLE_EARLY,
        MICROS_TTY_CONSOLE_MAP_REQUESTED,
        MICROS_TTY_CONSOLE_MAPPED,
        MICROS_TTY_CONSOLE_STARTING,
        MICROS_TTY_CONSOLE_OWNED,
    };
    size_t index;

    for (index = 0; index < sizeof(phases) / sizeof(phases[0]); ++index) {
        struct micros_tty_handoff state;

        EXPECT_TRUE(
            advance_to_phase(phases[index], &state)
            && micros_tty_handoff_panic(&state)
                == MICROS_TTY_HANDOFF_OK
            && state.console_phase == MICROS_TTY_CONSOLE_PANIC
            && state.route_phase == MICROS_TTY_ROUTE_PANIC
            && !state.deadline_armed
            && state.deadline == 0
            && state.claimed_source == 0
            && micros_tty_handoff_panic(&state)
                == MICROS_TTY_HANDOFF_OK
            && micros_tty_handoff_begin(&state, 1, 1)
                == MICROS_TTY_HANDOFF_ERROR_STATE
            && micros_tty_handoff_validate(&state)
                == MICROS_TTY_HANDOFF_OK
        );
    }
    return true;
}

static bool test_invariant_rejection(void)
{
    struct micros_tty_handoff state;

    EXPECT_TRUE(
        micros_tty_handoff_initialize(&state)
            == MICROS_TTY_HANDOFF_OK
    );
    state.route_phase = MICROS_TTY_ROUTE_IDLE;
    EXPECT_TRUE(
        micros_tty_handoff_validate(&state)
            == MICROS_TTY_HANDOFF_ERROR_INVARIANT
        && micros_tty_handoff_panic(&state)
            == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_validate(&state)
            == MICROS_TTY_HANDOFF_OK
    );
    return true;
}

bool micros_tty_handoff_test_run(void)
{
    return (
        test_complete_handoff()
        && test_failure_preservation()
        && test_terminal_panic()
        && test_invariant_rejection()
    );
}
