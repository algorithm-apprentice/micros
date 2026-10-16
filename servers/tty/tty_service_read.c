#include "tty_service_internal.h"

#include <stddef.h>
#include <stdint.h>

static void tty_service_publish_read_completion(
    struct micros_tty_service_state *state,
    enum micros_tty_result result,
    uint64_t transferred_count
)
{
    state->read_completion =
        (struct micros_tty_service_completion){
            .active = true,
            .result = result,
            .request_id = state->pending_read.request_id,
            .transferred_count = transferred_count,
        };
    tty_service_request_clear_bytes(
        &state->pending_read,
        sizeof(state->pending_read)
    );
}

enum micros_tty_service_error micros_tty_service_complete_read(
    struct micros_tty_service_state *state,
    const struct micros_tty_service_io *io,
    uint64_t *notification
)
{
    enum micros_tty_service_copy_result copy_result;
    enum micros_tty_service_error service_error;
    enum micros_tty_core_error core_error;
    uint64_t staged_count;

    if (state == NULL || notification == NULL) {
        return MICROS_TTY_SERVICE_ERROR_ARGUMENT;
    }
    *notification = 0;
    service_error = micros_tty_service_validate(state);
    if (service_error != MICROS_TTY_SERVICE_OK) {
        return service_error;
    }
    if (
        !state->pending_read.active
        || !micros_tty_input_has_line(&state->terminal)
    ) {
        return MICROS_TTY_SERVICE_OK;
    }
    core_error = micros_tty_read_stage(
        &state->terminal,
        state->pending_read.count,
        &staged_count
    );
    if (core_error != MICROS_TTY_CORE_OK) {
        return MICROS_TTY_SERVICE_ERROR_INVARIANT;
    }
    copy_result = tty_service_copy_grant(
        io,
        MICROS_TTY_SERVICE_COPY_TO_VFS,
        state,
        state->pending_read.grant,
        state->pending_read.grant_offset,
        state->terminal.read_staging,
        (size_t)staged_count
    );
    if (copy_result == MICROS_TTY_SERVICE_COPY_INVARIANT) {
        (void)micros_tty_read_discard(&state->terminal);
        return MICROS_TTY_SERVICE_ERROR_INVARIANT;
    }
    if (copy_result == MICROS_TTY_SERVICE_COPY_REJECTED) {
        if (
            micros_tty_read_discard(&state->terminal)
                != MICROS_TTY_CORE_OK
        ) {
            return MICROS_TTY_SERVICE_ERROR_INVARIANT;
        }
        tty_service_publish_read_completion(
            state,
            MICROS_TTY_RESULT_GRANT,
            0
        );
    } else {
        if (
            micros_tty_read_commit(&state->terminal)
                != MICROS_TTY_CORE_OK
        ) {
            return MICROS_TTY_SERVICE_ERROR_INVARIANT;
        }
        tty_service_publish_read_completion(
            state,
            MICROS_TTY_RESULT_OK,
            staged_count
        );
    }
    *notification = MICROS_TTY_EVENT_COMPLETION;
    return micros_tty_service_validate(state);
}

enum micros_tty_service_error tty_service_handle_submit_read(
    struct micros_tty_service_state *state,
    const struct micros_tty_request *request,
    const struct micros_tty_service_io *io,
    struct micros_tty_service_reply_action *action
)
{
    enum micros_tty_service_copy_result copy_result;
    enum micros_tty_service_error error;
    uint64_t notification;

    error = tty_service_validate_submit(state, request, action);
    if (error != MICROS_TTY_SERVICE_OK || action->active) {
        return error;
    }
    if (
        state->pending_read.active
        || state->read_completion.active
    ) {
        return tty_service_prepare_reply(
            request->reply_token,
            request->type,
            MICROS_TTY_RESULT_BUSY,
            request->request_id,
            0,
            action
        );
    }
    copy_result = tty_service_copy_grant(
        io,
        MICROS_TTY_SERVICE_COPY_TO_VFS,
        state,
        request->grant,
        request->grant_offset + request->count,
        NULL,
        0
    );
    if (copy_result == MICROS_TTY_SERVICE_COPY_INVARIANT) {
        return MICROS_TTY_SERVICE_ERROR_INVARIANT;
    }
    if (copy_result == MICROS_TTY_SERVICE_COPY_REJECTED) {
        return tty_service_prepare_reply(
            request->reply_token,
            request->type,
            MICROS_TTY_RESULT_GRANT,
            request->request_id,
            0,
            action
        );
    }
    error = micros_tty_service_validate(state);
    if (error != MICROS_TTY_SERVICE_OK) {
        return error;
    }
    state->pending_read =
        (struct micros_tty_service_request){
            .active = true,
            .request_id = request->request_id,
            .grant = request->grant,
            .grant_offset = request->grant_offset,
            .count = request->count,
        };
    state->last_accepted_request_id = request->request_id;
    error = micros_tty_service_complete_read(
        state,
        io,
        &notification
    );
    if (error != MICROS_TTY_SERVICE_OK) {
        return error;
    }
    error = tty_service_prepare_reply(
        request->reply_token,
        request->type,
        MICROS_TTY_RESULT_OK,
        request->request_id,
        0,
        action
    );
    if (error == MICROS_TTY_SERVICE_OK) {
        action->notification_after_reply = notification;
    }
    return error;
}
