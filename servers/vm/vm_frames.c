#include "servers/vm/vm_internal.h"

#include "micros/sv39.h"

bool vm_core_physical_to_frame_index(
    const struct micros_vm_state *state,
    uint64_t physical_address,
    uint64_t *frame_index
)
{
    uint32_t range_index;

    if (
        physical_address % MICROS_FRAME_SIZE != 0
        || frame_index == NULL
    ) {
        return false;
    }
    for (
        range_index = 0;
        range_index
            < state->boot_info->header.managed_range_count;
        ++range_index
    ) {
        const struct micros_vm_managed_range *range =
            &state->boot_info->managed_ranges[range_index];
        uint64_t byte_count =
            range->frame_count * MICROS_FRAME_SIZE;

        if (
            physical_address >= range->base
            && physical_address - range->base < byte_count
        ) {
            *frame_index = range->frame_index
                + (
                    physical_address - range->base
                ) / MICROS_FRAME_SIZE;
            return true;
        }
    }
    return false;
}

bool vm_core_frame_index_to_physical(
    const struct micros_vm_state *state,
    uint64_t frame_index,
    uint64_t *physical_address
)
{
    uint32_t range_index;

    if (
        frame_index >= state->boot_summary.managed_frame_count
        || physical_address == NULL
    ) {
        return false;
    }
    for (
        range_index = 0;
        range_index
            < state->boot_info->header.managed_range_count;
        ++range_index
    ) {
        const struct micros_vm_managed_range *range =
            &state->boot_info->managed_ranges[range_index];

        if (
            frame_index >= range->frame_index
            && frame_index - range->frame_index < range->frame_count
        ) {
            *physical_address = range->base
                + (
                    frame_index - range->frame_index
                ) * MICROS_FRAME_SIZE;
            return true;
        }
    }
    return false;
}

bool vm_core_record_is_zero(
    const struct micros_vm_mapping_record *record
)
{
    return vm_core_bytes_are_zero(record, sizeof(*record));
}

bool vm_core_record_contains_physical(
    const struct micros_vm_mapping_record *record,
    uint64_t physical_address
)
{
    size_t entry_index;

    if (vm_core_record_is_zero(record)) {
        return false;
    }
    for (
        entry_index = 0;
        entry_index < record->entry_count;
        ++entry_index
    ) {
        if (
            record->entries[entry_index].physical_address
                == physical_address
        ) {
            return true;
        }
    }
    return false;
}

bool vm_core_physical_is_in_record_other_than(
    const struct micros_vm_state *state,
    uint64_t physical_address,
    size_t ignored_slot
)
{
    size_t slot;

    for (
        slot = 0;
        slot < MICROS_VM_MAPPING_RECORD_CAPACITY;
        ++slot
    ) {
        if (
            slot != ignored_slot
            && vm_core_record_contains_physical(
                &state->mapping_records[slot],
                physical_address
            )
        ) {
            return true;
        }
    }
    return false;
}

bool vm_core_select_lowest_frames(
    const struct micros_vm_state *state,
    size_t ignored_slot,
    size_t data_count,
    uint64_t table_frames[MICROS_VM_PAGE_TABLE_FRAME_COUNT],
    uint64_t data_frames[MICROS_APPLICATION_RESIDENT_PAGE_MAX]
)
{
    size_t selected_count = 0;
    size_t required_count =
        MICROS_VM_PAGE_TABLE_FRAME_COUNT + data_count;
    uint64_t frame_index;

    if (
        data_count == 0
        || data_count > MICROS_APPLICATION_RESIDENT_PAGE_MAX
    ) {
        return false;
    }
    for (
        frame_index = 0;
        frame_index < state->boot_summary.managed_frame_count;
        ++frame_index
    ) {
        uint64_t physical_address;
        bool ignored_record_frame = false;

        if (
            !vm_core_initial_frame_is_free(state, frame_index)
            || !vm_core_frame_index_to_physical(
                state,
                frame_index,
                &physical_address
            )
        ) {
            continue;
        }
        if (
            ignored_slot < MICROS_VM_MAPPING_RECORD_CAPACITY
            && vm_core_record_contains_physical(
                &state->mapping_records[ignored_slot],
                physical_address
            )
        ) {
            ignored_record_frame = true;
        }
        if (
            !ignored_record_frame
            && (
                state->boot_info->frame_states[frame_index]
                    != MICROS_VM_LOCAL_FRAME_FREE
                || vm_core_physical_is_in_record_other_than(
                    state,
                    physical_address,
                    MICROS_VM_MAPPING_RECORD_CAPACITY
                )
            )
        ) {
            continue;
        }
        if (
            ignored_record_frame
            && vm_core_physical_is_in_record_other_than(
                state,
                physical_address,
                ignored_slot
            )
        ) {
            return false;
        }
        if (selected_count < MICROS_VM_PAGE_TABLE_FRAME_COUNT) {
            table_frames[selected_count] = physical_address;
        } else {
            data_frames[
                selected_count - MICROS_VM_PAGE_TABLE_FRAME_COUNT
            ] = physical_address;
        }
        ++selected_count;
        if (selected_count == required_count) {
            return true;
        }
    }
    return false;
}

bool vm_core_selected_frames_match(
    const struct micros_vm_state *state,
    size_t ignored_slot
)
{
    uint64_t table_frames[MICROS_VM_PAGE_TABLE_FRAME_COUNT] = {0};
    uint64_t data_frames[MICROS_APPLICATION_RESIDENT_PAGE_MAX] = {0};
    size_t index;

    if (!vm_core_select_lowest_frames(
        state,
        ignored_slot,
        state->transaction.page_count,
        table_frames,
        data_frames
    )) {
        return false;
    }
    for (
        index = 0;
        index < MICROS_VM_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        if (
            table_frames[index]
                != state->transaction.table_frames[index]
        ) {
            return false;
        }
    }
    for (
        index = 0;
        index < state->transaction.page_count;
        ++index
    ) {
        if (
            data_frames[index]
                != state->transaction.data_frames[index]
        ) {
            return false;
        }
    }
    return true;
}
