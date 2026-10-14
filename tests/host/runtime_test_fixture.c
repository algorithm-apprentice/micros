#include "tests/host/runtime_test_internal.h"

#include <string.h>

#include "lib/runtime/raw_syscall.h"

static struct raw_capture capture;

struct raw_capture *runtime_test_capture(void)
{
    return &capture;
}

void runtime_test_reset_capture(int64_t result)
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

bool runtime_test_arguments_match(
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

struct micros_ipc_message runtime_test_message_pattern(
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
