#include "servers/tty/tty_service_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const micros_endpoint_t tty_endpoint = UINT32_C(0x00007003);
static const micros_endpoint_t vfs_endpoint = UINT32_C(0x00009005);

struct copy_fixture {
    enum micros_tty_service_copy_result result;
    uint8_t remote[MICROS_TTY_TRANSFER_MAX];
    size_t remote_length;
    uint64_t grant_length;
    size_t validation_count;
    size_t transfer_count;
};

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

static void write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

static int32_t read_result(const struct micros_ipc_message *message)
{
    uint32_t value =
        (uint32_t)message->payload[8]
        | (uint32_t)message->payload[9] << 8
        | (uint32_t)message->payload[10] << 16
        | (uint32_t)message->payload[11] << 24;

    return (int32_t)value;
}

static uint64_t read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static struct micros_ipc_message submit_request(
    uint32_t type,
    uint64_t request_id,
    micros_grant_t grant,
    uint64_t offset,
    uint64_t count
)
{
    struct micros_ipc_message message = {
        .source = vfs_endpoint,
        .type = type,
        .reply_token = UINT64_C(0x55),
    };

    write_u32_le(&message.payload[0], MICROS_TTY_PROTOCOL_VERSION);
    write_u64_le(&message.payload[8], request_id);
    write_u32_le(&message.payload[16], grant);
    write_u64_le(&message.payload[24], offset);
    write_u64_le(&message.payload[32], count);
    return message;
}

static struct micros_ipc_message identity_request(
    uint32_t type,
    uint64_t request_id
)
{
    struct micros_ipc_message message = {
        .source = vfs_endpoint,
        .type = type,
        .reply_token = UINT64_C(0x66),
    };

    write_u32_le(&message.payload[0], MICROS_TTY_PROTOCOL_VERSION);
    write_u64_le(&message.payload[8], request_id);
    return message;
}

static enum micros_tty_service_copy_result copy_operation(
    void *context,
    enum micros_tty_service_copy_direction direction,
    micros_endpoint_t endpoint,
    micros_grant_t grant,
    uint64_t offset,
    uint8_t *local,
    size_t length
)
{
    struct copy_fixture *fixture = context;

    if (
        fixture == NULL
        || endpoint != vfs_endpoint
        || grant != 7
        || length > sizeof(fixture->remote)
        || (length != 0 && local == NULL)
    ) {
        return MICROS_TTY_SERVICE_COPY_INVARIANT;
    }
    if (length == 0) {
        ++fixture->validation_count;
        return offset <= fixture->grant_length
            ? fixture->result
            : MICROS_TTY_SERVICE_COPY_REJECTED;
    }
    if (
        offset != 3
        || fixture->grant_length < offset
        || length > fixture->grant_length - offset
    ) {
        return MICROS_TTY_SERVICE_COPY_REJECTED;
    }
    if (fixture->result != MICROS_TTY_SERVICE_COPY_OK) {
        return fixture->result;
    }
    if (direction == MICROS_TTY_SERVICE_COPY_FROM_VFS) {
        memcpy(local, fixture->remote, length);
    } else {
        memcpy(fixture->remote, local, length);
        fixture->remote_length = length;
    }
    ++fixture->transfer_count;
    return MICROS_TTY_SERVICE_COPY_OK;
}

static bool initialize_service(
    struct micros_tty_service_state *state
)
{
    return (
        micros_tty_service_initialize(
            state,
            tty_endpoint,
            vfs_endpoint
        ) == MICROS_TTY_SERVICE_OK
        && micros_tty_service_validate(state)
            == MICROS_TTY_SERVICE_OK
        && micros_tty_service_commit_ownership(state)
            == MICROS_TTY_SERVICE_OK
        && micros_tty_service_validate(state)
            == MICROS_TTY_SERVICE_OK
    );
}

