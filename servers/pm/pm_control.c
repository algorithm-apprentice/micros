#include "servers/pm/pm_control.h"

#include <stddef.h>
#include <stdint.h>

#include "lib/runtime/raw_syscall.h"
#include "micros/endpoint.h"
#include "micros/syscall_abi.h"

micros_runtime_result_t micros_pm_control_reserve(
    struct micros_pm_reservation_result *result
)
{
    if (result == NULL) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    return micros_runtime_raw_syscall(
        MICROS_PM_CONTROL_RESERVE,
        (uint64_t)(uintptr_t)result,
        MICROS_PM_RESERVATION_SIZE,
        0,
        0,
        0,
        0,
        MICROS_SYSCALL_ABI_PM_CONTROL
    );
}

micros_runtime_result_t micros_pm_control_abort(uint64_t transaction)
{
    if (transaction == 0) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    return micros_runtime_raw_syscall(
        MICROS_PM_CONTROL_ABORT_RESERVED,
        transaction,
        0,
        0,
        0,
        0,
        0,
        MICROS_SYSCALL_ABI_PM_CONTROL
    );
}

micros_runtime_result_t micros_pm_control_prepare_execution(
    uint64_t transaction,
    uint64_t load_token,
    uint64_t mapping_generation,
    struct micros_pm_prepared_result *result
)
{
    if (
        transaction == 0
        || load_token == 0
        || mapping_generation == 0
        || result == NULL
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    return micros_runtime_raw_syscall(
        MICROS_PM_CONTROL_PREPARE_EXECUTION,
        transaction,
        load_token,
        mapping_generation,
        (uint64_t)(uintptr_t)result,
        MICROS_PM_PREPARED_SIZE,
        0,
        MICROS_SYSCALL_ABI_PM_CONTROL
    );
}

micros_runtime_result_t micros_pm_control_abort_prepared(
    uint64_t transaction,
    uint64_t load_token,
    uint64_t mapping_generation,
    uint64_t prepared_token
)
{
    if (
        transaction == 0
        || load_token == 0
        || mapping_generation == 0
        || prepared_token == 0
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    return micros_runtime_raw_syscall(
        MICROS_PM_CONTROL_ABORT_PREPARED,
        transaction,
        load_token,
        mapping_generation,
        prepared_token,
        0,
        0,
        MICROS_SYSCALL_ABI_PM_CONTROL
    );
}

micros_runtime_result_t micros_pm_control_activate(
    uint64_t transaction,
    uint64_t load_token,
    uint64_t mapping_generation,
    uint64_t prepared_token
)
{
    if (
        transaction == 0
        || load_token == 0
        || mapping_generation == 0
        || prepared_token == 0
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    return micros_runtime_raw_syscall(
        MICROS_PM_CONTROL_ACTIVATE,
        transaction,
        load_token,
        mapping_generation,
        prepared_token,
        0,
        0,
        MICROS_SYSCALL_ABI_PM_CONTROL
    );
}

micros_runtime_result_t micros_pm_control_exit_begin(
    uint32_t endpoint,
    struct micros_pm_exit_begin_result *result
)
{
    if (
        endpoint == MICROS_ENDPOINT_NONE
        || endpoint == MICROS_ENDPOINT_ANY
        || result == NULL
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    return micros_runtime_raw_syscall(
        MICROS_PM_CONTROL_EXIT_BEGIN,
        endpoint,
        (uint64_t)(uintptr_t)result,
        MICROS_PM_EXIT_BEGIN_SIZE,
        0,
        0,
        0,
        MICROS_SYSCALL_ABI_PM_CONTROL
    );
}

micros_runtime_result_t micros_pm_control_exit_detach(
    uint64_t exit_token,
    uint32_t endpoint
)
{
    if (
        exit_token == 0
        || endpoint == MICROS_ENDPOINT_NONE
        || endpoint == MICROS_ENDPOINT_ANY
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    return micros_runtime_raw_syscall(
        MICROS_PM_CONTROL_EXIT_DETACH,
        exit_token,
        endpoint,
        0,
        0,
        0,
        0,
        MICROS_SYSCALL_ABI_PM_CONTROL
    );
}

micros_runtime_result_t micros_pm_control_release_exited_process(
    uint64_t exit_token
)
{
    if (exit_token == 0) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    return micros_runtime_raw_syscall(
        MICROS_PM_CONTROL_RELEASE_EXITED_PROCESS,
        exit_token,
        0,
        0,
        0,
        0,
        0,
        MICROS_SYSCALL_ABI_PM_CONTROL
    );
}
