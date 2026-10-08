#ifndef MICROS_KERNEL_IPC_SYSCALL_PANIC_TEST_H
#define MICROS_KERNEL_IPC_SYSCALL_PANIC_TEST_H

#include <stdbool.h>

#include "kernel/syscall.h"

struct micros_hart;
struct micros_trap_frame;

bool micros_ipc_syscall_panic_test_before_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_syscall_return syscall_return
);

_Noreturn void micros_ipc_syscall_panic_runtime_run_self_test(void);

#endif
