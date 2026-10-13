#include "tests/host/pm_test_internal.h"

struct pm_protocol_case {
    struct micros_ipc_message message;
    struct micros_pm_request request;
    struct micros_pm_request sentinel;
    struct micros_ipc_message result;
    struct micros_ipc_message result_sentinel;
};

static bool check_successful_requests(struct pm_protocol_case *context)
{
    EXPECT_TRUE(
        MICROS_PM_PROCESS_CAPACITY == 64
        && MICROS_PM_PROTOCOL_VERSION == 1
        && sizeof(struct micros_pm_reservation_result) == 32
    );
    EXPECT_TRUE(
        micros_pm_decode_request(&context->message, &context->request)
            == MICROS_PM_PROTOCOL_OK
        && context->request.type == MICROS_PM_MESSAGE_EXIT
        && context->request.source == context->message.source
        && context->request.reply_token == context->message.reply_token
        && context->request.flags == 0
        && context->request.exit_code == -19
        && context->request.child_pid == 0
    );
    context->message = pm_test_wait_request(MICROS_PM_WAIT_NOHANG, UINT64_C(45));
    EXPECT_TRUE(
        micros_pm_decode_request(&context->message, &context->request)
            == MICROS_PM_PROTOCOL_OK
        && context->request.type == MICROS_PM_MESSAGE_WAIT
        && context->request.flags == MICROS_PM_WAIT_NOHANG
        && context->request.child_pid == UINT64_C(45)
        && context->request.exit_code == 0
    );
    context->message = pm_test_spawn_request(UINT32_C(0x1234), 256);
    EXPECT_TRUE(
        micros_pm_decode_request(&context->message, &context->request)
            == MICROS_PM_PROTOCOL_OK
        && context->request.type == MICROS_PM_MESSAGE_SPAWN
        && context->request.flags == 0
        && context->request.grant == UINT32_C(0x1234)
        && context->request.request_size == 256
        && context->request.child_pid == 0
        && context->request.exit_code == 0
    );
    context->message = pm_test_spawn_request(UINT32_MAX, 256);
    EXPECT_TRUE(
        micros_pm_decode_request(&context->message, &context->request)
            == MICROS_PM_PROTOCOL_OK
        && context->request.grant == UINT32_MAX
    );
    context->message = pm_test_spawn_request(1, 4096);
    EXPECT_TRUE(
        micros_pm_decode_request(&context->message, &context->request)
            == MICROS_PM_PROTOCOL_OK
        && context->request.request_size == 4096
    );
    return true;
}

