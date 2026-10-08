#include "micros/grant_syscall_core.h"

#include <stdint.h>

static bool capture_u32(uint64_t value, uint32_t *captured)
{
    if (captured == NULL || value > UINT32_MAX) {
        return false;
    }
    *captured = (uint32_t)value;
    return true;
}

enum micros_syscall_abi_result micros_grant_syscall_decode(
    const struct micros_syscall_arguments *arguments,
    struct micros_grant_syscall_request *request
)
{
    struct micros_grant_syscall_request candidate = {0};
    uint32_t captured;

    if (arguments == NULL || request == NULL) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    candidate.operation = arguments->a7;
    switch (candidate.operation) {
    case MICROS_SYSCALL_ABI_GRANT_CREATE:
        if (
            arguments->a4 != 0
            || arguments->a5 != 0
            || arguments->a6 != 0
            || !capture_u32(arguments->a0, &captured)
        ) {
            return MICROS_SYSCALL_ABI_ARGUMENT;
        }
        candidate.endpoint = captured;
        candidate.local_address = (uintptr_t)arguments->a1;
        candidate.length = (size_t)arguments->a2;
        if (!capture_u32(arguments->a3, &candidate.permissions)) {
            return MICROS_SYSCALL_ABI_ARGUMENT;
        }
        break;
    case MICROS_SYSCALL_ABI_GRANT_REVOKE:
        if (
            arguments->a1 != 0
            || arguments->a2 != 0
            || arguments->a3 != 0
            || arguments->a4 != 0
            || arguments->a5 != 0
            || arguments->a6 != 0
            || !capture_u32(arguments->a0, &captured)
        ) {
            return MICROS_SYSCALL_ABI_ARGUMENT;
        }
        candidate.grant = captured;
        break;
    case MICROS_SYSCALL_ABI_GRANT_COPY_FROM:
    case MICROS_SYSCALL_ABI_GRANT_COPY_TO:
        if (
            arguments->a5 != 0
            || arguments->a6 != 0
            || !capture_u32(arguments->a0, &captured)
        ) {
            return MICROS_SYSCALL_ABI_ARGUMENT;
        }
        candidate.endpoint = captured;
        if (!capture_u32(arguments->a1, &captured)) {
            return MICROS_SYSCALL_ABI_ARGUMENT;
        }
        candidate.grant = captured;
        candidate.offset = arguments->a2;
        candidate.local_address = (uintptr_t)arguments->a3;
        candidate.length = (size_t)arguments->a4;
        break;
    default:
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    *request = candidate;
    return MICROS_SYSCALL_ABI_OK;
}
