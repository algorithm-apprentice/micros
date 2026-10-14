#include "tests/host/runtime_test_internal.h"

#include <errno.h>
#include <string.h>

#include "lib/runtime/memory.h"
#include "micros/bootstrap_control.h"
#include "tests/qemu/bootstrap_launcher_control.h"

static bool runtime_test_ipc_requests(
    const micros_endpoint_t destination,
    struct micros_ipc_message *input,
    const struct micros_ipc_message *original,
    const struct micros_ipc_message *output,
    struct micros_ipc_message *receive,
    micros_runtime_result_t *result
)
{
    struct raw_capture *capture = runtime_test_capture();

    errno = EDOM;
    runtime_test_reset_capture(MICROS_SYSCALL_ABI_UNAUTHORIZED);
    *result = micros_runtime_send(destination, input);
    EXPECT_TRUE(
        *result == MICROS_SYSCALL_ABI_UNAUTHORIZED
        && errno == EDOM
        && memcmp(input, original, sizeof(*input)) == 0
        && runtime_test_arguments_match(
            destination,
            (uintptr_t)input,
            0,
            0,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_SEND
        )
    );

    errno = ERANGE;
    runtime_test_reset_capture(MICROS_SYSCALL_ABI_OK);
    capture->write_argument = RAW_WRITE_A1;
    capture->output = *output;
    *result = micros_runtime_receive(MICROS_ENDPOINT_ANY, receive);
    EXPECT_TRUE(
        *result == MICROS_SYSCALL_ABI_OK
        && errno == ERANGE
        && memcmp(receive, output, sizeof(*receive)) == 0
        && runtime_test_arguments_match(
            MICROS_ENDPOINT_ANY,
            (uintptr_t)receive,
            0,
            0,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_RECEIVE
        )
    );

    *receive = *original;
    errno = E2BIG;
    runtime_test_reset_capture(MICROS_SYSCALL_ABI_OK);
    capture->expected_input = receive;
    capture->write_argument = RAW_WRITE_A1;
    capture->output = *output;
    *result = micros_runtime_call(destination, receive);
    EXPECT_TRUE(
        *result == MICROS_SYSCALL_ABI_OK
        && errno == E2BIG
        && capture->input_observed
        && memcmp(
            &capture->input_snapshot,
            original,
            sizeof(*original)
        ) == 0
        && memcmp(receive, output, sizeof(*receive)) == 0
        && runtime_test_arguments_match(
            destination,
            (uintptr_t)receive,
            0,
            0,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_CALL
        )
    );
    return true;
}

static bool runtime_test_ipc_replies(
    const micros_endpoint_t destination,
    const uint64_t token,
    struct micros_ipc_message *input,
    const struct micros_ipc_message *original,
    const struct micros_ipc_message *output,
    struct micros_ipc_message *receive,
    micros_runtime_result_t *result
)
{
    struct raw_capture *capture = runtime_test_capture();

    errno = ENOSPC;
    runtime_test_reset_capture(MICROS_SYSCALL_ABI_REPLY_TOKEN);
    *result = micros_runtime_reply(token, input);
    EXPECT_TRUE(
        *result == MICROS_SYSCALL_ABI_REPLY_TOKEN
        && errno == ENOSPC
        && memcmp(input, original, sizeof(*input)) == 0
        && runtime_test_arguments_match(
            token,
            (uintptr_t)input,
            0,
            0,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_REPLY
        )
    );

    *receive = *original;
    errno = EBUSY;
    runtime_test_reset_capture(MICROS_SYSCALL_ABI_OK);
    capture->expected_input = input;
    capture->write_argument = RAW_WRITE_A3;
    capture->output = *output;
    *result = micros_runtime_reply_receive(
        token,
        input,
        destination,
        receive
    );
    EXPECT_TRUE(
        *result == MICROS_SYSCALL_ABI_OK
        && errno == EBUSY
        && capture->input_observed
        && memcmp(
            &capture->input_snapshot,
            original,
            sizeof(*original)
        ) == 0
        && memcmp(receive, output, sizeof(*receive)) == 0
        && runtime_test_arguments_match(
            token,
            (uintptr_t)input,
            destination,
            (uintptr_t)receive,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_REPLY_RECEIVE
        )
    );
    return true;
}

static bool runtime_test_ipc_notifications_and_failure(
    const micros_endpoint_t destination,
    const uint64_t event_mask,
    const struct micros_ipc_message *original,
    struct micros_ipc_message *receive,
    micros_runtime_result_t *result
)
{
    errno = ENOENT;
    runtime_test_reset_capture(MICROS_SYSCALL_ABI_DEAD_ENDPOINT);
    *result = micros_runtime_notify(destination, event_mask);
    EXPECT_TRUE(
        *result == MICROS_SYSCALL_ABI_DEAD_ENDPOINT
        && errno == ENOENT
        && runtime_test_arguments_match(
            destination,
            event_mask,
            0,
            0,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_NOTIFY
        )
    );

    *receive = *original;
    runtime_test_reset_capture(MICROS_SYSCALL_ABI_MEMORY_FAULT);
    *result = micros_runtime_call(destination, receive);
    EXPECT_TRUE(
        *result == MICROS_SYSCALL_ABI_MEMORY_FAULT
        && memcmp(receive, original, sizeof(*receive)) == 0
    );
    return true;
}

