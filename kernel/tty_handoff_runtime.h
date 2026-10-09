#ifndef MICROS_KERNEL_TTY_HANDOFF_RUNTIME_H
#define MICROS_KERNEL_TTY_HANDOFF_RUNTIME_H

#include "kernel/bootstrap_control_internal.h"
#include "kernel/tty_handoff_core.h"

struct micros_tty_handoff_runtime_state {
    uint32_t service_id;
    micros_endpoint_t endpoint;
    uint32_t image_id;
    uint32_t profile_id;
    struct micros_process_handle process;
    struct micros_thread_handle thread;
    uint64_t root_physical_address;
    struct micros_tty_handoff handoff;
};

enum micros_tty_device_authority_status {
    MICROS_TTY_DEVICE_AUTHORITY_NONE = 0,
    MICROS_TTY_DEVICE_AUTHORITY_ACTIVE,
    MICROS_TTY_DEVICE_AUTHORITY_INVARIANT,
};

enum micros_tty_handoff_error micros_tty_handoff_runtime_prepare(
    const struct micros_bootstrap_binding *binding,
    const struct micros_bootstrap_manifest_entry *entry
);

enum micros_tty_handoff_error micros_tty_handoff_runtime_bind_bootstrap(
    const struct micros_bootstrap_control_state *bootstrap
);

enum micros_tty_handoff_error micros_tty_handoff_runtime_reset(void);

const struct micros_tty_handoff_runtime_state *
micros_tty_handoff_runtime_state(void);

struct micros_tty_handoff_runtime_state *
micros_tty_handoff_runtime_authoritative_state(void);

enum micros_tty_handoff_error micros_tty_handoff_runtime_begin(
    uint64_t now,
    uint64_t interval
);

enum micros_tty_handoff_error
micros_tty_handoff_runtime_prepare_begin(
    uint64_t now,
    uint64_t interval,
    struct micros_tty_handoff *candidate
);

void micros_tty_handoff_runtime_commit_begin_deadline_prevalidated(
    const struct micros_tty_handoff *candidate
);

void micros_tty_handoff_runtime_commit_begin_phase_prevalidated(
    const struct micros_tty_handoff *candidate
);

enum micros_tty_handoff_error
micros_tty_handoff_runtime_prepare_mapped(
    struct micros_tty_handoff *candidate
);

void micros_tty_handoff_runtime_commit_mapped_prevalidated(
    const struct micros_tty_handoff *candidate
);

enum micros_tty_handoff_error
micros_tty_handoff_runtime_prepare_release(
    struct micros_tty_handoff *candidate
);

void micros_tty_handoff_runtime_commit_release_prevalidated(
    const struct micros_tty_handoff *candidate
);

enum micros_tty_handoff_error
micros_tty_handoff_runtime_prepare_ready(
    uint64_t now,
    struct micros_tty_handoff *candidate
);

void micros_tty_handoff_runtime_commit_ready_prevalidated(
    const struct micros_tty_handoff *candidate
);

bool micros_tty_handoff_runtime_role_ready(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
);

bool micros_tty_handoff_runtime_deadline_expired(uint64_t now);

enum micros_tty_device_authority_status
micros_tty_handoff_runtime_device_authority(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    struct micros_tty_device_authority *authority
);

enum micros_tty_handoff_error micros_tty_handoff_runtime_validate(
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
);

#endif
