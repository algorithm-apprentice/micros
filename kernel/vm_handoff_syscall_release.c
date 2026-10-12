#include "kernel/vm_handoff_syscall_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct abort_command_plans {
    struct micros_vm_load_abort_action action;
    struct micros_user_address_space_vm_scratch_plan scratch;
    struct micros_user_address_space_vm_teardown_plan teardown;
    struct micros_frame_vm_transferable_release_plan transferable;
    struct micros_frame_vm_process_release_plan process;
};

static void prepare_frozen_abort_release(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_vm_handoff_state *state,
    struct micros_process_handle process,
    struct abort_command_plans *plans
)
{
    uint64_t data_frames[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
    size_t index;

    micros_vm_handoff_syscall_clear_bytes(
        data_frames,
        sizeof(data_frames)
    );
    if (micros_user_address_space_prepare_vm_teardown(
        process,
        plans->action.root_physical_address,
        plans->action.table_frames,
        plans->action.entries,
        plans->action.entry_count,
        &plans->teardown
    ) != MICROS_USER_ADDRESS_SPACE_OK) {
        micros_vm_handoff_syscall_fail(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    for (index = 0; index < plans->action.entry_count; ++index) {
        data_frames[index] =
            plans->action.entries[index].physical_address;
    }
    if (micros_frame_ownership_runtime_vm_release_process_preflight(
        process,
        data_frames,
        plans->action.entry_count,
        plans->action.table_frames,
        &plans->process
    ) != MICROS_FRAME_OWNERSHIP_OK) {
        micros_vm_handoff_syscall_fail(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
}

static void prepare_loading_abort_release(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_vm_handoff_state *state,
    struct abort_command_plans *plans
)
{
    uint64_t data_frames[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
    size_t data_frame_count = plans->action.entry_count;
    size_t index;

    micros_vm_handoff_syscall_clear_bytes(
        data_frames,
        sizeof(data_frames)
    );
    for (index = 0; index < plans->action.entry_count; ++index) {
        data_frames[index] =
            plans->action.entries[index].physical_address;
    }
    if (plans->action.scratch_mapped) {
        if (data_frame_count >= MICROS_APPLICATION_RESIDENT_PAGE_MAX) {
            micros_vm_handoff_syscall_fail(
                hart,
                frame,
                MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
                state
            );
        }
        data_frames[data_frame_count] =
            plans->action.scratch_physical_address;
        ++data_frame_count;
        if (micros_user_address_space_prepare_vm_scratch_removal(
            state->process,
            state->root_physical_address,
            plans->action.scratch_physical_address,
            &plans->scratch
        ) != MICROS_USER_ADDRESS_SPACE_OK) {
            micros_vm_handoff_syscall_fail(
                hart,
                frame,
                MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
                state
            );
        }
    }
    if (micros_frame_ownership_runtime_vm_release_transferable_preflight(
        data_frames,
        data_frame_count,
        &plans->transferable
    ) != MICROS_FRAME_OWNERSHIP_OK) {
        micros_vm_handoff_syscall_fail(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
}

static void commit_abort_release(
    struct micros_vm_handoff_state *state,
    struct abort_command_plans *plans
)
{
    if (plans->action.frozen) {
        micros_user_address_space_commit_vm_teardown_prevalidated(
            &plans->teardown
        );
        micros_frame_ownership_runtime_vm_release_process_commit_prevalidated(
            &plans->process
        );
    } else {
        if (plans->action.scratch_mapped) {
            micros_user_address_space_commit_vm_scratch_removal(
                &plans->scratch
            );
        }
        micros_frame_ownership_runtime_vm_release_transferable_commit_prevalidated(
            &plans->transferable
        );
    }
    micros_vm_load_abort_commit_prevalidated(state);
}

enum micros_syscall_return micros_vm_handoff_syscall_handle_load_abort(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    struct micros_vm_handoff_state *state,
    const struct micros_pm_control_state *pm,
    const struct micros_vm_load_abort_request *request
)
{
    struct abort_command_plans plans;
    struct micros_process_handle reserved_process;
    enum micros_vm_handoff_error vm_error;
    enum reservation_check_result reservation;
    enum micros_syscall_abi_result result;

    micros_vm_handoff_syscall_clear_bytes(&plans, sizeof(plans));
    vm_error = micros_vm_load_abort_preflight(
        state,
        request->load_token,
        request->mapping_generation,
        &plans.action
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
    reservation = micros_vm_handoff_syscall_check_reservation(
        pm,
        context->objects,
        state->load.process_reservation_token,
        &plans.action.process,
        !plans.action.frozen,
        &reserved_process
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
    if (plans.action.frozen) {
        prepare_frozen_abort_release(
            hart,
            frame,
            state,
            reserved_process,
            &plans
        );
    } else {
        prepare_loading_abort_release(hart, frame, state, &plans);
    }
    commit_abort_release(state, &plans);
    return micros_vm_handoff_syscall_return_result(
        frame,
        MICROS_SYSCALL_ABI_OK
    );
}

enum micros_syscall_return micros_vm_handoff_syscall_handle_exit_release(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    struct micros_vm_handoff_state *state,
    const struct micros_vm_exit_release_request *request
)
{
    struct micros_vm_exit_release_action action;
    struct micros_user_address_space_vm_teardown_plan address_plan;
    struct micros_frame_vm_process_release_plan ownership_plan;
    uint64_t data_frames[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
    enum micros_vm_handoff_error vm_error;
    enum micros_user_address_space_error address_error;
    enum micros_frame_ownership_error ownership_error;
    enum micros_syscall_abi_result result;
    size_t index;

    micros_vm_handoff_syscall_clear_bytes(&action, sizeof(action));
    micros_vm_handoff_syscall_clear_bytes(&address_plan, sizeof(address_plan));
    micros_vm_handoff_syscall_clear_bytes(&ownership_plan, sizeof(ownership_plan));
    micros_vm_handoff_syscall_clear_bytes(data_frames, sizeof(data_frames));
    vm_error = micros_vm_exit_release_preflight(
        state,
        request->exit_token,
        request->load_token,
        request->mapping_generation,
        &action
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
    address_error = micros_user_address_space_prepare_vm_teardown(
        action.process,
        action.root_physical_address,
        action.table_frames,
        action.entries,
        action.entry_count,
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
    for (index = 0; index < action.entry_count; ++index) {
        data_frames[index] =
            action.entries[index].physical_address;
    }
    ownership_error =
        micros_frame_ownership_runtime_vm_release_process_preflight(
            action.process,
            data_frames,
            action.entry_count,
            action.table_frames,
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

    micros_user_address_space_commit_vm_teardown_prevalidated(
        &address_plan
    );
    micros_frame_ownership_runtime_vm_release_process_commit_prevalidated(
        &ownership_plan
    );
    micros_vm_exit_release_commit_prevalidated(
        state,
        action.process
    );
    return micros_vm_handoff_syscall_return_result(frame, MICROS_SYSCALL_ABI_OK);
}
