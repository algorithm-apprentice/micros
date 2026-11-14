#include "servers/vm/vm_internal.h"

#include "lib/vm/vm_boot_internal.h"
#include "micros/sv39.h"

_Static_assert(MICROS_VM_MAPPING_RECORD_CAPACITY < MICROS_FRAME_SIZE,
               "membership slots and their multiple-owner marker fit a page key");

bool vm_core_physical_to_frame_index(
    const struct micros_vm_state *state,
    uint64_t physical_address,
    uint64_t *frame_index
)
{
    return physical_address % MICROS_FRAME_SIZE == 0 && frame_index != NULL
        && vm_boot_physical_to_frame_index(state->boot_info, physical_address, frame_index);
}

bool vm_core_frame_index_to_physical(
    const struct micros_vm_state *state,
    uint64_t frame_index,
    uint64_t *physical_address
)
{
    size_t first = 0;
    size_t end;

    if (
        frame_index >= state->boot_summary.managed_frame_count
        || physical_address == NULL
    ) {
        return false;
    }
    end = state->boot_info->header.managed_range_count;
    while (first < end) {
        size_t middle = first + (end - first) / 2;
        const struct micros_vm_managed_range *range = &state->boot_info->managed_ranges[middle];

        if (frame_index < range->frame_index) {
            end = middle;
        } else {
            uint64_t offset = frame_index - range->frame_index;

            if (offset >= range->frame_count) {
                first = middle + 1;
            } else {
                *physical_address = range->base + offset * MICROS_FRAME_SIZE;
                return true;
            }
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

static void reduce_membership_keys(struct vm_core_membership_view *view)
{
    size_t index = 0;

    while (index < view->keys.count) {
        uint64_t key = view->keys.keys64[index++];
        uint64_t physical = key & ~(uint64_t)(MICROS_FRAME_SIZE - 1);
        uint64_t slot = key & (MICROS_FRAME_SIZE - 1);
        bool multiple = false;

        while (index < view->keys.count
               && (view->keys.keys64[index] & ~(uint64_t)(MICROS_FRAME_SIZE - 1)) == physical) {
            if ((view->keys.keys64[index] & (MICROS_FRAME_SIZE - 1)) != slot) {
                multiple = true;
            }
            ++index;
        }
        view->keys.keys64[view->group_count++] =
            physical | (multiple ? MICROS_VM_MAPPING_RECORD_CAPACITY : slot);
    }
}

enum micros_vm_core_error vm_core_membership_begin(
    const struct micros_vm_state *state,
    struct vm_core_membership_view *view
)
{
    if (state == NULL || view == NULL || !vm_core_record_headers_are_valid(state)
        || !micros_vm_keys_begin64(&view->keys)) {
        return MICROS_VM_CORE_ERROR_INVARIANT;
    }
    view->group_count = 0;
    for (size_t slot = 0; slot < MICROS_VM_MAPPING_RECORD_CAPACITY; ++slot) {
        const struct micros_vm_mapping_record *record = &state->mapping_records[slot];

        for (size_t index = 0; index < record->entry_count; ++index) {
            if (!micros_vm_keys_append_page(&view->keys, record->entries[index].physical_address, slot)) {
                (void)vm_core_membership_end(view);
                return MICROS_VM_CORE_ERROR_INVARIANT;
            }
        }
    }
    if (!micros_vm_keys_sort(&view->keys)) {
        (void)vm_core_membership_end(view);
        return MICROS_VM_CORE_ERROR_INVARIANT;
    }
    reduce_membership_keys(view);
    return MICROS_VM_CORE_OK;
}

enum micros_vm_core_error vm_core_membership_query(
    const struct vm_core_membership_view *view,
    uint64_t physical_address,
    size_t ignored_slot,
    bool *present
)
{
    size_t first = 0;
    size_t end;

    if (view == NULL || present == NULL || !micros_vm_keys_view_is_valid(&view->keys)
        || view->keys.keys64 == NULL || view->group_count > view->keys.count
        || physical_address % MICROS_FRAME_SIZE != 0) {
        return MICROS_VM_CORE_ERROR_INVARIANT;
    }
    end = view->group_count;
    while (first < end) {
        size_t middle = first + (end - first) / 2;
        uint64_t key = view->keys.keys64[middle];
        uint64_t physical = key & ~(uint64_t)(MICROS_FRAME_SIZE - 1);

        if (physical_address < physical) {
            end = middle;
        } else if (physical_address > physical) {
            first = middle + 1;
        } else {
            uint64_t slot = key & (MICROS_FRAME_SIZE - 1);

            if (slot > MICROS_VM_MAPPING_RECORD_CAPACITY) {
                return MICROS_VM_CORE_ERROR_INVARIANT;
            }
            *present = slot == MICROS_VM_MAPPING_RECORD_CAPACITY || slot != ignored_slot;
            return MICROS_VM_CORE_OK;
        }
    }
    *present = false;
    return MICROS_VM_CORE_OK;
}

bool vm_core_membership_end(struct vm_core_membership_view *view)
{
    if (view == NULL || !micros_vm_keys_end(&view->keys)) {
        return false;
    }
    view->group_count = 0;
    return true;
}

enum micros_vm_core_error vm_core_select_lowest_frames(
    const struct micros_vm_state *state,
    size_t ignored_slot,
    size_t data_count,
    uint64_t table_frames[MICROS_VM_PAGE_TABLE_FRAME_COUNT],
    uint64_t data_frames[MICROS_APPLICATION_RESIDENT_PAGE_MAX]
)
{
    struct vm_core_membership_view membership;
    enum micros_vm_core_error error;
    size_t selected_count = 0;
    size_t required_count =
        MICROS_VM_PAGE_TABLE_FRAME_COUNT + data_count;
    uint64_t frame_index;

    if (
        data_count == 0
        || data_count > MICROS_APPLICATION_RESIDENT_PAGE_MAX
    ) {
        return MICROS_VM_CORE_ERROR_ARGUMENT;
    }
    error = vm_core_membership_begin(state, &membership);
    if (error != MICROS_VM_CORE_OK) {
        return error;
    }
    for (
        frame_index = 0;
        frame_index < state->boot_summary.managed_frame_count;
        ++frame_index
    ) {
        uint64_t physical_address;
        bool ignored_record_frame = false;
        bool present;

        if (!vm_core_initial_frame_is_free(state, frame_index)) {
            continue;
        }
        if (!vm_core_frame_index_to_physical(state, frame_index, &physical_address)
            || (state->boot_info->frame_states[frame_index] != MICROS_VM_LOCAL_FRAME_FREE
                && (state->boot_info->frame_states[frame_index] < MICROS_VM_LOCAL_FRAME_TRANSFERABLE
                    || state->boot_info->frame_states[frame_index] > MICROS_VM_LOCAL_FRAME_PROCESS_PAGE_TABLE))) {
            error = MICROS_VM_CORE_ERROR_INVARIANT;
            goto done;
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
            )
        ) {
            continue;
        }
        error = vm_core_membership_query(&membership, physical_address,
            ignored_record_frame ? ignored_slot : MICROS_VM_MAPPING_RECORD_CAPACITY, &present);
        if (error != MICROS_VM_CORE_OK) {
            goto done;
        }
        if (present) {
            if (ignored_record_frame) {
                error = MICROS_VM_CORE_ERROR_INVARIANT;
                goto done;
            }
            continue;
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
            error = MICROS_VM_CORE_OK;
            goto done;
        }
    }
    error = MICROS_VM_CORE_ERROR_NO_SPACE;
done:
    return vm_core_membership_end(&membership) ? error : MICROS_VM_CORE_ERROR_INVARIANT;
}

bool vm_core_selected_frames_match(
    const struct micros_vm_state *state,
    size_t ignored_slot
)
{
    uint64_t table_frames[MICROS_VM_PAGE_TABLE_FRAME_COUNT] = {0};
    uint64_t data_frames[MICROS_APPLICATION_RESIDENT_PAGE_MAX] = {0};
    size_t index;

    if (vm_core_select_lowest_frames(
        state,
        ignored_slot,
        state->transaction.page_count,
        table_frames,
        data_frames
    ) != MICROS_VM_CORE_OK) {
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
