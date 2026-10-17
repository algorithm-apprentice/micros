#include "kernel/syscall_internal.h"

#include "arch/riscv64/trap_context.h"
#include "kernel/bootstrap_syscall.h"
#include "kernel/grant/grant_syscall.h"
#include "kernel/ipc/ipc_syscall.h"
#include "kernel/pm_control_syscall.h"
#include "kernel/tty/tty_control_syscall.h"
#include "kernel/vm_handoff_syscall.h"

static struct micros_syscall_arguments capture_arguments(
    const struct micros_trap_frame *frame
)
{
    return (struct micros_syscall_arguments){
        .a0 = frame->a0,
        .a1 = frame->a1,
        .a2 = frame->a2,
        .a3 = frame->a3,
        .a4 = frame->a4,
        .a5 = frame->a5,
        .a6 = frame->a6,
        .a7 = frame->a7,
    };
}

static enum micros_syscall_return syscall_dispatch_operation(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_syscall_arguments *arguments
)
{
    switch (arguments->a7) {
    case MICROS_SYSCALL_ABI_SEND:
    case MICROS_SYSCALL_ABI_RECEIVE:
    case MICROS_SYSCALL_ABI_CALL:
    case MICROS_SYSCALL_ABI_REPLY:
    case MICROS_SYSCALL_ABI_REPLY_RECEIVE:
    case MICROS_SYSCALL_ABI_NOTIFY:
        return micros_ipc_handle_captured_user_ecall(
            hart,
            frame,
            context,
            arguments
        );
    case MICROS_SYSCALL_ABI_GRANT_CREATE:
    case MICROS_SYSCALL_ABI_GRANT_REVOKE:
    case MICROS_SYSCALL_ABI_GRANT_COPY_FROM:
    case MICROS_SYSCALL_ABI_GRANT_COPY_TO:
    case MICROS_SYSCALL_ABI_GRANT_VALIDATE:
        return micros_grant_handle_captured_user_ecall(
            hart,
            frame,
            context,
            arguments
        );
    case MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL:
        return micros_bootstrap_handle_captured_user_ecall(
            hart,
            frame,
            context,
            arguments
        );
    case MICROS_SYSCALL_ABI_VM_HANDOFF:
        return micros_vm_handoff_handle_captured_user_ecall(
            hart,
            frame,
            context,
            arguments
        );
    case MICROS_SYSCALL_ABI_PM_CONTROL:
        return micros_pm_control_handle_captured_user_ecall(
            hart,
            frame,
            context,
            arguments
        );
    case MICROS_SYSCALL_ABI_TTY_CONTROL:
        return micros_tty_control_handle_captured_user_ecall(
            hart,
            frame,
            context,
            arguments
        );
    default:
        frame->a0 =
            (uint64_t)(int64_t)MICROS_SYSCALL_ABI_ARGUMENT;
        return MICROS_SYSCALL_RETURN_NORMAL;
    }
}

enum micros_syscall_return micros_syscall_handle_user_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct micros_syscall_context context;
    struct micros_syscall_arguments arguments;

    if (hart == NULL || frame == NULL) {
        syscall_panic_syscall(hart, frame, "syscall-argument");
    }
    if (frame->sepc > UINT64_MAX - 4) {
        syscall_panic_syscall(hart, frame, "syscall-sepc-overflow");
    }
    arguments = capture_arguments(frame);
    frame->sepc += 4;
    if (!syscall_decode_controls(frame, &arguments)) {
        return MICROS_SYSCALL_RETURN_NORMAL;
    }
    syscall_resolve_context(hart, frame, &arguments, &context);
    return syscall_dispatch_operation(hart, frame, &context, &arguments);
}
