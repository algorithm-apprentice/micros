#include "kernel/syscall.h"

#include <stdint.h>

#include "arch/riscv64/trap_context.h"
#include "kernel/grant_syscall.h"
#include "kernel/ipc_syscall.h"
#include "kernel/kernel_object_runtime_internal.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"

static _Noreturn void panic_syscall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const char *reason
)
{
    MICROS_TRAP_PANIC(
        hart == NULL ? 0 : hart->hardware_id,
        reason,
        frame
    );
}

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

enum micros_syscall_return micros_syscall_handle_user_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct micros_syscall_context context;
    struct micros_syscall_arguments arguments;
    const struct micros_thread *thread;
    const struct micros_process *process;

    if (hart == NULL || frame == NULL) {
        panic_syscall(hart, frame, "syscall-argument");
    }
    context.objects =
        micros_kernel_object_runtime_authoritative_registry();
    context.hart = micros_kernel_object_runtime_boot_hart_handle();
    if (
        context.objects == NULL
        || context.hart.slot >= MICROS_HART_CAPACITY
        || hart != &context.objects->harts[context.hart.slot]
        || micros_hart_current_thread(
            context.objects,
            context.hart,
            &context.current
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_thread_resolve(
            context.objects,
            context.current,
            &thread
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_process_resolve(
            context.objects,
            thread->owner,
            &process
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        panic_syscall(hart, frame, "syscall-current-invariant");
    }
    context.process = thread->owner;
    if (frame->sepc > UINT64_MAX - 4) {
        panic_syscall(hart, frame, "syscall-sepc-overflow");
    }
    arguments = capture_arguments(frame);
    frame->sepc += 4;
    switch (arguments.a7) {
    case MICROS_SYSCALL_ABI_SEND:
    case MICROS_SYSCALL_ABI_RECEIVE:
    case MICROS_SYSCALL_ABI_CALL:
    case MICROS_SYSCALL_ABI_REPLY:
    case MICROS_SYSCALL_ABI_REPLY_RECEIVE:
    case MICROS_SYSCALL_ABI_NOTIFY:
        return micros_ipc_handle_captured_user_ecall(
            hart,
            frame,
            &context,
            &arguments
        );
    case MICROS_SYSCALL_ABI_GRANT_CREATE:
    case MICROS_SYSCALL_ABI_GRANT_REVOKE:
    case MICROS_SYSCALL_ABI_GRANT_COPY_FROM:
    case MICROS_SYSCALL_ABI_GRANT_COPY_TO:
        return micros_grant_handle_captured_user_ecall(
            hart,
            frame,
            &context,
            &arguments
        );
    default:
        frame->a0 =
            (uint64_t)(int64_t)MICROS_SYSCALL_ABI_ARGUMENT;
        return MICROS_SYSCALL_RETURN_NORMAL;
    }
    (void)process;
}
