#include "kernel/vm/vm_handoff_internal.h"
#include "lib/vm/vm_keys.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static enum micros_vm_handoff_error freeze_frames_are_unique(
    const struct micros_vm_kernel_mapping_record *record,
    const struct micros_vm_load_freeze_plan *request
)
{
    struct micros_vm_key_view keys;
    size_t data_count = record == NULL ? 0 : record->entry_count;
    size_t duplicate;

    if (!micros_vm_keys_begin64(&keys)) {
        return MICROS_VM_HANDOFF_ERROR_INVARIANT;
    }
    for (size_t index = 0; index < data_count + MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++index) {
        uint64_t physical = index < data_count
            ? record->entries[index].physical_address : request->table_frames[index - data_count];

        if (!micros_vm_keys_append_page(&keys, physical, index)) {
            (void)micros_vm_keys_end(&keys);
            return MICROS_VM_HANDOFF_ERROR_INVARIANT;
        }
    }
    if (!micros_vm_keys_finish_pages(&keys, &duplicate)) {
        return MICROS_VM_HANDOFF_ERROR_INVARIANT;
    }
    return duplicate == SIZE_MAX ? MICROS_VM_HANDOFF_OK : MICROS_VM_HANDOFF_ERROR_ARGUMENT;
}

static bool freeze_request_fields_are_valid(
    const struct micros_vm_load_freeze_plan *request
)
{
    size_t table_index;

    if (
        request->version != MICROS_VM_LOAD_FREEZE_PLAN_VERSION
        || request->size != MICROS_VM_LOAD_FREEZE_PLAN_SIZE
        || request->reserved[0] != 0
        || request->reserved[1] != 0
        || request->entry != MICROS_APPLICATION_ENTRY
        || !micros_vm_handoff_stack_pointer_is_valid(request->stack_pointer)
        || request->image_page_count < MICROS_APPLICATION_IMAGE_PAGE_MIN
        || request->image_page_count > MICROS_APPLICATION_IMAGE_PAGE_MAX
        || request->data_page_count != request->image_page_count + 1
    ) {
        return false;
    }
    for (
        table_index = 0;
        table_index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
        ++table_index
    ) {
        if (!micros_vm_handoff_address_is_page_aligned(request->table_frames[table_index])) {
            return false;
        }
    }
    return true;
}

enum micros_vm_handoff_error micros_vm_load_freeze_preflight(
    const struct micros_vm_handoff_state *state,
    const struct micros_frame_ownership *ownership,
    const struct micros_vm_load_freeze_plan *request,
    struct micros_vm_load_freeze_state_plan *plan
)
{
    struct micros_vm_load_freeze_state_plan candidate;
    const struct micros_vm_kernel_mapping_record *record;
    enum micros_vm_handoff_error error;
    uint64_t mapping_generation;

    if (
        request == NULL
        || plan == NULL
        || !micros_vm_handoff_pointer_is_aligned(
            request,
            _Alignof(struct micros_vm_load_freeze_plan)
        )
        || !micros_vm_handoff_pointer_is_aligned(
            plan,
            _Alignof(struct micros_vm_load_freeze_state_plan)
        )
    ) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    error = micros_vm_handoff_validate_selected(state);
    if (error != MICROS_VM_HANDOFF_OK) {
        return error;
    }
    if (!freeze_request_fields_are_valid(request)) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    error = freeze_frames_are_unique(NULL, request);
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
    record = &state->mappings[state->load.process.slot];
    if (
        request->data_page_count != record->entry_count
        || !micros_vm_handoff_record_entries_are_frozen_shape(record)
    ) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    error = freeze_frames_are_unique(record, request);
    if (error != MICROS_VM_HANDOFF_OK) {
        return error;
    }
    if (micros_frame_ownership_validate_metadata(ownership) != MICROS_FRAME_OWNERSHIP_OK
        || ownership->phase != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF) {
        return MICROS_VM_HANDOFF_ERROR_INVARIANT;
    }
    for (size_t index = 0; index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++index) {
        bool present;

        error = micros_vm_handoff_frame_membership_prevalidated(
            state, ownership, request->table_frames[index], &present);
        if (error != MICROS_VM_HANDOFF_OK) {
            return error;
        }
        if (present) {
            return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
        }
    }
    if (
        request->page_plan_digest
        != micros_vm_load_page_plan_digest(
            request->image_page_count,
            request->data_page_count,
            record->entries
        )
    ) {
        return MICROS_VM_HANDOFF_ERROR_SUMMARY;
    }
    mapping_generation = micros_vm_handoff_next_nonzero_token(state->last_mapping_generation);
    if (mapping_generation == 0) {
        return MICROS_VM_HANDOFF_ERROR_CAPACITY;
    }

    micros_vm_handoff_clear_bytes(&candidate, sizeof(candidate));
    candidate.mapping_generation = mapping_generation;
    candidate.action.process = state->load.process;
    candidate.action.load_token = state->load.load_token;
    candidate.action.mapping_generation = mapping_generation;
    candidate.action.root_physical_address = request->table_frames[0];
    micros_vm_handoff_copy_bytes(
        candidate.action.table_frames,
        request->table_frames,
        sizeof(candidate.action.table_frames)
    );
    candidate.action.entry = request->entry;
    candidate.action.stack_pointer = request->stack_pointer;
    candidate.action.entry_count = record->entry_count;
    micros_vm_handoff_copy_bytes(
        candidate.action.entries,
        record->entries,
        sizeof(candidate.action.entries)
    );
    candidate.result.version = MICROS_VM_LOAD_FREEZE_RESULT_VERSION;
    candidate.result.size = MICROS_VM_LOAD_FREEZE_RESULT_SIZE;
    candidate.result.mapping_generation = mapping_generation;
    micros_vm_handoff_copy_bytes(plan, &candidate, sizeof(candidate));
    return MICROS_VM_HANDOFF_OK;
}

void micros_vm_load_freeze_commit_prevalidated(
    struct micros_vm_handoff_state *state,
    const struct micros_vm_load_freeze_state_plan *plan
)
{
    struct micros_vm_kernel_mapping_record *record =
        &state->mappings[state->load.process.slot];

    state->last_mapping_generation = plan->mapping_generation;
    state->load.phase = MICROS_VM_LOAD_PHASE_FROZEN;
    record->mapping_generation = plan->mapping_generation;
    record->root_physical_address = plan->action.root_physical_address;
    micros_vm_handoff_copy_bytes(
        record->table_frames,
        plan->action.table_frames,
        sizeof(record->table_frames)
    );
    record->entry = plan->action.entry;
    record->stack_pointer = plan->action.stack_pointer;
    record->phase = MICROS_VM_KERNEL_MAPPING_FROZEN;
}
