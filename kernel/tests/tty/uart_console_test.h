#ifndef MICROS_KERNEL_UART_CONSOLE_TEST_H
#define MICROS_KERNEL_UART_CONSOLE_TEST_H

#include <stdint.h>

_Noreturn void micros_uart_console_runtime_run_self_test(
    uintptr_t hart_id
);

#endif
