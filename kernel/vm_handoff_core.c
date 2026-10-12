#include "kernel/vm_handoff_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "micros/bootstrap.h"
#include "micros/endpoint.h"
#include "micros/frame_allocator.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"

#define MICROS_VM_PAGE_DIGEST_OFFSET UINT64_C(14695981039346656037)
#define MICROS_VM_PAGE_DIGEST_PRIME UINT64_C(1099511628211)

bool micros_vm_handoff_bytes_are_zero(const void *storage, size_t size)
{
    const unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

void micros_vm_handoff_clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

void micros_vm_handoff_copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *output = destination;
    const unsigned char *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
    }
}

bool micros_vm_handoff_pointer_is_aligned(const void *pointer, size_t alignment)
{
    return (uintptr_t)pointer % alignment == 0;
}

bool micros_vm_handoff_process_is_valid(struct micros_process_handle process)
{
    return (
        process.slot < MICROS_PROCESS_CAPACITY
        && process.generation != 0
        && process.generation <= MICROS_PROCESS_GENERATION_MAX
    );
}

bool micros_vm_handoff_thread_is_valid(struct micros_thread_handle thread)
{
    return (
        thread.slot < MICROS_THREAD_CAPACITY
        && thread.generation != 0
    );
}

bool micros_vm_handoff_process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

bool micros_vm_handoff_address_is_page_aligned(uint64_t address)
{
    return address != 0 && address % MICROS_FRAME_SIZE == 0;
}

bool micros_vm_handoff_user_page_is_valid(uint64_t virtual_address)
{
    return (
        virtual_address >= MICROS_USER_VIRTUAL_BASE
        && virtual_address < MICROS_USER_VIRTUAL_END
        && virtual_address % MICROS_FRAME_SIZE == 0
    );
}

bool micros_vm_handoff_user_permissions_are_valid(uint32_t permissions)
{
    const uint32_t defined = MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE
        | MICROS_SV39_PERMISSION_EXECUTE
        | MICROS_SV39_PERMISSION_USER;

    return (
        (permissions & ~defined) == 0
        && (permissions & MICROS_SV39_PERMISSION_USER) != 0
        && (permissions & MICROS_SV39_PERMISSION_READ) != 0
        && (
            (permissions & MICROS_SV39_PERMISSION_WRITE) == 0
            || (
                (permissions & MICROS_SV39_PERMISSION_EXECUTE) == 0
            )
        )
    );
}

bool micros_vm_handoff_mapping_entry_is_valid(
    const struct micros_vm_kernel_mapping_entry *entry
)
{
    return (
        entry->reserved == 0
        && micros_vm_handoff_user_page_is_valid(entry->virtual_address)
        && entry->virtual_address != MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
        && micros_vm_handoff_address_is_page_aligned(entry->physical_address)
        && micros_vm_handoff_user_permissions_are_valid(entry->permissions)
    );
}

bool micros_vm_handoff_stack_pointer_is_valid(uint64_t stack_pointer)
{
    return (
        stack_pointer
            >= MICROS_APPLICATION_STACK_TOP
                - MICROS_APPLICATION_STACK_OCCUPIED_MAX
        && stack_pointer < MICROS_APPLICATION_STACK_TOP
        && stack_pointer % 16 == 0
    );
}

uint64_t micros_vm_handoff_next_nonzero_token(uint64_t previous)
{
    return previous == UINT64_MAX ? 0 : previous + 1;
}

bool micros_vm_handoff_endpoint_matches_process(
    uint32_t endpoint,
    struct micros_process_handle process
)
{
    return (
        micros_vm_handoff_process_is_valid(process)
        && endpoint
            == (
                (process.generation << MICROS_ENDPOINT_SLOT_BITS)
                | process.slot
            )
        && endpoint != MICROS_ENDPOINT_NONE
        && endpoint != MICROS_ENDPOINT_ANY
    );
}

bool micros_vm_handoff_summary_is_valid(
    const struct micros_vm_boot_summary *summary
)
{
    return (
        summary->version == MICROS_VM_BOOT_INFO_VERSION
        && summary->managed_range_count != 0
        && summary->managed_range_count <= MICROS_VM_MAX_MANAGED_RANGES
        && summary->address_space_count != 0
        && summary->address_space_count
            <= MICROS_VM_MAX_STATIC_ADDRESS_SPACES
        && summary->mapping_count != 0
        && summary->mapping_count <= MICROS_VM_MAX_STATIC_MAPPINGS
        && summary->managed_frame_count != 0
        && summary->managed_frame_count <= MICROS_VM_MAX_MANAGED_FRAMES
        && summary->free_frame_count <= summary->managed_frame_count
        && summary->vm_self_wired_frame_count != 0
        && summary->vm_self_wired_frame_count
            <= summary->managed_frame_count
    );
}

