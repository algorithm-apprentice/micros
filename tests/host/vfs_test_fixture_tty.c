#include "tests/host/vfs_test_fixture_internal.h"

#include <string.h>

static void tty_submit_read(
    struct vfs_test_fixture *fixture,
    const struct micros_vfs_tty_request *request,
    struct micros_vfs_tty_response *response
)
{
    struct vfs_test_backend_grant *grant;

    if (
        request->request_id == 0
        || request->request_id
            <= fixture->tty_last_accepted_request_id
        || fixture->pending_read.active
        || fixture->read_completion.active
    ) {
        response->result = MICROS_TTY_RESULT_REQUEST;
        return;
    }
    grant = vfs_test_fixture_lookup_backend_grant(
        fixture,
        request->grant,
        fixture->tty_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        request->count
    );
    if (grant == NULL) {
        response->result = MICROS_TTY_RESULT_GRANT;
        return;
    }
    fixture->tty_last_accepted_request_id = request->request_id;
    if (fixture->tty_queued_input_length != 0) {
        size_t count = fixture->tty_queued_input_length;

        if (count > request->count) {
            count = request->count;
        }
        memcpy(grant->local, fixture->tty_queued_input, count);
        memset(
            fixture->tty_queued_input,
            0,
            sizeof(fixture->tty_queued_input)
        );
        fixture->tty_queued_input_length = 0;
        fixture->read_completion =
            (struct vfs_test_tty_completion){
                .active = true,
                .request_id = request->request_id,
                .count = count,
            };
    } else {
        fixture->pending_read =
            (struct vfs_test_tty_request){
                .active = true,
                .request_id = request->request_id,
                .grant = request->grant,
                .count = request->count,
            };
    }
    response->result = MICROS_TTY_RESULT_OK;
}

static void tty_submit_write(
    struct vfs_test_fixture *fixture,
    const struct micros_vfs_tty_request *request,
    struct micros_vfs_tty_response *response
)
{
    struct vfs_test_backend_grant *grant;

    if (
        request->request_id == 0
        || request->request_id
            <= fixture->tty_last_accepted_request_id
    ) {
        response->result = MICROS_TTY_RESULT_REQUEST;
        return;
    }
    if (fixture->tty_output_busy) {
        fixture->tty_writable_armed = true;
        response->result = MICROS_TTY_RESULT_BUSY;
        return;
    }
    grant = vfs_test_fixture_lookup_backend_grant(
        fixture,
        request->grant,
        fixture->tty_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        request->count
    );
    if (grant == NULL) {
        response->result = MICROS_TTY_RESULT_GRANT;
        return;
    }
    memcpy(
        fixture->tty_output,
        grant->local,
        request->count
    );
    fixture->tty_output_length = request->count;
    fixture->tty_output_busy = true;
    fixture->write_completion =
        (struct vfs_test_tty_completion){
            .active = true,
            .request_id = request->request_id,
            .count = request->count,
        };
    fixture->tty_last_accepted_request_id = request->request_id;
    response->result = MICROS_TTY_RESULT_OK;
}

static void tty_cancel(
    struct vfs_test_fixture *fixture,
    const struct micros_vfs_tty_request *request,
    struct micros_vfs_tty_response *response
)
{
    if (
        fixture->pending_read.active
        && fixture->pending_read.request_id
            == request->request_id
    ) {
        memset(
            &fixture->pending_read,
            0,
            sizeof(fixture->pending_read)
        );
        response->result = MICROS_TTY_RESULT_OK;
        return;
    }
    response->result = MICROS_TTY_RESULT_REQUEST;
}

