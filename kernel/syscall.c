#include "kernel/syscall.h"

#include <stdint.h>

#include "arch/riscv64/trap_context.h"
#include "kernel/bootstrap_syscall.h"
#include "kernel/bootstrap_runtime.h"
#include "kernel/grant_syscall.h"
#include "kernel/ipc_syscall.h"
#include "kernel/kernel_object_runtime_internal.h"
#include "kernel/pm_control_core.h"
#include "kernel/pm_control_syscall.h"
#include "kernel/tty_control_core.h"
#include "kernel/tty_control_syscall.h"
#include "kernel/vm_handoff_core.h"
#include "kernel/vm_handoff_syscall.h"
#include "micros/bootstrap_control.h"
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

union micros_vm_handoff_decoded_request {
    struct micros_vm_handoff_request ready;
    struct micros_vm_tty_mapping_request tty_mapping;
    struct micros_vm_load_begin_request load_begin;
    struct micros_vm_scratch_map_request scratch_map;
    struct micros_vm_scratch_commit_request scratch_commit;
    struct micros_vm_load_freeze_request load_freeze;
    struct micros_vm_load_abort_request load_abort;
    struct micros_vm_load_commit_request load_commit;
    struct micros_vm_exit_release_request exit_release;
};

static enum micros_syscall_abi_result decode_vm_handoff_request(
    const struct micros_syscall_arguments *arguments,
    union micros_vm_handoff_decoded_request *request
)
{
    uint32_t command;

    if (
        micros_vm_handoff_decode_command(arguments, &command)
            != MICROS_SYSCALL_ABI_OK
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    switch (command) {
    case MICROS_VM_HANDOFF_READY:
        return micros_vm_handoff_decode(arguments, &request->ready);
    case MICROS_VM_HANDOFF_MAP_TTY_UART:
        return micros_vm_handoff_decode_tty_mapping(
            arguments,
            &request->tty_mapping
        );
    case MICROS_VM_LOAD_BEGIN:
        return micros_vm_load_begin_decode(
            arguments,
            &request->load_begin
        );
    case MICROS_VM_SCRATCH_MAP:
        return micros_vm_scratch_map_decode(
            arguments,
            &request->scratch_map
        );
    case MICROS_VM_SCRATCH_COMMIT:
        return micros_vm_scratch_commit_decode(
            arguments,
            &request->scratch_commit
        );
    case MICROS_VM_LOAD_FREEZE:
        return micros_vm_load_freeze_decode(
            arguments,
            &request->load_freeze
        );
    case MICROS_VM_LOAD_ABORT:
        return micros_vm_load_abort_decode(
            arguments,
            &request->load_abort
        );
    case MICROS_VM_LOAD_COMMIT:
        return micros_vm_load_commit_decode(
            arguments,
            &request->load_commit
        );
    case MICROS_VM_EXIT_RELEASE:
        return micros_vm_exit_release_decode(
            arguments,
            &request->exit_release
        );
    default:
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
}

enum micros_syscall_return micros_syscall_handle_user_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct micros_syscall_context context;
    struct micros_syscall_arguments arguments;
    struct micros_bootstrap_control_request bootstrap_request;
    union micros_vm_handoff_decoded_request vm_handoff_request;
    struct micros_pm_control_request pm_control_request;
    struct micros_tty_control_request tty_control_request;
    const struct micros_thread *thread;
    const struct micros_process *process;

    if (hart == NULL || frame == NULL) {
        panic_syscall(hart, frame, "syscall-argument");
    }
    if (frame->sepc > UINT64_MAX - 4) {
        panic_syscall(hart, frame, "syscall-sepc-overflow");
    }
    arguments = capture_arguments(frame);
    frame->sepc += 4;
    if (
        arguments.a7 == MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL
        && micros_bootstrap_control_decode(
            &arguments,
            &bootstrap_request
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        frame->a0 =
            (uint64_t)(int64_t)MICROS_SYSCALL_ABI_ARGUMENT;
        return MICROS_SYSCALL_RETURN_NORMAL;
    }
    if (arguments.a7 == MICROS_SYSCALL_ABI_VM_HANDOFF) {
        if (
            decode_vm_handoff_request(
                &arguments,
                &vm_handoff_request
            ) != MICROS_SYSCALL_ABI_OK
        ) {
            frame->a0 =
                (uint64_t)(int64_t)MICROS_SYSCALL_ABI_ARGUMENT;
            return MICROS_SYSCALL_RETURN_NORMAL;
        }
    }
    if (
        arguments.a7 == MICROS_SYSCALL_ABI_PM_CONTROL
        && micros_pm_control_decode(
            &arguments,
            &pm_control_request
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        frame->a0 =
            (uint64_t)(int64_t)MICROS_SYSCALL_ABI_ARGUMENT;
        return MICROS_SYSCALL_RETURN_NORMAL;
    }
    if (
        arguments.a7 == MICROS_SYSCALL_ABI_TTY_CONTROL
        && micros_tty_control_decode(
            &arguments,
            &tty_control_request
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        frame->a0 =
            (uint64_t)(int64_t)MICROS_SYSCALL_ABI_ARGUMENT;
        return MICROS_SYSCALL_RETURN_NORMAL;
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
        const struct micros_bootstrap_control_state *state =
            micros_bootstrap_runtime_state();

        if (
            (
                arguments.a7
                    == MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL
                || arguments.a7
                    == MICROS_SYSCALL_ABI_VM_HANDOFF
                || arguments.a7
                    == MICROS_SYSCALL_ABI_PM_CONTROL
                || arguments.a7
                    == MICROS_SYSCALL_ABI_TTY_CONTROL
            )
            && state != NULL
            && state->phase == MICROS_BOOTSTRAP_PHASE_RUNNING
        ) {
            micros_bootstrap_runtime_fail(
                MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
                0,
                MICROS_ENDPOINT_NONE,
                0
            );
        }
        panic_syscall(hart, frame, "syscall-current-invariant");
    }
    context.process = thread->owner;
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
    case MICROS_SYSCALL_ABI_GRANT_VALIDATE:
        return micros_grant_handle_captured_user_ecall(
            hart,
            frame,
            &context,
            &arguments
        );
    case MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL:
        return micros_bootstrap_handle_captured_user_ecall(
            hart,
            frame,
            &context,
            &arguments
        );
    case MICROS_SYSCALL_ABI_VM_HANDOFF:
        return micros_vm_handoff_handle_captured_user_ecall(
            hart,
            frame,
            &context,
            &arguments
        );
    case MICROS_SYSCALL_ABI_PM_CONTROL:
        return micros_pm_control_handle_captured_user_ecall(
            hart,
            frame,
            &context,
            &arguments
        );
    case MICROS_SYSCALL_ABI_TTY_CONTROL:
        return micros_tty_control_handle_captured_user_ecall(
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
    (void)bootstrap_request;
    (void)vm_handoff_request;
    (void)pm_control_request;
    (void)tty_control_request;
    (void)process;
}
