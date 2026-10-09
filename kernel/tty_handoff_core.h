#ifndef MICROS_KERNEL_TTY_HANDOFF_CORE_H
#define MICROS_KERNEL_TTY_HANDOFF_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "micros/tty.h"

enum micros_tty_console_phase {
    MICROS_TTY_CONSOLE_EARLY = 1,
    MICROS_TTY_CONSOLE_MAP_REQUESTED,
    MICROS_TTY_CONSOLE_MAPPED,
    MICROS_TTY_CONSOLE_STARTING,
    MICROS_TTY_CONSOLE_OWNED,
    MICROS_TTY_CONSOLE_PANIC,
};

enum micros_tty_route_phase {
    MICROS_TTY_ROUTE_DISABLED = 1,
    MICROS_TTY_ROUTE_IDLE,
    MICROS_TTY_ROUTE_IN_SERVICE,
    MICROS_TTY_ROUTE_PANIC,
};

enum micros_tty_handoff_error {
    MICROS_TTY_HANDOFF_OK = 0,
    MICROS_TTY_HANDOFF_ERROR_ARGUMENT,
    MICROS_TTY_HANDOFF_ERROR_STATE,
    MICROS_TTY_HANDOFF_ERROR_RANGE,
    MICROS_TTY_HANDOFF_ERROR_INVARIANT,
};

struct micros_tty_handoff {
    uint64_t initialization_magic;
    enum micros_tty_console_phase console_phase;
    enum micros_tty_route_phase route_phase;
    uint64_t deadline;
    uint32_t claimed_source;
    bool deadline_armed;
};

enum micros_tty_handoff_error micros_tty_handoff_initialize(
    struct micros_tty_handoff *state
);

enum micros_tty_handoff_error micros_tty_handoff_validate(
    const struct micros_tty_handoff *state
);

enum micros_tty_handoff_error micros_tty_handoff_begin(
    struct micros_tty_handoff *state,
    uint64_t now,
    uint64_t interval
);

enum micros_tty_handoff_error micros_tty_handoff_mark_mapped(
    struct micros_tty_handoff *state
);

enum micros_tty_handoff_error micros_tty_handoff_release(
    struct micros_tty_handoff *state
);

enum micros_tty_handoff_error micros_tty_handoff_commit(
    struct micros_tty_handoff *state
);

enum micros_tty_handoff_error micros_tty_handoff_claim(
    struct micros_tty_handoff *state,
    uint32_t source
);

enum micros_tty_handoff_error micros_tty_handoff_complete(
    struct micros_tty_handoff *state,
    uint32_t source
);

enum micros_tty_handoff_error micros_tty_handoff_accept_ready(
    struct micros_tty_handoff *state,
    uint64_t now
);

bool micros_tty_handoff_deadline_expired(
    const struct micros_tty_handoff *state,
    uint64_t now
);

enum micros_tty_handoff_error micros_tty_handoff_panic(
    struct micros_tty_handoff *state
);

#endif
