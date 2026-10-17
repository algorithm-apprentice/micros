#ifndef MICROS_KERNEL_SYSCALL_H
#define MICROS_KERNEL_SYSCALL_H

#include "micros/kernel_objects.h"
#include "micros/syscall_abi.h"

struct micros_hart;
struct micros_trap_frame;

enum micros_syscall_return {
    MICROS_SYSCALL_RETURN_NORMAL = 0,
    MICROS_SYSCALL_RETURN_CAPTURED,
};

struct micros_syscall_context {
    struct micros_kernel_objects *objects;
    struct micros_hart_handle hart;
    struct micros_thread_handle current;
    struct micros_process_handle process;
};

enum micros_syscall_return micros_syscall_handle_user_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

#endif
