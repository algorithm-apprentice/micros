#include "servers/vm/vm_internal.h"

#include "lib/vm/vm_boot_internal.h"
#include "micros/sv39.h"
#include "tests/qemu/vm_validation_work.h"

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

static void make_frame_available(struct micros_vm_state *state, uint64_t frame_index)
{
    state->free_bitmap[frame_index / 64] |= UINT64_C(1) << (frame_index % 64);
    state->boot_info->frame_states[frame_index] = MICROS_VM_LOCAL_FRAME_FREE;
}

void vm_core_release_frame_prevalidated(struct micros_vm_state *state, uint64_t frame_index)
{
    make_frame_available(state, frame_index);
    if (state->free_page_cache_count < MICROS_VM_FREE_PAGE_CACHE_CAPACITY) {
        state->free_page_cache[state->free_page_cache_count++] = (uint32_t)frame_index;
    }
}

static enum micros_vm_core_error reserve_frame(
    struct micros_vm_state *state, uint64_t frame_index, uint64_t *physical
)
{
    uint64_t address;

    if (!vm_core_initial_frame_is_free(state, frame_index)
        || !vm_core_frame_availability_is_valid(state, frame_index)
        || state->boot_info->frame_states[frame_index] != MICROS_VM_LOCAL_FRAME_FREE
        || !vm_core_frame_index_to_physical(state, frame_index, &address)) {
        return MICROS_VM_CORE_ERROR_INVARIANT;
    }
    state->free_bitmap[frame_index / 64] &= ~(UINT64_C(1) << (frame_index % 64));
    state->boot_info->frame_states[frame_index] = MICROS_VM_LOCAL_FRAME_LOAD_RESERVED;
    state->allocation_origin = (uint32_t)frame_index;
    *physical = address;
    return MICROS_VM_CORE_OK;
}

static enum micros_vm_core_error reserve_cached_frame(struct micros_vm_state *state, uint64_t *physical)
{
    while (state->free_page_cache_count != 0) {
        uint32_t frame_index;

        MICROS_VM01_WORK_HIT("vm01-allocator-cache", MICROS_WORK_OWNER_VM_SERVICE,
                            MICROS_WORK_KIND_RECORD_VISIT, 1);
        frame_index = state->free_page_cache[state->free_page_cache_count - 1];
        --state->free_page_cache_count;
        if (!vm_core_initial_frame_is_free(state, frame_index)
            || !vm_core_frame_availability_is_valid(state, frame_index)) {
            return MICROS_VM_CORE_ERROR_INVARIANT;
        }
        if (state->boot_info->frame_states[frame_index] == MICROS_VM_LOCAL_FRAME_FREE) {
            return reserve_frame(state, frame_index, physical);
        }
    }
    return MICROS_VM_CORE_ERROR_NO_SPACE;
}

static enum micros_vm_core_error reserve_scanned_frame(
    struct micros_vm_state *state, uint32_t origin, uint64_t *physical
)
{
    size_t word_index = origin / 64;
    uint64_t mask = UINT64_MAX >> (63 - origin % 64);

    for (;;) {
        uint64_t word;

        MICROS_VM01_WORK_HIT("vm01-allocator-search", MICROS_WORK_OWNER_VM_SERVICE,
                            MICROS_WORK_KIND_BITMAP_WORD_LOAD, 1);
        word = state->free_bitmap[word_index] & mask;
        if (word != 0) {
            unsigned bit = 63;

            while ((word & (UINT64_C(1) << bit)) == 0) {
                --bit;
            }
            return reserve_frame(state, word_index * UINT64_C(64) + bit, physical);
        }
        if (word_index == 0) {
            return MICROS_VM_CORE_ERROR_NO_SPACE;
        }
        --word_index;
        mask = UINT64_MAX;
    }
}

static enum micros_vm_core_error reserve_next_frame(struct micros_vm_state *state, uint64_t *physical)
{
    enum micros_vm_core_error error = reserve_cached_frame(state, physical);
    uint32_t highest = (uint32_t)state->boot_summary.managed_frame_count - 1;
    uint32_t origin = state->allocation_origin < state->boot_summary.managed_frame_count
        ? state->allocation_origin : highest;

    if (error == MICROS_VM_CORE_ERROR_NO_SPACE) {
        error = reserve_scanned_frame(state, origin, physical);
        if (error == MICROS_VM_CORE_ERROR_NO_SPACE) {
            error = reserve_scanned_frame(state, highest, physical);
        }
    }
    return error;
}

