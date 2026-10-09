#include "servers/tty/tty_core.h"

#include <stdbool.h>
#include <stdint.h>
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

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
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
    struct micros_ipc_message message;

    memset(&message, 0, sizeof(message));
    message.source = UINT32_C(0x00001005);
    message.type = type;
    message.reply_token = UINT64_C(0x55);
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
    struct micros_ipc_message message;

    memset(&message, 0, sizeof(message));
    message.source = UINT32_C(0x00001005);
    message.type = type;
    message.reply_token = UINT64_C(0x66);
    write_u32_le(&message.payload[0], MICROS_TTY_PROTOCOL_VERSION);
    write_u64_le(&message.payload[8], request_id);
    return message;
}

static bool test_protocol_contract(void)
{
    struct micros_ipc_message message = submit_request(
        MICROS_TTY_MESSAGE_SUBMIT_READ,
        UINT64_C(7),
        UINT32_C(0x1234),
        UINT64_C(9),
        UINT64_C(17)
    );
    struct micros_tty_request request;
    struct micros_tty_request sentinel;

    EXPECT_TRUE(
        MICROS_TTY_PROTOCOL_VERSION == 1
        && MICROS_TTY_INPUT_CAPACITY == 256
        && MICROS_TTY_ECHO_CAPACITY == 256
        && MICROS_TTY_TRANSFER_MAX == 4096
        && MICROS_TTY_WRITE_CAPACITY == 4096
        && MICROS_TTY_UART_VIRTUAL_BASE
            == UINT64_C(0x000000007fffe000)
        && MICROS_TTY_UART_PHYSICAL_BASE
            == UINT64_C(0x0000000010000000)
        && MICROS_TTY_UART_MAPPED_LENGTH == UINT64_C(0x1000)
        && MICROS_TTY_UART_IRQ_SOURCE == 10
    );
    EXPECT_TRUE(
        micros_tty_decode_request(&message, &request)
            == MICROS_TTY_PROTOCOL_OK
        && request.type == MICROS_TTY_MESSAGE_SUBMIT_READ
        && request.source == message.source
        && request.reply_token == message.reply_token
        && request.request_id == 7
        && request.grant == UINT32_C(0x1234)
        && request.grant_offset == 9
        && request.count == 17
    );

    message = submit_request(
        MICROS_TTY_MESSAGE_SUBMIT_WRITE,
        UINT64_C(8),
        UINT32_C(0x4321),
        UINT64_C(11),
        UINT64_C(19)
    );
    EXPECT_TRUE(
        micros_tty_decode_request(&message, &request)
            == MICROS_TTY_PROTOCOL_OK
        && request.type == MICROS_TTY_MESSAGE_SUBMIT_WRITE
        && request.request_id == 8
        && request.grant == UINT32_C(0x4321)
        && request.grant_offset == 11
        && request.count == 19
    );

    message = identity_request(MICROS_TTY_MESSAGE_CANCEL, 9);
    EXPECT_TRUE(
        micros_tty_decode_request(&message, &request)
            == MICROS_TTY_PROTOCOL_OK
        && request.type == MICROS_TTY_MESSAGE_CANCEL
        && request.request_id == 9
        && request.grant_offset == 0
        && request.count == 0
    );
    message = identity_request(MICROS_TTY_MESSAGE_COLLECT, 10);
    EXPECT_TRUE(
        micros_tty_decode_request(&message, &request)
            == MICROS_TTY_PROTOCOL_OK
        && request.type == MICROS_TTY_MESSAGE_COLLECT
        && request.request_id == 10
    );

    memset(&sentinel, 0xa5, sizeof(sentinel));
    request = sentinel;
    message.type = UINT32_C(0x000200ff);
    EXPECT_TRUE(
        micros_tty_decode_request(&message, &request)
            == MICROS_TTY_PROTOCOL_BAD_TYPE
        && memcmp(&request, &sentinel, sizeof(request)) == 0
    );
    message = identity_request(MICROS_TTY_MESSAGE_CANCEL, 10);
    write_u32_le(&message.payload[0], 2);
    request = sentinel;
    EXPECT_TRUE(
        micros_tty_decode_request(&message, &request)
            == MICROS_TTY_PROTOCOL_BAD_VERSION
        && memcmp(&request, &sentinel, sizeof(request)) == 0
    );
    message = submit_request(
        MICROS_TTY_MESSAGE_SUBMIT_READ,
        10,
        3,
        0,
        1
    );
    message.payload[20] = 1;
    request = sentinel;
    EXPECT_TRUE(
        micros_tty_decode_request(&message, &request)
            == MICROS_TTY_PROTOCOL_MALFORMED
        && memcmp(&request, &sentinel, sizeof(request)) == 0
    );
    message = identity_request(MICROS_TTY_MESSAGE_COLLECT, 10);
    message.payload[47] = 1;
    request = sentinel;
    EXPECT_TRUE(
        micros_tty_decode_request(&message, &request)
            == MICROS_TTY_PROTOCOL_MALFORMED
        && memcmp(&request, &sentinel, sizeof(request)) == 0
    );
    message = identity_request(MICROS_TTY_MESSAGE_CANCEL, 10);
    message.reply_token = 0;
    request = sentinel;
    EXPECT_TRUE(
        micros_tty_decode_request(&message, &request)
            == MICROS_TTY_PROTOCOL_INVARIANT
        && memcmp(&request, &sentinel, sizeof(request)) == 0
    );
    return true;
}

