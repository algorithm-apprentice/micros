#ifndef MICROS_KERNEL_IPC_SYSCALL_TEST_H
#define MICROS_KERNEL_IPC_SYSCALL_TEST_H

#include <stdbool.h>

struct micros_hart;
struct micros_trap_frame;

bool micros_ipc_syscall_test_before_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

bool micros_ipc_syscall_test_after_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

bool micros_ipc_syscall_test_before_timer_return(
    struct micros_hart *hart
);

bool micros_ipc_syscall_test_after_timer_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

_Noreturn void micros_ipc_syscall_runtime_run_self_test(void);
_Noreturn void micros_ipc_syscall_test_finish(void);

#endif