static bool test_read_completion_and_collection(void)
{
    struct micros_tty_service_state state;
    struct copy_fixture fixture = {
        .result = MICROS_TTY_SERVICE_COPY_OK,
        .grant_length = 11,
    };
    const struct micros_tty_service_io io = {
        .copy = copy_operation,
        .context = &fixture,
    };
    struct micros_tty_service_reply_action action;
    struct micros_ipc_message message =
        submit_request(
            MICROS_TTY_MESSAGE_SUBMIT_READ,
            1,
            7,
            3,
            8
        );
    struct micros_tty_effects effects;
    uint64_t notification;

    EXPECT_TRUE(
        initialize_service(&state)
        && micros_tty_service_handle_call(
            &state,
            &message,
            &io,
            &action
        ) == MICROS_TTY_SERVICE_OK
        && read_result(&action.message) == MICROS_TTY_RESULT_OK
        && action.notification_after_reply == 0
        && state.pending_read.active
        && fixture.validation_count == 1
        && micros_tty_service_commit_reply(
            &state,
            &action,
            &notification
        ) == MICROS_TTY_SERVICE_OK
        && notification == 0
    );
    EXPECT_TRUE(
        micros_tty_receive_byte(&state.terminal, 'o', &effects)
            == MICROS_TTY_CORE_OK
        && micros_tty_receive_byte(&state.terminal, 'k', &effects)
            == MICROS_TTY_CORE_OK
        && micros_tty_receive_byte(&state.terminal, '\r', &effects)
            == MICROS_TTY_CORE_OK
        && micros_tty_service_complete_read(
            &state,
            &io,
            &notification
        ) == MICROS_TTY_SERVICE_OK
        && notification == MICROS_TTY_EVENT_COMPLETION
        && !state.pending_read.active
        && state.read_completion.active
        && fixture.remote_length == 3
        && memcmp(fixture.remote, "ok\n", 3) == 0
    );
    message = identity_request(MICROS_TTY_MESSAGE_COLLECT, 1);
    EXPECT_TRUE(
        micros_tty_service_handle_call(
            &state,
            &message,
            &io,
            &action
        ) == MICROS_TTY_SERVICE_OK
        && read_result(&action.message) == MICROS_TTY_RESULT_OK
        && read_u64_le(&action.message.payload[24]) == 3
        && state.read_completion.active
        && micros_tty_service_commit_reply(
            &state,
            &action,
            &notification
        ) == MICROS_TTY_SERVICE_OK
        && !state.read_completion.active
        && notification == 0
    );
    return true;
}

static bool test_cancel_and_grant_failure(void)
{
    struct micros_tty_service_state state;
    struct copy_fixture fixture = {
        .result = MICROS_TTY_SERVICE_COPY_OK,
        .grant_length = 11,
    };
    const struct micros_tty_service_io io = {
        .copy = copy_operation,
        .context = &fixture,
    };
    struct micros_tty_service_reply_action action;
    struct micros_ipc_message message =
        submit_request(
            MICROS_TTY_MESSAGE_SUBMIT_READ,
            1,
            7,
            3,
            8
        );
    uint64_t notification;

    EXPECT_TRUE(
        initialize_service(&state)
        && micros_tty_service_handle_call(
            &state,
            &message,
            &io,
            &action
        ) == MICROS_TTY_SERVICE_OK
        && micros_tty_service_commit_reply(
            &state,
            &action,
            &notification
        ) == MICROS_TTY_SERVICE_OK
    );
    message = identity_request(MICROS_TTY_MESSAGE_CANCEL, 1);
    EXPECT_TRUE(
        micros_tty_service_handle_call(
            &state,
            &message,
            &io,
            &action
        ) == MICROS_TTY_SERVICE_OK
        && read_result(&action.message) == MICROS_TTY_RESULT_OK
        && !state.pending_read.active
        && micros_tty_service_commit_reply(
            &state,
            &action,
            &notification
        ) == MICROS_TTY_SERVICE_OK
    );
    fixture.result = MICROS_TTY_SERVICE_COPY_REJECTED;
    message = submit_request(
        MICROS_TTY_MESSAGE_SUBMIT_READ,
        2,
        7,
        3,
        8
    );
    EXPECT_TRUE(
        micros_tty_service_handle_call(
            &state,
            &message,
            &io,
            &action
        ) == MICROS_TTY_SERVICE_OK
        && read_result(&action.message) == MICROS_TTY_RESULT_GRANT
        && state.last_accepted_request_id == 1
        && !state.pending_read.active
    );
    return true;
}

