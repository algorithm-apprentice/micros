#ifndef MICROS_KERNEL_GRANT_SYSCALL_TEST_H
#define MICROS_KERNEL_GRANT_SYSCALL_TEST_H

#include <stdbool.h>

#include "kernel/syscall.h"

struct micros_hart;
struct micros_trap_frame;

enum micros_grant_syscall_test_control_result {
    MICROS_GRANT_SYSCALL_TEST_CONTROL_INACTIVE = 0,
    MICROS_GRANT_SYSCALL_TEST_CONTROL_USER_RETURN,
    MICROS_GRANT_SYSCALL_TEST_CONTROL_SUPERVISOR_RETURN,
    MICROS_GRANT_SYSCALL_TEST_CONTROL_MISMATCH,
};

bool micros_grant_syscall_test_before_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

bool micros_grant_syscall_test_after_dispatch(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_syscall_return syscall_return
);

bool micros_grant_syscall_test_after_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

enum micros_grant_syscall_test_control_result
micros_grant_syscall_test_handle_control_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

_Noreturn void micros_grant_syscall_test_finish(void);
_Noreturn void micros_grant_syscall_runtime_run_self_test(void);

#endif