bool test_ipc_wrappers(void)
{
    const micros_endpoint_t destination = UINT32_C(0xf1234567);
    const uint64_t token = UINT64_C(0xfedcba9876543210);
    const uint64_t event_mask = UINT64_C(0x8123456789abcdef);
    struct micros_ipc_message input =
        runtime_test_message_pattern(UINT32_C(0x10203040), UINT8_C(0x10));
    struct micros_ipc_message original = input;
    struct micros_ipc_message output =
        runtime_test_message_pattern(UINT32_C(0x50607080), UINT8_C(0x80));
    struct micros_ipc_message receive = original;
    micros_runtime_result_t result;

    if (!runtime_test_ipc_requests(
        destination, &input, &original, &output, &receive, &result
    )) {
        return false;
    }
    if (!runtime_test_ipc_replies(
        destination, token, &input, &original, &output, &receive, &result
    )) {
        return false;
    }
    return runtime_test_ipc_notifications_and_failure(
        destination, event_mask, &original, &receive, &result
    );
}

static bool runtime_test_grant_creation(
    const micros_endpoint_t endpoint,
    const uintptr_t base,
    const size_t length,
    const uint32_t permissions,
    micros_grant_t *output,
    micros_runtime_result_t *result,
    int64_t *error
)
{
    struct raw_capture *capture = runtime_test_capture();

    errno = EDOM;
    runtime_test_reset_capture(UINT32_C(0x7ffffffe));
    *result = micros_runtime_grant_create(
        endpoint,
        base,
        length,
        permissions,
        output
    );
    EXPECT_TRUE(
        *result == MICROS_SYSCALL_ABI_OK
        && *output == UINT32_C(0x7ffffffe)
        && errno == EDOM
        && runtime_test_arguments_match(
            endpoint,
            base,
            length,
            permissions,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_GRANT_CREATE
        )
    );

    for (
        *error = MICROS_SYSCALL_ABI_ARGUMENT;
        *error >= MICROS_SYSCALL_ABI_RANGE;
        --*error
    ) {
        *output = UINT32_C(0xa5a5a5a5);
        runtime_test_reset_capture(*error);
        *result = micros_runtime_grant_create(
            endpoint,
            base,
            length,
            permissions,
            output
        );
        EXPECT_TRUE(
            *result == *error
            && *output == UINT32_C(0xa5a5a5a5)
            && capture->calls == 1
        );
    }

    *output = UINT32_C(0x5a5a5a5a);
    runtime_test_reset_capture(MICROS_SYSCALL_ABI_OK);
    *result = micros_runtime_grant_create(
        endpoint,
        base,
        length,
        permissions,
        NULL
    );
    EXPECT_TRUE(
        *result == MICROS_SYSCALL_ABI_ARGUMENT
        && *output == UINT32_C(0x5a5a5a5a)
        && capture->calls == 0
    );
    return true;
}

static bool runtime_test_grant_marshalling(
    const micros_endpoint_t endpoint,
    const micros_grant_t grant,
    const size_t offset,
    const uintptr_t local_address,
    const size_t length,
    micros_runtime_result_t *result
)
{
    runtime_test_reset_capture(MICROS_SYSCALL_ABI_STALE_GRANT);
    *result = micros_runtime_grant_revoke(grant);
    EXPECT_TRUE(
        *result == MICROS_SYSCALL_ABI_STALE_GRANT
        && runtime_test_arguments_match(
            grant,
            0,
            0,
            0,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_GRANT_REVOKE
        )
    );

    runtime_test_reset_capture(MICROS_SYSCALL_ABI_RANGE);
    *result = micros_runtime_grant_copy_from(
        endpoint,
        grant,
        offset,
        local_address,
        length
    );
    EXPECT_TRUE(
        *result == MICROS_SYSCALL_ABI_RANGE
        && runtime_test_arguments_match(
            endpoint,
            grant,
            offset,
            local_address,
            length,
            0,
            0,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM
        )
    );

    runtime_test_reset_capture(MICROS_SYSCALL_ABI_MEMORY_FAULT);
    *result = micros_runtime_grant_copy_to(
        endpoint,
        grant,
        offset,
        local_address,
        length
    );
    EXPECT_TRUE(
        *result == MICROS_SYSCALL_ABI_MEMORY_FAULT
        && runtime_test_arguments_match(
            endpoint,
            grant,
            offset,
            local_address,
            length,
            0,
            0,
            MICROS_SYSCALL_ABI_GRANT_COPY_TO
        )
    );

    runtime_test_reset_capture(MICROS_SYSCALL_ABI_UNAUTHORIZED);
    *result = micros_runtime_grant_validate(
        endpoint,
        grant,
        offset,
        length,
        MICROS_GRANT_PERMISSION_WRITE
    );
    EXPECT_TRUE(
        *result == MICROS_SYSCALL_ABI_UNAUTHORIZED
        && runtime_test_arguments_match(
            endpoint,
            grant,
            offset,
            length,
            MICROS_GRANT_PERMISSION_WRITE,
            0,
            0,
            MICROS_SYSCALL_ABI_GRANT_VALIDATE
        )
    );
    return true;
}

