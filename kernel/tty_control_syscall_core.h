#ifndef MICROS_KERNEL_TTY_CONTROL_SYSCALL_CORE_H
#define MICROS_KERNEL_TTY_CONTROL_SYSCALL_CORE_H

#include <stddef.h>

#include "kernel/bootstrap_control_internal.h"
#include "kernel/syscall.h"
#include "kernel/tty_handoff_runtime.h"
#include "micros/bootstrap.h"
#include "micros/endpoint.h"

enum micros_tty_control_authority_result {
    MICROS_TTY_CONTROL_AUTHORITY_AUTHORIZED = 0,
    MICROS_TTY_CONTROL_AUTHORITY_UNAUTHORIZED,
    MICROS_TTY_CONTROL_AUTHORITY_INVARIANT,
};

enum micros_tty_control_authority_result
micros_tty_control_syscall_presence_classify(
    const struct micros_tty_handoff_runtime_state *state,
    size_t bootstrap_entry_count,
    uint32_t planned_tty_service_id
);

enum micros_tty_control_authority_result
micros_tty_control_syscall_caller_classify(
    const struct micros_tty_handoff_runtime_state *state,
    const struct micros_syscall_context *context,
    const struct micros_process *caller_process,
    const struct micros_thread *caller_thread
);

enum micros_tty_control_authority_result
micros_tty_control_syscall_authority_classify(
    const struct micros_tty_handoff_runtime_state *state,
    size_t bootstrap_entry_count,
    uint32_t planned_tty_service_id,
    const struct micros_bootstrap_binding *binding,
    const struct micros_bootstrap_manifest_entry *entry,
    const struct micros_syscall_context *context,
    const struct micros_process *caller_process,
    const struct micros_thread *caller_thread,
    const struct micros_endpoint_record *endpoint,
    const struct micros_privilege_profile *profile
);

enum micros_tty_control_authority_result
micros_tty_control_syscall_authority_resolve(
    const struct micros_tty_handoff_runtime_state *state,
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_syscall_context *context
);

#endif