bool micros_vm_handoff_mapping_record_is_zero(
    const struct micros_vm_kernel_mapping_record *record
)
{
    return micros_vm_handoff_bytes_are_zero(record, sizeof(*record));
}

static int permission_group(uint32_t permissions)
{
    const uint32_t text = MICROS_SV39_PERMISSION_USER
        | MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_EXECUTE;
    const uint32_t read_only =
        MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ;
    const uint32_t writable = MICROS_SV39_PERMISSION_USER
        | MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE;

    if (permissions == text) {
        return 0;
    }
    if (permissions == read_only) {
        return 1;
    }
    return permissions == writable ? 2 : -1;
}

static bool record_entries_are_loading_shape(
    const struct micros_vm_kernel_mapping_record *record
)
{
    int group = 0;
    size_t index;

    if (record->entry_count > MICROS_APPLICATION_RESIDENT_PAGE_MAX) {
        return false;
    }
    for (index = 0; index < record->entry_count; ++index) {
        const struct micros_vm_kernel_mapping_entry *entry =
            &record->entries[index];
        int next_group;
        size_t prior_index;

        if (!micros_vm_handoff_mapping_entry_is_valid(entry)) {
            return false;
        }
        for (prior_index = 0; prior_index < index; ++prior_index) {
            if (
                record->entries[prior_index].physical_address
                == entry->physical_address
            ) {
                return false;
            }
        }
        if (entry->virtual_address == MICROS_APPLICATION_STACK_BASE) {
            if (
                index + 1 != record->entry_count
                || index < MICROS_APPLICATION_IMAGE_PAGE_MIN
                || index > MICROS_APPLICATION_IMAGE_PAGE_MAX
                || permission_group(entry->permissions) != 2
                || group != 2
            ) {
                return false;
            }
            continue;
        }
        if (
            index >= MICROS_APPLICATION_IMAGE_PAGE_MAX
            || entry->virtual_address
                != MICROS_APPLICATION_ENTRY + index * MICROS_FRAME_SIZE
        ) {
            return false;
        }
        next_group = permission_group(entry->permissions);
        if (
            next_group < 0
            || (
                index == 0
                && next_group != 0
            )
            || (
                index != 0
                && (
                    next_group < group
                    || next_group > group + 1
                )
            )
        ) {
            return false;
        }
        group = next_group;
    }
    for (
        index = record->entry_count;
        index < MICROS_APPLICATION_RESIDENT_PAGE_MAX;
        ++index
    ) {
        if (
            !micros_vm_handoff_bytes_are_zero(
                &record->entries[index],
                sizeof(record->entries[index])
            )
        ) {
            return false;
        }
    }
    return true;
}

bool micros_vm_handoff_loading_record_can_append(
    const struct micros_vm_kernel_mapping_record *record,
    const struct micros_vm_kernel_mapping_entry *entry
)
{
    int next_group;
    int prior_group = 0;
    size_t index;

    if (
        !record_entries_are_loading_shape(record)
        || !micros_vm_handoff_mapping_entry_is_valid(entry)
        || record->entry_count >= MICROS_APPLICATION_RESIDENT_PAGE_MAX
    ) {
        return false;
    }
    for (index = 0; index < record->entry_count; ++index) {
        if (
            record->entries[index].physical_address
            == entry->physical_address
        ) {
            return false;
        }
    }
    if (
        record->entry_count != 0
        && record->entries[record->entry_count - 1].virtual_address
            == MICROS_APPLICATION_STACK_BASE
    ) {
        return false;
    }
    next_group = permission_group(entry->permissions);
    if (entry->virtual_address == MICROS_APPLICATION_STACK_BASE) {
        return (
            record->entry_count >= MICROS_APPLICATION_IMAGE_PAGE_MIN
            && record->entry_count <= MICROS_APPLICATION_IMAGE_PAGE_MAX
            && next_group == 2
            && permission_group(
                record->entries[record->entry_count - 1].permissions
            ) == 2
        );
    }
    if (
        record->entry_count >= MICROS_APPLICATION_IMAGE_PAGE_MAX
        || entry->virtual_address
            != MICROS_APPLICATION_ENTRY
                + record->entry_count * MICROS_FRAME_SIZE
        || next_group < 0
    ) {
        return false;
    }
    if (record->entry_count == 0) {
        return next_group == 0;
    }
    prior_group = permission_group(
        record->entries[record->entry_count - 1].permissions
    );
    return (
        next_group >= prior_group
        && next_group <= prior_group + 1
    );
}

