#include "kernel/pm/pm_control_internal.h"

#include <stddef.h>
#include <stdint.h>

static enum micros_syscall_abi_result decode_reservation_command(
    const struct micros_syscall_arguments *arguments,
    struct micros_pm_control_request *candidate
)
{
    if (arguments->a0 == MICROS_PM_CONTROL_RESERVE) {
        if (
            arguments->a1 % 8 != 0
            || arguments->a2 != MICROS_PM_RESERVATION_SIZE
            || arguments->a3 != 0
            || arguments->a4 != 0
            || arguments->a5 != 0
            || arguments->a6 != 0
        ) {
            return MICROS_SYSCALL_ABI_ARGUMENT;
        }
        candidate->command = MICROS_PM_CONTROL_RESERVE;
        candidate->output_address = arguments->a1;
    } else {
        if (
            arguments->a1 == 0
            || arguments->a2 != 0
            || arguments->a3 != 0
            || arguments->a4 != 0
            || arguments->a5 != 0
            || arguments->a6 != 0
        ) {
            return MICROS_SYSCALL_ABI_ARGUMENT;
        }
        candidate->command = MICROS_PM_CONTROL_ABORT_RESERVED;
        candidate->transaction = arguments->a1;
    }
    return MICROS_SYSCALL_ABI_OK;
}

static enum micros_syscall_abi_result decode_execution_command(
    const struct micros_syscall_arguments *arguments,
    struct micros_pm_control_request *candidate
)
{
    if (arguments->a0 == MICROS_PM_CONTROL_PREPARE_EXECUTION) {
        if (
            arguments->a4 % 8 != 0
            || arguments->a5 != MICROS_PM_PREPARED_SIZE
            || arguments->a6 != 0
        ) {
            return MICROS_SYSCALL_ABI_ARGUMENT;
        }
        candidate->command = MICROS_PM_CONTROL_PREPARE_EXECUTION;
        candidate->transaction = arguments->a1;
        candidate->load_token = arguments->a2;
        candidate->mapping_generation = arguments->a3;
        candidate->output_address = arguments->a4;
    } else {
        if (arguments->a5 != 0 || arguments->a6 != 0) {
            return MICROS_SYSCALL_ABI_ARGUMENT;
        }
        candidate->command = (uint32_t)arguments->a0;
        candidate->transaction = arguments->a1;
        candidate->load_token = arguments->a2;
        candidate->mapping_generation = arguments->a3;
        candidate->prepared_token = arguments->a4;
    }
    return MICROS_SYSCALL_ABI_OK;
}

static enum micros_syscall_abi_result decode_exit_command(
    const struct micros_syscall_arguments *arguments,
    struct micros_pm_control_request *candidate
)
{
    if (arguments->a0 == MICROS_PM_CONTROL_EXIT_BEGIN) {
        if (
            arguments->a1 > UINT32_MAX
            || arguments->a2 % 8 != 0
            || arguments->a3 != MICROS_PM_EXIT_BEGIN_SIZE
            || arguments->a4 != 0
            || arguments->a5 != 0
            || arguments->a6 != 0
        ) {
            return MICROS_SYSCALL_ABI_ARGUMENT;
        }
        candidate->command = MICROS_PM_CONTROL_EXIT_BEGIN;
        candidate->endpoint = (uint32_t)arguments->a1;
        candidate->output_address = arguments->a2;
    } else if (
        arguments->a0 == MICROS_PM_CONTROL_EXIT_DETACH
    ) {
        if (
            arguments->a2 > UINT32_MAX
            || arguments->a3 != 0
            || arguments->a4 != 0
            || arguments->a5 != 0
            || arguments->a6 != 0
        ) {
            return MICROS_SYSCALL_ABI_ARGUMENT;
        }
        candidate->command = MICROS_PM_CONTROL_EXIT_DETACH;
        candidate->transaction = arguments->a1;
        candidate->endpoint = (uint32_t)arguments->a2;
    } else {
        if (
            arguments->a2 != 0
            || arguments->a3 != 0
            || arguments->a4 != 0
            || arguments->a5 != 0
            || arguments->a6 != 0
        ) {
            return MICROS_SYSCALL_ABI_ARGUMENT;
        }
        candidate->command =
            MICROS_PM_CONTROL_RELEASE_EXITED_PROCESS;
        candidate->transaction = arguments->a1;
    }
    return MICROS_SYSCALL_ABI_OK;
}

enum micros_syscall_abi_result micros_pm_control_decode(
    const struct micros_syscall_arguments *arguments,
    struct micros_pm_control_request *request
)
{
    struct micros_pm_control_request candidate;
    enum micros_syscall_abi_result result;

    if (
        arguments == NULL
        || request == NULL
        || !micros_pm_control_pointer_is_aligned(
            arguments,
            _Alignof(struct micros_syscall_arguments)
        )
        || !micros_pm_control_pointer_is_aligned(
            request,
            _Alignof(struct micros_pm_control_request)
        )
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    micros_pm_control_clear_bytes(&candidate, sizeof(candidate));
    candidate.endpoint = MICROS_ENDPOINT_NONE;
    if (arguments->a7 != MICROS_SYSCALL_ABI_PM_CONTROL) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    if (
        arguments->a0 == MICROS_PM_CONTROL_RESERVE
        || arguments->a0 == MICROS_PM_CONTROL_ABORT_RESERVED
    ) {
        result = decode_reservation_command(
            arguments,
            &candidate
        );
    } else if (
        arguments->a0 == MICROS_PM_CONTROL_PREPARE_EXECUTION
        || arguments->a0 == MICROS_PM_CONTROL_ACTIVATE
        || arguments->a0 == MICROS_PM_CONTROL_ABORT_PREPARED
    ) {
        result = decode_execution_command(
            arguments,
            &candidate
        );
    } else if (
        arguments->a0 == MICROS_PM_CONTROL_EXIT_BEGIN
        || arguments->a0 == MICROS_PM_CONTROL_EXIT_DETACH
        || arguments->a0
            == MICROS_PM_CONTROL_RELEASE_EXITED_PROCESS
    ) {
        result = decode_exit_command(arguments, &candidate);
    } else {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    if (result != MICROS_SYSCALL_ABI_OK) {
        return result;
    }
    micros_pm_control_copy_bytes(
        request,
        &candidate,
        sizeof(candidate)
    );
    return MICROS_SYSCALL_ABI_OK;
}
