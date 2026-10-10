#include "servers/tty/tty_control.h"

#include <stdint.h>

#include "lib/runtime/raw_syscall.h"
#include "micros/syscall_abi.h"
#include "micros/tty.h"

micros_runtime_result_t micros_tty_control_commit(
    micros_endpoint_t self_endpoint
)
{
    uint64_t irq_and_length;

    if (
        self_endpoint == MICROS_ENDPOINT_NONE
        || self_endpoint == MICROS_ENDPOINT_ANY
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    irq_and_length =
        (uint64_t)MICROS_TTY_UART_IRQ_SOURCE << 32
        | MICROS_TTY_UART_MAPPED_LENGTH;
    return micros_runtime_raw_syscall(
        MICROS_TTY_CONTROL_COMMIT,
        MICROS_TTY_CONTROL_VERSION,
        MICROS_TTY_SERVICE_ID,
        self_endpoint,
        MICROS_TTY_UART_VIRTUAL_BASE,
        MICROS_TTY_UART_PHYSICAL_BASE,
        irq_and_length,
        MICROS_SYSCALL_ABI_TTY_CONTROL
    );
}

micros_runtime_result_t micros_tty_control_irq_complete(void)
{
    return micros_runtime_raw_syscall(
        MICROS_TTY_CONTROL_IRQ_COMPLETE,
        MICROS_TTY_CONTROL_VERSION,
        MICROS_TTY_UART_IRQ_SOURCE,
        0,
        0,
        0,
        0,
        MICROS_SYSCALL_ABI_TTY_CONTROL
    );
}