static bool test_result_contract(void)
{
    struct micros_ipc_message result;
    struct micros_ipc_message sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    result = sentinel;
    EXPECT_TRUE(
        micros_tty_build_result(
            MICROS_TTY_MESSAGE_COLLECT,
            MICROS_TTY_RESULT_OK,
            UINT64_C(0x0102030405060708),
            UINT64_C(37),
            &result
        ) == MICROS_TTY_CORE_OK
        && result.source == 0
        && result.type == MICROS_TTY_MESSAGE_RESULT
        && result.reply_token == 0
        && read_u32_le(&result.payload[0])
            == MICROS_TTY_PROTOCOL_VERSION
        && read_u32_le(&result.payload[4])
            == MICROS_TTY_MESSAGE_COLLECT
        && (int32_t)read_u32_le(&result.payload[8])
            == MICROS_TTY_RESULT_OK
        && read_u32_le(&result.payload[12]) == 0
        && read_u64_le(&result.payload[16])
            == UINT64_C(0x0102030405060708)
        && read_u64_le(&result.payload[24]) == 37
        && read_u64_le(&result.payload[32]) == 0
        && read_u64_le(&result.payload[40]) == 0
    );
    result = sentinel;
    EXPECT_TRUE(
        micros_tty_build_result(
            MICROS_TTY_MESSAGE_SUBMIT_WRITE,
            MICROS_TTY_RESULT_BUSY,
            41,
            1,
            &result
        ) == MICROS_TTY_CORE_ERROR_ARGUMENT
        && memcmp(&result, &sentinel, sizeof(result)) == 0
    );
    result = sentinel;
    EXPECT_TRUE(
        micros_tty_build_result(
            MICROS_TTY_MESSAGE_SUBMIT_WRITE,
            (enum micros_tty_result)-10,
            41,
            0,
            &result
        ) == MICROS_TTY_CORE_ERROR_ARGUMENT
        && memcmp(&result, &sentinel, sizeof(result)) == 0
    );
    return true;
}

static bool take_expected_output(
    struct micros_tty_state *state,
    const uint8_t *expected,
    size_t expected_count
)
{
    size_t index;

    for (index = 0; index < expected_count; ++index) {
        struct micros_tty_effects effects;
        uint8_t byte = 0;

        memset(&effects, 0, sizeof(effects));
        EXPECT_TRUE(
            micros_tty_output_take(state, &byte, &effects)
                == MICROS_TTY_CORE_OK
            && byte == expected[index]
            && !effects.enable_transmit
            && effects.disable_transmit
                == (index + 1 == expected_count)
        );
    }
    EXPECT_TRUE(!micros_tty_output_pending(state));
    return true;
}

