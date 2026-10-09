#include "tests/qemu/bootstrap_launcher_control.h"

#include "lib/runtime/raw_syscall.h"
#include "micros/bootstrap_control.h"

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