static bool test_write_completion_and_retry(void)
{
    struct micros_tty_service_state state;
    struct copy_fixture fixture = {
        .result = MICROS_TTY_SERVICE_COPY_OK,
        .remote = {'a', '\n'},
        .remote_length = 2,
        .grant_length = 5,
    };
    const struct micros_tty_service_io io = {
        .copy = copy_operation,
        .context = &fixture,
    };
    struct micros_tty_service_reply_action action;
    struct micros_ipc_message message =
        submit_request(
            MICROS_TTY_MESSAGE_SUBMIT_WRITE,
            1,
            7,
            3,
            2
        );
    struct micros_tty_effects effects;
    uint64_t notification;
    uint8_t byte;

    EXPECT_TRUE(
        initialize_service(&state)
        && micros_tty_service_handle_call(
            &state,
            &message,
            &io,
            &action
        ) == MICROS_TTY_SERVICE_OK
        && read_result(&action.message) == MICROS_TTY_RESULT_OK
        && action.uart_effects.enable_transmit
        && action.notification_after_reply
            == MICROS_TTY_EVENT_COMPLETION
        && state.write_completion.active
        && state.terminal.write_resident
        && fixture.validation_count == 1
        && fixture.transfer_count == 1
        && micros_tty_service_commit_reply(
            &state,
            &action,
            &notification
        ) == MICROS_TTY_SERVICE_OK
        && notification == MICROS_TTY_EVENT_COMPLETION
    );
    message = identity_request(MICROS_TTY_MESSAGE_COLLECT, 1);
    EXPECT_TRUE(
        micros_tty_service_handle_call(
            &state,
            &message,
            &io,
            &action
        ) == MICROS_TTY_SERVICE_OK
        && read_u64_le(&action.message.payload[24]) == 2
        && micros_tty_service_commit_reply(
            &state,
            &action,
            &notification
        ) == MICROS_TTY_SERVICE_OK
        && !state.write_completion.active
        && state.terminal.write_resident
    );
    message = submit_request(
        MICROS_TTY_MESSAGE_SUBMIT_WRITE,
        2,
        7,
        3,
        2
    );
    EXPECT_TRUE(
        micros_tty_service_handle_call(
            &state,
            &message,
            &io,
            &action
        ) == MICROS_TTY_SERVICE_OK
        && read_result(&action.message) == MICROS_TTY_RESULT_BUSY
        && state.terminal.write_retry_armed
        && state.last_accepted_request_id == 1
    );
    EXPECT_TRUE(
        micros_tty_output_take(&state.terminal, &byte, &effects)
            == MICROS_TTY_CORE_OK
        && byte == 'a'
        && !effects.writable
        && micros_tty_output_take(
            &state.terminal,
            &byte,
            &effects
        ) == MICROS_TTY_CORE_OK
        && byte == '\r'
        && !effects.writable
        && micros_tty_output_take(
            &state.terminal,
            &byte,
            &effects
        ) == MICROS_TTY_CORE_OK
        && byte == '\n'
        && effects.writable
        && effects.disable_transmit
    );
    return true;
}

static bool test_caller_phase_and_request_order(void)
{
    struct micros_tty_service_state state;
    struct copy_fixture fixture = {
        .result = MICROS_TTY_SERVICE_COPY_OK,
        .grant_length = 11,
    };
    const struct micros_tty_service_io io = {
        .copy = copy_operation,
        .context = &fixture,
    };
    struct micros_tty_service_reply_action action;
    struct micros_ipc_message message =
        submit_request(
            MICROS_TTY_MESSAGE_SUBMIT_READ,
            1,
            7,
            3,
            8
        );

    EXPECT_TRUE(
        micros_tty_service_initialize(
            &state,
            tty_endpoint,
            vfs_endpoint
        ) == MICROS_TTY_SERVICE_OK
        && micros_tty_service_handle_call(
            &state,
            &message,
            &io,
            &action
        ) == MICROS_TTY_SERVICE_OK
        && read_result(&action.message) == MICROS_TTY_RESULT_STATE
    );
    EXPECT_TRUE(
        micros_tty_service_commit_ownership(&state)
            == MICROS_TTY_SERVICE_OK
    );
    message.source = UINT32_C(0x0000a006);
    EXPECT_TRUE(
        micros_tty_service_handle_call(
            &state,
            &message,
            &io,
            &action
        ) == MICROS_TTY_SERVICE_OK
        && read_result(&action.message) == MICROS_TTY_RESULT_CALLER
    );
    message.source = vfs_endpoint;
    message = submit_request(
        MICROS_TTY_MESSAGE_SUBMIT_READ,
        0,
        7,
        3,
        8
    );
    EXPECT_TRUE(
        micros_tty_service_handle_call(
            &state,
            &message,
            &io,
            &action
        ) == MICROS_TTY_SERVICE_OK
        && read_result(&action.message)
            == MICROS_TTY_RESULT_MALFORMED
    );
    return true;
}

