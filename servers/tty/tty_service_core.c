#include "servers/tty/tty_service_core.h"

#include <stddef.h>
#include <stdint.h>

#define MICROS_TTY_SERVICE_INITIALIZATION_MAGIC \
    UINT64_C(0x4d49435454595356)

static void clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static bool endpoint_is_valid(micros_endpoint_t endpoint)
{
    return (
        endpoint != MICROS_ENDPOINT_NONE
        && endpoint != MICROS_ENDPOINT_ANY
    );
}

static bool request_is_clear(
    const struct micros_tty_service_request *request
)
{
    return (
        !request->active
        && request->request_id == 0
        && request->grant == 0
        && request->grant_offset == 0
        && request->count == 0
    );
}

static bool completion_is_clear(
    const struct micros_tty_service_completion *completion
)
{
    return (
        !completion->active
        && completion->result == MICROS_TTY_RESULT_OK
        && completion->request_id == 0
        && completion->transferred_count == 0
    );
}

static bool completion_is_valid(
    const struct micros_tty_service_completion *completion,
    bool read_completion,
    uint64_t last_accepted_request_id
)
{
    if (!completion->active) {
        return completion_is_clear(completion);
    }
    if (
        completion->request_id == 0
        || completion->request_id > last_accepted_request_id
    ) {
        return false;
    }
    if (read_completion) {
        return (
            (
                completion->result == MICROS_TTY_RESULT_OK
                && completion->transferred_count != 0
                && completion->transferred_count
                    <= MICROS_TTY_TRANSFER_MAX
            )
            || (
                completion->result == MICROS_TTY_RESULT_GRANT
                && completion->transferred_count == 0
            )
        );
    }
    return (
        completion->result == MICROS_TTY_RESULT_OK
        && completion->transferred_count != 0
        && completion->transferred_count
            <= MICROS_TTY_TRANSFER_MAX
    );
}