static void tty_collect(
    struct vfs_test_fixture *fixture,
    const struct micros_vfs_tty_request *request,
    struct micros_vfs_tty_response *response
)
{
    if (
        fixture->read_completion.active
        && fixture->read_completion.request_id
            == request->request_id
    ) {
        response->result = MICROS_TTY_RESULT_OK;
        response->transferred_count =
            fixture->read_completion.count;
        memset(
            &fixture->read_completion,
            0,
            sizeof(fixture->read_completion)
        );
        return;
    }
    if (
        fixture->write_completion.active
        && fixture->write_completion.request_id
            == request->request_id
    ) {
        response->result = MICROS_TTY_RESULT_OK;
        response->transferred_count =
            fixture->write_completion.count;
        memset(
            &fixture->write_completion,
            0,
            sizeof(fixture->write_completion)
        );
        return;
    }
    response->result = fixture->pending_read.active
        ? MICROS_TTY_RESULT_PENDING
        : MICROS_TTY_RESULT_REQUEST;
}

enum micros_vfs_backend_status vfs_test_fixture_tty_call(
    void *context,
    const struct micros_vfs_tty_request *request,
    struct micros_vfs_tty_response *response
)
{
    struct vfs_test_fixture *fixture = context;

    ++fixture->tty_calls;
    if (
        fixture->force_tty_invariant
        || request == NULL
        || response == NULL
    ) {
        return MICROS_VFS_BACKEND_INVARIANT;
    }
    memset(response, 0, sizeof(*response));
    response->request_id = request->request_id;
    if (
        fixture->inject_tty_result
        && request->operation == fixture->injected_tty_operation
    ) {
        fixture->inject_tty_result = false;
        response->result = fixture->injected_tty_result;
        return MICROS_VFS_BACKEND_OK;
    }
    switch (request->operation) {
    case MICROS_VFS_TTY_SUBMIT_READ:
        tty_submit_read(fixture, request, response);
        break;
    case MICROS_VFS_TTY_SUBMIT_WRITE:
        tty_submit_write(fixture, request, response);
        break;
    case MICROS_VFS_TTY_CANCEL:
        tty_cancel(fixture, request, response);
        break;
    case MICROS_VFS_TTY_COLLECT:
        tty_collect(fixture, request, response);
        break;
    default:
        return MICROS_VFS_BACKEND_INVARIANT;
    }
    return MICROS_VFS_BACKEND_OK;
}

bool vfs_test_fixture_complete_read(
    struct vfs_test_fixture *fixture,
    const uint8_t *bytes,
    size_t length
)
{
    struct vfs_test_backend_grant *grant;

    if (
        !fixture->pending_read.active
        || fixture->read_completion.active
        || length == 0
        || length > fixture->pending_read.count
    ) {
        return false;
    }
    grant = vfs_test_fixture_lookup_backend_grant(
        fixture,
        fixture->pending_read.grant,
        fixture->tty_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        length
    );
    if (grant == NULL) {
        return false;
    }
    memcpy(grant->local, bytes, length);
    fixture->read_completion =
        (struct vfs_test_tty_completion){
            .active = true,
            .request_id = fixture->pending_read.request_id,
            .count = length,
        };
    memset(
        &fixture->pending_read,
        0,
        sizeof(fixture->pending_read)
    );
    return true;
}

bool vfs_test_fixture_queue_input(
    struct vfs_test_fixture *fixture,
    const uint8_t *bytes,
    size_t length
)
{
    if (
        fixture == NULL
        || bytes == NULL
        || length == 0
        || length > MICROS_TTY_TRANSFER_MAX
        || fixture->pending_read.active
        || fixture->read_completion.active
        || fixture->tty_queued_input_length != 0
    ) {
        return false;
    }
    memcpy(fixture->tty_queued_input, bytes, length);
    fixture->tty_queued_input_length = length;
    return true;
}

uint64_t vfs_test_fixture_drain_output(
    struct vfs_test_fixture *fixture
)
{
    uint64_t events = 0;

    fixture->tty_output_busy = false;
    fixture->tty_output_length = 0;
    if (fixture->tty_writable_armed) {
        fixture->tty_writable_armed = false;
        events |= MICROS_TTY_EVENT_WRITABLE;
    }
    return events;
}