static bool test_canonical_input_and_staged_read(void)
{
    static const uint8_t expected_echo[] = {
        'a', 'b', '\b', ' ', '\b', 'c', '\r', '\n',
    };
    struct micros_tty_state state;
    struct micros_tty_effects effects;
    uint64_t staged = UINT64_MAX;

    EXPECT_TRUE(
        micros_tty_state_initialize(&state) == MICROS_TTY_CORE_OK
        && micros_tty_state_validate(&state) == MICROS_TTY_CORE_OK
        && !micros_tty_input_has_line(&state)
        && !micros_tty_output_pending(&state)
    );

    memset(&effects, 0, sizeof(effects));
    EXPECT_TRUE(
        micros_tty_receive_byte(&state, 'a', &effects)
            == MICROS_TTY_CORE_OK
        && effects.enable_transmit
        && !effects.disable_transmit
    );
    memset(&effects, 0, sizeof(effects));
    EXPECT_TRUE(
        micros_tty_receive_byte(&state, 'b', &effects)
            == MICROS_TTY_CORE_OK
        && !effects.enable_transmit
    );
    EXPECT_TRUE(
        micros_tty_receive_byte(&state, 0x7f, &effects)
            == MICROS_TTY_CORE_OK
        && micros_tty_receive_byte(&state, 'c', &effects)
            == MICROS_TTY_CORE_OK
        && micros_tty_receive_byte(&state, '\r', &effects)
            == MICROS_TTY_CORE_OK
        && micros_tty_receive_byte(&state, 0x03, &effects)
            == MICROS_TTY_CORE_OK
        && micros_tty_input_has_line(&state)
        && state.input_count == 3
        && state.completed_line_count == 1
        && state.current_line_length == 0
        && state.echo_count == sizeof(expected_echo)
    );

    EXPECT_TRUE(
        micros_tty_read_stage(&state, 2, &staged)
            == MICROS_TTY_CORE_OK
        && staged == 2
        && state.read_staging_length == 2
        && state.read_staging[0] == 'a'
        && state.read_staging[1] == 'c'
        && state.input_count == 3
        && micros_tty_read_commit(&state) == MICROS_TTY_CORE_OK
        && state.input_count == 1
        && state.completed_line_count == 1
    );
    staged = UINT64_MAX;
    EXPECT_TRUE(
        micros_tty_read_stage(&state, MICROS_TTY_TRANSFER_MAX, &staged)
            == MICROS_TTY_CORE_OK
        && staged == 1
        && state.read_staging[0] == '\n'
        && micros_tty_read_commit(&state) == MICROS_TTY_CORE_OK
        && state.input_count == 0
        && state.completed_line_count == 0
        && !micros_tty_input_has_line(&state)
        && micros_tty_state_validate(&state) == MICROS_TTY_CORE_OK
    );
    EXPECT_TRUE(
        take_expected_output(
            &state,
            expected_echo,
            sizeof(expected_echo)
        )
    );
    return true;
}

static bool test_read_discard_preserves_input(void)
{
    struct micros_tty_state state;
    struct micros_tty_effects effects;
    uint64_t staged = 0;

    EXPECT_TRUE(
        micros_tty_state_initialize(&state) == MICROS_TTY_CORE_OK
        && micros_tty_receive_byte(&state, 'o', &effects)
            == MICROS_TTY_CORE_OK
        && micros_tty_receive_byte(&state, 'k', &effects)
            == MICROS_TTY_CORE_OK
        && micros_tty_receive_byte(&state, '\n', &effects)
            == MICROS_TTY_CORE_OK
        && micros_tty_read_stage(&state, 8, &staged)
            == MICROS_TTY_CORE_OK
        && staged == 3
        && micros_tty_read_discard(&state) == MICROS_TTY_CORE_OK
        && state.input_count == 3
        && state.completed_line_count == 1
        && state.read_staging_length == 0
        && micros_tty_read_stage(&state, 8, &staged)
            == MICROS_TTY_CORE_OK
        && staged == 3
        && memcmp(state.read_staging, "ok\n", 3) == 0
    );
    return true;
}

static bool test_input_and_echo_bounds(void)
{
    struct micros_tty_state state;
    struct micros_tty_effects effects;
    uint64_t staged = 0;
    size_t index;

    EXPECT_TRUE(
        micros_tty_state_initialize(&state) == MICROS_TTY_CORE_OK
    );
    for (index = 0; index < MICROS_TTY_INPUT_CAPACITY - 1; ++index) {
        EXPECT_TRUE(
            micros_tty_receive_byte(&state, 'x', &effects)
                == MICROS_TTY_CORE_OK
        );
    }
    EXPECT_TRUE(
        state.input_count == MICROS_TTY_INPUT_CAPACITY - 1
        && state.echo_count == MICROS_TTY_ECHO_CAPACITY - 1
        && micros_tty_receive_byte(&state, 'y', &effects)
            == MICROS_TTY_CORE_OK
        && state.input_count == MICROS_TTY_INPUT_CAPACITY - 1
        && state.input_drop_count == 1
        && micros_tty_receive_byte(&state, '\n', &effects)
            == MICROS_TTY_CORE_OK
        && state.input_count == MICROS_TTY_INPUT_CAPACITY
        && state.completed_line_count == 1
        && state.echo_count == MICROS_TTY_ECHO_CAPACITY - 1
        && state.echo_drop_count == 2
        && micros_tty_receive_byte(&state, '\n', &effects)
            == MICROS_TTY_CORE_OK
        && state.input_drop_count == 2
        && state.completed_line_count == 1
        && micros_tty_read_stage(
            &state,
            MICROS_TTY_TRANSFER_MAX,
            &staged
        ) == MICROS_TTY_CORE_OK
        && staged == MICROS_TTY_INPUT_CAPACITY
        && state.read_staging[MICROS_TTY_INPUT_CAPACITY - 1]
            == '\n'
        && micros_tty_state_validate(&state) == MICROS_TTY_CORE_OK
    );
    return true;
}

