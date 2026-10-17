#ifndef MICROS_KERNEL_TTY_HANDOFF_RUNTIME_INTERNAL_H
#define MICROS_KERNEL_TTY_HANDOFF_RUNTIME_INTERNAL_H

#include "kernel/tty/tty_handoff_runtime.h"

struct tty_handoff_context {
    struct micros_tty_handoff_runtime_state tty_handoff_state;
    const struct micros_bootstrap_control_state *tty_bootstrap_state;
};

struct tty_handoff_context *tty_handoff_context(void);

void tty_handoff_clear_bytes(void *storage, size_t size);

void tty_handoff_copy_bytes(void *destination, const void *source, size_t size);

bool tty_handoff_endpoint_matches_process(
    micros_endpoint_t endpoint,
    struct micros_process_handle process
);

bool tty_handoff_identity_is_valid(
    const struct micros_tty_handoff_runtime_state *state
);

bool tty_handoff_binding_identity_matches(
    struct tty_handoff_context *context,
    const struct micros_bootstrap_control_state *bootstrap
);

#endif
