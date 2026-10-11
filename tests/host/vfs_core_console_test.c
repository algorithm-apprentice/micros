#include "tests/host/vfs_core_test_fixture.h"
#include "tests/host/vfs_test_suites.h"

#include <stdio.h>
#include <string.h>

#define EXPECT_TRUE(expression) \
    do { \
        if (!(expression)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %s\n", \
                __FILE__, \
                __LINE__, \
                #expression \
            ); \
            return false; \
        } \
    } while (false)

static const uint8_t input[] = "ok\n";
static const uint8_t output[] = "hello";

struct console_write_case {
    struct micros_vfs_request request;
};

static bool delayed_read_completion_is_exact(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    micros_grant_t destination;

    EXPECT_TRUE(vfs_core_test_initialize_full(context));
    destination = vfs_test_fixture_add_application_grant(
        &context->fixture,
        context->application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        NULL,
        16
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_READ
    );
    selected.descriptor = 0;
    selected.grant = destination;
    selected.count = 16;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && !action.active
        && context->state.pending.state
            == MICROS_VFS_PENDING_TTY_READ_WAIT_COMPLETION
        && context->state.backend_page_owner
            == MICROS_VFS_PAGE_TTY_READ
        && context->fixture.backend_grant_count == 1
        && context->state.next_tty_request_id == 2
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_CLOSE
    );
    selected.descriptor = 2;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_BUSY
    );
    selected.descriptor = MICROS_VFS_DESCRIPTOR_CAPACITY;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_DESCRIPTOR
    );
    EXPECT_TRUE(
        vfs_test_fixture_complete_read(
            &context->fixture,
            input,
            sizeof(input) - 1
        )
        && micros_vfs_handle_tty_notification(
            &context->state,
            MICROS_TTY_EVENT_COMPLETION,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.active
        && action.request_type == MICROS_VFS_MESSAGE_READ
        && action.result == MICROS_VFS_RESULT_OK
        && action.transferred_count == sizeof(input) - 1
        && action.position == 0
        && memcmp(
            vfs_test_fixture_application_bytes(
                &context->fixture,
                destination
            ),
            input,
            sizeof(input) - 1
        ) == 0
        && context->state.pending.state == MICROS_VFS_PENDING_NONE
        && context->fixture.backend_grant_count == 0
    );
    return true;
}

static bool prequeued_read_completion_is_exact(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    micros_grant_t destination;

    EXPECT_TRUE(
        vfs_core_test_initialize_full(context)
        && vfs_test_fixture_queue_input(
            &context->fixture,
            input,
            sizeof(input) - 1
        )
    );
    destination = vfs_test_fixture_add_application_grant(
        &context->fixture,
        context->application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        NULL,
        16
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_READ
    );
    selected.descriptor = 0;
    selected.grant = destination;
    selected.count = 16;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && !action.active
        && context->state.pending.state
            == MICROS_VFS_PENDING_TTY_READ_WAIT_COMPLETION
        && !context->fixture.pending_read.active
        && context->fixture.read_completion.active
        && micros_vfs_handle_tty_notification(
            &context->state,
            MICROS_TTY_EVENT_COMPLETION,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.active
        && action.transferred_count == sizeof(input) - 1
        && memcmp(
            vfs_test_fixture_application_bytes(
                &context->fixture,
                destination
            ),
            input,
            sizeof(input) - 1
        ) == 0
        && context->state.pending.state == MICROS_VFS_PENDING_NONE
        && context->fixture.backend_grant_count == 0
    );
    return true;
}

