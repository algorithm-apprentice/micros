#include "kernel/vm/vm_handoff_syscall_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static enum micros_syscall_abi_result decode_request(
    const struct micros_syscall_arguments *arguments,
    uint32_t command,
    union vm_decoded_request *request
)
{
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

struct vm_handoff_syscall_authorities {
    struct micros_vm_handoff_state *state;
    struct micros_bootstrap_control_state *bootstrap;
    struct micros_endpoint_registry *registry;
    const struct micros_frame_ownership *ownership;
    const struct micros_pm_control_state *pm;
};

static void require_current_vm_thread(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context
)
{
    const struct micros_thread *current_thread;

    if (
        micros_thread_resolve(
            context->objects,
            context->current,
            &current_thread
        ) != MICROS_KERNEL_OBJECT_OK
        || current_thread->owner.slot != context->process.slot
        || current_thread->owner.generation
            != context->process.generation
    ) {
        micros_vm_handoff_syscall_panic(
            hart,
            frame,
            "vm-handoff-current"
        );
    }
}

static bool acquire_vm_authorities(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    struct vm_handoff_syscall_authorities *authorities
)
{
    authorities->state =
        micros_vm_handoff_runtime_authoritative_state();
    authorities->bootstrap =
        micros_bootstrap_runtime_authoritative_state();
    authorities->registry =
        micros_ipc_runtime_authoritative_registry();
    if (
        !micros_vm_handoff_syscall_vm_is_authorized(
            authorities->state,
            authorities->registry,
            context->objects,
            context
        )
    ) {
        return false;
    }
    authorities->ownership =
        micros_frame_ownership_runtime_ledger();
    if (
        authorities->ownership == NULL
        || (
            authorities->bootstrap != NULL
            && authorities->bootstrap->transitions.entry_count
                > MICROS_BOOTSTRAP_SERVICE_CAPACITY
        )
    ) {
        micros_vm_handoff_syscall_fail(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            authorities->state
        );
    }
    return true;
}

static enum micros_syscall_return dispatch_boot_command(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    uint32_t command,
    const union vm_decoded_request *request,
    const struct vm_handoff_syscall_authorities *authorities
)
{
    if (command == MICROS_VM_HANDOFF_READY) {
        return micros_vm_handoff_syscall_handle_ready(
            hart,
            frame,
            authorities->state,
            authorities->bootstrap,
            authorities->ownership,
            &request->ready
        );
    }
    return micros_vm_handoff_syscall_handle_tty_mapping(
        hart,
        frame,
        context,
        authorities->state,
        authorities->bootstrap,
        authorities->registry,
        authorities->ownership,
        &request->tty_mapping
    );
}

static void require_post_handoff_runtime(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    struct vm_handoff_syscall_authorities *authorities
)
{
    authorities->pm = micros_pm_control_runtime_state();
    if (
        micros_bootstrap_runtime_validate()
            != MICROS_BOOTSTRAP_OK
        || micros_vm_handoff_runtime_validate(
            authorities->bootstrap,
            authorities->registry,
            context->objects
        ) != MICROS_VM_HANDOFF_OK
        || micros_pm_control_runtime_validate(
            authorities->bootstrap,
            authorities->registry,
            context->objects
        ) != MICROS_PM_CONTROL_OK
        || micros_user_address_space_validate(
            authorities->state->process
        ) != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        micros_vm_handoff_syscall_fail(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            authorities->state
        );
    }
}

static enum micros_syscall_return dispatch_dynamic_command(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    uint32_t command,
    const union vm_decoded_request *request,
    const struct vm_handoff_syscall_authorities *authorities
)
{
    switch (command) {
    case MICROS_VM_LOAD_BEGIN:
        return micros_vm_handoff_syscall_handle_load_begin(
            hart,
            frame,
            context,
            authorities->state,
            authorities->pm,
            &request->load_begin
        );
    case MICROS_VM_SCRATCH_MAP:
        return micros_vm_handoff_syscall_handle_scratch_map(
            hart,
            frame,
            authorities->state,
            &request->scratch_map
        );
    case MICROS_VM_SCRATCH_COMMIT:
        return micros_vm_handoff_syscall_handle_scratch_commit(
            hart,
            frame,
            authorities->state,
            &request->scratch_commit
        );
    case MICROS_VM_LOAD_FREEZE:
        return micros_vm_handoff_syscall_handle_load_freeze(
            hart,
            frame,
            context,
            authorities->state,
            authorities->pm,
            &request->load_freeze
        );
    case MICROS_VM_LOAD_ABORT:
        return micros_vm_handoff_syscall_handle_load_abort(
            hart,
            frame,
            context,
            authorities->state,
            authorities->pm,
            &request->load_abort
        );
    case MICROS_VM_LOAD_COMMIT:
        return micros_vm_handoff_syscall_handle_load_commit(
            hart,
            frame,
            authorities->state,
            &request->load_commit
        );
    case MICROS_VM_EXIT_RELEASE:
        return micros_vm_handoff_syscall_handle_exit_release(
            hart,
            frame,
            authorities->state,
            &request->exit_release
        );
    default:
        return micros_vm_handoff_syscall_return_result(
            frame,
            MICROS_SYSCALL_ABI_ARGUMENT
        );
    }
}

enum micros_syscall_return
micros_vm_handoff_handle_captured_user_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_syscall_arguments *arguments
)
{
    union vm_decoded_request request;
    struct vm_handoff_syscall_authorities authorities = {0};
    uint32_t command;

    if (
        hart == NULL
        || frame == NULL
        || context == NULL
        || arguments == NULL
        || context->objects == NULL
    ) {
        micros_vm_handoff_syscall_panic(hart, frame, "vm-handoff-argument");
    }
    if (
        micros_vm_handoff_decode_command(arguments, &command)
            != MICROS_SYSCALL_ABI_OK
    ) {
        return micros_vm_handoff_syscall_return_result(frame, MICROS_SYSCALL_ABI_ARGUMENT);
    }
    if (
        decode_request(arguments, command, &request)
            != MICROS_SYSCALL_ABI_OK
    ) {
        return micros_vm_handoff_syscall_return_result(frame, MICROS_SYSCALL_ABI_ARGUMENT);
    }
    require_current_vm_thread(hart, frame, context);
    if (!acquire_vm_authorities(
        hart,
        frame,
        context,
        &authorities
    )) {
        return micros_vm_handoff_syscall_return_result(
            frame,
            MICROS_SYSCALL_ABI_UNAUTHORIZED
        );
    }
    if (
        command == MICROS_VM_HANDOFF_READY
        || command == MICROS_VM_HANDOFF_MAP_TTY_UART
    ) {
        return dispatch_boot_command(
            hart,
            frame,
            context,
            command,
            &request,
            &authorities
        );
    }
    if (
        !micros_vm_handoff_syscall_post_handoff_phase_is_ready(
            authorities.bootstrap,
            authorities.state,
            authorities.ownership
        )
    ) {
        return micros_vm_handoff_syscall_return_result(frame, MICROS_SYSCALL_ABI_STATE);
    }
    require_post_handoff_runtime(hart, frame, context, &authorities);
    return dispatch_dynamic_command(
        hart,
        frame,
        context,
        command,
        &request,
        &authorities
    );
}
