#include "servers/vfs/vfs_internal.h"

#if defined(MICROS_BUILD_VFS_SERVICE_TEST)
static enum micros_vfs_core_error submit_test_drain(
    struct micros_vfs_state *state,
    uint64_t reply_token,
    uint64_t request_id,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
);
#endif

static void advance_tty_request_id(
    struct micros_vfs_state *state,
    uint64_t request_id
)
{
    if (state->next_tty_request_id != request_id) {
        return;
    }
    state->next_tty_request_id =
        vfs_tty_request_id_successor(request_id);
}

static enum micros_vfs_core_error call_tty(
    const struct micros_vfs_io *io,
    const struct micros_vfs_tty_request *request,
    struct micros_vfs_tty_response *response
)
{
    if (
        io == NULL
        || io->tty_call == NULL
        || io->tty_call(
            io->context,
            request,
            response
        ) != MICROS_VFS_BACKEND_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error vfs_console_handle_read(
    struct micros_vfs_state *state,
    size_t process_slot,
    size_t open_file_slot,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    enum micros_vfs_result *immediate_result
)
{
    struct micros_vfs_tty_request tty_request = {
        .operation = MICROS_VFS_TTY_SUBMIT_READ,
        .request_id = state->next_tty_request_id,
        .count = request->count,
    };
    struct micros_vfs_tty_response response = {0};
    enum micros_vfs_result result;
    micros_grant_t grant = MICROS_GRANT_NONE;

    if (
        vfs_validate_application_range(
            request,
            io,
            MICROS_GRANT_PERMISSION_WRITE,
            &result
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (result != MICROS_VFS_RESULT_OK) {
        *immediate_result = result;
        return MICROS_VFS_CORE_OK;
    }
    if (state->next_tty_request_id == 0) {
        *immediate_result = MICROS_VFS_RESULT_NO_SPACE;
        return MICROS_VFS_CORE_OK;
    }
    state->backend_page_owner = MICROS_VFS_PAGE_SYNCHRONOUS;
    if (
        vfs_create_backend_grant(
            state,
            io,
            state->tty_endpoint,
            state->backend_page,
            request->count,
            MICROS_GRANT_PERMISSION_WRITE,
            &grant,
            &result
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (result != MICROS_VFS_RESULT_OK) {
        state->backend_page_owner = MICROS_VFS_PAGE_FREE;
        *immediate_result = result;
        return MICROS_VFS_CORE_OK;
    }
    tty_request.grant = grant;
    if (
        call_tty(io, &tty_request, &response)
            != MICROS_VFS_CORE_OK
        || response.result != MICROS_TTY_RESULT_OK
        || response.request_id != tty_request.request_id
        || response.transferred_count != 0
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    advance_tty_request_id(state, tty_request.request_id);
    state->pending =
        (struct micros_vfs_pending_operation){
            .state =
                MICROS_VFS_PENDING_TTY_READ_WAIT_COMPLETION,
            .process_slot = (uint16_t)process_slot,
            .descriptor = (uint16_t)request->descriptor,
            .open_file = (uint16_t)open_file_slot,
            .endpoint = request->source,
            .reply_token = request->reply_token,
            .application_grant = request->grant,
            .tty_grant = grant,
            .application_offset = request->grant_offset,
            .count = request->count,
            .request_id = tty_request.request_id,
        };
    state->backend_page_owner = MICROS_VFS_PAGE_TTY_READ;
    *immediate_result = MICROS_VFS_RESULT_OK;
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error vfs_console_handle_write(
    struct micros_vfs_state *state,
    size_t process_slot,
    size_t open_file_slot,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    enum micros_vfs_result *immediate_result
)
{
    struct micros_vfs_tty_request tty_request = {
        .operation = MICROS_VFS_TTY_SUBMIT_WRITE,
        .request_id = state->next_tty_request_id,
        .count = request->count,
    };
    struct micros_vfs_tty_response response = {0};
    enum micros_vfs_client_result copy_result;
    enum micros_vfs_result result;
    micros_grant_t grant = MICROS_GRANT_NONE;

    if (io == NULL || io->client_copy == NULL) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    state->backend_page_owner = MICROS_VFS_PAGE_SYNCHRONOUS;
    copy_result = io->client_copy(
        io->context,
        MICROS_VFS_CLIENT_FROM_APPLICATION,
        request->source,
        request->grant,
        request->grant_offset,
        state->backend_page,
        request->count
    );
    if (copy_result == MICROS_VFS_CLIENT_REJECTED) {
        state->backend_page_owner = MICROS_VFS_PAGE_FREE;
        *immediate_result = MICROS_VFS_RESULT_GRANT;
        return MICROS_VFS_CORE_OK;
    }
    if (copy_result != MICROS_VFS_CLIENT_OK) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (state->next_tty_request_id == 0) {
        state->backend_page_owner = MICROS_VFS_PAGE_FREE;
        *immediate_result = MICROS_VFS_RESULT_NO_SPACE;
        return MICROS_VFS_CORE_OK;
    }
    if (
        vfs_create_backend_grant(
            state,
            io,
            state->tty_endpoint,
            state->backend_page,
            request->count,
            MICROS_GRANT_PERMISSION_READ,
            &grant,
            &result
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (result != MICROS_VFS_RESULT_OK) {
        state->backend_page_owner = MICROS_VFS_PAGE_FREE;
        *immediate_result = result;
        return MICROS_VFS_CORE_OK;
    }
    tty_request.grant = grant;
    if (
        call_tty(io, &tty_request, &response)
            != MICROS_VFS_CORE_OK
        || response.request_id != tty_request.request_id
        || response.transferred_count != 0
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (response.result == MICROS_TTY_RESULT_BUSY) {
        state->pending =
            (struct micros_vfs_pending_operation){
                .state =
                    MICROS_VFS_PENDING_TTY_WRITE_WAIT_WRITABLE,
                .process_slot = (uint16_t)process_slot,
                .descriptor = (uint16_t)request->descriptor,
                .open_file = (uint16_t)open_file_slot,
                .endpoint = request->source,
                .reply_token = request->reply_token,
                .application_grant = MICROS_GRANT_NONE,
                .tty_grant = grant,
                .count = request->count,
                .request_id = tty_request.request_id,
            };
        state->backend_page_owner =
            MICROS_VFS_PAGE_TTY_WRITE_RETRY;
        *immediate_result = MICROS_VFS_RESULT_OK;
        return MICROS_VFS_CORE_OK;
    }
    if (response.result != MICROS_TTY_RESULT_OK) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (
        vfs_revoke_backend_grant(io, grant)
            != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    state->backend_page_owner = MICROS_VFS_PAGE_FREE;
    advance_tty_request_id(state, tty_request.request_id);
    state->pending =
        (struct micros_vfs_pending_operation){
            .state =
                MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION,
            .process_slot = (uint16_t)process_slot,
            .descriptor = (uint16_t)request->descriptor,
            .open_file = (uint16_t)open_file_slot,
            .endpoint = request->source,
            .reply_token = request->reply_token,
            .application_grant = MICROS_GRANT_NONE,
            .tty_grant = MICROS_GRANT_NONE,
            .count = request->count,
            .request_id = tty_request.request_id,
        };
    *immediate_result = MICROS_VFS_RESULT_OK;
    return MICROS_VFS_CORE_OK;
}

static void clear_pending(struct micros_vfs_state *state)
{
    vfs_zero_bytes(&state->pending, sizeof(state->pending));
}

static void set_pending_debt(
    struct micros_vfs_state *state,
    enum micros_vfs_pending_state debt
)
{
    clear_pending(state);
    state->pending.state = debt;
}

static void prepare_pending_action(
    const struct micros_vfs_pending_operation *pending,
    uint32_t request_type,
    uint64_t transferred_count,
    struct micros_vfs_result_action *action
)
{
    vfs_zero_bytes(action, sizeof(*action));
    action->active = true;
    action->reply_token = pending->reply_token;
    action->request_type = request_type;
    action->result = MICROS_VFS_RESULT_OK;
    action->transferred_count = transferred_count;
}

static enum micros_vfs_core_error collect_tty_completion(
    struct micros_vfs_state *state,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
)
{
    struct micros_vfs_pending_operation pending =
        state->pending;
    struct micros_vfs_tty_request request = {
        .operation = MICROS_VFS_TTY_COLLECT,
        .request_id = pending.request_id,
    };
    struct micros_vfs_tty_response response = {0};
    enum micros_vfs_client_result copy_result;

    if (
        call_tty(io, &request, &response)
            != MICROS_VFS_CORE_OK
        || response.result != MICROS_TTY_RESULT_OK
        || response.request_id != pending.request_id
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (
        pending.state
            == MICROS_VFS_PENDING_TTY_READ_WAIT_COMPLETION
    ) {
        if (
            response.transferred_count == 0
            || response.transferred_count > pending.count
            || vfs_revoke_backend_grant(
                io,
                pending.tty_grant
            ) != MICROS_VFS_CORE_OK
            || io->client_copy == NULL
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        state->backend_page_owner = MICROS_VFS_PAGE_FREE;
        copy_result = io->client_copy(
            io->context,
            MICROS_VFS_CLIENT_TO_APPLICATION,
            pending.endpoint,
            pending.application_grant,
            pending.application_offset,
            state->backend_page,
            response.transferred_count
        );
        if (copy_result != MICROS_VFS_CLIENT_OK) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        clear_pending(state);
        prepare_pending_action(
            &pending,
            MICROS_VFS_MESSAGE_READ,
            response.transferred_count,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (
        pending.state
            == MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION
    ) {
        if (response.transferred_count != pending.count) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        clear_pending(state);
        prepare_pending_action(
            &pending,
            MICROS_VFS_MESSAGE_WRITE,
            response.transferred_count,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    return MICROS_VFS_CORE_ERROR_INVARIANT;
}

static enum micros_vfs_core_error retry_tty_write(
    struct micros_vfs_state *state,
    const struct micros_vfs_io *io
)
{
    struct micros_vfs_tty_request request = {
        .operation = MICROS_VFS_TTY_SUBMIT_WRITE,
        .grant = state->pending.tty_grant,
        .request_id = state->pending.request_id,
        .count = state->pending.count,
    };
    struct micros_vfs_tty_response response = {0};

    if (
        call_tty(io, &request, &response)
            != MICROS_VFS_CORE_OK
        || response.request_id != request.request_id
        || response.transferred_count != 0
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (response.result == MICROS_TTY_RESULT_BUSY) {
        return MICROS_VFS_CORE_OK;
    }
    if (
        response.result != MICROS_TTY_RESULT_OK
        || vfs_revoke_backend_grant(
            io,
            state->pending.tty_grant
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    state->backend_page_owner = MICROS_VFS_PAGE_FREE;
    advance_tty_request_id(state, request.request_id);
    state->pending.state =
        MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION;
    state->pending.application_grant = MICROS_GRANT_NONE;
    state->pending.tty_grant = MICROS_GRANT_NONE;
    state->pending.application_offset = 0;
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error vfs_console_cleanup_pending_operation(
    struct micros_vfs_state *state,
    micros_endpoint_t endpoint,
    const struct micros_vfs_io *io,
    bool *debt
)
{
    struct micros_vfs_pending_operation pending =
        state->pending;
    struct micros_vfs_tty_request request = {
        .request_id = pending.request_id,
    };
    struct micros_vfs_tty_response response = {0};

    *debt = false;
    if (
        pending.state == MICROS_VFS_PENDING_NONE
        || pending.endpoint != endpoint
    ) {
        return MICROS_VFS_CORE_OK;
    }
    switch (pending.state) {
    case MICROS_VFS_PENDING_TTY_READ_WAIT_COMPLETION:
        request.operation = MICROS_VFS_TTY_CANCEL;
        if (
            call_tty(io, &request, &response)
                != MICROS_VFS_CORE_OK
            || response.request_id != pending.request_id
            || response.transferred_count != 0
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        if (response.result == MICROS_TTY_RESULT_REQUEST) {
            request.operation = MICROS_VFS_TTY_COLLECT;
            if (
                call_tty(io, &request, &response)
                    != MICROS_VFS_CORE_OK
                || response.result != MICROS_TTY_RESULT_OK
                || response.request_id != pending.request_id
                || response.transferred_count == 0
                || response.transferred_count > pending.count
            ) {
                return MICROS_VFS_CORE_ERROR_INVARIANT;
            }
            *debt = true;
        } else if (response.result != MICROS_TTY_RESULT_OK) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        if (
            vfs_revoke_backend_grant(io, pending.tty_grant)
                != MICROS_VFS_CORE_OK
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        state->backend_page_owner = MICROS_VFS_PAGE_FREE;
        if (*debt) {
            set_pending_debt(
                state,
                MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT
            );
        } else {
            clear_pending(state);
        }
        return MICROS_VFS_CORE_OK;
    case MICROS_VFS_PENDING_TTY_WRITE_WAIT_WRITABLE:
        if (
            vfs_revoke_backend_grant(io, pending.tty_grant)
                != MICROS_VFS_CORE_OK
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        state->backend_page_owner = MICROS_VFS_PAGE_FREE;
        advance_tty_request_id(state, pending.request_id);
        set_pending_debt(
            state,
            MICROS_VFS_PENDING_TTY_WRITABLE_NOTICE_DEBT
        );
        *debt = true;
        return MICROS_VFS_CORE_OK;
    case MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION:
        request.operation = MICROS_VFS_TTY_COLLECT;
        if (
            call_tty(io, &request, &response)
                != MICROS_VFS_CORE_OK
            || response.result != MICROS_TTY_RESULT_OK
            || response.request_id != pending.request_id
            || response.transferred_count != pending.count
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        set_pending_debt(
            state,
            MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT
        );
        *debt = true;
        return MICROS_VFS_CORE_OK;
    case MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT:
    case MICROS_VFS_PENDING_TTY_WRITABLE_NOTICE_DEBT:
#if defined(MICROS_BUILD_VFS_SERVICE_TEST)
    case MICROS_VFS_PENDING_TTY_TEST_DRAIN_WAIT_WRITABLE:
#endif
    case MICROS_VFS_PENDING_NONE:
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_ERROR_INVARIANT;
}

static uint64_t expected_tty_event(
    enum micros_vfs_pending_state state
)
{
    switch (state) {
    case MICROS_VFS_PENDING_TTY_READ_WAIT_COMPLETION:
    case MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION:
    case MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT:
        return MICROS_TTY_EVENT_COMPLETION;
    case MICROS_VFS_PENDING_TTY_WRITE_WAIT_WRITABLE:
    case MICROS_VFS_PENDING_TTY_WRITABLE_NOTICE_DEBT:
#if defined(MICROS_BUILD_VFS_SERVICE_TEST)
    case MICROS_VFS_PENDING_TTY_TEST_DRAIN_WAIT_WRITABLE:
#endif
        return MICROS_TTY_EVENT_WRITABLE;
    case MICROS_VFS_PENDING_NONE:
        return 0;
    }
    return 0;
}

enum micros_vfs_core_error vfs_console_handle_notification(
    struct micros_vfs_state *state,
    uint64_t events,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
)
{
    if (events != expected_tty_event(state->pending.state)) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    vfs_zero_bytes(action, sizeof(*action));
    if ((events & MICROS_TTY_EVENT_COMPLETION) != 0) {
        if (
            state->pending.state
                == MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT
        ) {
            clear_pending(state);
        } else if (
            state->pending.state
                == MICROS_VFS_PENDING_TTY_READ_WAIT_COMPLETION
            || state->pending.state
                == MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION
        ) {
            if (
                collect_tty_completion(state, io, action)
                    != MICROS_VFS_CORE_OK
            ) {
                return MICROS_VFS_CORE_ERROR_INVARIANT;
            }
        } else {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
    }
    if ((events & MICROS_TTY_EVENT_WRITABLE) != 0) {
        if (
            state->pending.state
                == MICROS_VFS_PENDING_TTY_WRITABLE_NOTICE_DEBT
        ) {
            clear_pending(state);
        } else if (
            state->pending.state
                == MICROS_VFS_PENDING_TTY_WRITE_WAIT_WRITABLE
        ) {
            if (
                retry_tty_write(state, io)
                    != MICROS_VFS_CORE_OK
            ) {
                return MICROS_VFS_CORE_ERROR_INVARIANT;
            }
#if defined(MICROS_BUILD_VFS_SERVICE_TEST)
        } else if (
            state->pending.state
                == MICROS_VFS_PENDING_TTY_TEST_DRAIN_WAIT_WRITABLE
        ) {
            uint64_t reply_token =
                state->pending.reply_token;
            uint64_t request_id =
                state->pending.request_id;

            if (
                submit_test_drain(
                    state,
                    reply_token,
                    request_id,
                    io,
                    action
                ) != MICROS_VFS_CORE_OK
            ) {
                return MICROS_VFS_CORE_ERROR_INVARIANT;
            }
#endif
        } else {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
    }
    return MICROS_VFS_CORE_OK;
}

#if defined(MICROS_BUILD_VFS_SERVICE_TEST)
static void prepare_test_drain_action(
    uint64_t reply_token,
    enum micros_vfs_result result,
    struct micros_vfs_result_action *action
)
{
    vfs_zero_bytes(action, sizeof(*action));
    action->active = true;
    action->reply_token = reply_token;
    action->request_type = MICROS_VFS_TEST_MESSAGE_DRAIN;
    action->result = result;
}

static enum micros_vfs_core_error submit_test_drain(
    struct micros_vfs_state *state,
    uint64_t reply_token,
    uint64_t request_id,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
)
{
    struct micros_vfs_tty_request request = {
        .operation = MICROS_VFS_TTY_SUBMIT_WRITE,
        .grant = MICROS_GRANT_NONE,
        .request_id = request_id,
        .count = 1,
    };
    struct micros_vfs_tty_response response = {0};

    if (
        call_tty(io, &request, &response)
            != MICROS_VFS_CORE_OK
        || response.request_id != request_id
        || response.transferred_count != 0
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (response.result == MICROS_TTY_RESULT_BUSY) {
        state->pending =
            (struct micros_vfs_pending_operation){
                .state =
                    MICROS_VFS_PENDING_TTY_TEST_DRAIN_WAIT_WRITABLE,
                .reply_token = reply_token,
                .application_grant = MICROS_GRANT_NONE,
                .tty_grant = MICROS_GRANT_NONE,
                .request_id = request_id,
            };
        return MICROS_VFS_CORE_OK;
    }
    if (response.result != MICROS_TTY_RESULT_GRANT) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    advance_tty_request_id(state, request_id);
    clear_pending(state);
    prepare_test_drain_action(
        reply_token,
        MICROS_VFS_RESULT_OK,
        action
    );
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error vfs_console_begin_test_drain(
    struct micros_vfs_state *state,
    uint64_t reply_token,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
)
{
    if (state->pending.state != MICROS_VFS_PENDING_NONE) {
        prepare_test_drain_action(
            reply_token,
            MICROS_VFS_RESULT_BUSY,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (state->next_tty_request_id == 0) {
        prepare_test_drain_action(
            reply_token,
            MICROS_VFS_RESULT_NO_SPACE,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (
        submit_test_drain(
            state,
            reply_token,
            state->next_tty_request_id,
            io,
            action
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_OK;
}
#endif