static bool check_rejected_requests(struct pm_protocol_case *context)
{
    size_t index;

    memset(&context->sentinel, 0xa5, sizeof(context->sentinel));
    context->request = context->sentinel;
    context->message.type = UINT32_C(0x000100ff);
    EXPECT_TRUE(
        micros_pm_decode_request(&context->message, &context->request)
            == MICROS_PM_PROTOCOL_BAD_TYPE
        && memcmp(&context->request, &context->sentinel, sizeof(context->request)) == 0
    );
    context->message = pm_test_wait_request(0, 0);
    pm_test_write_u32_le(&context->message.payload[0], 2);
    context->request = context->sentinel;
    EXPECT_TRUE(
        micros_pm_decode_request(&context->message, &context->request)
            == MICROS_PM_PROTOCOL_BAD_VERSION
        && memcmp(&context->request, &context->sentinel, sizeof(context->request)) == 0
    );
    context->message = pm_test_wait_request(UINT32_C(0x2), 0);
    context->request = context->sentinel;
    EXPECT_TRUE(
        micros_pm_decode_request(&context->message, &context->request)
            == MICROS_PM_PROTOCOL_MALFORMED
        && memcmp(&context->request, &context->sentinel, sizeof(context->request)) == 0
    );
    context->message = pm_test_spawn_request(1, 255);
    context->request = context->sentinel;
    EXPECT_TRUE(
        micros_pm_decode_request(&context->message, &context->request)
            == MICROS_PM_PROTOCOL_MALFORMED
        && memcmp(&context->request, &context->sentinel, sizeof(context->request)) == 0
    );
    context->message = pm_test_spawn_request(1, 4097);
    context->request = context->sentinel;
    EXPECT_TRUE(
        micros_pm_decode_request(&context->message, &context->request)
            == MICROS_PM_PROTOCOL_MALFORMED
        && memcmp(&context->request, &context->sentinel, sizeof(context->request)) == 0
    );
    context->message = pm_test_spawn_request(1, 256);
    pm_test_write_u32_le(&context->message.payload[4], 1);
    context->request = context->sentinel;
    EXPECT_TRUE(
        micros_pm_decode_request(&context->message, &context->request)
            == MICROS_PM_PROTOCOL_MALFORMED
        && memcmp(&context->request, &context->sentinel, sizeof(context->request)) == 0
    );
    for (index = 0; index < 32; ++index) {
        context->message = pm_test_spawn_request(1, 256);
        pm_test_write_u32_le(
            &context->message.payload[4],
            UINT32_C(1) << index
        );
        context->request = context->sentinel;
        EXPECT_TRUE(
            micros_pm_decode_request(&context->message, &context->request)
                == MICROS_PM_PROTOCOL_MALFORMED
            && memcmp(&context->request, &context->sentinel, sizeof(context->request)) == 0
        );
    }
    for (index = 16; index < sizeof(context->message.payload); ++index) {
        context->message = pm_test_spawn_request(1, 256);
        context->message.payload[index] = 1;
        context->request = context->sentinel;
        EXPECT_TRUE(
            micros_pm_decode_request(&context->message, &context->request)
                == MICROS_PM_PROTOCOL_MALFORMED
            && memcmp(&context->request, &context->sentinel, sizeof(context->request)) == 0
        );
    }
    context->message = pm_test_wait_request(0, 0);
    context->message.reply_token = 0;
    context->request = context->sentinel;
    EXPECT_TRUE(
        micros_pm_decode_request(&context->message, &context->request)
            == MICROS_PM_PROTOCOL_INVARIANT
        && memcmp(&context->request, &context->sentinel, sizeof(context->request)) == 0
    );

    return true;
}

static bool check_wait_and_error_results(struct pm_protocol_case *context)
{
    memset(&context->result_sentinel, 0xa5, sizeof(context->result_sentinel));
    context->result = context->result_sentinel;
    EXPECT_TRUE(
        micros_pm_build_result(
            MICROS_PM_MESSAGE_WAIT,
            MICROS_PM_RESULT_OK,
            UINT64_C(0x0102030405060708),
            MICROS_PM_EXIT_NORMAL,
            -23,
            &context->result
        ) == MICROS_PM_MODEL_OK
        && context->result.source == 0
        && context->result.type == MICROS_PM_MESSAGE_RESULT
        && context->result.reply_token == 0
        && pm_test_read_u32_le(&context->result.payload[0])
            == MICROS_PM_PROTOCOL_VERSION
        && pm_test_read_u32_le(&context->result.payload[4])
            == MICROS_PM_MESSAGE_WAIT
        && (int32_t)pm_test_read_u32_le(&context->result.payload[8])
            == MICROS_PM_RESULT_OK
        && pm_test_read_u32_le(&context->result.payload[12]) == 0
        && pm_test_read_u64_le(&context->result.payload[16])
            == UINT64_C(0x0102030405060708)
        && pm_test_read_u32_le(&context->result.payload[24])
            == MICROS_PM_EXIT_NORMAL
        && (int32_t)pm_test_read_u32_le(&context->result.payload[28]) == -23
    );
    context->result = context->result_sentinel;
    EXPECT_TRUE(
        micros_pm_build_result(
            MICROS_PM_MESSAGE_WAIT,
            MICROS_PM_RESULT_NO_CHILD,
            1,
            MICROS_PM_EXIT_NORMAL,
            0,
            &context->result
        ) == MICROS_PM_MODEL_ERROR_ARGUMENT
        && memcmp(&context->result, &context->result_sentinel, sizeof(context->result)) == 0
    );
    context->result = context->result_sentinel;
    EXPECT_TRUE(
        micros_pm_build_result(
            MICROS_PM_MESSAGE_RESULT,
            MICROS_PM_RESULT_BAD_TYPE,
            0,
            MICROS_PM_EXIT_NONE,
            0,
            &context->result
        ) == MICROS_PM_MODEL_OK
        && context->result.type == MICROS_PM_MESSAGE_RESULT
        && pm_test_read_u32_le(&context->result.payload[4])
            == MICROS_PM_MESSAGE_RESULT
        && (int32_t)pm_test_read_u32_le(&context->result.payload[8])
            == MICROS_PM_RESULT_BAD_TYPE
    );
    return true;
}

