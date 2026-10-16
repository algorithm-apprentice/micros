#include "tty_service_internal.h"

#include <stddef.h>
#include <stdint.h>

bool tty_service_endpoint_is_valid(micros_endpoint_t endpoint)
{
    return (
        endpoint != MICROS_ENDPOINT_NONE
        && endpoint != MICROS_ENDPOINT_ANY
    );
}

static bool tty_service_request_is_clear(
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

static bool tty_service_completion_is_clear(
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

static bool tty_service_completion_is_valid(
    const struct micros_tty_service_completion *completion,
    bool read_completion,
    uint64_t last_accepted_request_id
)
{
    if (!completion->active) {
        return tty_service_completion_is_clear(completion);
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
        || !tty_service_endpoint_is_valid(state->self_endpoint)
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
            && !tty_service_request_is_clear(&state->pending_read)
        )
        || !tty_service_completion_is_valid(
            &state->read_completion,
            true,
            state->last_accepted_request_id
        )
        || !tty_service_completion_is_valid(
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
