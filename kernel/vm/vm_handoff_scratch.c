#include "kernel/vm/vm_handoff_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum micros_vm_handoff_error micros_vm_scratch_map_preflight(
    const struct micros_vm_handoff_state *state,
    const struct micros_vm_scratch_map_request *request,
    struct micros_vm_scratch_plan *plan
)
{
    struct micros_vm_scratch_plan candidate;
    struct micros_vm_kernel_mapping_entry entry;
    const struct micros_vm_kernel_mapping_record *record;
    enum micros_vm_handoff_error error;
    size_t slot;

    if (
        request == NULL
        || plan == NULL
        || !micros_vm_handoff_pointer_is_aligned(
            request,
            _Alignof(struct micros_vm_scratch_map_request)
        )
        || !micros_vm_handoff_pointer_is_aligned(plan, _Alignof(struct micros_vm_scratch_plan))
    ) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    error = micros_vm_handoff_state_validate(state);
    if (error != MICROS_VM_HANDOFF_OK) {
        return error;
    }
    if (
        state->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || state->load.phase != MICROS_VM_LOAD_PHASE_LOADING
        || state->load.scratch_phase != MICROS_VM_SCRATCH_CLEAR
        || request->load_token != state->load.load_token
    ) {
        return MICROS_VM_HANDOFF_ERROR_STATE;
    }

    micros_vm_handoff_clear_bytes(&entry, sizeof(entry));
    entry.virtual_address = request->virtual_address;
    entry.physical_address = request->physical_address;
    entry.permissions = request->permissions;
    record = &state->mappings[state->load.process.slot];
    if (!micros_vm_handoff_mapping_entry_is_valid(&entry)) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    if (!micros_vm_handoff_loading_record_can_append(record, &entry)) {
        return MICROS_VM_HANDOFF_ERROR_STATE;
    }
    for (slot = 0; slot < MICROS_PROCESS_CAPACITY; ++slot) {
        if (
            micros_vm_handoff_record_contains_physical_address(
                &state->mappings[slot],
                entry.physical_address
            )
        ) {
            return MICROS_VM_HANDOFF_ERROR_STATE;
        }
    }

    micros_vm_handoff_clear_bytes(&candidate, sizeof(candidate));
    candidate.entry = entry;
    micros_vm_handoff_copy_bytes(plan, &candidate, sizeof(candidate));
    return MICROS_VM_HANDOFF_OK;
}

void micros_vm_scratch_map_commit_prevalidated(
    struct micros_vm_handoff_state *state,
    const struct micros_vm_scratch_plan *plan
)
{
    micros_vm_scratch_map_bind_prevalidated(state, plan);
    micros_vm_scratch_map_finish_prevalidated(state);
}

void micros_vm_scratch_map_bind_prevalidated(
    struct micros_vm_handoff_state *state,
    const struct micros_vm_scratch_plan *plan
)
{
    state->load.scratch = plan->entry;
}

void micros_vm_scratch_map_finish_prevalidated(
    struct micros_vm_handoff_state *state
)
{
    state->load.scratch_phase = MICROS_VM_SCRATCH_MAPPED;
}

enum micros_vm_handoff_error micros_vm_scratch_commit_preflight(
    const struct micros_vm_handoff_state *state,
    const struct micros_vm_scratch_commit_request *request,
    struct micros_vm_scratch_plan *plan
)
{
    struct micros_vm_scratch_plan candidate;
    enum micros_vm_handoff_error error;

    if (
        request == NULL
        || plan == NULL
        || !micros_vm_handoff_pointer_is_aligned(
            request,
            _Alignof(struct micros_vm_scratch_commit_request)
        )
        || !micros_vm_handoff_pointer_is_aligned(plan, _Alignof(struct micros_vm_scratch_plan))
    ) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    error = micros_vm_handoff_state_validate(state);
    if (error != MICROS_VM_HANDOFF_OK) {
        return error;
    }
    if (
        state->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || state->load.phase != MICROS_VM_LOAD_PHASE_LOADING
        || state->load.scratch_phase != MICROS_VM_SCRATCH_MAPPED
        || request->load_token != state->load.load_token
        || request->virtual_address != state->load.scratch.virtual_address
        || request->physical_address != state->load.scratch.physical_address
    ) {
        return MICROS_VM_HANDOFF_ERROR_STATE;
    }
    micros_vm_handoff_clear_bytes(&candidate, sizeof(candidate));
    candidate.entry = state->load.scratch;
    micros_vm_handoff_copy_bytes(plan, &candidate, sizeof(candidate));
    return MICROS_VM_HANDOFF_OK;
}

void micros_vm_scratch_commit_commit_prevalidated(
    struct micros_vm_handoff_state *state,
    const struct micros_vm_scratch_plan *plan
)
{
    struct micros_vm_kernel_mapping_record *record =
        &state->mappings[state->load.process.slot];

    record->entries[record->entry_count] = plan->entry;
    ++record->entry_count;
    micros_vm_handoff_clear_bytes(&state->load.scratch, sizeof(state->load.scratch));
    state->load.scratch_phase = MICROS_VM_SCRATCH_CLEAR;
}