bool micros_vm_handoff_record_entries_are_frozen_shape(
    const struct micros_vm_kernel_mapping_record *record
)
{
    bool saw_read_only = false;
    bool saw_writable = false;
    size_t image_page_count;
    size_t index;

    if (
        record->entry_count < MICROS_APPLICATION_IMAGE_PAGE_MIN + 1
        || record->entry_count > MICROS_APPLICATION_RESIDENT_PAGE_MAX
        || !record_entries_are_loading_shape(record)
    ) {
        return false;
    }
    image_page_count = record->entry_count - 1;
    if (
        image_page_count < MICROS_APPLICATION_IMAGE_PAGE_MIN
        || image_page_count > MICROS_APPLICATION_IMAGE_PAGE_MAX
        || record->entries[image_page_count].virtual_address
            != MICROS_APPLICATION_STACK_BASE
        || permission_group(record->entries[image_page_count - 1].permissions)
            != 2
    ) {
        return false;
    }
    for (index = 0; index < image_page_count; ++index) {
        int group = permission_group(record->entries[index].permissions);

        saw_read_only = saw_read_only || group == 1;
        saw_writable = saw_writable || group == 2;
    }
    return saw_read_only && saw_writable;
}

bool micros_vm_handoff_frozen_record_is_valid(
    const struct micros_vm_kernel_mapping_record *record
)
{
    size_t table_index;
    size_t entry_index;

    if (
        record->mapping_generation == 0
        || !micros_vm_handoff_address_is_page_aligned(record->root_physical_address)
        || record->root_physical_address != record->table_frames[0]
        || record->entry != MICROS_APPLICATION_ENTRY
        || !micros_vm_handoff_stack_pointer_is_valid(record->stack_pointer)
        || !micros_vm_handoff_record_entries_are_frozen_shape(record)
    ) {
        return false;
    }
    for (
        table_index = 0;
        table_index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
        ++table_index
    ) {
        if (!micros_vm_handoff_address_is_page_aligned(record->table_frames[table_index])) {
            return false;
        }
        for (entry_index = 0; entry_index < table_index; ++entry_index) {
            if (
                record->table_frames[entry_index]
                == record->table_frames[table_index]
            ) {
                return false;
            }
        }
        for (
            entry_index = 0;
            entry_index < record->entry_count;
            ++entry_index
        ) {
            if (
                record->entries[entry_index].physical_address
                == record->table_frames[table_index]
            ) {
                return false;
            }
        }
    }
    return true;
}

bool micros_vm_handoff_record_contains_physical_address(
    const struct micros_vm_kernel_mapping_record *record,
    uint64_t physical_address
)
{
    size_t index;

    for (index = 0; index < record->entry_count; ++index) {
        if (record->entries[index].physical_address == physical_address) {
            return true;
        }
    }
    if (record->phase == MICROS_VM_KERNEL_MAPPING_LOADING) {
        return false;
    }
    for (index = 0; index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++index) {
        if (record->table_frames[index] == physical_address) {
            return true;
        }
    }
    return false;
}

static bool record_overlaps_prior_records(
    const struct micros_vm_handoff_state *state,
    size_t slot
)
{
    const struct micros_vm_kernel_mapping_record *record =
        &state->mappings[slot];
    size_t prior_slot;
    size_t index;

    for (prior_slot = 0; prior_slot < slot; ++prior_slot) {
        const struct micros_vm_kernel_mapping_record *prior =
            &state->mappings[prior_slot];

        if (prior->phase == MICROS_VM_KERNEL_MAPPING_UNUSED) {
            continue;
        }
        for (index = 0; index < record->entry_count; ++index) {
            if (
                micros_vm_handoff_record_contains_physical_address(
                    prior,
                    record->entries[index].physical_address
                )
            ) {
                return true;
            }
        }
        if (record->phase != MICROS_VM_KERNEL_MAPPING_LOADING) {
            for (
                index = 0;
                index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
                ++index
            ) {
                if (
                    micros_vm_handoff_record_contains_physical_address(
                        prior,
                        record->table_frames[index]
                    )
                ) {
                    return true;
                }
            }
        }
    }
    return false;
}

