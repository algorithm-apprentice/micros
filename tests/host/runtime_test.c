#include "micros/runtime.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lib/runtime/memory.h"
#include "lib/runtime/raw_syscall.h"
#include "micros/bootstrap_control.h"
#include "tests/qemu/bootstrap_launcher_control.h"

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

enum {
    RAW_WRITE_NONE = 0,
    RAW_WRITE_A1,
    RAW_WRITE_A3,
};

struct raw_capture {
    uint64_t arguments[8];
    int64_t result;
    size_t calls;
    int write_argument;
    const struct micros_ipc_message *expected_input;
    struct micros_ipc_message input_snapshot;
    struct micros_ipc_message output;
    bool input_observed;
};

static struct raw_capture capture;

static void reset_capture(int64_t result)
{
    memset(&capture, 0, sizeof(capture));
    capture.result = result;
}

int64_t micros_runtime_raw_syscall(
    uint64_t a0,
    uint64_t a1,
    uint64_t a2,
    uint64_t a3,
    uint64_t a4,
    uint64_t a5,
    uint64_t a6,
    uint64_t a7
)
{
    struct micros_ipc_message *output = NULL;

    capture.arguments[0] = a0;
    capture.arguments[1] = a1;
    capture.arguments[2] = a2;
    capture.arguments[3] = a3;
    capture.arguments[4] = a4;
    capture.arguments[5] = a5;
    capture.arguments[6] = a6;
    capture.arguments[7] = a7;
    ++capture.calls;
    if (capture.expected_input != NULL) {
        capture.input_snapshot = *capture.expected_input;
        capture.input_observed = true;
    }
    if (capture.write_argument == RAW_WRITE_A1) {
        output = (struct micros_ipc_message *)(uintptr_t)a1;
    } else if (capture.write_argument == RAW_WRITE_A3) {
        output = (struct micros_ipc_message *)(uintptr_t)a3;
    }
    if (output != NULL) {
        *output = capture.output;
    }
    return capture.result;
}

static bool arguments_match(
    uint64_t a0,
    uint64_t a1,
    uint64_t a2,
    uint64_t a3,
    uint64_t a4,
    uint64_t a5,
    uint64_t a6,
    uint64_t a7
)
{
    const uint64_t expected[] = {
        a0,
        a1,
        a2,
        a3,
        a4,
        a5,
        a6,
        a7,
    };

    return (
        capture.calls == 1
        && memcmp(capture.arguments, expected, sizeof(expected)) == 0
    );
}

static struct micros_ipc_message message_pattern(
    uint32_t type,
    uint8_t seed
)
{
    struct micros_ipc_message message = {
        .source = UINT32_C(0xa1a2a3a4),
        .type = type,
        .reply_token = UINT64_C(0xb1b2b3b4b5b6b7b8),
    };
    size_t index;

    for (index = 0; index < sizeof(message.payload); ++index) {
        message.payload[index] = (uint8_t)(seed + index);
    }
    return message;
}

