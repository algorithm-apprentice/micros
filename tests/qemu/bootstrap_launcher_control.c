#include "tests/qemu/bootstrap_launcher_control.h"

#include "lib/runtime/raw_syscall.h"
#include "tests/qemu/bootstrap_launcher_internal.h"

int64_t bootstrap_launcher_bootstrap_control(
    uint32_t command,
    uint32_t service_id,
    micros_endpoint_t endpoint,
    uint64_t value,
    uint32_t detail
)
{
    return micros_runtime_raw_syscall(
        command,
        service_id,
        endpoint,
        value,
        detail,
        0,
        0,
        MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL
    );
}

_Noreturn void bootstrap_launcher_fail(
    uint32_t service_id,
    micros_endpoint_t endpoint,
    enum micros_bootstrap_failure_reason reason,
    uint32_t detail
)
{
    (void)bootstrap_launcher_bootstrap_control(
        MICROS_BOOTSTRAP_COMMAND_FAIL,
        service_id,
        endpoint,
        reason,
        detail
    );
    __builtin_trap();
}

int64_t micros_bootstrap_launcher_console_begin(uint32_t service_id)
{
    return micros_runtime_raw_syscall(
        MICROS_BOOTSTRAP_COMMAND_CONSOLE_BEGIN,
        service_id,
        0,
        0,
        0,
        0,
        0,
        MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL
    );
}
