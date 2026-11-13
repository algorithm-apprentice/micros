#include "kernel/boot/bootstrap_syscall_internal.h"

#include <stddef.h>

#include "kernel/grant/grant_runtime_internal.h"
#include "kernel/grant/grant_internal.h"
#include "kernel/ipc/ipc_runtime_internal.h"
#include "kernel/tty/tty_handoff_runtime.h"
#include "micros/user_execution.h"

static void require_selected_context(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_bootstrap_binding *binding
)
{
    struct micros_user_context context;

    if (
        micros_user_execution_inspect(binding->thread, &context) != MICROS_USER_EXECUTION_OK
        || micros_user_execution_validate_context(binding->thread, &context) != MICROS_USER_EXECUTION_OK
    ) {
        bootstrap_syscall_fail_active_bootstrap(
            hart, frame, MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY, 0, MICROS_ENDPOINT_NONE
        );
    }
}

static void preflight_selected_runtime(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_bootstrap_control_request *request,
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *registry
)
{
    struct micros_bootstrap_service_selection selected;
    enum micros_bootstrap_error error;
    bool tty_required = request->command == MICROS_BOOTSTRAP_COMMAND_COMPLETE
        && state->plan.console_service_id != 0;

    error = micros_bootstrap_control_preflight_command(state, registry, context->objects, &selected);
    if (error != MICROS_BOOTSTRAP_OK || !bootstrap_syscall_root_is_owned(selected.binding)) {
        bootstrap_syscall_fail_active_bootstrap(
            hart, frame, MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY, 0, MICROS_ENDPOINT_NONE
        );
    }
    if (request->service_id != 0) {
        error = micros_bootstrap_control_select_service(
            state, registry, context->objects, request->service_id, &selected
        );
        if (error != MICROS_BOOTSTRAP_OK && error != MICROS_BOOTSTRAP_ERROR_ARGUMENT) {
            bootstrap_syscall_fail_active_bootstrap(
                hart, frame, MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY, 0, MICROS_ENDPOINT_NONE
            );
        }
        if (error == MICROS_BOOTSTRAP_OK) {
            if (!bootstrap_syscall_root_is_owned(selected.binding)) {
                bootstrap_syscall_fail_active_bootstrap(
                    hart, frame, MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY, 0, MICROS_ENDPOINT_NONE
                );
            }
            if (
                request->command == MICROS_BOOTSTRAP_COMMAND_RELEASE
                || request->command == MICROS_BOOTSTRAP_COMMAND_ACCEPT_READY
            ) {
                require_selected_context(hart, frame, selected.binding);
            }
            tty_required = tty_required
                || selected.manifest->role_flags == MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER;
        }
    }
    if (tty_required && micros_tty_handoff_runtime_validate_bootstrap_binding(
        state, registry, context->objects
    ) != MICROS_TTY_HANDOFF_OK) {
        bootstrap_syscall_fail_active_bootstrap(
            hart, frame, MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY, 0, MICROS_ENDPOINT_NONE
        );
    }
}

static enum micros_syscall_abi_result preflight_runtime(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_bootstrap_control_request *request,
    struct micros_bootstrap_control_state **state,
    struct micros_endpoint_registry **registry,
    struct micros_grant_registry **grants
)
{
    *state = micros_bootstrap_runtime_authoritative_state();
    if (*state == NULL) {
        return MICROS_SYSCALL_ABI_UNAUTHORIZED;
    }
    *registry = micros_ipc_runtime_authoritative_registry();
    if (*registry == NULL) {
        bootstrap_syscall_fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            0,
            MICROS_ENDPOINT_NONE
        );
    }
    if (
        !bootstrap_syscall_controller_is_authorized(
            *state,
            *registry,
            context->objects,
            context
        )
    ) {
        return MICROS_SYSCALL_ABI_UNAUTHORIZED;
    }
    preflight_selected_runtime(hart, frame, context, request, *state, *registry);
    *grants = micros_grant_runtime_authoritative_registry();
    if (
        *grants == NULL
        || micros_grant_preflight_context(*grants, *registry, context->objects) != MICROS_GRANT_OK
    ) {
        bootstrap_syscall_fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            0,
            MICROS_ENDPOINT_NONE
        );
    }
    return MICROS_SYSCALL_ABI_OK;
}

static enum micros_syscall_return dispatch_command(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_bootstrap_control_request *request,
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry,
    const struct micros_grant_registry *grants
)
{
    switch (request->command) {
    case MICROS_BOOTSTRAP_COMMAND_CONSOLE_BEGIN:
        return bootstrap_syscall_handle_console_begin(
            hart,
            frame,
            context,
            request,
            state,
            registry
        );
    case MICROS_BOOTSTRAP_COMMAND_RELEASE:
        return bootstrap_syscall_handle_release(
            hart,
            frame,
            context,
            request,
            state,
            registry
        );
    case MICROS_BOOTSTRAP_COMMAND_ACCEPT_READY:
        return bootstrap_syscall_handle_accept_ready(
            hart,
            frame,
            context,
            request,
            state,
            registry
        );
    case MICROS_BOOTSTRAP_COMMAND_FAIL:
        return bootstrap_syscall_handle_failure(frame, request, state);
    case MICROS_BOOTSTRAP_COMMAND_COMPLETE:
        return bootstrap_syscall_handle_complete(
            hart,
            frame,
            context,
            state,
            registry,
            grants
        );
    }
    bootstrap_syscall_fail_active_bootstrap(
        hart,
        frame,
        MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
        0,
        MICROS_ENDPOINT_NONE
    );
}

enum micros_syscall_return
micros_bootstrap_handle_captured_user_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_syscall_arguments *arguments
)
{
    struct micros_bootstrap_control_request request;
    struct micros_bootstrap_control_state *state;
    struct micros_endpoint_registry *registry;
    struct micros_grant_registry *grants;
    enum micros_syscall_abi_result result;

    if (
        hart == NULL
        || frame == NULL
        || context == NULL
        || arguments == NULL
        || context->objects == NULL
    ) {
        bootstrap_syscall_panic_bootstrap_syscall(
            hart,
            frame,
            "bootstrap-syscall-argument"
        );
    }
    result = micros_bootstrap_control_decode(arguments, &request);
    if (result != MICROS_SYSCALL_ABI_OK) {
        return bootstrap_syscall_return_result(frame, result);
    }
    result = preflight_runtime(
        hart,
        frame,
        context,
        &request,
        &state,
        &registry,
        &grants
    );
    if (result != MICROS_SYSCALL_ABI_OK) {
        return bootstrap_syscall_return_result(frame, result);
    }
    return dispatch_command(
        hart,
        frame,
        context,
        &request,
        state,
        registry,
        grants
    );
}