static bool test_immediate_read_and_reply_owned_collection(void)
{
    struct micros_tty_service_state state;
    struct copy_fixture fixture = {
        .result = MICROS_TTY_SERVICE_COPY_OK,
        .grant_length = 11,
    };
    const struct micros_tty_service_io io = {
        .copy = copy_operation,
        .context = &fixture,
    };
    struct micros_tty_service_reply_action first_action;
    struct micros_tty_service_reply_action second_action;
    struct micros_ipc_message message;
    struct micros_tty_effects effects;
    uint64_t notification;

    EXPECT_TRUE(
        initialize_service(&state)
        && micros_tty_receive_byte(
            &state.terminal,
            'x',
            &effects
        ) == MICROS_TTY_CORE_OK
        && micros_tty_receive_byte(
            &state.terminal,
            '\r',
            &effects
        ) == MICROS_TTY_CORE_OK
    );
    message = submit_request(
        MICROS_TTY_MESSAGE_SUBMIT_READ,
        1,
        7,
        3,
        8
    );
    EXPECT_TRUE(
        micros_tty_service_handle_call(
            &state,
            &message,
            &io,
            &first_action
        ) == MICROS_TTY_SERVICE_OK
        && read_result(&first_action.message)
            == MICROS_TTY_RESULT_OK
        && first_action.notification_after_reply
            == MICROS_TTY_EVENT_COMPLETION
        && !state.pending_read.active
        && state.read_completion.active
        && fixture.remote_length == 2
        && memcmp(fixture.remote, "x\n", 2) == 0
        && micros_tty_service_commit_reply(
            &state,
            &first_action,
            &notification
        ) == MICROS_TTY_SERVICE_OK
        && notification == MICROS_TTY_EVENT_COMPLETION
    );
    message = identity_request(MICROS_TTY_MESSAGE_COLLECT, 1);
    EXPECT_TRUE(
        micros_tty_service_handle_call(
            &state,
            &message,
            &io,
            &first_action
        ) == MICROS_TTY_SERVICE_OK
        && micros_tty_service_handle_call(
            &state,
            &message,
            &io,
            &second_action
        ) == MICROS_TTY_SERVICE_OK
        && read_result(&first_action.message)
            == MICROS_TTY_RESULT_OK
        && read_result(&second_action.message)
            == MICROS_TTY_RESULT_OK
        && read_u64_le(&first_action.message.payload[24]) == 2
        && read_u64_le(&second_action.message.payload[24]) == 2
        && state.read_completion.active
        && micros_tty_service_commit_reply(
            &state,
            &second_action,
            &notification
        ) == MICROS_TTY_SERVICE_OK
        && !state.read_completion.active
    );
    return true;
}

static bool test_revoked_retained_read_preserves_input(void)
{
    struct micros_tty_service_state state;
    struct copy_fixture fixture = {
        .result = MICROS_TTY_SERVICE_COPY_OK,
        .grant_length = 11,
    };
    const struct micros_tty_service_io io = {
        .copy = copy_operation,
        .context = &fixture,
    };
    struct micros_tty_service_reply_action action;
    struct micros_ipc_message message =
        submit_request(
            MICROS_TTY_MESSAGE_SUBMIT_READ,
            1,
            7,
            3,
            8
        );
    struct micros_tty_effects effects;
    uint64_t notification;

    EXPECT_TRUE(
        initialize_service(&state)
        && micros_tty_service_handle_call(
            &state,
            &message,
            &io,
            &action
        ) == MICROS_TTY_SERVICE_OK
        && micros_tty_service_commit_reply(
            &state,
            &action,
            &notification
        ) == MICROS_TTY_SERVICE_OK
        && micros_tty_receive_byte(
            &state.terminal,
            'r',
            &effects
        ) == MICROS_TTY_CORE_OK
        && micros_tty_receive_byte(
            &state.terminal,
            '\r',
            &effects
        ) == MICROS_TTY_CORE_OK
    );
    fixture.result = MICROS_TTY_SERVICE_COPY_REJECTED;
    EXPECT_TRUE(
        micros_tty_service_complete_read(
            &state,
            &io,
            &notification
        ) == MICROS_TTY_SERVICE_OK
        && notification == MICROS_TTY_EVENT_COMPLETION
        && !state.pending_read.active
        && state.read_completion.active
        && state.read_completion.result
            == MICROS_TTY_RESULT_GRANT
        && micros_tty_input_has_line(&state.terminal)
    );
    message = identity_request(MICROS_TTY_MESSAGE_COLLECT, 1);
    EXPECT_TRUE(
        micros_tty_service_handle_call(
            &state,
            &message,
            &io,
            &action
        ) == MICROS_TTY_SERVICE_OK
        && read_result(&action.message)
            == MICROS_TTY_RESULT_GRANT
        && read_u64_le(&action.message.payload[24]) == 0
        && micros_tty_service_commit_reply(
            &state,
            &action,
            &notification
        ) == MICROS_TTY_SERVICE_OK
    );
    fixture.result = MICROS_TTY_SERVICE_COPY_OK;
    message = submit_request(
        MICROS_TTY_MESSAGE_SUBMIT_READ,
        2,
        7,
        3,
        8
    );
    EXPECT_TRUE(
        micros_tty_service_handle_call(
            &state,
            &message,
            &io,
            &action
        ) == MICROS_TTY_SERVICE_OK
        && action.notification_after_reply
            == MICROS_TTY_EVENT_COMPLETION
        && fixture.remote_length == 2
        && memcmp(fixture.remote, "r\n", 2) == 0
        && !micros_tty_input_has_line(&state.terminal)
    );
    return true;
}