static bool order_reserved_frames(
    size_t data_count,
    uint64_t table_frames[MICROS_VM_PAGE_TABLE_FRAME_COUNT],
    uint64_t data_frames[MICROS_APPLICATION_RESIDENT_PAGE_MAX]
)
{
    struct micros_vm_key_view keys;
    size_t count = MICROS_VM_PAGE_TABLE_FRAME_COUNT + data_count;
    bool ordered = false;

    if (!micros_vm_keys_begin64(&keys)) {
        return false;
    }
    for (size_t index = 0; index < count; ++index) {
        uint64_t physical = index < MICROS_VM_PAGE_TABLE_FRAME_COUNT
            ? table_frames[index] : data_frames[index - MICROS_VM_PAGE_TABLE_FRAME_COUNT];

        if (!micros_vm_keys_append64(&keys, physical)) {
            goto done;
        }
    }
    if (!micros_vm_keys_sort(&keys)) {
        goto done;
    }
    for (size_t index = 0; index < count; ++index) {
        if (index < MICROS_VM_PAGE_TABLE_FRAME_COUNT) {
            table_frames[index] = keys.keys64[index];
        } else {
            data_frames[index - MICROS_VM_PAGE_TABLE_FRAME_COUNT] = keys.keys64[index];
        }
    }
    ordered = true;
done:
    return micros_vm_keys_end(&keys) && ordered;
}

enum micros_vm_core_error vm_core_reserve_frames(
    struct micros_vm_state *state,
    size_t data_count,
    uint64_t table_frames[MICROS_VM_PAGE_TABLE_FRAME_COUNT],
    uint64_t data_frames[MICROS_APPLICATION_RESIDENT_PAGE_MAX]
)
{
    uint32_t old_count, old_origin;
    size_t selected = 0;
    enum micros_vm_core_error error = MICROS_VM_CORE_OK;

    if (state == NULL || table_frames == NULL || data_frames == NULL
        || data_count == 0 || data_count > MICROS_APPLICATION_RESIDENT_PAGE_MAX) {
        return MICROS_VM_CORE_ERROR_ARGUMENT;
    }
    if (state->boot_info == NULL || !vm_core_allocator_metadata_is_valid(state)) {
        return MICROS_VM_CORE_ERROR_INVARIANT;
    }
    old_count = state->free_page_cache_count;
    old_origin = state->allocation_origin;
    for (; selected < MICROS_VM_PAGE_TABLE_FRAME_COUNT + data_count; ++selected) {
        uint64_t *physical = selected < MICROS_VM_PAGE_TABLE_FRAME_COUNT
            ? &table_frames[selected] : &data_frames[selected - MICROS_VM_PAGE_TABLE_FRAME_COUNT];

        error = reserve_next_frame(state, physical);
        if (error != MICROS_VM_CORE_OK) {
            break;
        }
    }
    if (error == MICROS_VM_CORE_OK && !order_reserved_frames(data_count, table_frames, data_frames)) {
        error = MICROS_VM_CORE_ERROR_INVARIANT;
    }
    if (error != MICROS_VM_CORE_OK) {
        for (size_t index = 0; index < selected; ++index) {
            uint64_t frame_index;
            uint64_t physical = index < MICROS_VM_PAGE_TABLE_FRAME_COUNT
                ? table_frames[index] : data_frames[index - MICROS_VM_PAGE_TABLE_FRAME_COUNT];

            (void)vm_core_physical_to_frame_index(state, physical, &frame_index);
            make_frame_available(state, frame_index);
        }
        state->free_page_cache_count = old_count;
        state->allocation_origin = old_origin;
    }
    return error;
}

bool vm_core_transaction_frames_match(const struct micros_vm_state *state)
{
    const struct micros_vm_service_load_transaction *transaction = &state->transaction;

    for (size_t index = 0; index < MICROS_VM_PAGE_TABLE_FRAME_COUNT + transaction->page_count; ++index) {
        bool table = index < MICROS_VM_PAGE_TABLE_FRAME_COUNT;
        size_t page = table ? 0 : index - MICROS_VM_PAGE_TABLE_FRAME_COUNT;
        uint64_t physical = table ? transaction->table_frames[index] : transaction->data_frames[page];
        uint64_t frame_index;
        uint8_t expected = MICROS_VM_LOCAL_FRAME_LOAD_RESERVED;

        if (state->phase == MICROS_VM_LOADER_FROZEN) {
            expected = table ? MICROS_VM_LOCAL_FRAME_PROCESS_PAGE_TABLE : MICROS_VM_LOCAL_FRAME_PROCESS_WIRED;
        } else if (!table && (page < transaction->completed_page_count || transaction->scratch_page == page + 1)) {
            expected = MICROS_VM_LOCAL_FRAME_TRANSFERABLE;
        }
        if (!vm_core_physical_to_frame_index(state, physical, &frame_index)
            || !vm_core_initial_frame_is_free(state, frame_index)
            || !vm_core_frame_availability_is_valid(state, frame_index)
            || state->boot_info->frame_states[frame_index] != expected) {
            return false;
        }
    }
    return true;
}
