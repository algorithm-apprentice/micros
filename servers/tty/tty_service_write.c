#include "tty_service_internal.h"

#include <stddef.h>
#include <stdint.h>

struct tty_service_write_work {
    enum micros_tty_service_copy_result copy_result;
    enum micros_tty_service_error error;
    struct micros_tty_effects effects;
};

static void tty_service_publish_write(
    struct micros_tty_service_state *state,
    const struct micros_tty_request *request,
    struct micros_tty_service_reply_action *action,
    struct tty_service_write_work *work
)
{
    state->last_accepted_request_id = request->request_id;
    state->write_completion =
        (struct micros_tty_service_completion){
            .active = true,
            .result = MICROS_TTY_RESULT_OK,
            .request_id = request->request_id,
            .transferred_count = request->count,
        };
    work->error = tty_service_prepare_reply(
        request->reply_token,
        request->type,
        MICROS_TTY_RESULT_OK,
        request->request_id,
        0,
        action
    );
    if (work->error == MICROS_TTY_SERVICE_OK) {
        action->uart_effects = work->effects;
        action->notification_after_reply =
            MICROS_TTY_EVENT_COMPLETION;
    }
}

enum micros_tty_service_error tty_service_handle_submit_write(
    struct micros_tty_service_state *state,
    const struct micros_tty_request *request,
    const struct micros_tty_service_io *io,
    struct micros_tty_service_reply_action *action
)
{
    struct tty_service_write_work work;

    work.error = tty_service_validate_submit(state, request, action);
    if (work.error != MICROS_TTY_SERVICE_OK || action->active) {
        return work.error;
    }
    if (state->write_completion.active) {
        work.error = micros_tty_service_validate(state);
        if (work.error != MICROS_TTY_SERVICE_OK) {
            return work.error;
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
        work.error = micros_tty_service_validate(state);
        if (
            work.error != MICROS_TTY_SERVICE_OK
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
    work.copy_result = tty_service_copy_grant(
        io,
        MICROS_TTY_SERVICE_COPY_FROM_VFS,
        state,
        request->grant,
        request->grant_offset,
        NULL,
        0
    );
    if (work.copy_result == MICROS_TTY_SERVICE_COPY_INVARIANT) {
        return MICROS_TTY_SERVICE_ERROR_INVARIANT;
    }
    if (work.copy_result == MICROS_TTY_SERVICE_COPY_REJECTED) {
        return tty_service_prepare_reply(
            request->reply_token,
            request->type,
            MICROS_TTY_RESULT_GRANT,
            request->request_id,
            0,
            action
        );
    }
    work.error = micros_tty_service_validate(state);
    if (work.error != MICROS_TTY_SERVICE_OK) {
        return work.error;
    }
    work.copy_result = tty_service_copy_grant(
        io,
        MICROS_TTY_SERVICE_COPY_FROM_VFS,
        state,
        request->grant,
        request->grant_offset,
        state->terminal.write,
        (size_t)request->count
    );
    if (work.copy_result == MICROS_TTY_SERVICE_COPY_INVARIANT) {
        return MICROS_TTY_SERVICE_ERROR_INVARIANT;
    }
    if (work.copy_result == MICROS_TTY_SERVICE_COPY_REJECTED) {
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
            &work.effects
        ) != MICROS_TTY_CORE_OK
    ) {
        return MICROS_TTY_SERVICE_ERROR_INVARIANT;
    }
    tty_service_publish_write(state, request, action, &work);
    return work.error;
}