static bool test_ipc_wrappers(void)
{
    const micros_endpoint_t destination = UINT32_C(0xf1234567);
    const uint64_t token = UINT64_C(0xfedcba9876543210);
    const uint64_t event_mask = UINT64_C(0x8123456789abcdef);
    struct micros_ipc_message input =
        message_pattern(UINT32_C(0x10203040), UINT8_C(0x10));
    struct micros_ipc_message original = input;
    struct micros_ipc_message output =
        message_pattern(UINT32_C(0x50607080), UINT8_C(0x80));
    struct micros_ipc_message receive = original;
    micros_runtime_result_t result;

    errno = EDOM;
    reset_capture(MICROS_SYSCALL_ABI_UNAUTHORIZED);
    result = micros_runtime_send(destination, &input);
    EXPECT_TRUE(
        result == MICROS_SYSCALL_ABI_UNAUTHORIZED
        && errno == EDOM
        && memcmp(&input, &original, sizeof(input)) == 0
        && arguments_match(
            destination,
            (uintptr_t)&input,
            0,
            0,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_SEND
        )
    );

    errno = ERANGE;
    reset_capture(MICROS_SYSCALL_ABI_OK);
    capture.write_argument = RAW_WRITE_A1;
    capture.output = output;
    result = micros_runtime_receive(MICROS_ENDPOINT_ANY, &receive);
    EXPECT_TRUE(
        result == MICROS_SYSCALL_ABI_OK
        && errno == ERANGE
        && memcmp(&receive, &output, sizeof(receive)) == 0
        && arguments_match(
            MICROS_ENDPOINT_ANY,
            (uintptr_t)&receive,
            0,
            0,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_RECEIVE
        )
    );

    receive = original;
    errno = E2BIG;
    reset_capture(MICROS_SYSCALL_ABI_OK);
    capture.expected_input = &receive;
    capture.write_argument = RAW_WRITE_A1;
    capture.output = output;
    result = micros_runtime_call(destination, &receive);
    EXPECT_TRUE(
        result == MICROS_SYSCALL_ABI_OK
        && errno == E2BIG
        && capture.input_observed
        && memcmp(
            &capture.input_snapshot,
            &original,
            sizeof(original)
        ) == 0
        && memcmp(&receive, &output, sizeof(receive)) == 0
        && arguments_match(
            destination,
            (uintptr_t)&receive,
            0,
            0,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_CALL
        )
    );

    errno = ENOSPC;
    reset_capture(MICROS_SYSCALL_ABI_REPLY_TOKEN);
    result = micros_runtime_reply(token, &input);
    EXPECT_TRUE(
        result == MICROS_SYSCALL_ABI_REPLY_TOKEN
        && errno == ENOSPC
        && memcmp(&input, &original, sizeof(input)) == 0
        && arguments_match(
            token,
            (uintptr_t)&input,
            0,
            0,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_REPLY
        )
    );

    receive = original;
    errno = EBUSY;
    reset_capture(MICROS_SYSCALL_ABI_OK);
    capture.expected_input = &input;
    capture.write_argument = RAW_WRITE_A3;
    capture.output = output;
    result = micros_runtime_reply_receive(
        token,
        &input,
        destination,
        &receive
    );
    EXPECT_TRUE(
        result == MICROS_SYSCALL_ABI_OK
        && errno == EBUSY
        && capture.input_observed
        && memcmp(
            &capture.input_snapshot,
            &original,
            sizeof(original)
        ) == 0
        && memcmp(&receive, &output, sizeof(receive)) == 0
        && arguments_match(
            token,
            (uintptr_t)&input,
            destination,
            (uintptr_t)&receive,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_REPLY_RECEIVE
        )
    );

    errno = ENOENT;
    reset_capture(MICROS_SYSCALL_ABI_DEAD_ENDPOINT);
    result = micros_runtime_notify(destination, event_mask);
    EXPECT_TRUE(
        result == MICROS_SYSCALL_ABI_DEAD_ENDPOINT
        && errno == ENOENT
        && arguments_match(
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

    receive = original;
    reset_capture(MICROS_SYSCALL_ABI_MEMORY_FAULT);
    result = micros_runtime_call(destination, &receive);
    EXPECT_TRUE(
        result == MICROS_SYSCALL_ABI_MEMORY_FAULT
        && memcmp(&receive, &original, sizeof(receive)) == 0
    );
    return true;
}

static bool test_grant_wrappers(void)
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

    errno = EDOM;
    reset_capture(UINT32_C(0x7ffffffe));
    result = micros_runtime_grant_create(
        endpoint,
        base,
        length,
        permissions,
        &output
    );
    EXPECT_TRUE(
        result == MICROS_SYSCALL_ABI_OK
        && output == UINT32_C(0x7ffffffe)
        && errno == EDOM
        && arguments_match(
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
        error = MICROS_SYSCALL_ABI_ARGUMENT;
        error >= MICROS_SYSCALL_ABI_RANGE;
        --error
    ) {
        output = UINT32_C(0xa5a5a5a5);
        reset_capture(error);
        result = micros_runtime_grant_create(
            endpoint,
            base,
            length,
            permissions,
            &output
        );
        EXPECT_TRUE(
            result == error
            && output == UINT32_C(0xa5a5a5a5)
            && capture.calls == 1
        );
    }

    output = UINT32_C(0x5a5a5a5a);
    reset_capture(MICROS_SYSCALL_ABI_OK);
    result = micros_runtime_grant_create(
        endpoint,
        base,
        length,
        permissions,
        NULL
    );
    EXPECT_TRUE(
        result == MICROS_SYSCALL_ABI_ARGUMENT
        && output == UINT32_C(0x5a5a5a5a)
        && capture.calls == 0
    );

    reset_capture(MICROS_SYSCALL_ABI_STALE_GRANT);
    result = micros_runtime_grant_revoke(grant);
    EXPECT_TRUE(
        result == MICROS_SYSCALL_ABI_STALE_GRANT
        && arguments_match(
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

    reset_capture(MICROS_SYSCALL_ABI_RANGE);
    result = micros_runtime_grant_copy_from(
        endpoint,
        grant,
        offset,
        local_address,
        length
    );
    EXPECT_TRUE(
        result == MICROS_SYSCALL_ABI_RANGE
        && arguments_match(
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

    reset_capture(MICROS_SYSCALL_ABI_MEMORY_FAULT);
    result = micros_runtime_grant_copy_to(
        endpoint,
        grant,
        offset,
        local_address,
        length
    );
    EXPECT_TRUE(
        result == MICROS_SYSCALL_ABI_MEMORY_FAULT
        && arguments_match(
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

    reset_capture(MICROS_SYSCALL_ABI_UNAUTHORIZED);
    result = micros_runtime_grant_validate(
        endpoint,
        grant,
        offset,
        length,
        MICROS_GRANT_PERMISSION_WRITE
    );
    EXPECT_TRUE(
        result == MICROS_SYSCALL_ABI_UNAUTHORIZED
        && arguments_match(
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

    for (
        error = MICROS_SYSCALL_ABI_ARGUMENT;
        error >= MICROS_SYSCALL_ABI_RANGE;
        --error
    ) {
        reset_capture(error);
        errno = EILSEQ;
        result = micros_runtime_notify(endpoint, UINT64_C(1));
        EXPECT_TRUE(result == error && errno == EILSEQ);
    }
    return true;
}

static bool test_bootstrap_launcher_console_begin(void)
{
    reset_capture(MICROS_SYSCALL_ABI_OK);
    EXPECT_TRUE(
        micros_bootstrap_launcher_console_begin(
            MICROS_TTY_SERVICE_ID
        ) == MICROS_SYSCALL_ABI_OK
        && arguments_match(
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

static bool test_memory_support(void)
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

int main(void)
{
    if (
        !test_ipc_wrappers()
        || !test_grant_wrappers()
        || !test_bootstrap_launcher_console_begin()
        || !test_memory_support()
    ) {
        return 1;
    }
    return 0;
}