static bool test_rejected_and_reused_submit_ids(void)
{
    struct micros_tty_service_state state;
    struct copy_fixture fixture = {
        .result = MICROS_TTY_SERVICE_COPY_REJECTED,
        .remote = {'z'},
        .remote_length = 1,
        .grant_length = 4,
    };
    const struct micros_tty_service_io io = {
        .copy = copy_operation,
        .context = &fixture,
    };
    struct micros_tty_service_reply_action action;
    struct micros_ipc_message message =
        submit_request(
            MICROS_TTY_MESSAGE_SUBMIT_WRITE,
            4,
            7,
            3,
            1
        );

    EXPECT_TRUE(
        initialize_service(&state)
        && micros_tty_service_handle_call(
            &state,
            &message,
            &io,
            &action
        ) == MICROS_TTY_SERVICE_OK
        && read_result(&action.message)
            == MICROS_TTY_RESULT_GRANT
        && state.last_accepted_request_id == 0
    );
    fixture.result = MICROS_TTY_SERVICE_COPY_OK;
    EXPECT_TRUE(
        micros_tty_service_handle_call(
            &state,
            &message,
            &io,
            &action
        ) == MICROS_TTY_SERVICE_OK
        && read_result(&action.message) == MICROS_TTY_RESULT_OK
        && state.last_accepted_request_id == 4
    );
    message = submit_request(
        MICROS_TTY_MESSAGE_SUBMIT_READ,
        4,
        7,
        3,
        0
    );
    EXPECT_TRUE(
        micros_tty_service_handle_call(
            &state,
            &message,
            &io,
            &action
        ) == MICROS_TTY_SERVICE_OK
        && read_result(&action.message)
            == MICROS_TTY_RESULT_REQUEST
        && state.last_accepted_request_id == 4
    );
    message = submit_request(
        MICROS_TTY_MESSAGE_SUBMIT_WRITE,
        3,
        7,
        3,
        0
    );
    EXPECT_TRUE(
        micros_tty_service_handle_call(
            &state,
            &message,
            &io,
            &action
        ) == MICROS_TTY_SERVICE_OK
        && read_result(&action.message)
            == MICROS_TTY_RESULT_REQUEST
        && state.last_accepted_request_id == 4
    );
    return true;
}

static bool test_read_submission_validates_full_grant_range(void)
{
    struct micros_tty_service_state state;
    struct copy_fixture fixture = {
        .result = MICROS_TTY_SERVICE_COPY_OK,
        .grant_length = 4,
    };
    const struct micros_tty_service_io io = {
        .copy = copy_operation,
        .context = &fixture,
    };
    struct micros_tty_service_reply_action action;
    struct micros_ipc_message message =
        submit_request(
            MICROS_TTY_MESSAGE_SUBMIT_READ,
            1,
            7,
            3,
            2
        );
    struct micros_tty_effects effects;

    EXPECT_TRUE(
        initialize_service(&state)
        && micros_tty_receive_byte(
            &state.terminal,
            'g',
            &effects
        ) == MICROS_TTY_CORE_OK
        && micros_tty_receive_byte(
            &state.terminal,
            '\r',
            &effects
        ) == MICROS_TTY_CORE_OK
        && micros_tty_service_handle_call(
            &state,
            &message,
            &io,
            &action
        ) == MICROS_TTY_SERVICE_OK
        && read_result(&action.message)
            == MICROS_TTY_RESULT_GRANT
        && state.last_accepted_request_id == 0
        && !state.pending_read.active
        && !state.read_completion.active
        && micros_tty_input_has_line(&state.terminal)
        && fixture.validation_count == 1
        && fixture.transfer_count == 0
    );
    return true;
}

int main(void)
{
    return (
        test_read_completion_and_collection()
        && test_cancel_and_grant_failure()
        && test_write_completion_and_retry()
        && test_caller_phase_and_request_order()
        && test_immediate_read_and_reply_owned_collection()
        && test_revoked_retained_read_preserves_input()
        && test_rejected_and_reused_submit_ids()
        && test_read_submission_validates_full_grant_range()
    ) ? 0 : 1;
}