static bool test_output_priority_and_crlf(void)
{
    static const uint8_t write_bytes[] = {'a', '\n', 'b'};
    static const uint8_t expected[] = {'x', 'a', '\r', '\n', 'b'};
    struct micros_tty_state state;
    struct micros_tty_effects effects;

    EXPECT_TRUE(
        micros_tty_state_initialize(&state) == MICROS_TTY_CORE_OK
    );
    memset(&effects, 0, sizeof(effects));
    EXPECT_TRUE(
        micros_tty_write_accept(
            &state,
            write_bytes,
            sizeof(write_bytes),
            &effects
        ) == MICROS_TTY_CORE_OK
        && effects.enable_transmit
        && state.write_resident
        && state.write_length == sizeof(write_bytes)
    );
    memset(&effects, 0, sizeof(effects));
    EXPECT_TRUE(
        micros_tty_receive_byte(&state, 'x', &effects)
            == MICROS_TTY_CORE_OK
        && !effects.enable_transmit
        && take_expected_output(&state, expected, sizeof(expected))
        && !state.write_resident
        && state.write_length == 0
        && micros_tty_state_validate(&state) == MICROS_TTY_CORE_OK
    );
    return true;
}

static bool test_writable_retry_transition(void)
{
    static const uint8_t byte = 'z';
    struct micros_tty_state state;
    struct micros_tty_effects effects;
    uint8_t output = 0;

    EXPECT_TRUE(
        micros_tty_state_initialize(&state) == MICROS_TTY_CORE_OK
        && micros_tty_write_accept(&state, &byte, 1, &effects)
            == MICROS_TTY_CORE_OK
        && micros_tty_write_arm_retry(&state)
            == MICROS_TTY_CORE_OK
        && state.write_retry_armed
    );
    memset(&effects, 0, sizeof(effects));
    EXPECT_TRUE(
        micros_tty_output_take(&state, &output, &effects)
            == MICROS_TTY_CORE_OK
        && output == byte
        && effects.writable
        && effects.disable_transmit
        && !state.write_resident
        && !state.write_retry_armed
        && !state.transmit_interrupt_enabled
        && micros_tty_write_arm_retry(&state)
            == MICROS_TTY_CORE_ERROR_STATE
    );
    return true;
}

static bool test_failure_preservation_and_invariants(void)
{
    struct micros_tty_state state;
    struct micros_tty_state snapshot;
    struct micros_tty_effects effects;
    struct micros_tty_effects effects_sentinel;
    uint64_t staged = UINT64_MAX;
    uint64_t staged_sentinel = staged;

    EXPECT_TRUE(
        micros_tty_state_initialize(&state) == MICROS_TTY_CORE_OK
    );
    state.receive_error_count = UINT64_MAX;
    EXPECT_TRUE(
        micros_tty_record_receive_error(&state)
            == MICROS_TTY_CORE_OK
        && state.receive_error_count == UINT64_MAX
    );
    snapshot = state;
    memset(&effects_sentinel, 0xa5, sizeof(effects_sentinel));
    effects = effects_sentinel;
    EXPECT_TRUE(
        micros_tty_write_accept(&state, NULL, 0, &effects)
            == MICROS_TTY_CORE_ERROR_ARGUMENT
        && memcmp(&state, &snapshot, sizeof(state)) == 0
        && memcmp(
            &effects,
            &effects_sentinel,
            sizeof(effects)
        ) == 0
        && micros_tty_read_stage(&state, 1, &staged)
            == MICROS_TTY_CORE_ERROR_NOT_READY
        && staged == staged_sentinel
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );
    state.input_count = MICROS_TTY_INPUT_CAPACITY + 1;
    EXPECT_TRUE(
        micros_tty_state_validate(&state)
            == MICROS_TTY_CORE_ERROR_INVARIANT
    );
    return true;
}

int main(void)
{
    if (
        !test_protocol_contract()
        || !test_result_contract()
        || !test_canonical_input_and_staged_read()
        || !test_read_discard_preserves_input()
        || !test_input_and_echo_bounds()
        || !test_output_priority_and_crlf()
        || !test_writable_retry_transition()
        || !test_failure_preservation_and_invariants()
    ) {
        return 1;
    }
    return 0;
}
