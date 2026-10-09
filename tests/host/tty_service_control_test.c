#include "servers/tty/tty_control.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "lib/runtime/raw_syscall.h"
#include "micros/syscall_abi.h"
#include "micros/tty.h"

static struct micros_syscall_arguments captured;
static size_t syscall_count;
static int64_t syscall_result;

#define EXPECT_TRUE(expression) \
    do { \
        if (!(expression)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %s\n", \
                __FILE__, \
                __LINE__, \
                #expression \
            ); \
            return false; \
        } \
    } while (false)

int64_t micros_runtime_raw_syscall(
    uint64_t a0,
    uint64_t a1,
    uint64_t a2,
    uint64_t a3,
    uint64_t a4,
    uint64_t a5,
    uint64_t a6,
    uint64_t a7
)
{
    captured = (struct micros_syscall_arguments){
        .a0 = a0,
        .a1 = a1,
        .a2 = a2,
        .a3 = a3,
        .a4 = a4,
        .a5 = a5,
        .a6 = a6,
        .a7 = a7,
    };
    ++syscall_count;
    return syscall_result;
}

static bool test_commit_tuple(void)
{
    const micros_endpoint_t endpoint = UINT32_C(0x00007003);

    syscall_count = 0;
    syscall_result = MICROS_SYSCALL_ABI_STATE;
    EXPECT_TRUE(
        micros_tty_control_commit(MICROS_ENDPOINT_NONE)
            == MICROS_SYSCALL_ABI_ARGUMENT
        && micros_tty_control_commit(MICROS_ENDPOINT_ANY)
            == MICROS_SYSCALL_ABI_ARGUMENT
        && syscall_count == 0
        && micros_tty_control_commit(endpoint)
            == MICROS_SYSCALL_ABI_STATE
        && syscall_count == 1
        && captured.a0 == MICROS_TTY_CONTROL_COMMIT
        && captured.a1 == MICROS_TTY_CONTROL_VERSION
        && captured.a2 == MICROS_TTY_SERVICE_ID
        && captured.a3 == endpoint
        && captured.a4 == MICROS_TTY_UART_VIRTUAL_BASE
        && captured.a5 == MICROS_TTY_UART_PHYSICAL_BASE
        && captured.a6
            == (
                (uint64_t)MICROS_TTY_UART_IRQ_SOURCE << 32
                | MICROS_TTY_UART_MAPPED_LENGTH
            )
        && captured.a7 == MICROS_SYSCALL_ABI_TTY_CONTROL
    );
    return true;
}

static bool test_irq_complete_tuple(void)
{
    syscall_count = 0;
    syscall_result = MICROS_SYSCALL_ABI_OK;
    EXPECT_TRUE(
        micros_tty_control_irq_complete() == MICROS_SYSCALL_ABI_OK
        && syscall_count == 1
        && captured.a0 == MICROS_TTY_CONTROL_IRQ_COMPLETE
        && captured.a1 == MICROS_TTY_CONTROL_VERSION
        && captured.a2 == MICROS_TTY_UART_IRQ_SOURCE
        && captured.a3 == 0
        && captured.a4 == 0
        && captured.a5 == 0
        && captured.a6 == 0
        && captured.a7 == MICROS_SYSCALL_ABI_TTY_CONTROL
    );
    return true;
}

int main(void)
{
    return (
        test_commit_tuple()
        && test_irq_complete_tuple()
    ) ? 0 : 1;
}
