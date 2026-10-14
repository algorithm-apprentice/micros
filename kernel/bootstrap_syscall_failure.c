#include "kernel/bootstrap_syscall_internal.h"

#include <stddef.h>

static enum micros_bootstrap_diagnostic_reason map_failure_reason(
    enum micros_bootstrap_failure_reason reason
)
{
    switch (reason) {
    case MICROS_BOOTSTRAP_FAILURE_READY_MALFORMED:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_READY_MALFORMED;
    case MICROS_BOOTSTRAP_FAILURE_READY_FOREIGN:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_READY_FOREIGN;
    case MICROS_BOOTSTRAP_FAILURE_READY_EARLY:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_READY_EARLY;
    case MICROS_BOOTSTRAP_FAILURE_READY_DUPLICATE:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_READY_DUPLICATE;
    case MICROS_BOOTSTRAP_FAILURE_RELEASE_ORDER:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_ORDER;
    case MICROS_BOOTSTRAP_FAILURE_RELEASE_TRANSITION:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_TRANSITION;
    case MICROS_BOOTSTRAP_FAILURE_AUTHORITY:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY;
    case MICROS_BOOTSTRAP_FAILURE_COMPLETION:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_COMPLETION;
    case MICROS_BOOTSTRAP_FAILURE_READY_ROLE_GATE:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_READY_ROLE_GATE;
    case MICROS_BOOTSTRAP_FAILURE_CONSOLE_PROTOCOL:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_CONSOLE_PROTOCOL;
    }
    return MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY;
}

static uint64_t failure_detail(
    const struct micros_bootstrap_control_state *state,
    const struct micros_bootstrap_control_request *request
)
{
    if (
        request->failure_reason
        == MICROS_BOOTSTRAP_FAILURE_READY_MALFORMED
    ) {
        return request->failure_detail;
    }
    if (
        request->failure_reason
            == MICROS_BOOTSTRAP_FAILURE_RELEASE_ORDER
        && state->transitions.next_order_index
            < state->transitions.entry_count
    ) {
        return state->transitions.ordered_service_ids[
            state->transitions.next_order_index
        ];
    }
    return 0;
}

static enum micros_bootstrap_error validate_failure_request(
    const struct micros_bootstrap_control_state *state,
    const struct micros_bootstrap_control_request *request
)
{
    const struct micros_bootstrap_binding *binding =
        micros_bootstrap_control_find_binding(
            state,
            request->service_id
        );

    if (
        micros_bootstrap_control_validate_failure_detail(request)
        != MICROS_SYSCALL_ABI_OK
    ) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    switch (request->failure_reason) {
    case MICROS_BOOTSTRAP_FAILURE_READY_FOREIGN:
        if (binding == NULL) {
            return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
        }
        return (
            state->transitions.starting_service_id != 0
            && request->service_id
                == state->transitions.starting_service_id
            && request->endpoint != MICROS_ENDPOINT_NONE
            && request->endpoint != MICROS_ENDPOINT_ANY
        )
            ? MICROS_BOOTSTRAP_OK
            : MICROS_BOOTSTRAP_ERROR_STATE;
    case MICROS_BOOTSTRAP_FAILURE_READY_EARLY:
    case MICROS_BOOTSTRAP_FAILURE_READY_DUPLICATE:
        if (binding == NULL) {
            return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
        }
        return request->endpoint == binding->endpoint
            ? MICROS_BOOTSTRAP_OK
            : MICROS_BOOTSTRAP_ERROR_STATE;
    case MICROS_BOOTSTRAP_FAILURE_READY_MALFORMED:
        if (binding == NULL) {
            return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
        }
        return (
            request->service_id
                == state->transitions.starting_service_id
            || request->endpoint == binding->endpoint
        )
            ? MICROS_BOOTSTRAP_OK
            : MICROS_BOOTSTRAP_ERROR_STATE;
    case MICROS_BOOTSTRAP_FAILURE_RELEASE_ORDER:
    case MICROS_BOOTSTRAP_FAILURE_RELEASE_TRANSITION:
        if (binding == NULL) {
            return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
        }
        return request->endpoint == MICROS_ENDPOINT_NONE
            ? MICROS_BOOTSTRAP_OK
            : MICROS_BOOTSTRAP_ERROR_STATE;
    case MICROS_BOOTSTRAP_FAILURE_AUTHORITY:
    case MICROS_BOOTSTRAP_FAILURE_COMPLETION:
        if (request->service_id != 0 && binding == NULL) {
            return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
        }
        return request->endpoint == MICROS_ENDPOINT_NONE
            ? MICROS_BOOTSTRAP_OK
            : MICROS_BOOTSTRAP_ERROR_STATE;
    case MICROS_BOOTSTRAP_FAILURE_READY_ROLE_GATE:
        if (binding == NULL) {
            return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
        }
        return (
            request->service_id
                == state->transitions.starting_service_id
            && request->endpoint == binding->endpoint
        )
            ? MICROS_BOOTSTRAP_OK
            : MICROS_BOOTSTRAP_ERROR_STATE;
    case MICROS_BOOTSTRAP_FAILURE_CONSOLE_PROTOCOL:
        return (
            state->plan.console_service_id != 0
            && request->service_id
                == state->plan.console_service_id
            && request->endpoint == MICROS_ENDPOINT_NONE
        )
            ? MICROS_BOOTSTRAP_OK
            : MICROS_BOOTSTRAP_ERROR_STATE;
    }
    return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
}

enum micros_syscall_return bootstrap_syscall_handle_failure(
    struct micros_trap_frame *frame,
    const struct micros_bootstrap_control_request *request,
    const struct micros_bootstrap_control_state *state
)
{
    enum micros_bootstrap_error error =
        validate_failure_request(state, request);

    if (error != MICROS_BOOTSTRAP_OK) {
        return bootstrap_syscall_return_result(
            frame,
            bootstrap_syscall_map_bootstrap_error(error, false)
        );
    }
    micros_bootstrap_runtime_fail(
        map_failure_reason(request->failure_reason),
        request->service_id,
        request->endpoint,
        failure_detail(state, request)
    );
}
