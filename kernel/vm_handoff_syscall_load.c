#include "kernel/vm_handoff_syscall_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/sv39.h"

enum micros_syscall_return micros_vm_handoff_syscall_handle_load_begin(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    struct micros_vm_handoff_state *state,
    const struct micros_pm_control_state *pm,
    const struct micros_vm_load_begin_request *request
)
{
    struct micros_process_handle process;
    struct micros_vm_load_begin_plan load_plan;
    struct vm_fixed_object_plan output_plan;
    enum reservation_check_result reservation;
    enum vm_fixed_object_error object_error;
    enum micros_vm_handoff_error vm_error;
    enum micros_syscall_abi_result result;

    micros_vm_handoff_syscall_clear_bytes(&load_plan, sizeof(load_plan));
    micros_vm_handoff_syscall_clear_bytes(&output_plan, sizeof(output_plan));
    object_error = micros_vm_handoff_syscall_prepare_fixed_object(
        context->process,
        request->output_address,
        sizeof(struct micros_vm_load_begin_result),
        MICROS_SV39_PERMISSION_WRITE,
        &output_plan
    );
    if (object_error == VM_FIXED_OBJECT_ERROR_MEMORY_FAULT) {
        return micros_vm_handoff_syscall_return_result(
            frame,
            MICROS_SYSCALL_ABI_MEMORY_FAULT
        );
    }
    if (object_error != VM_FIXED_OBJECT_OK) {
        micros_vm_handoff_syscall_fail(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    reservation = micros_vm_handoff_syscall_check_reservation(
        pm,
        context->objects,
        request->process_reservation_token,
        NULL,
        true,
        &process
    );
    if (reservation == RESERVATION_CHECK_STATE) {
        return micros_vm_handoff_syscall_return_result(frame, MICROS_SYSCALL_ABI_STATE);
    }
    if (reservation != RESERVATION_CHECK_OK) {
        micros_vm_handoff_syscall_fail(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    vm_error = micros_vm_load_begin_preflight(
        state,
        request->process_reservation_token,
        process,
        true,
        &load_plan
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

    micros_vm_load_begin_commit_prevalidated(state, &load_plan);
    micros_vm_handoff_syscall_write_fixed_object_prevalidated(
        &output_plan,
        &load_plan.result
    );
    return micros_vm_handoff_syscall_return_result(frame, MICROS_SYSCALL_ABI_OK);
}

enum micros_syscall_return micros_vm_handoff_syscall_handle_load_commit(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    struct micros_vm_handoff_state *state,
    const struct micros_vm_load_commit_request *request
)
{
    enum micros_vm_handoff_error vm_error;
    enum micros_syscall_abi_result result;

    vm_error = micros_vm_load_commit_preflight(
        state,
        request->load_token,
        request->mapping_generation
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
    micros_vm_load_commit_commit_prevalidated(state);
    return micros_vm_handoff_syscall_return_result(frame, MICROS_SYSCALL_ABI_OK);
}
