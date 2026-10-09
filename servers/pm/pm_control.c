#include "servers/pm/pm_control.h"

#include <stddef.h>
#include <stdint.h>

#include "lib/runtime/raw_syscall.h"
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
