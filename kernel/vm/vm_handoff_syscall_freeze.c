#include "kernel/vm/vm_handoff_syscall_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/sv39.h"

struct freeze_command_plans {
    struct micros_vm_load_freeze_plan request;
    struct micros_vm_load_freeze_state_plan state;
    struct micros_frame_vm_freeze_plan ownership;
    struct micros_user_address_space_vm_freeze_plan address;
    struct vm_fixed_object_plan input;
    struct vm_fixed_object_plan output;
};

static enum vm_fixed_object_error prepare_freeze_fixed_objects(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_vm_handoff_state *state,
    const struct micros_vm_load_freeze_request *request,
    struct freeze_command_plans *plans
)
{
    enum vm_fixed_object_error error;

    error = micros_vm_handoff_syscall_prepare_fixed_object(
        context->process,
        request->plan_address,
        sizeof(plans->request),
        MICROS_SV39_PERMISSION_READ,
        &plans->input
    );
    if (error != VM_FIXED_OBJECT_OK) {
        if (error == VM_FIXED_OBJECT_ERROR_MEMORY_FAULT) {
            return error;
        }
        micros_vm_handoff_syscall_fail(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    micros_vm_handoff_syscall_read_fixed_object_prevalidated(
        &plans->input,
        &plans->request
    );
    error = micros_vm_handoff_syscall_prepare_fixed_object(
        context->process,
        request->output_address,
        sizeof(struct micros_vm_load_freeze_result),
        MICROS_SV39_PERMISSION_WRITE,
        &plans->output
    );
    if (
        error != VM_FIXED_OBJECT_OK
        && error != VM_FIXED_OBJECT_ERROR_MEMORY_FAULT
    ) {
        micros_vm_handoff_syscall_fail(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    return error;
}

static enum reservation_check_result prepare_freeze_resources(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_vm_handoff_state *state,
    const struct micros_pm_control_state *pm,
    struct freeze_command_plans *plans
)
{
    struct micros_process_handle reserved_process;
    uint64_t data_frames[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
    enum reservation_check_result reservation;
    size_t index;

    micros_vm_handoff_syscall_clear_bytes(
        data_frames,
        sizeof(data_frames)
    );
    reservation = micros_vm_handoff_syscall_check_reservation(
        pm,
        context->objects,
        state->load.process_reservation_token,
        &plans->state.action.process,
        true,
        &reserved_process
    );
    if (reservation != RESERVATION_CHECK_OK) {
        return reservation;
    }
    for (index = 0; index < plans->state.action.entry_count; ++index) {
        data_frames[index] =
            plans->state.action.entries[index].physical_address;
    }
    if (micros_frame_ownership_runtime_vm_freeze_preflight(
        reserved_process,
        data_frames,
        plans->state.action.entry_count,
        plans->state.action.table_frames,
        &plans->ownership
    ) != MICROS_FRAME_OWNERSHIP_OK) {
        micros_vm_handoff_syscall_fail(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    if (micros_user_address_space_prepare_vm_freeze(
        &plans->state.action,
        &plans->address
    ) != MICROS_USER_ADDRESS_SPACE_OK) {
        micros_vm_handoff_syscall_fail(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    return RESERVATION_CHECK_OK;
}

enum micros_syscall_return micros_vm_handoff_syscall_handle_load_freeze(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    struct micros_vm_handoff_state *state,
    const struct micros_pm_control_state *pm,
    const struct micros_vm_load_freeze_request *request
)
{
    struct freeze_command_plans plans;
    enum vm_fixed_object_error object_error;
    enum micros_vm_handoff_error vm_error;
    enum reservation_check_result reservation;
    enum micros_syscall_abi_result result;

    micros_vm_handoff_syscall_clear_bytes(&plans, sizeof(plans));
    object_error = prepare_freeze_fixed_objects(
        hart,
        frame,
        context,
        state,
        request,
        &plans
    );
    if (object_error == VM_FIXED_OBJECT_ERROR_MEMORY_FAULT) {
        return micros_vm_handoff_syscall_return_result(
            frame,
            MICROS_SYSCALL_ABI_MEMORY_FAULT
        );
    }
    vm_error = micros_vm_load_freeze_preflight(
        state,
        &plans.request,
        &plans.state
    );
    if (vm_error != MICROS_VM_HANDOFF_OK) {
        if (micros_vm_handoff_syscall_map_error(vm_error, &result)) {
            return micros_vm_handoff_syscall_return_result(frame, result);
        }
        micros_vm_handoff_syscall_fail(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    reservation = prepare_freeze_resources(
        hart,
        frame,
        context,
        state,
        pm,
        &plans
    );
    if (reservation == RESERVATION_CHECK_STATE) {
        return micros_vm_handoff_syscall_return_result(
            frame,
            MICROS_SYSCALL_ABI_STATE
        );
    }
    if (reservation != RESERVATION_CHECK_OK) {
        micros_vm_handoff_syscall_fail(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    micros_user_address_space_construct_vm_freeze_prevalidated(
        &plans.address
    );
    micros_frame_ownership_runtime_vm_freeze_commit_prevalidated(
        &plans.ownership
    );
    micros_user_address_space_install_vm_freeze_prevalidated(
        &plans.address
    );
    micros_vm_load_freeze_commit_prevalidated(state, &plans.state);
    micros_vm_handoff_syscall_write_fixed_object_prevalidated(
        &plans.output,
        &plans.state.result
    );
    return micros_vm_handoff_syscall_return_result(
        frame,
        MICROS_SYSCALL_ABI_OK
    );
}