static bool direct_write_completion_is_exact(
    struct vfs_core_test_context *context,
    struct console_write_case *test_case
)
{
    struct micros_vfs_result_action action;
    struct micros_vfs_result_action action_snapshot;
    micros_grant_t source;

    source = vfs_test_fixture_add_application_grant(
        &context->fixture,
        context->application_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        output,
        sizeof(output) - 1
    );
    test_case->request = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_WRITE
    );
    test_case->request.descriptor = 1;
    test_case->request.grant = source;
    test_case->request.count = sizeof(output) - 1;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &test_case->request,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && !action.active
        && context->state.pending.state
            == MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION
        && context->fixture.backend_grant_count == 0
        && context->state.next_tty_request_id == 3
        && context->fixture.tty_output_length
            == sizeof(output) - 1
        && memcmp(
            context->fixture.tty_output,
            output,
            sizeof(output) - 1
        ) == 0
    );
    memset(&action, UINT8_C(0xa5), sizeof(action));
    action_snapshot = action;
    context->snapshot = context->state;
    EXPECT_TRUE(
        micros_vfs_handle_tty_notification(
            &context->state,
            MICROS_TTY_EVENT_COMPLETION
                | MICROS_TTY_EVENT_WRITABLE,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_ERROR_INVARIANT
        && memcmp(
            &context->state,
            &context->snapshot,
            sizeof(context->state)
        ) == 0
        && memcmp(
            &action,
            &action_snapshot,
            sizeof(action)
        ) == 0
    );
    EXPECT_TRUE(
        micros_vfs_handle_tty_notification(
            &context->state,
            MICROS_TTY_EVENT_COMPLETION,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.active
        && action.request_type == MICROS_VFS_MESSAGE_WRITE
        && action.result == MICROS_VFS_RESULT_OK
        && action.transferred_count == sizeof(output) - 1
        && context->state.pending.state == MICROS_VFS_PENDING_NONE
    );
    return true;
}

static bool busy_write_writable_retry_is_exact(
    struct vfs_core_test_context *context,
    const struct console_write_case *test_case
)
{
    struct micros_vfs_result_action action;
    uint64_t events;

    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &test_case->request,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && !action.active
        && context->state.pending.state
            == MICROS_VFS_PENDING_TTY_WRITE_WAIT_WRITABLE
        && context->fixture.backend_grant_count == 1
        && context->state.next_tty_request_id == 3
    );
    events = vfs_test_fixture_drain_output(&context->fixture);
    context->snapshot = context->state;
    EXPECT_TRUE(
        events == MICROS_TTY_EVENT_WRITABLE
        && micros_vfs_handle_tty_notification(
            &context->state,
            events | MICROS_TTY_EVENT_COMPLETION,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_ERROR_INVARIANT
        && memcmp(
            &context->state,
            &context->snapshot,
            sizeof(context->state)
        ) == 0
        && context->fixture.backend_grant_count == 1
        && context->state.next_tty_request_id == 3
        && micros_vfs_handle_tty_notification(
            &context->state,
            events,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && !action.active
        && context->state.pending.state
            == MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION
        && context->fixture.backend_grant_count == 0
        && context->state.next_tty_request_id == 4
        && micros_vfs_handle_tty_notification(
            &context->state,
            MICROS_TTY_EVENT_COMPLETION,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.active
        && action.transferred_count == sizeof(output) - 1
        && context->state.pending.state == MICROS_VFS_PENDING_NONE
    );
    return true;
}

static bool detach_before_read_completion_is_exact(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    enum micros_vfs_trusted_result trusted_result;
    micros_grant_t destination;

    EXPECT_TRUE(vfs_core_test_initialize_full(context));
    destination = vfs_test_fixture_add_application_grant(
        &context->fixture,
        context->application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        NULL,
        8
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_READ
    );
    selected.descriptor = 0;
    selected.grant = destination;
    selected.count = 8;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && micros_vfs_detach(
            &context->state,
            context->application_endpoint,
            &context->fixture.io,
            &trusted_result
        ) == MICROS_VFS_CORE_OK
        && trusted_result == MICROS_VFS_TRUSTED_OK
        && context->state.pending.state == MICROS_VFS_PENDING_NONE
        && context->fixture.backend_grant_count == 0
        && context->state.vnodes[0].local_reference_count == 1
    );
    return true;
}

static bool detach_after_read_completion_is_exact(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    enum micros_vfs_trusted_result trusted_result;
    micros_grant_t destination;

    EXPECT_TRUE(vfs_core_test_initialize_full(context));
    destination = vfs_test_fixture_add_application_grant(
        &context->fixture,
        context->application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        NULL,
        8
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_READ
    );
    selected.descriptor = 0;
    selected.grant = destination;
    selected.count = 8;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && vfs_test_fixture_complete_read(
            &context->fixture,
            input,
            sizeof(input) - 1
        )
        && micros_vfs_detach(
            &context->state,
            context->application_endpoint,
            &context->fixture.io,
            &trusted_result
        ) == MICROS_VFS_CORE_OK
        && trusted_result == MICROS_VFS_TRUSTED_PENDING
        && context->state.pending.state
            == MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT
        && context->fixture.backend_grant_count == 0
        && micros_vfs_handle_tty_notification(
            &context->state,
            MICROS_TTY_EVENT_COMPLETION,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && !action.active
        && context->state.pending.state == MICROS_VFS_PENDING_NONE
        && micros_vfs_state_validate(&context->state)
            == MICROS_VFS_CORE_OK
    );
    return true;
}

static bool detach_during_write_completion_is_exact(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    enum micros_vfs_trusted_result trusted_result;
    micros_grant_t source;

    EXPECT_TRUE(vfs_core_test_initialize_full(context));
    source = vfs_test_fixture_add_application_grant(
        &context->fixture,
        context->application_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        output,
        sizeof(output) - 1
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_WRITE
    );
    selected.descriptor = 1;
    selected.grant = source;
    selected.count = sizeof(output) - 1;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && context->state.pending.state
            == MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION
        && micros_vfs_detach(
            &context->state,
            context->application_endpoint,
            &context->fixture.io,
            &trusted_result
        ) == MICROS_VFS_CORE_OK
        && trusted_result == MICROS_VFS_TRUSTED_PENDING
        && context->state.pending.state
            == MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT
        && context->fixture.backend_grant_count == 0
        && context->state.vnodes[0].local_reference_count == 1
        && micros_vfs_handle_tty_notification(
            &context->state,
            MICROS_TTY_EVENT_COMPLETION,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && !action.active
        && context->state.pending.state == MICROS_VFS_PENDING_NONE
    );
    return true;
}

static bool detach_during_writable_wait_is_exact(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    enum micros_vfs_trusted_result trusted_result;
    micros_grant_t source;

    EXPECT_TRUE(vfs_core_test_initialize_full(context));
    context->fixture.tty_output_busy = true;
    source = vfs_test_fixture_add_application_grant(
        &context->fixture,
        context->application_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        output,
        sizeof(output) - 1
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_WRITE
    );
    selected.descriptor = 1;
    selected.grant = source;
    selected.count = sizeof(output) - 1;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && context->state.pending.state
            == MICROS_VFS_PENDING_TTY_WRITE_WAIT_WRITABLE
        && micros_vfs_detach(
            &context->state,
            context->application_endpoint,
            &context->fixture.io,
            &trusted_result
        ) == MICROS_VFS_CORE_OK
        && trusted_result == MICROS_VFS_TRUSTED_PENDING
        && context->state.pending.state
            == MICROS_VFS_PENDING_TTY_WRITABLE_NOTICE_DEBT
        && context->fixture.backend_grant_count == 0
        && context->state.next_tty_request_id == 2
    );
    context->fixture.tty_output_busy = false;
    EXPECT_TRUE(
        micros_vfs_handle_tty_notification(
            &context->state,
            MICROS_TTY_EVENT_WRITABLE,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && context->state.pending.state == MICROS_VFS_PENDING_NONE
    );
    return true;
}

static bool grant_and_request_id_failures_are_exact(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    micros_grant_t destination;
    micros_grant_t source;
    size_t tty_calls;

    EXPECT_TRUE(vfs_core_test_initialize_full(context));
    destination = vfs_test_fixture_add_application_grant(
        &context->fixture,
        context->application_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        NULL,
        8
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_READ
    );
    selected.descriptor = 0;
    selected.grant = destination;
    selected.count = 8;
    tty_calls = context->fixture.tty_calls;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_GRANT
        && context->fixture.tty_calls == tty_calls
    );
    context->state.next_tty_request_id = 0;
    source = vfs_test_fixture_add_application_grant(
        &context->fixture,
        context->application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        output,
        sizeof(output) - 1
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_WRITE
    );
    selected.descriptor = 1;
    selected.grant = source;
    selected.count = sizeof(output) - 1;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_GRANT
        && context->fixture.tty_calls == tty_calls
    );
    source = vfs_test_fixture_add_application_grant(
        &context->fixture,
        context->application_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        output,
        sizeof(output) - 1
    );
    selected.grant = source;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_NO_SPACE
        && context->fixture.tty_calls == tty_calls
    );
    destination = vfs_test_fixture_add_application_grant(
        &context->fixture,
        context->application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        NULL,
        8
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_READ
    );
    selected.descriptor = 0;
    selected.grant = destination;
    selected.count = 8;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_NO_SPACE
        && context->fixture.tty_calls == tty_calls
    );
    return true;
}