static bool runtime_test_negative_results(
    const micros_endpoint_t endpoint,
    micros_runtime_result_t *result,
    int64_t *error
)
{
    for (
        *error = MICROS_SYSCALL_ABI_ARGUMENT;
        *error >= MICROS_SYSCALL_ABI_RANGE;
        --*error
    ) {
        runtime_test_reset_capture(*error);
        errno = EILSEQ;
        *result = micros_runtime_notify(endpoint, UINT64_C(1));
        EXPECT_TRUE(*result == *error && errno == EILSEQ);
    }
    return true;
}

bool test_grant_wrappers(void)
{
    const micros_endpoint_t endpoint = UINT32_C(0xfedcba98);
    const micros_grant_t grant = UINT32_C(0xf1234567);
    const uintptr_t base = UINT64_C(0x000000007fff1020);
    const size_t length = UINT64_C(0x0000000100000021);
    const size_t offset = UINT64_C(0x0000000200000032);
    const uintptr_t local_address =
        UINT64_C(0x0000000041234568);
    const uint32_t permissions = UINT32_C(0x80000003);
    micros_grant_t output = UINT32_C(0xa5a5a5a5);
    micros_runtime_result_t result;
    int64_t error;

    if (!runtime_test_grant_creation(
        endpoint, base, length, permissions, &output, &result, &error
    )) {
        return false;
    }
    if (!runtime_test_grant_marshalling(
        endpoint, grant, offset, local_address, length, &result
    )) {
        return false;
    }
    return runtime_test_negative_results(endpoint, &result, &error);
}

bool test_bootstrap_launcher_console_begin(void)
{
    runtime_test_reset_capture(MICROS_SYSCALL_ABI_OK);
    EXPECT_TRUE(
        micros_bootstrap_launcher_console_begin(
            MICROS_TTY_SERVICE_ID
        ) == MICROS_SYSCALL_ABI_OK
        && runtime_test_arguments_match(
            MICROS_BOOTSTRAP_COMMAND_CONSOLE_BEGIN,
            MICROS_TTY_SERVICE_ID,
            0,
            0,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL
        )
    );
    return true;
}

bool test_memory_support(void)
{
    unsigned char source[320];
    unsigned char destination[320];
    unsigned char expected[320];
    size_t index;

    for (index = 0; index < sizeof(source); ++index) {
        source[index] = (unsigned char)(index * 37U + 11U);
        destination[index] = UINT8_C(0xa5);
        expected[index] = UINT8_C(0xa5);
    }
    EXPECT_TRUE(
        micros_runtime_memcpy(destination + 17, source + 19, 0)
            == destination + 17
        && memcmp(destination, expected, sizeof(destination)) == 0
    );
    expected[17] = source[19];
    EXPECT_TRUE(
        micros_runtime_memcpy(destination + 17, source + 19, 1)
            == destination + 17
        && memcmp(destination, expected, sizeof(destination)) == 0
    );
    memcpy(expected + 23, source + 29, 257);
    EXPECT_TRUE(
        micros_runtime_memcpy(destination + 23, source + 29, 257)
            == destination + 23
        && memcmp(destination, expected, sizeof(destination)) == 0
    );

    memset(destination, UINT8_C(0x3c), sizeof(destination));
    memset(expected, UINT8_C(0x3c), sizeof(expected));
    EXPECT_TRUE(
        micros_runtime_memset(destination + 7, 0x1a5, 0)
            == destination + 7
        && memcmp(destination, expected, sizeof(destination)) == 0
    );
    memset(expected + 7, UINT8_C(0xa5), 1);
    EXPECT_TRUE(
        micros_runtime_memset(destination + 7, 0x1a5, 1)
            == destination + 7
        && memcmp(destination, expected, sizeof(destination)) == 0
    );
    memset(expected + 31, UINT8_C(0x5a), 257);
    EXPECT_TRUE(
        micros_runtime_memset(destination + 31, 0x5a, 257)
            == destination + 31
        && memcmp(destination, expected, sizeof(destination)) == 0
    );
    return true;
}