static bool record_tokens_are_unique(
    const struct micros_vm_handoff_state *state,
    size_t slot
)
{
    const struct micros_vm_kernel_mapping_record *record =
        &state->mappings[slot];
    size_t prior_slot;

    for (prior_slot = 0; prior_slot < slot; ++prior_slot) {
        const struct micros_vm_kernel_mapping_record *prior =
            &state->mappings[prior_slot];

        if (prior->phase == MICROS_VM_KERNEL_MAPPING_UNUSED) {
            continue;
        }
        if (
            prior->load_token == record->load_token
            || (
                record->mapping_generation != 0
                && prior->mapping_generation
                    == record->mapping_generation
            )
            || (
                record->exit_token != 0
                && prior->exit_token == record->exit_token
            )
        ) {
            return false;
        }
    }
    return true;
}

static bool mapping_record_is_valid(
    const struct micros_vm_handoff_state *state,
    size_t slot
)
{
    const struct micros_vm_kernel_mapping_record *record =
        &state->mappings[slot];

    if (record->phase == MICROS_VM_KERNEL_MAPPING_UNUSED) {
        return micros_vm_handoff_mapping_record_is_zero(record);
    }
    if (
        record->entry_count > MICROS_APPLICATION_RESIDENT_PAGE_MAX
        || !micros_vm_handoff_process_is_valid(record->process)
        || record->process.slot != slot
        || micros_vm_handoff_process_handles_equal(record->process, state->process)
        || record->load_token == 0
        || record->load_token > state->last_load_token
        || !record_tokens_are_unique(state, slot)
        || record_overlaps_prior_records(state, slot)
    ) {
        return false;
    }
    if (record->phase == MICROS_VM_KERNEL_MAPPING_LOADING) {
        return (
            record->mapping_generation == 0
            && record->exit_token == 0
            && record->root_physical_address == 0
            && micros_vm_handoff_bytes_are_zero(
                record->table_frames,
                sizeof(record->table_frames)
            )
            && record->entry == 0
            && record->stack_pointer == 0
            && record_entries_are_loading_shape(record)
        );
    }
    if (
        record->phase != MICROS_VM_KERNEL_MAPPING_FROZEN
        && record->phase != MICROS_VM_KERNEL_MAPPING_PREPARED
        && record->phase != MICROS_VM_KERNEL_MAPPING_ACTIVATED_PENDING_COMMIT
        && record->phase != MICROS_VM_KERNEL_MAPPING_ACTIVE
        && record->phase != MICROS_VM_KERNEL_MAPPING_EXIT_DETACHED
    ) {
        return false;
    }
    return (
        record->mapping_generation <= state->last_mapping_generation
        && micros_vm_handoff_frozen_record_is_valid(record)
        && (
            (
                record->phase == MICROS_VM_KERNEL_MAPPING_EXIT_DETACHED
                && record->exit_token != 0
            )
            || (
                record->phase != MICROS_VM_KERNEL_MAPPING_EXIT_DETACHED
                && record->exit_token == 0
            )
        )
    );
}

static bool vm_handoff_identity_is_valid(
    const struct micros_vm_handoff_state *state
)
{
    return (
        state->initialization_magic != MICROS_VM_HANDOFF_MAGIC
        ? false
        : (
            (
                state->phase == MICROS_VM_HANDOFF_PHASE_PREPARED
                || state->phase
                    == MICROS_VM_HANDOFF_PHASE_HANDED_OFF
            )
            && state->service_id != 0
            && state->service_id <= 63
            && micros_vm_handoff_endpoint_matches_process(
                state->endpoint,
                state->process
            )
            && state->image_id != 0
            && state->profile_id != 0
            && state->profile_id < MICROS_PRIVILEGE_PROFILE_CAPACITY
            && micros_vm_handoff_thread_is_valid(state->thread)
            && state->root_physical_address != 0
            && state->root_physical_address % MICROS_FRAME_SIZE == 0
            && state->boot_info_address != 0
            && state->boot_info_address
                % MICROS_VM_BOOT_INFO_ALIGNMENT == 0
            && state->boot_info_size == MICROS_VM_BOOT_INFO_SIZE
            && state->reserved == 0
            && micros_vm_handoff_summary_is_valid(&state->summary)
            && state->last_mapping_generation
                <= state->last_load_token
        )
    );
}

