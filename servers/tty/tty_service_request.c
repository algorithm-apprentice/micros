#include "tty_service_internal.h"

#include <stddef.h>
#include <stdint.h>

void tty_service_request_clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

enum micros_tty_service_error tty_service_prepare_reply(
    uint64_t reply_token,
    uint32_t request_type,
    enum micros_tty_result result,
    uint64_t request_id,
    uint64_t transferred_count,
    struct micros_tty_service_reply_action *action
)
{
    struct micros_tty_service_reply_action candidate;

    tty_service_request_clear_bytes(&candidate, sizeof(candidate));
    if (
        reply_token == 0
        || micros_tty_build_result(
            request_type,
            result,
            request_id,
            transferred_count,
            &candidate.message
        ) != MICROS_TTY_CORE_OK
    ) {
        return MICROS_TTY_SERVICE_ERROR_INVARIANT;
    }
    candidate.active = true;
    candidate.reply_token = reply_token;
    *action = candidate;
    return MICROS_TTY_SERVICE_OK;
}

enum micros_tty_service_error tty_service_prepare_protocol_reply(
    const struct micros_ipc_message *message,
    enum micros_tty_protocol_status status,
    struct micros_tty_service_reply_action *action
)
{
    enum micros_tty_result result;

    switch (status) {
    case MICROS_TTY_PROTOCOL_BAD_TYPE:
        result = MICROS_TTY_RESULT_BAD_TYPE;
        break;
    case MICROS_TTY_PROTOCOL_BAD_VERSION:
        result = MICROS_TTY_RESULT_BAD_VERSION;
        break;
    case MICROS_TTY_PROTOCOL_MALFORMED:
        result = MICROS_TTY_RESULT_MALFORMED;
        break;
    case MICROS_TTY_PROTOCOL_INVARIANT:
    case MICROS_TTY_PROTOCOL_OK:
        return MICROS_TTY_SERVICE_ERROR_INVARIANT;
    }
    return tty_service_prepare_reply(
        message->reply_token,
        message->type,
        result,
        0,
        0,
        action
    );
}

enum micros_tty_service_copy_result tty_service_copy_grant(
    const struct micros_tty_service_io *io,
    enum micros_tty_service_copy_direction direction,
    const struct micros_tty_service_state *state,
    micros_grant_t grant,
    uint64_t grant_offset,
    uint8_t *local,
    size_t length
)
{
    if (io == NULL || io->copy == NULL) {
        return MICROS_TTY_SERVICE_COPY_INVARIANT;
    }
    return io->copy(
        io->context,
        direction,
        state->vfs_endpoint,
        grant,
        grant_offset,
        local,
        length
    );
}

enum micros_tty_service_error tty_service_validate_submit(
    const struct micros_tty_service_state *state,
    const struct micros_tty_request *request,
    struct micros_tty_service_reply_action *action
)
{
    if (request->request_id == 0) {
        return tty_service_prepare_reply(
            request->reply_token,
            request->type,
            MICROS_TTY_RESULT_MALFORMED,
            request->request_id,
            0,
            action
        );
    }
    if (
        request->request_id <= state->last_accepted_request_id
    ) {
        return tty_service_prepare_reply(
            request->reply_token,
            request->type,
            MICROS_TTY_RESULT_REQUEST,
            request->request_id,
            0,
            action
        );
    }
    if (
        request->count == 0
        || request->count > MICROS_TTY_TRANSFER_MAX
        || UINT64_MAX - request->grant_offset < request->count
    ) {
        return tty_service_prepare_reply(
            request->reply_token,
            request->type,
            MICROS_TTY_RESULT_MALFORMED,
            request->request_id,
            0,
            action
        );
    }
    return MICROS_TTY_SERVICE_OK;
}

