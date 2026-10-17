#ifndef MICROS_KERNEL_BOOTSTRAP_SYSCALL_INTERNAL_H
#define MICROS_KERNEL_BOOTSTRAP_SYSCALL_INTERNAL_H

#include "kernel/boot/bootstrap_runtime.h"
#include "kernel/boot/bootstrap_syscall.h"

_Noreturn void bootstrap_syscall_panic_bootstrap_syscall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const char *reason
);

_Noreturn void bootstrap_syscall_fail_active_bootstrap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_bootstrap_diagnostic_reason reason,
    uint32_t service_id,
    micros_endpoint_t endpoint
);

enum micros_syscall_return bootstrap_syscall_return_result(
    struct micros_trap_frame *frame,
    enum micros_syscall_abi_result result
);

enum micros_syscall_abi_result bootstrap_syscall_map_bootstrap_error(
    enum micros_bootstrap_error error,
    bool endpoint_identity
);

enum micros_syscall_abi_result bootstrap_syscall_map_ipc_error(
    enum micros_ipc_error error
);

bool bootstrap_syscall_controller_is_authorized(
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_syscall_context *context
);

bool bootstrap_syscall_role_gate_ready(
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    uint32_t service_id,
    bool release
);

enum micros_syscall_return bootstrap_syscall_handle_console_begin(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_bootstrap_control_request *request,
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry
);

enum micros_syscall_return bootstrap_syscall_handle_release(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_bootstrap_control_request *request,
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry
);

enum micros_syscall_return bootstrap_syscall_handle_accept_ready(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_bootstrap_control_request *request,
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry
);

enum micros_syscall_return bootstrap_syscall_handle_complete(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry,
    const struct micros_grant_registry *grants
);

enum micros_syscall_return bootstrap_syscall_handle_failure(
    struct micros_trap_frame *frame,
    const struct micros_bootstrap_control_request *request,
    const struct micros_bootstrap_control_state *state
);

#endif
