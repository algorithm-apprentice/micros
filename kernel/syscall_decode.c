#include "kernel/syscall_internal.h"

#include "arch/riscv64/trap_context.h"
#include "kernel/pm_control_core.h"
#include "kernel/tty/tty_control_core.h"
#include "kernel/vm/vm_handoff_core.h"
#include "micros/bootstrap_control.h"

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

bool syscall_decode_controls(
    struct micros_trap_frame *frame,
    const struct micros_syscall_arguments *arguments
)
{
    struct micros_bootstrap_control_request bootstrap_request;
    union micros_vm_handoff_decoded_request vm_handoff_request;
    struct micros_pm_control_request pm_control_request;
    struct micros_tty_control_request tty_control_request;

    if (
        arguments->a7 == MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL
        && micros_bootstrap_control_decode(
            arguments,
            &bootstrap_request
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        frame->a0 =
            (uint64_t)(int64_t)MICROS_SYSCALL_ABI_ARGUMENT;
        return false;
    }
    if (arguments->a7 == MICROS_SYSCALL_ABI_VM_HANDOFF) {
        if (
            decode_vm_handoff_request(
                arguments,
                &vm_handoff_request
            ) != MICROS_SYSCALL_ABI_OK
        ) {
            frame->a0 =
                (uint64_t)(int64_t)MICROS_SYSCALL_ABI_ARGUMENT;
            return false;
        }
    }
    if (
        arguments->a7 == MICROS_SYSCALL_ABI_PM_CONTROL
        && micros_pm_control_decode(
            arguments,
            &pm_control_request
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        frame->a0 =
            (uint64_t)(int64_t)MICROS_SYSCALL_ABI_ARGUMENT;
        return false;
    }
    if (
        arguments->a7 == MICROS_SYSCALL_ABI_TTY_CONTROL
        && micros_tty_control_decode(
            arguments,
            &tty_control_request
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        frame->a0 =
            (uint64_t)(int64_t)MICROS_SYSCALL_ABI_ARGUMENT;
        return false;
    }
    return true;
}
