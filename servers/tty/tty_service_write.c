#include "tty_service_internal.h"

#include <stddef.h>
#include <stdint.h>

enum micros_tty_service_error tty_service_handle_submit_write(
    struct micros_tty_service_state *state,
    const struct micros_tty_request *request,
    const struct micros_tty_service_io *io,
    struct micros_tty_service_reply_action *action
)
{
    enum micros_tty_service_copy_result copy_result;
    enum micros_tty_service_error error;
    struct micros_tty_effects effects;

    error = tty_service_validate_submit(state, request, action);
    if (error != MICROS_TTY_SERVICE_OK || action->active) {
        return error;
    }
    if (state->write_completion.active) {
        error = micros_tty_service_validate(state);
        if (error != MICROS_TTY_SERVICE_OK) {
            return error;
        }
        return tty_service_prepare_reply(
            request->reply_token,
            request->type,
            MICROS_TTY_RESULT_BUSY,
            request->request_id,
            0,
            action
        );
    }
    if (state->terminal.write_resident) {
        error = micros_tty_service_validate(state);
        if (
            error != MICROS_TTY_SERVICE_OK
            || micros_tty_write_arm_retry(&state->terminal)
                != MICROS_TTY_CORE_OK
        ) {
            return MICROS_TTY_SERVICE_ERROR_INVARIANT;
        }
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
        MICROS_TTY_SERVICE_COPY_FROM_VFS,
        state,
        request->grant,
        request->grant_offset,
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
    copy_result = tty_service_copy_grant(
        io,
        MICROS_TTY_SERVICE_COPY_FROM_VFS,
        state,
        request->grant,
        request->grant_offset,
        state->terminal.write,
        (size_t)request->count
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
    if (
        micros_tty_write_accept(
            &state->terminal,
            state->terminal.write,
            request->count,
            &effects
        ) != MICROS_TTY_CORE_OK
    ) {
        return MICROS_TTY_SERVICE_ERROR_INVARIANT;
    }
    state->last_accepted_request_id = request->request_id;
    state->write_completion =
        (struct micros_tty_service_completion){
            .active = true,
            .result = MICROS_TTY_RESULT_OK,
            .request_id = request->request_id,
            .transferred_count = request->count,
        };
    error = tty_service_prepare_reply(
        request->reply_token,
        request->type,
        MICROS_TTY_RESULT_OK,
        request->request_id,
        0,
        action
    );
    if (error == MICROS_TTY_SERVICE_OK) {
        action->uart_effects = effects;
        action->notification_after_reply =
            MICROS_TTY_EVENT_COMPLETION;
    }
    return error;
}
