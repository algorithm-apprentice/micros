#include "micros/bootstrap_control.h"

#include <stddef.h>
#include <stdint.h>

static bool value_fits_u32(uint64_t value)
{
    return value <= UINT32_MAX;
}

enum micros_syscall_abi_result micros_bootstrap_control_decode(
    const struct micros_syscall_arguments *arguments,
    struct micros_bootstrap_control_request *request
)
{
    struct micros_bootstrap_control_request candidate = {0};

    if (
        arguments == NULL
        || request == NULL
        || arguments->a7 != MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL
        || arguments->a5 != 0
        || arguments->a6 != 0
        || !value_fits_u32(arguments->a0)
        || !value_fits_u32(arguments->a1)
        || !value_fits_u32(arguments->a2)
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    candidate.command = (enum micros_bootstrap_command)(
        uint32_t
    )arguments->a0;
    candidate.service_id = (uint32_t)arguments->a1;
    candidate.endpoint = (micros_endpoint_t)arguments->a2;
    switch (candidate.command) {
    case MICROS_BOOTSTRAP_COMMAND_RELEASE:
        if (
            arguments->a2 != 0
            || arguments->a3 != 0
            || arguments->a4 != 0
        ) {
            return MICROS_SYSCALL_ABI_ARGUMENT;
        }
        break;
    case MICROS_BOOTSTRAP_COMMAND_ACCEPT_READY:
        if (arguments->a4 != 0) {
            return MICROS_SYSCALL_ABI_ARGUMENT;
        }
        candidate.reply_token = arguments->a3;
        break;
    case MICROS_BOOTSTRAP_COMMAND_FAIL:
        if (
            !value_fits_u32(arguments->a3)
            || !value_fits_u32(arguments->a4)
        ) {
            return MICROS_SYSCALL_ABI_ARGUMENT;
        }
        candidate.failure_reason =
            (enum micros_bootstrap_failure_reason)(
                uint32_t
            )arguments->a3;
        candidate.failure_detail = (uint32_t)arguments->a4;
        break;
    case MICROS_BOOTSTRAP_COMMAND_COMPLETE:
        if (
            arguments->a1 != 0
            || arguments->a2 != 0
            || arguments->a3 != 0
            || arguments->a4 != 0
        ) {
            return MICROS_SYSCALL_ABI_ARGUMENT;
        }
        break;
    default:
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    *request = candidate;
    return MICROS_SYSCALL_ABI_OK;
}

enum micros_syscall_abi_result
micros_bootstrap_control_validate_failure_detail(
    const struct micros_bootstrap_control_request *request
)
{
    if (
        request == NULL
        || request->command != MICROS_BOOTSTRAP_COMMAND_FAIL
        || request->failure_reason
            < MICROS_BOOTSTRAP_FAILURE_READY_MALFORMED
        || request->failure_reason
            > MICROS_BOOTSTRAP_FAILURE_READY_ROLE_GATE
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    if (
        request->failure_reason
        == MICROS_BOOTSTRAP_FAILURE_READY_MALFORMED
    ) {
        return request->failure_detail >= 1
                && request->failure_detail <= 9
            ? MICROS_SYSCALL_ABI_OK
            : MICROS_SYSCALL_ABI_ARGUMENT;
    }
    return request->failure_detail == 0
        ? MICROS_SYSCALL_ABI_OK
        : MICROS_SYSCALL_ABI_ARGUMENT;
}
