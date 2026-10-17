#ifndef MICROS_KERNEL_SYSCALL_INTERNAL_H
#define MICROS_KERNEL_SYSCALL_INTERNAL_H

#include "kernel/trap/syscall.h"

_Noreturn void syscall_panic_syscall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const char *reason
);
bool syscall_decode_controls(
    struct micros_trap_frame *frame,
    const struct micros_syscall_arguments *arguments
);
void syscall_resolve_context(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_arguments *arguments,
    struct micros_syscall_context *context
);

#endif
