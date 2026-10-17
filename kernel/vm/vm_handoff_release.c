#include "kernel/vm/vm_handoff_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum micros_vm_handoff_error micros_vm_load_abort_preflight(
    const struct micros_vm_handoff_state *state,
    uint64_t load_token,
    uint64_t mapping_generation,
    struct micros_vm_load_abort_action *action
)
{
    struct micros_vm_load_abort_action candidate;
    const struct micros_vm_kernel_mapping_record *record;
    enum micros_vm_handoff_error error;

    if (
        action == NULL
        || !micros_vm_handoff_pointer_is_aligned(
            action,
            _Alignof(struct micros_vm_load_abort_action)
        )
    ) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    error = micros_vm_handoff_state_validate(state);
    if (error != MICROS_VM_HANDOFF_OK) {
        return error;
    }
    if (
        state->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || state->load.phase == MICROS_VM_LOAD_PHASE_IDLE
        || load_token == 0
        || load_token != state->load.load_token
    ) {
        return MICROS_VM_HANDOFF_ERROR_STATE;
    }
    record = &state->mappings[state->load.process.slot];
    if (
        (
            state->load.phase == MICROS_VM_LOAD_PHASE_LOADING
            && mapping_generation != 0
        )
        || (
            state->load.phase == MICROS_VM_LOAD_PHASE_FROZEN
            && (
                mapping_generation != record->mapping_generation
                || record->phase != MICROS_VM_KERNEL_MAPPING_FROZEN
            )
        )
    ) {
        return MICROS_VM_HANDOFF_ERROR_STATE;
    }

    micros_vm_handoff_clear_bytes(&candidate, sizeof(candidate));
    candidate.frozen = state->load.phase == MICROS_VM_LOAD_PHASE_FROZEN;
    candidate.scratch_mapped =
        state->load.scratch_phase == MICROS_VM_SCRATCH_MAPPED;
    candidate.process = state->load.process;
    candidate.root_physical_address = record->root_physical_address;
    candidate.scratch_physical_address =
        candidate.scratch_mapped ? state->load.scratch.physical_address : 0;
    candidate.entry_count = record->entry_count;
    micros_vm_handoff_copy_bytes(
        candidate.table_frames,
        record->table_frames,
        sizeof(candidate.table_frames)
    );
    micros_vm_handoff_copy_bytes(candidate.entries, record->entries, sizeof(candidate.entries));
    micros_vm_handoff_copy_bytes(action, &candidate, sizeof(candidate));
    return MICROS_VM_HANDOFF_OK;
}

void micros_vm_load_abort_commit_prevalidated(
    struct micros_vm_handoff_state *state
)
{
    micros_vm_handoff_clear_bytes(
        &state->mappings[state->load.process.slot],
        sizeof(state->mappings[state->load.process.slot])
    );
    micros_vm_handoff_clear_bytes(&state->load, sizeof(state->load));
}

enum micros_vm_handoff_error micros_vm_load_mark_exit_detached(
    struct micros_vm_handoff_state *state,
    struct micros_process_handle process,
    uint64_t exit_token,
    uint64_t load_token,
    uint64_t mapping_generation
)
{
    struct micros_vm_kernel_mapping_record *record;
    enum micros_vm_handoff_error error =
        micros_vm_handoff_state_validate(state);

    if (error != MICROS_VM_HANDOFF_OK) {
        return error;
    }
    if (
        state->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || !micros_vm_handoff_process_is_valid(process)
        || exit_token == 0
    ) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    record = &state->mappings[process.slot];
    if (
        record->phase != MICROS_VM_KERNEL_MAPPING_ACTIVE
        || !micros_vm_handoff_process_handles_equal(record->process, process)
        || record->load_token != load_token
        || record->mapping_generation != mapping_generation
    ) {
        return MICROS_VM_HANDOFF_ERROR_STATE;
    }
    record->exit_token = exit_token;
    record->phase = MICROS_VM_KERNEL_MAPPING_EXIT_DETACHED;
    return MICROS_VM_HANDOFF_OK;
}

enum micros_vm_handoff_error micros_vm_exit_release_preflight(
    const struct micros_vm_handoff_state *state,
    uint64_t exit_token,
    uint64_t load_token,
    uint64_t mapping_generation,
    struct micros_vm_exit_release_action *action
)
{
    struct micros_vm_exit_release_action candidate;
    const struct micros_vm_kernel_mapping_record *record = NULL;
    enum micros_vm_handoff_error error;
    size_t slot;

    if (
        action == NULL
        || !micros_vm_handoff_pointer_is_aligned(
            action,
            _Alignof(struct micros_vm_exit_release_action)
        )
    ) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    error = micros_vm_handoff_state_validate(state);
    if (error != MICROS_VM_HANDOFF_OK) {
        return error;
    }
    if (
        state->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || exit_token == 0
        || load_token == 0
        || mapping_generation == 0
    ) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    for (slot = 0; slot < MICROS_PROCESS_CAPACITY; ++slot) {
        const struct micros_vm_kernel_mapping_record *candidate_record =
            &state->mappings[slot];

        if (
            candidate_record->phase == MICROS_VM_KERNEL_MAPPING_EXIT_DETACHED
            && candidate_record->exit_token == exit_token
        ) {
            if (record != NULL) {
                return MICROS_VM_HANDOFF_ERROR_INVARIANT;
            }
            record = candidate_record;
        }
    }
    if (
        record == NULL
        || record->load_token != load_token
        || record->mapping_generation != mapping_generation
    ) {
        return MICROS_VM_HANDOFF_ERROR_STATE;
    }

    micros_vm_handoff_clear_bytes(&candidate, sizeof(candidate));
    candidate.process = record->process;
    candidate.root_physical_address = record->root_physical_address;
    candidate.entry_count = record->entry_count;
    micros_vm_handoff_copy_bytes(
        candidate.table_frames,
        record->table_frames,
        sizeof(candidate.table_frames)
    );
    micros_vm_handoff_copy_bytes(candidate.entries, record->entries, sizeof(candidate.entries));
    micros_vm_handoff_copy_bytes(action, &candidate, sizeof(candidate));
    return MICROS_VM_HANDOFF_OK;
}

void micros_vm_exit_release_commit_prevalidated(
    struct micros_vm_handoff_state *state,
    struct micros_process_handle process
)
{
    micros_vm_handoff_clear_bytes(
        &state->mappings[process.slot],
        sizeof(state->mappings[process.slot])
    );
}
