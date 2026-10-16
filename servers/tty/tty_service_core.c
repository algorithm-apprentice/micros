#include "tty_service_internal.h"

#include <stddef.h>
#include <stdint.h>

enum micros_tty_service_error micros_tty_service_initialize(
    struct micros_tty_service_state *state,
    micros_endpoint_t self_endpoint,
    micros_endpoint_t vfs_endpoint
)
{
    if (
        state == NULL
        || !tty_service_endpoint_is_valid(self_endpoint)
        || vfs_endpoint == MICROS_ENDPOINT_ANY
    ) {
        return MICROS_TTY_SERVICE_ERROR_ARGUMENT;
    }
    tty_service_request_clear_bytes(state, sizeof(*state));
    state->initialization_magic =
        MICROS_TTY_SERVICE_INITIALIZATION_MAGIC;
    state->self_endpoint = self_endpoint;
    state->vfs_endpoint = vfs_endpoint;
    if (
        micros_tty_state_initialize(&state->terminal)
            != MICROS_TTY_CORE_OK
    ) {
        return MICROS_TTY_SERVICE_ERROR_INVARIANT;
    }
    return MICROS_TTY_SERVICE_OK;
}

enum micros_tty_service_error micros_tty_service_commit_ownership(
    struct micros_tty_service_state *state
)
{
    enum micros_tty_service_error error;

    if (state == NULL) {
        return MICROS_TTY_SERVICE_ERROR_ARGUMENT;
    }
    error = micros_tty_service_validate(state);
    if (error != MICROS_TTY_SERVICE_OK) {
        return error;
    }
    if (state->owned) {
        return MICROS_TTY_SERVICE_ERROR_STATE;
    }
    state->owned = true;
    return MICROS_TTY_SERVICE_OK;
}

enum micros_tty_service_error micros_tty_service_handle_call(
    struct micros_tty_service_state *state,
    const struct micros_ipc_message *message,
    const struct micros_tty_service_io *io,
    struct micros_tty_service_reply_action *action
)
{
    struct micros_tty_request request;
    enum micros_tty_protocol_status status;

    if (
        state == NULL
        || message == NULL
        || action == NULL
        || message->reply_token == 0
        || (message->type & MICROS_IPC_TYPE_KERNEL_MASK) != 0
    ) {
        return MICROS_TTY_SERVICE_ERROR_ARGUMENT;
    }
    tty_service_request_clear_bytes(action, sizeof(*action));
    status = micros_tty_decode_request(message, &request);
    if (status != MICROS_TTY_PROTOCOL_OK) {
        return status == MICROS_TTY_PROTOCOL_INVARIANT
            ? MICROS_TTY_SERVICE_ERROR_INVARIANT
            : tty_service_prepare_protocol_reply(message, status, action);
    }
    if (request.source != state->vfs_endpoint) {
        return tty_service_prepare_reply(
            request.reply_token,
            request.type,
            MICROS_TTY_RESULT_CALLER,
            request.request_id,
            0,
            action
        );
    }
    if (!state->owned) {
        return tty_service_prepare_reply(
            request.reply_token,
            request.type,
            MICROS_TTY_RESULT_STATE,
            request.request_id,
            0,
            action
        );
    }
    switch (request.type) {
    case MICROS_TTY_MESSAGE_SUBMIT_READ:
        return tty_service_handle_submit_read(state, &request, io, action);
    case MICROS_TTY_MESSAGE_SUBMIT_WRITE:
        return tty_service_handle_submit_write(state, &request, io, action);
    case MICROS_TTY_MESSAGE_CANCEL:
        return tty_service_handle_cancel(state, &request, action);
    case MICROS_TTY_MESSAGE_COLLECT:
        return tty_service_handle_collect(state, &request, action);
    default:
        return MICROS_TTY_SERVICE_ERROR_INVARIANT;
    }
}