static bool prepared_vm_handoff_state_is_valid(
    const struct micros_vm_handoff_state *state
)
{
    size_t slot;

    if (
        state->last_load_token != 0
        || state->last_mapping_generation != 0
        || !micros_vm_handoff_bytes_are_zero(
            &state->load,
            sizeof(state->load)
        )
    ) {
        return false;
    }
    for (slot = 0; slot < MICROS_PROCESS_CAPACITY; ++slot) {
        if (
            !micros_vm_handoff_mapping_record_is_zero(
                &state->mappings[slot]
            )
        ) {
            return false;
        }
    }
    return true;
}

static bool mapping_slots_are_valid(
    const struct micros_vm_handoff_state *state,
    size_t *loading_slot,
    size_t *frozen_slot
)
{
    size_t slot;

    *loading_slot = MICROS_PROCESS_CAPACITY;
    *frozen_slot = MICROS_PROCESS_CAPACITY;
    for (slot = 0; slot < MICROS_PROCESS_CAPACITY; ++slot) {
        const struct micros_vm_kernel_mapping_record *record =
            &state->mappings[slot];

        if (!mapping_record_is_valid(state, slot)) {
            return false;
        }
        if (record->phase == MICROS_VM_KERNEL_MAPPING_LOADING) {
            if (*loading_slot != MICROS_PROCESS_CAPACITY) {
                return false;
            }
            *loading_slot = slot;
        } else if (
            record->phase == MICROS_VM_KERNEL_MAPPING_FROZEN
            || record->phase == MICROS_VM_KERNEL_MAPPING_PREPARED
            || record->phase
                == MICROS_VM_KERNEL_MAPPING_ACTIVATED_PENDING_COMMIT
        ) {
            if (*frozen_slot != MICROS_PROCESS_CAPACITY) {
                return false;
            }
            *frozen_slot = slot;
        }
    }
    return true;
}

static bool active_load_identity_is_valid(
    const struct micros_vm_handoff_state *state
)
{
    return (
        (
            state->load.phase == MICROS_VM_LOAD_PHASE_LOADING
            || state->load.phase == MICROS_VM_LOAD_PHASE_FROZEN
        )
        && state->load.process_reservation_token != 0
        && state->load.load_token != 0
        && state->load.load_token <= state->last_load_token
        && micros_vm_handoff_process_is_valid(state->load.process)
        && !micros_vm_handoff_process_handles_equal(
            state->load.process,
            state->process
        )
        && state->load.reserved == 0
        && state->load.scratch_phase <= MICROS_VM_SCRATCH_MAPPED
    );
}

static bool loading_transaction_is_valid(
    const struct micros_vm_handoff_state *state,
    size_t loading_slot,
    size_t frozen_slot
)
{
    const struct micros_vm_kernel_mapping_record *record;
    size_t slot;

    if (
        loading_slot != state->load.process.slot
        || frozen_slot != MICROS_PROCESS_CAPACITY
    ) {
        return false;
    }
    record = &state->mappings[loading_slot];
    if (
        !micros_vm_handoff_process_handles_equal(
            record->process,
            state->load.process
        )
        || record->load_token != state->load.load_token
    ) {
        return false;
    }
    if (state->load.scratch_phase == MICROS_VM_SCRATCH_CLEAR) {
        return micros_vm_handoff_bytes_are_zero(
            &state->load.scratch,
            sizeof(state->load.scratch)
        );
    }
    if (
        !micros_vm_handoff_loading_record_can_append(
            record,
            &state->load.scratch
        )
    ) {
        return false;
    }
    for (slot = 0; slot < MICROS_PROCESS_CAPACITY; ++slot) {
        if (
            micros_vm_handoff_record_contains_physical_address(
                &state->mappings[slot],
                state->load.scratch.physical_address
            )
        ) {
            return false;
        }
    }
    return true;
}