static enum micros_tty_service_error prepare_reply(
    uint64_t reply_token,
    uint32_t request_type,
    enum micros_tty_result result,
    uint64_t request_id,
    uint64_t transferred_count,
    struct micros_tty_service_reply_action *action
)
{
    struct micros_tty_service_reply_action candidate;

    clear_bytes(&candidate, sizeof(candidate));
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

static enum micros_tty_service_error prepare_protocol_reply(
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
    return prepare_reply(
        message->reply_token,
        message->type,
        result,
        0,
        0,
        action
    );
}

static enum micros_tty_service_copy_result copy_grant(
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

static enum micros_tty_service_error validate_submit(
    const struct micros_tty_service_state *state,
    const struct micros_tty_request *request,
    struct micros_tty_service_reply_action *action
)
{
    if (request->request_id == 0) {
        return prepare_reply(
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
        return prepare_reply(
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
        return prepare_reply(
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

static void publish_read_completion(
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
    clear_bytes(
        &state->pending_read,
        sizeof(state->pending_read)
    );
}

enum micros_tty_service_error micros_tty_service_initialize(
    struct micros_tty_service_state *state,
    micros_endpoint_t self_endpoint,
    micros_endpoint_t vfs_endpoint
)
{
    if (
        state == NULL
        || !endpoint_is_valid(self_endpoint)
        || vfs_endpoint == MICROS_ENDPOINT_ANY
    ) {
        return MICROS_TTY_SERVICE_ERROR_ARGUMENT;
    }
    clear_bytes(state, sizeof(*state));
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

enum micros_tty_service_error micros_tty_service_validate(
    const struct micros_tty_service_state *state
)
{
    if (state == NULL) {
        return MICROS_TTY_SERVICE_ERROR_ARGUMENT;
    }
    if (
        state->initialization_magic
            != MICROS_TTY_SERVICE_INITIALIZATION_MAGIC
        || !endpoint_is_valid(state->self_endpoint)
        || state->vfs_endpoint == MICROS_ENDPOINT_ANY
        || micros_tty_state_validate(&state->terminal)
            != MICROS_TTY_CORE_OK
        || (
            state->pending_read.active
            && (
                state->pending_read.request_id == 0
                || state->pending_read.request_id
                    > state->last_accepted_request_id
                || state->pending_read.grant == MICROS_GRANT_NONE
                || state->pending_read.count == 0
                || state->pending_read.count
                    > MICROS_TTY_TRANSFER_MAX
                || UINT64_MAX - state->pending_read.grant_offset
                    < state->pending_read.count
            )
        )
        || (
            !state->pending_read.active
            && !request_is_clear(&state->pending_read)
        )
        || !completion_is_valid(
            &state->read_completion,
            true,
            state->last_accepted_request_id
        )
        || !completion_is_valid(
            &state->write_completion,
            false,
            state->last_accepted_request_id
        )
        || (
            state->pending_read.active
            && state->read_completion.active
        )
        || state->terminal.read_staging_length != 0
        || (
            state->terminal.write_retry_armed
            && state->write_completion.active
        )
        || (
            state->last_accepted_request_id == 0
            && (
                state->pending_read.active
                || state->read_completion.active
                || state->write_completion.active
            )
        )
        || (
            state->vfs_endpoint == MICROS_ENDPOINT_NONE
            && (
                state->last_accepted_request_id != 0
                || state->pending_read.active
                || state->read_completion.active
                || state->write_completion.active
            )
        )
        || (
            !state->owned
            && (
                state->last_accepted_request_id != 0
                || state->pending_read.active
                || state->read_completion.active
                || state->write_completion.active
                || micros_tty_output_pending(&state->terminal)
            )
        )
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
    copy_result = copy_grant(
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
        publish_read_completion(
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
        publish_read_completion(
            state,
            MICROS_TTY_RESULT_OK,
            staged_count
        );
    }
    *notification = MICROS_TTY_EVENT_COMPLETION;
    return micros_tty_service_validate(state);
}

static enum micros_tty_service_error handle_submit_read(
    struct micros_tty_service_state *state,
    const struct micros_tty_request *request,
    const struct micros_tty_service_io *io,
    struct micros_tty_service_reply_action *action
)
{
    enum micros_tty_service_copy_result copy_result;
    enum micros_tty_service_error error;
    uint64_t notification;

    error = validate_submit(state, request, action);
    if (error != MICROS_TTY_SERVICE_OK || action->active) {
        return error;
    }
    if (
        state->pending_read.active
        || state->read_completion.active
    ) {
        return prepare_reply(
            request->reply_token,
            request->type,
            MICROS_TTY_RESULT_BUSY,
            request->request_id,
            0,
            action
        );
    }
    copy_result = copy_grant(
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
        return prepare_reply(
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
    error = prepare_reply(
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

static enum micros_tty_service_error handle_submit_write(
    struct micros_tty_service_state *state,
    const struct micros_tty_request *request,
    const struct micros_tty_service_io *io,
    struct micros_tty_service_reply_action *action
)
{
    enum micros_tty_service_copy_result copy_result;
    enum micros_tty_service_error error;
    struct micros_tty_effects effects;

    error = validate_submit(state, request, action);
    if (error != MICROS_TTY_SERVICE_OK || action->active) {
        return error;
    }
    if (state->write_completion.active) {
        error = micros_tty_service_validate(state);
        if (error != MICROS_TTY_SERVICE_OK) {
            return error;
        }
        return prepare_reply(
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
        return prepare_reply(
            request->reply_token,
            request->type,
            MICROS_TTY_RESULT_BUSY,
            request->request_id,
            0,
            action
        );
    }
    copy_result = copy_grant(
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
        return prepare_reply(
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
    copy_result = copy_grant(
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
        return prepare_reply(
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
    error = prepare_reply(
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

static enum micros_tty_service_error handle_cancel(
    struct micros_tty_service_state *state,
    const struct micros_tty_request *request,
    struct micros_tty_service_reply_action *action
)
{
    enum micros_tty_service_error error;

    if (request->request_id == 0) {
        return prepare_reply(
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
        return prepare_reply(
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
    clear_bytes(
        &state->pending_read,
        sizeof(state->pending_read)
    );
    return prepare_reply(
        request->reply_token,
        request->type,
        MICROS_TTY_RESULT_OK,
        request->request_id,
        0,
        action
    );
}

static enum micros_tty_service_error handle_collect(
    struct micros_tty_service_state *state,
    const struct micros_tty_request *request,
    struct micros_tty_service_reply_action *action
)
{
    const struct micros_tty_service_completion *completion;
    enum micros_tty_service_release release;
    enum micros_tty_service_error error;

    if (request->request_id == 0) {
        return prepare_reply(
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
        return prepare_reply(
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
        return prepare_reply(
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
    error = prepare_reply(
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
    clear_bytes(action, sizeof(*action));
    status = micros_tty_decode_request(message, &request);
    if (status != MICROS_TTY_PROTOCOL_OK) {
        return status == MICROS_TTY_PROTOCOL_INVARIANT
            ? MICROS_TTY_SERVICE_ERROR_INVARIANT
            : prepare_protocol_reply(message, status, action);
    }
    if (request.source != state->vfs_endpoint) {
        return prepare_reply(
            request.reply_token,
            request.type,
            MICROS_TTY_RESULT_CALLER,
            request.request_id,
            0,
            action
        );
    }
    if (!state->owned) {
        return prepare_reply(
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
        return handle_submit_read(state, &request, io, action);
    case MICROS_TTY_MESSAGE_SUBMIT_WRITE:
        return handle_submit_write(state, &request, io, action);
    case MICROS_TTY_MESSAGE_CANCEL:
        return handle_cancel(state, &request, action);
    case MICROS_TTY_MESSAGE_COLLECT:
        return handle_collect(state, &request, action);
    default:
        return MICROS_TTY_SERVICE_ERROR_INVARIANT;
    }
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
        clear_bytes(completion, sizeof(*completion));
    }
    *notification = action->notification_after_reply;
    clear_bytes(action, sizeof(*action));
    return micros_tty_service_validate(state);
}
