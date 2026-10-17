#include "kernel/bootstrap_syscall_internal.h"

#include "kernel/endpoint/endpoint_internal.h"
#include "kernel/scheduler/scheduler_core_internal.h"

enum micros_syscall_return bootstrap_syscall_handle_complete(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry,
    const struct micros_grant_registry *grants
)
{
    struct micros_bootstrap_complete_plan plan;
    struct micros_scheduler_current_ipc_guard guard = {0};
    const struct micros_bootstrap_binding *controller;
    enum micros_bootstrap_error error;

    error = micros_bootstrap_control_prepare_complete(
        state,
        registry,
        context->objects,
        grants,
        &plan
    );
    if (error != MICROS_BOOTSTRAP_OK) {
        if (error != MICROS_BOOTSTRAP_ERROR_STATE) {
            bootstrap_syscall_fail_active_bootstrap(
                hart,
                frame,
                MICROS_BOOTSTRAP_DIAGNOSTIC_COMPLETION,
                0,
                MICROS_ENDPOINT_NONE
            );
        }
        return bootstrap_syscall_return_result(
            frame,
            bootstrap_syscall_map_bootstrap_error(error, false)
        );
    }
    if (
        micros_scheduler_preflight_current_ipc(
            context->objects,
            context->hart
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return bootstrap_syscall_return_result(
            frame,
            MICROS_SYSCALL_ABI_STATE
        );
    }
    controller = &state->bindings[
        plan.controller_binding_index
    ];
    if (
        micros_scheduler_begin_current_ipc(
            context->objects,
            context->hart,
            &guard
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        bootstrap_syscall_fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_COMPLETION,
            0,
            MICROS_ENDPOINT_NONE
        );
    }
    if (
        micros_thread_scheduler_remove(
            context->objects,
            controller->thread
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        bootstrap_syscall_fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_COMPLETION,
            0,
            MICROS_ENDPOINT_NONE
        );
    }
    micros_endpoint_commit_source_only_prevalidated(
        registry,
        controller->endpoint
    );
    micros_bootstrap_control_commit_complete_prevalidated(
        state,
        registry,
        context->objects,
        &plan
    );
    if (
        micros_bootstrap_control_validate(
            state,
            registry,
            context->objects
        ) != MICROS_BOOTSTRAP_OK
        || micros_bootstrap_runtime_validate()
            != MICROS_BOOTSTRAP_OK
    ) {
        bootstrap_syscall_fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_COMPLETION,
            0,
            MICROS_ENDPOINT_NONE
        );
    }
    return MICROS_SYSCALL_RETURN_CAPTURED;
}
