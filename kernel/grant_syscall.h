#ifndef MICROS_KERNEL_GRANT_SYSCALL_H
#define MICROS_KERNEL_GRANT_SYSCALL_H

#include "kernel/syscall.h"

enum micros_syscall_return
micros_grant_handle_captured_user_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_syscall_arguments *arguments
);

#endif