static bool frozen_transaction_is_valid(
    const struct micros_vm_handoff_state *state,
    size_t loading_slot,
    size_t frozen_slot
)
{
    return (
        state->load.scratch_phase == MICROS_VM_SCRATCH_CLEAR
        && micros_vm_handoff_bytes_are_zero(
            &state->load.scratch,
            sizeof(state->load.scratch)
        )
        && loading_slot == MICROS_PROCESS_CAPACITY
        && frozen_slot == state->load.process.slot
        && micros_vm_handoff_process_handles_equal(
            state->mappings[frozen_slot].process,
            state->load.process
        )
        && state->mappings[frozen_slot].load_token
            == state->load.load_token
    );
}

enum micros_vm_handoff_error micros_vm_handoff_state_validate(
    const struct micros_vm_handoff_state *state
)
{
    size_t loading_slot;
    size_t frozen_slot;

    if (
        state == NULL
        || !micros_vm_handoff_pointer_is_aligned(
            state,
            _Alignof(struct micros_vm_handoff_state)
        )
    ) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    if (!vm_handoff_identity_is_valid(state)) {
        return MICROS_VM_HANDOFF_ERROR_INVARIANT;
    }
    if (state->phase == MICROS_VM_HANDOFF_PHASE_PREPARED) {
        return prepared_vm_handoff_state_is_valid(state)
            ? MICROS_VM_HANDOFF_OK
            : MICROS_VM_HANDOFF_ERROR_INVARIANT;
    }
    if (
        !mapping_slots_are_valid(
            state,
            &loading_slot,
            &frozen_slot
        )
    ) {
        return MICROS_VM_HANDOFF_ERROR_INVARIANT;
    }
    if (state->load.phase == MICROS_VM_LOAD_PHASE_IDLE) {
        return (
            micros_vm_handoff_bytes_are_zero(
                &state->load,
                sizeof(state->load)
            )
            && loading_slot == MICROS_PROCESS_CAPACITY
            && frozen_slot == MICROS_PROCESS_CAPACITY
        )
            ? MICROS_VM_HANDOFF_OK
            : MICROS_VM_HANDOFF_ERROR_INVARIANT;
    }
    if (!active_load_identity_is_valid(state)) {
        return MICROS_VM_HANDOFF_ERROR_INVARIANT;
    }
    if (state->load.phase == MICROS_VM_LOAD_PHASE_LOADING) {
        return loading_transaction_is_valid(
            state,
            loading_slot,
            frozen_slot
        )
            ? MICROS_VM_HANDOFF_OK
            : MICROS_VM_HANDOFF_ERROR_INVARIANT;
    }
    return frozen_transaction_is_valid(
        state,
        loading_slot,
        frozen_slot
    )
        ? MICROS_VM_HANDOFF_OK
        : MICROS_VM_HANDOFF_ERROR_INVARIANT;
}

static uint64_t page_digest_byte(uint64_t digest, uint8_t byte)
{
    digest ^= byte;
    return digest * MICROS_VM_PAGE_DIGEST_PRIME;
}

static uint64_t page_digest_u32(uint64_t digest, uint32_t value)
{
    size_t index;

    for (index = 0; index < 4; ++index) {
        digest = page_digest_byte(digest, (uint8_t)(value >> (index * 8)));
    }
    return digest;
}

static uint64_t page_digest_u64(uint64_t digest, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        digest = page_digest_byte(digest, (uint8_t)(value >> (index * 8)));
    }
    return digest;
}

uint64_t micros_vm_load_page_plan_digest(
    uint32_t image_page_count,
    uint32_t data_page_count,
    const struct micros_vm_kernel_mapping_entry *entries
)
{
    uint64_t digest = MICROS_VM_PAGE_DIGEST_OFFSET;
    size_t index;

    if (
        entries == NULL
        || !micros_vm_handoff_pointer_is_aligned(
            entries,
            _Alignof(struct micros_vm_kernel_mapping_entry)
        )
        || image_page_count < MICROS_APPLICATION_IMAGE_PAGE_MIN
        || image_page_count > MICROS_APPLICATION_IMAGE_PAGE_MAX
        || data_page_count != image_page_count + 1
        || data_page_count > MICROS_APPLICATION_RESIDENT_PAGE_MAX
    ) {
        return 0;
    }
    digest = page_digest_u32(digest, image_page_count);
    digest = page_digest_u32(digest, data_page_count);
    for (index = 0; index < data_page_count; ++index) {
        digest = page_digest_u64(digest, entries[index].virtual_address);
        digest = page_digest_u64(digest, entries[index].physical_address);
        digest = page_digest_u32(digest, entries[index].permissions);
        digest = page_digest_u32(digest, 0);
    }
    return digest;
}
