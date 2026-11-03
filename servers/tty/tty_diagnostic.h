#ifndef MICROS_SERVERS_TTY_DIAGNOSTIC_H
#define MICROS_SERVERS_TTY_DIAGNOSTIC_H

#include "micros/diagnostic.h"
#include "servers/tty/tty_core.h"

struct micros_tty_diagnostic {
    _Alignas(4096) uint8_t snapshot[MICROS_DIAGNOSTIC_SNAPSHOT_SIZE];
    uint32_t previous;
    uint32_t length;
    uint32_t offset;
    bool pending;
    bool initialized;
};

bool micros_tty_diagnostic_initialize(
    struct micros_tty_diagnostic *diagnostic,
    micros_endpoint_t self_endpoint
);
bool micros_tty_diagnostic_progress(
    struct micros_tty_diagnostic *diagnostic,
    struct micros_tty_state *terminal,
    bool notified,
    struct micros_tty_effects *effects
);
bool micros_tty_diagnostic_notification(
    const struct micros_ipc_message *message,
    uint64_t *events
);

#endif
