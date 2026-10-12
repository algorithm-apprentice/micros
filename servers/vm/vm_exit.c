#include "servers/vm/vm_internal.h"

enum micros_vm_core_error micros_vm_exit_release_begin(
    struct micros_vm_state *state,
    uint64_t load_token,
    uint64_t mapping_generation,
    uint64_t semantic_pid,
    struct micros_vm_exit_action *action
)
{
    struct micros_vm_exit_action candidate;
    enum micros_vm_core_error error = vm_core_validate_state(state, false);
    size_t slot;
    size_t found_slot = MICROS_VM_MAPPING_RECORD_CAPACITY;

    if (action == NULL) {
        return MICROS_VM_CORE_ERROR_ARGUMENT;
    }
    if (error != MICROS_VM_CORE_OK) {
        return error;
    }
    if (
        state->phase != MICROS_VM_LOADER_IDLE
        || load_token == 0
        || mapping_generation == 0
        || semantic_pid == 0
    ) {
        return MICROS_VM_CORE_ERROR_STATE;
    }
    for (
        slot = 0;
        slot < MICROS_VM_MAPPING_RECORD_CAPACITY;
        ++slot
    ) {
        const struct micros_vm_mapping_record *record =
            &state->mapping_records[slot];

        if (
            record->phase == MICROS_VM_MAPPING_RECORD_ACTIVE
            && record->load_token == load_token
            && record->mapping_generation == mapping_generation
            && record->semantic_pid == semantic_pid
        ) {
            if (found_slot != MICROS_VM_MAPPING_RECORD_CAPACITY) {
                return MICROS_VM_CORE_ERROR_INVARIANT;
            }
            found_slot = slot;
        }
    }
    if (found_slot == MICROS_VM_MAPPING_RECORD_CAPACITY) {
        return MICROS_VM_CORE_ERROR_STATE;
    }
    vm_core_zero_bytes(&candidate, sizeof(candidate));
    candidate.load_token = load_token;
    candidate.mapping_generation = mapping_generation;
    candidate.semantic_pid = semantic_pid;
    candidate.process_generation =
        state->mapping_records[found_slot].process_generation;
    candidate.process_slot = (uint16_t)found_slot;

    state->mapping_records[found_slot].phase =
        MICROS_VM_MAPPING_RECORD_EXIT_RELEASING;
    state->phase = MICROS_VM_LOADER_EXIT_RELEASING;
    *action = candidate;
    return vm_core_validate_state(state, false);
}

enum micros_vm_core_error micros_vm_exit_release_complete(
    struct micros_vm_state *state,
    const struct micros_vm_exit_action *action
)
{
    enum micros_vm_core_error error = vm_core_validate_state(state, false);
    struct micros_vm_mapping_record *record;
    size_t entry_index;

    if (action == NULL) {
        return MICROS_VM_CORE_ERROR_ARGUMENT;
    }
    if (error != MICROS_VM_CORE_OK) {
        return error;
    }
    if (
        state->phase != MICROS_VM_LOADER_EXIT_RELEASING
        || action->reserved != 0
        || action->process_slot >= MICROS_VM_MAPPING_RECORD_CAPACITY
    ) {
        return MICROS_VM_CORE_ERROR_STATE;
    }
    record = &state->mapping_records[action->process_slot];
    if (
        record->phase
            != MICROS_VM_MAPPING_RECORD_EXIT_RELEASING
        || record->load_token != action->load_token
        || record->mapping_generation
            != action->mapping_generation
        || record->semantic_pid != action->semantic_pid
        || record->process_generation
            != action->process_generation
        || record->process_slot != action->process_slot
    ) {
        return MICROS_VM_CORE_ERROR_STATE;
    }
    for (
        entry_index = 0;
        entry_index < record->entry_count;
        ++entry_index
    ) {
        uint64_t frame_index;

        if (
            !vm_core_physical_to_frame_index(
                state,
                record->entries[entry_index].physical_address,
                &frame_index
            )
        ) {
            return MICROS_VM_CORE_ERROR_INVARIANT;
        }
        state->boot_info->frame_states[frame_index] =
            MICROS_VM_LOCAL_FRAME_FREE;
    }
    vm_core_zero_bytes(record, sizeof(*record));
    state->phase = MICROS_VM_LOADER_IDLE;
    return vm_core_validate_state(state, false);
}
