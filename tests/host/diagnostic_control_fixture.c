#include "kernel/tty/tty_control_core.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static bool request_matches(
    const struct micros_tty_control_request *request,
    uint64_t command
)
{
    if (
        request->command != command
        || request->version != 1
        || request->physical_base != 0
        || request->irq_source != 0
    ) {
        return false;
    }
    if (command == 3) {
        return (
            request->service_id == 4
            && request->endpoint == UINT32_C(0x1003)
            && request->virtual_base == UINT64_C(0x40010000)
            && request->mapped_length == 10016
        );
    }
    return (
        request->service_id == 0
        && request->endpoint == 0
        && request->virtual_base == 0
        && request->mapped_length == 0
    );
}

static void observe_decode(const struct micros_syscall_arguments *arguments)
{
    struct micros_tty_control_request request;
    unsigned char before[sizeof(request)];
    enum micros_syscall_abi_result result;
    bool shape;

    memset(&request, 0xa5, sizeof(request));
    memcpy(before, &request, sizeof(request));
    result = micros_tty_control_decode(arguments, &request);
    shape = result == 0 && request_matches(&request, arguments->a0);
    printf(
        "{\"command\":%" PRIu64 ",\"result\":%d,\"shape\":%s,"
        "\"preserved\":%s}\n",
        arguments->a0,
        (int)result,
        shape ? "true" : "false",
        memcmp(before, &request, sizeof(request)) == 0 ? "true" : "false"
    );
}

int main(void)
{
    const struct micros_syscall_arguments registration = {
        .a0 = 3,
        .a1 = 1,
        .a2 = 4,
        .a3 = UINT32_C(0x1003),
        .a4 = UINT64_C(0x40010000),
        .a5 = 10016,
        .a6 = 0,
        .a7 = 14,
    };
    const struct micros_syscall_arguments readback = {
        .a0 = 4,
        .a1 = 1,
        .a2 = 17,
        .a3 = 0,
        .a4 = 0,
        .a5 = 0,
        .a6 = 0,
        .a7 = 14,
    };

    observe_decode(&registration);
    observe_decode(&readback);
    return 0;
}