static bool check_spawn_results(struct pm_protocol_case *context)
{
    context->result = context->result_sentinel;
    EXPECT_TRUE(
        micros_pm_build_result(
            MICROS_PM_MESSAGE_SPAWN,
            MICROS_PM_RESULT_OK,
            UINT64_C(0x0102030405060708),
            MICROS_PM_EXIT_NONE,
            0,
            &context->result
        ) == MICROS_PM_MODEL_OK
        && pm_test_read_u32_le(&context->result.payload[4])
            == MICROS_PM_MESSAGE_SPAWN
        && (int32_t)pm_test_read_u32_le(&context->result.payload[8])
            == MICROS_PM_RESULT_OK
        && pm_test_read_u64_le(&context->result.payload[16])
            == UINT64_C(0x0102030405060708)
        && pm_test_read_u32_le(&context->result.payload[24])
            == MICROS_PM_EXIT_NONE
        && pm_test_read_u32_le(&context->result.payload[28]) == 0
    );
    context->result = context->result_sentinel;
    EXPECT_TRUE(
        micros_pm_build_result(
            MICROS_PM_MESSAGE_SPAWN,
            MICROS_PM_RESULT_OK,
            0,
            MICROS_PM_EXIT_NONE,
            0,
            &context->result
        ) == MICROS_PM_MODEL_ERROR_ARGUMENT
        && memcmp(&context->result, &context->result_sentinel, sizeof(context->result)) == 0
    );
    context->result = context->result_sentinel;
    EXPECT_TRUE(
        micros_pm_build_result(
            MICROS_PM_MESSAGE_SPAWN,
            MICROS_PM_RESULT_DESCRIPTOR,
            0,
            MICROS_PM_EXIT_NONE,
            0,
            &context->result
        ) == MICROS_PM_MODEL_OK
        && (int32_t)pm_test_read_u32_le(&context->result.payload[8])
            == MICROS_PM_RESULT_DESCRIPTOR
        && pm_test_bytes_are_zero(&context->result.payload[12], 36)
    );
    context->result = context->result_sentinel;
    EXPECT_TRUE(
        micros_pm_build_result(
            MICROS_PM_MESSAGE_SPAWN,
            MICROS_PM_RESULT_DESCRIPTOR,
            1,
            MICROS_PM_EXIT_NONE,
            0,
            &context->result
        ) == MICROS_PM_MODEL_ERROR_ARGUMENT
        && memcmp(&context->result, &context->result_sentinel, sizeof(context->result)) == 0
    );
    return true;
}

bool test_protocol_contract(void)
{
    struct pm_protocol_case context;

    context.message = pm_test_exit_request(-19);
    return (
        check_successful_requests(&context)
        && check_rejected_requests(&context)
        && check_wait_and_error_results(&context)
        && check_spawn_results(&context)
    );
}
