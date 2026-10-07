#ifndef MICROS_KERNEL_IPC_SYSCALL_H
#define MICROS_KERNEL_IPC_SYSCALL_H

struct micros_hart;
struct micros_trap_frame;

enum micros_ipc_syscall_return {
    MICROS_IPC_SYSCALL_RETURN_NORMAL = 0,
    MICROS_IPC_SYSCALL_RETURN_CAPTURED,
};

enum micros_ipc_syscall_return micros_ipc_handle_user_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

#endif