enum micros_tty_service_error tty_service_handle_cancel(
    struct micros_tty_service_state *state,
    const struct micros_tty_request *request,
    struct micros_tty_service_reply_action *action
)
{
    enum micros_tty_service_error error;

    if (request->request_id == 0) {
        return tty_service_prepare_reply(
            request->reply_token,
            request->type,
            MICROS_TTY_RESULT_MALFORMED,
            0,
            0,
            action
        );
    }
    if (
        !state->pending_read.active
        || state->pending_read.request_id != request->request_id
    ) {
        return tty_service_prepare_reply(
            request->reply_token,
            request->type,
            MICROS_TTY_RESULT_REQUEST,
            request->request_id,
            0,
            action
        );
    }
    error = micros_tty_service_validate(state);
    if (error != MICROS_TTY_SERVICE_OK) {
        return error;
    }
    tty_service_request_clear_bytes(
        &state->pending_read,
        sizeof(state->pending_read)
    );
    return tty_service_prepare_reply(
        request->reply_token,
        request->type,
        MICROS_TTY_RESULT_OK,
        request->request_id,
        0,
        action
    );
}

enum micros_tty_service_error tty_service_handle_collect(
    struct micros_tty_service_state *state,
    const struct micros_tty_request *request,
    struct micros_tty_service_reply_action *action
)
{
    const struct micros_tty_service_completion *completion;
    enum micros_tty_service_release release;
    enum micros_tty_service_error error;

    if (request->request_id == 0) {
        return tty_service_prepare_reply(
            request->reply_token,
            request->type,
            MICROS_TTY_RESULT_MALFORMED,
            0,
            0,
            action
        );
    }
    if (
        state->pending_read.active
        && state->pending_read.request_id == request->request_id
    ) {
        return tty_service_prepare_reply(
            request->reply_token,
            request->type,
            MICROS_TTY_RESULT_PENDING,
            request->request_id,
            0,
            action
        );
    }
    if (
        state->read_completion.active
        && state->read_completion.request_id == request->request_id
    ) {
        completion = &state->read_completion;
        release = MICROS_TTY_SERVICE_RELEASE_READ;
    } else if (
        state->write_completion.active
        && state->write_completion.request_id
            == request->request_id
    ) {
        completion = &state->write_completion;
        release = MICROS_TTY_SERVICE_RELEASE_WRITE;
    } else {
        return tty_service_prepare_reply(
            request->reply_token,
            request->type,
            MICROS_TTY_RESULT_REQUEST,
            request->request_id,
            0,
            action
        );
    }
    error = micros_tty_service_validate(state);
    if (error != MICROS_TTY_SERVICE_OK) {
        return error;
    }
    error = tty_service_prepare_reply(
        request->reply_token,
        request->type,
        completion->result,
        request->request_id,
        completion->transferred_count,
        action
    );
    if (error == MICROS_TTY_SERVICE_OK) {
        action->release = release;
        action->release_request_id = request->request_id;
    }
    return error;
}

enum micros_tty_service_error micros_tty_service_commit_reply(
    struct micros_tty_service_state *state,
    struct micros_tty_service_reply_action *action,
    uint64_t *notification
)
{
    struct micros_tty_service_completion *completion;

    if (
        state == NULL
        || action == NULL
        || notification == NULL
        || !action->active
        || action->reply_token == 0
    ) {
        return MICROS_TTY_SERVICE_ERROR_ARGUMENT;
    }
    if (action->release != MICROS_TTY_SERVICE_RELEASE_NONE) {
        completion = action->release
                == MICROS_TTY_SERVICE_RELEASE_READ
            ? &state->read_completion
            : &state->write_completion;
        if (
            !completion->active
            || completion->request_id
                != action->release_request_id
        ) {
            return MICROS_TTY_SERVICE_ERROR_INVARIANT;
        }
        tty_service_request_clear_bytes(completion, sizeof(*completion));
    }
    *notification = action->notification_after_reply;
    tty_service_request_clear_bytes(action, sizeof(*action));
    return micros_tty_service_validate(state);
}
