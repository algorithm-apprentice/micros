#include "kernel/vm/vm_handoff_syscall_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum micros_syscall_return micros_vm_handoff_syscall_handle_scratch_map(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    struct micros_vm_handoff_state *state,
    const struct micros_vm_scratch_map_request *request
)
{
    struct micros_vm_scratch_plan scratch;
    struct micros_frame_vm_claim_plan ownership_plan;
    struct micros_user_address_space_vm_scratch_plan address_plan;
    enum micros_vm_handoff_error vm_error;
    enum micros_frame_ownership_error ownership_error;
    enum micros_user_address_space_error address_error;
    enum micros_syscall_abi_result result;

    micros_vm_handoff_syscall_clear_bytes(&scratch, sizeof(scratch));
    micros_vm_handoff_syscall_clear_bytes(&ownership_plan, sizeof(ownership_plan));
    micros_vm_handoff_syscall_clear_bytes(&address_plan, sizeof(address_plan));
    vm_error = micros_vm_scratch_map_preflight(
        state,
        request,
        &scratch
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
    ownership_error =
        micros_frame_ownership_runtime_vm_claim_preflight(
            request->physical_address,
            &ownership_plan
        );
        if (ownership_error != MICROS_FRAME_OWNERSHIP_OK) {
            micros_vm_handoff_syscall_fail(
                hart,
                frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    address_error =
        micros_user_address_space_prepare_vm_scratch_mapping(
            state->process,
            state->root_physical_address,
            request->physical_address,
            &address_plan
        );
        if (address_error != MICROS_USER_ADDRESS_SPACE_OK) {
            micros_vm_handoff_syscall_fail(
                hart,
                frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }

    micros_vm_scratch_map_bind_prevalidated(state, &scratch);
    micros_frame_ownership_runtime_vm_claim_commit_prevalidated(
        &ownership_plan
    );
    micros_user_address_space_commit_vm_scratch_mapping(
        &address_plan
    );
    micros_vm_scratch_map_finish_prevalidated(state);
    return micros_vm_handoff_syscall_return_result(frame, MICROS_SYSCALL_ABI_OK);
}

enum micros_syscall_return micros_vm_handoff_syscall_handle_scratch_commit(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    struct micros_vm_handoff_state *state,
    const struct micros_vm_scratch_commit_request *request
)
{
    struct micros_vm_scratch_plan scratch;
    struct micros_user_address_space_vm_scratch_plan address_plan;
    enum micros_vm_handoff_error vm_error;
    enum micros_user_address_space_error address_error;
    enum micros_syscall_abi_result result;

    micros_vm_handoff_syscall_clear_bytes(&scratch, sizeof(scratch));
    micros_vm_handoff_syscall_clear_bytes(&address_plan, sizeof(address_plan));
    vm_error = micros_vm_scratch_commit_preflight(
        state,
        request,
        &scratch
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
    address_error =
        micros_user_address_space_prepare_vm_scratch_removal(
            state->process,
            state->root_physical_address,
            request->physical_address,
            &address_plan
        );
        if (address_error != MICROS_USER_ADDRESS_SPACE_OK) {
            micros_vm_handoff_syscall_fail(
                hart,
                frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }

    micros_user_address_space_commit_vm_scratch_removal(
        &address_plan
    );
    micros_vm_scratch_commit_commit_prevalidated(
        state,
        &scratch
    );
    return micros_vm_handoff_syscall_return_result(frame, MICROS_SYSCALL_ABI_OK);
}
