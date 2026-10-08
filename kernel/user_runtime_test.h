#ifndef MICROS_KERNEL_USER_RUNTIME_TEST_H
#define MICROS_KERNEL_USER_RUNTIME_TEST_H

#include <stdbool.h>

#include "kernel/syscall.h"

struct micros_hart;
struct micros_trap_frame;

enum micros_user_runtime_test_trap_result {
    MICROS_USER_RUNTIME_TEST_TRAP_MISMATCH = 0,
    MICROS_USER_RUNTIME_TEST_TRAP_USER_RETURN,
    MICROS_USER_RUNTIME_TEST_TRAP_SUPERVISOR_RETURN,
};

enum micros_user_runtime_test_trap_result
micros_user_runtime_test_handle_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

bool micros_user_runtime_test_before_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

bool micros_user_runtime_test_after_dispatch(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_syscall_return syscall_return
);

_Noreturn void micros_user_runtime_test_finish(void);
_Noreturn void micros_user_runtime_runtime_run_self_test(void);

#endif