bool vfs_core_console_async_cleanup_run(
    struct vfs_core_test_context *context
)
{
    struct console_write_case write_case;

    return (
        delayed_read_completion_is_exact(context)
        && prequeued_read_completion_is_exact(context)
        && direct_write_completion_is_exact(
            context,
            &write_case
        )
        && busy_write_writable_retry_is_exact(
            context,
            &write_case
        )
        && detach_before_read_completion_is_exact(context)
        && detach_after_read_completion_is_exact(context)
        && detach_during_write_completion_is_exact(context)
        && detach_during_writable_wait_is_exact(context)
        && grant_and_request_id_failures_are_exact(context)
    );
}

bool vfs_core_console_test_drain_run(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_result_action action;
    uint64_t events;

    EXPECT_TRUE(vfs_core_test_initialize_full(context));
    context->fixture.tty_output_busy = true;
    EXPECT_TRUE(
        micros_vfs_begin_test_drain(
            &context->state,
            context->application_endpoint,
            99,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && !action.active
        && context->state.pending.state
            == MICROS_VFS_PENDING_TTY_TEST_DRAIN_WAIT_WRITABLE
        && context->state.pending.reply_token == 99
        && context->state.pending.request_id == 1
        && context->state.pending.tty_grant == MICROS_GRANT_NONE
        && context->fixture.backend_grant_count == 0
        && context->fixture.tty_output_length == 0
    );
    events = vfs_test_fixture_drain_output(&context->fixture);
    EXPECT_TRUE(
        events == MICROS_TTY_EVENT_WRITABLE
        && micros_vfs_handle_tty_notification(
            &context->state,
            events,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.active
        && action.reply_token == 99
        && action.request_type == MICROS_VFS_TEST_MESSAGE_DRAIN
        && action.result == MICROS_VFS_RESULT_OK
        && action.transferred_count == 0
        && context->state.pending.state == MICROS_VFS_PENDING_NONE
        && context->state.next_tty_request_id == 2
        && context->fixture.backend_grant_count == 0
        && context->fixture.tty_output_length == 0
    );
    EXPECT_TRUE(
        micros_vfs_begin_test_drain(
            &context->state,
            context->application_endpoint,
            100,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.active
        && action.reply_token == 100
        && action.result == MICROS_VFS_RESULT_OK
        && context->state.pending.state == MICROS_VFS_PENDING_NONE
        && context->state.next_tty_request_id == 3
    );
    return true;
}
