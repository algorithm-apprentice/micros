#include "servers/vm/vm_internal.h"
#include "lib/vm/vm_boot_internal.h"
#include "lib/vm/vm_keys.h"
#include "tests/qemu/vm_validation_work.h"

#include "micros/endpoint.h"
#include "micros/sv39.h"

static uint64_t vm_validation_digest_bytes(
    uint64_t digest,
    const uint8_t *bytes,
    size_t size
)
{
    while (size >= 8) {
        digest = (digest ^ bytes[0]) * MICROS_VM_PAGE_DIGEST_PRIME;
        digest = (digest ^ bytes[1]) * MICROS_VM_PAGE_DIGEST_PRIME;
        digest = (digest ^ bytes[2]) * MICROS_VM_PAGE_DIGEST_PRIME;
        digest = (digest ^ bytes[3]) * MICROS_VM_PAGE_DIGEST_PRIME;
        digest = (digest ^ bytes[4]) * MICROS_VM_PAGE_DIGEST_PRIME;
        digest = (digest ^ bytes[5]) * MICROS_VM_PAGE_DIGEST_PRIME;
        digest = (digest ^ bytes[6]) * MICROS_VM_PAGE_DIGEST_PRIME;
        digest = (digest ^ bytes[7]) * MICROS_VM_PAGE_DIGEST_PRIME;
        bytes += 8;
        size -= 8;
    }
    while (size != 0) {
        digest = (digest ^ *bytes++) * MICROS_VM_PAGE_DIGEST_PRIME;
        --size;
    }
    return digest;
}

static uint64_t vm_validation_digest_zero_bytes(
    uint64_t digest,
    size_t zero_count
)
{
    uint64_t factor = MICROS_VM_PAGE_DIGEST_PRIME;

    while (zero_count != 0) {
        if ((zero_count & 1) != 0) {
            digest *= factor;
        }
        factor *= factor;
        zero_count >>= 1;
    }
    return digest;
}

static uint64_t vm_validation_digest_active_frame_states(
    const struct micros_vm_state *state,
    size_t managed_frame_count,
    uint64_t digest
)
{
    for (size_t frame_index = 0; frame_index < managed_frame_count; ++frame_index) {
        uint8_t local = state->boot_info->frame_states[frame_index];
        uint8_t byte = local >= MICROS_VM_LOCAL_FRAME_TRANSFERABLE
                && local <= MICROS_VM_LOCAL_FRAME_PROCESS_PAGE_TABLE
            ? MICROS_VM_FRAME_FREE : local;

        digest = (digest ^ byte) * MICROS_VM_PAGE_DIGEST_PRIME;
    }
    return digest;
}

static bool vm_validation_free_tail_is_zero(
    const struct micros_vm_state *state,
    size_t managed_frame_count
)
{
    size_t first_word = managed_frame_count / 64;
    size_t boundary_bit = managed_frame_count % 64;

    if (boundary_bit != 0) {
        uint64_t inactive_mask = UINT64_MAX << boundary_bit;

        if (
            (
                state->free_bitmap[first_word]
                & inactive_mask
            ) != 0
        ) {
            return false;
        }
        ++first_word;
    }
    for (
        ;
        first_word < MICROS_VM_FREE_BITMAP_WORDS;
        ++first_word
    ) {
        if (state->free_bitmap[first_word] != 0) {
            return false;
        }
    }
    return true;
}

static bool vm_core_boot_header_matches_summary(
    const struct micros_vm_state *state
)
{
    const struct micros_vm_boot_header *header =
        &state->boot_info->header;
    const struct micros_vm_boot_summary *summary =
        &state->boot_summary;

    return (
        header->version == summary->version
        && header->managed_range_count
            == summary->managed_range_count
        && header->address_space_count
            == summary->address_space_count
        && header->mapping_count == summary->mapping_count
        && header->managed_frame_count
            == summary->managed_frame_count
        && header->free_frame_count == summary->free_frame_count
        && header->vm_self_wired_frame_count
            == summary->vm_self_wired_frame_count
        && header->digest == summary->digest
    );
}

static bool vm_core_boot_database_is_immutable(
    const struct micros_vm_state *state
)
{
    const uint8_t *bytes = (const uint8_t *)state->boot_info;
    uint64_t managed_frame_count =
        state->boot_summary.managed_frame_count;
    size_t digest_offset =
        offsetof(struct micros_vm_boot_info, header.digest);
    size_t frame_state_offset =
        offsetof(struct micros_vm_boot_info, frame_states);
    uint64_t digest = MICROS_VM_PAGE_DIGEST_OFFSET;
    size_t active_frame_count;
    size_t inactive_frame_count;

    if (
        managed_frame_count == 0
        || managed_frame_count > MICROS_VM_MAX_MANAGED_FRAMES
    ) {
        return false;
    }
    active_frame_count = (size_t)managed_frame_count;
    inactive_frame_count =
        MICROS_VM_MAX_MANAGED_FRAMES - active_frame_count;

    digest = vm_validation_digest_bytes(
        digest,
        bytes,
        digest_offset
    );
    digest = vm_validation_digest_zero_bytes(
        digest,
        sizeof(state->boot_info->header.digest)
    );
    digest = vm_validation_digest_bytes(
        digest,
        &bytes[
            digest_offset
            + sizeof(state->boot_info->header.digest)
        ],
        frame_state_offset
            - digest_offset
            - sizeof(state->boot_info->header.digest)
    );
    digest = vm_validation_digest_active_frame_states(
        state,
        active_frame_count,
        digest
    );
    if (
        !vm_core_bytes_are_zero(
            &state->boot_info->frame_states[active_frame_count],
            inactive_frame_count
        )
        || !vm_validation_free_tail_is_zero(
            state,
            active_frame_count
        )
    ) {
        return false;
    }
    digest = vm_validation_digest_zero_bytes(
        digest,
        inactive_frame_count
    );
    return digest == state->boot_summary.digest;
}

static bool vm_core_transaction_buffers_are_canonical(
    const struct micros_vm_state *state
)
{
    return (
        state->transaction.executable_size
            <= sizeof(state->executable_bytes)
        && state->transaction.request_size
            <= sizeof(state->request_bytes)
        && vm_core_bytes_are_zero(
            &state->executable_bytes[
                state->transaction.executable_size
            ],
            sizeof(state->executable_bytes)
                - state->transaction.executable_size
        )
        && vm_core_bytes_are_zero(
            &state->request_bytes[
                state->transaction.request_size
            ],
            sizeof(state->request_bytes)
                - state->transaction.request_size
        )
    );
}

static bool vm_core_transaction_plans_are_canonical(
    const struct micros_vm_state *state
)
{
    struct micros_vm_elf_plan elf_plan;
    struct micros_application_stack_plan stack_plan;

    if (
        micros_vm_elf_plan(
            state->executable_bytes,
            state->transaction.executable_size,
            &elf_plan
        ) != MICROS_VM_ELF_OK
        || micros_application_stack_plan(
            state->request_bytes,
            state->transaction.request_size,
            &stack_plan
        ) != MICROS_APPLICATION_STACK_OK
    ) {
        return false;
    }
    return (
        vm_core_bytes_equal(
            &elf_plan,
            &state->transaction.elf_plan,
            sizeof(elf_plan)
        )
        && vm_core_stack_plan_equal(
            &stack_plan,
            &state->transaction.stack_plan
        )
    );
}

static bool vm_core_transaction_header_is_canonical(
    const struct micros_vm_state *state
)
{
    const struct micros_vm_service_load_transaction *transaction =
        &state->transaction;

    if (
        transaction->process_reservation_token == 0
        || transaction->semantic_pid == 0
        || !vm_core_endpoint_is_canonical(
            transaction->stack_values.pm_endpoint
        )
        || !vm_core_endpoint_is_canonical(
            transaction->stack_values.vfs_endpoint
        )
        || transaction->stack_values.pid
            != transaction->semantic_pid
        || transaction->executable_size == 0
        || transaction->executable_size
            > MICROS_APPLICATION_EXECUTABLE_SIZE_MAX
        || transaction->request_size
            < MICROS_SPAWN_REQUEST_SIZE_MIN
        || transaction->request_size > MICROS_SPAWN_REQUEST_SIZE_MAX
        || transaction->elf_plan.page_count
            < MICROS_APPLICATION_IMAGE_PAGE_MIN
        || transaction->elf_plan.page_count
            > MICROS_APPLICATION_IMAGE_PAGE_MAX
        || transaction->page_count
            != transaction->elf_plan.page_count + 1
        || transaction->page_count
            > MICROS_APPLICATION_RESIDENT_PAGE_MAX
        || transaction->completed_page_count
            > transaction->page_count
        || transaction->scratch_page > transaction->page_count
        || (
            transaction->scratch_page != 0
            && transaction->scratch_page
                != transaction->completed_page_count + 1
        )
        || transaction->scratch_filled > 1
        || (
            transaction->scratch_page == 0
            && transaction->scratch_filled != 0
        )
    ) {
        return false;
    }
    return true;
}

static bool vm_core_transaction_frames_are_ordered(
    const struct micros_vm_state *state
)
{
    const struct micros_vm_service_load_transaction *transaction = &state->transaction;
    size_t index;

    for (
        index = 1;
        index < MICROS_VM_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        if (
            transaction->table_frames[index - 1]
                >= transaction->table_frames[index]
        ) {
            return false;
        }
    }
    if (
        transaction->table_frames[
            MICROS_VM_PAGE_TABLE_FRAME_COUNT - 1
        ] >= transaction->data_frames[0]
    ) {
        return false;
    }
    for (index = 1; index < transaction->page_count; ++index) {
        if (
            transaction->data_frames[index - 1]
                >= transaction->data_frames[index]
        ) {
            return false;
        }
    }
    for (
        index = transaction->page_count;
        index < MICROS_APPLICATION_RESIDENT_PAGE_MAX;
        ++index
    ) {
        if (transaction->data_frames[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool vm_core_transaction_common_is_canonical(
    const struct micros_vm_state *state
)
{
    return vm_core_transaction_header_is_canonical(state)
        && vm_core_transaction_plans_are_canonical(state)
        && vm_core_transaction_frames_are_ordered(state);
}

static bool vm_core_record_matches_transaction(
    const struct micros_vm_state *state,
    const struct micros_vm_mapping_record *record
)
{
    const struct micros_vm_service_load_transaction *transaction =
        &state->transaction;
    static const uint8_t table_roles[MICROS_VM_PAGE_TABLE_FRAME_COUNT] = {
        MICROS_VM_MAPPING_ENTRY_ROOT_PAGE_TABLE,
        MICROS_VM_MAPPING_ENTRY_LEVEL1_PAGE_TABLE,
        MICROS_VM_MAPPING_ENTRY_IMAGE_PAGE_TABLE,
        MICROS_VM_MAPPING_ENTRY_STACK_PAGE_TABLE,
    };
    size_t index;

    if (
        record->load_token != transaction->load_token
        || record->mapping_generation
            != transaction->mapping_generation
        || record->semantic_pid != transaction->semantic_pid
        || record->process_generation
            != transaction->process.generation
        || record->process_slot != transaction->process.slot
        || record->entry_count
            != MICROS_VM_PAGE_TABLE_FRAME_COUNT
                + transaction->page_count
    ) {
        return false;
    }
    for (
        index = 0;
        index < MICROS_VM_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        const struct micros_vm_mapping_entry *entry =
            &record->entries[index];

        if (
            entry->virtual_address != 0
            || entry->physical_address
                != transaction->table_frames[index]
            || entry->permissions != 0
            || entry->role != table_roles[index]
            || !vm_core_bytes_are_zero(
                entry->reserved,
                sizeof(entry->reserved)
            )
        ) {
            return false;
        }
    }
    for (index = 0; index < transaction->page_count; ++index) {
        const struct micros_vm_mapping_entry *entry =
            &record->entries[
                MICROS_VM_PAGE_TABLE_FRAME_COUNT + index
            ];
        uint64_t virtual_address = index
                < transaction->elf_plan.page_count
            ? transaction->elf_plan.pages[index].virtual_address
            : MICROS_APPLICATION_STACK_BASE;
        uint32_t permissions = index
                < transaction->elf_plan.page_count
            ? transaction->elf_plan.pages[index].permissions
            : MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE;

        if (
            entry->virtual_address != virtual_address
            || entry->physical_address
                != transaction->data_frames[index]
            || entry->permissions != permissions
            || entry->role != MICROS_VM_MAPPING_ENTRY_USER_PAGE
            || !vm_core_bytes_are_zero(
                entry->reserved,
                sizeof(entry->reserved)
            )
        ) {
            return false;
        }
    }
    return true;
}

struct vm_validation_counts {
    uint64_t local_load_reserved;
    uint64_t local_transferable;
    uint64_t local_process_wired;
    uint64_t local_process_table;
    uint64_t record_process_wired;
    uint64_t record_process_table;
    size_t loading_records;
    size_t frozen_records;
    size_t exit_records;
    size_t loading_slot;
    size_t frozen_slot;
};

static bool validate_local_frame_states(
    const struct micros_vm_state *state,
    struct vm_validation_counts *counts
)
{
    uint64_t frame_index;

    for (
        frame_index = 0;
        frame_index < state->boot_summary.managed_frame_count;
        ++frame_index
    ) {
        uint8_t local = state->boot_info->frame_states[frame_index];

        if (!vm_core_frame_availability_is_valid(state, frame_index)) {
            return false;
        }
        if (!vm_core_initial_frame_is_free(state, frame_index)) {
            if (
                local != MICROS_VM_FRAME_KERNEL
                && local != MICROS_VM_FRAME_SELF_WIRED
                && local != MICROS_VM_FRAME_SERVICE_WIRED
            ) {
                return false;
            }
            continue;
        }
        switch (local) {
        case MICROS_VM_LOCAL_FRAME_FREE:
            break;
        case MICROS_VM_LOCAL_FRAME_LOAD_RESERVED:
            ++counts->local_load_reserved;
            break;
        case MICROS_VM_LOCAL_FRAME_TRANSFERABLE:
            ++counts->local_transferable;
            break;
        case MICROS_VM_LOCAL_FRAME_PROCESS_WIRED:
            ++counts->local_process_wired;
            break;
        case MICROS_VM_LOCAL_FRAME_PROCESS_PAGE_TABLE:
            ++counts->local_process_table;
            break;
        default:
            return false;
        }
    }
    return vm_core_bytes_are_zero(
        &state->boot_info->frame_states[
            state->boot_summary.managed_frame_count
        ],
        sizeof(state->boot_info->frame_states)
            - state->boot_summary.managed_frame_count
    );
}

static bool validate_table_entry(
    const struct micros_vm_state *state,
    const struct micros_vm_mapping_record *record,
    size_t entry_index,
    uint64_t frame_index
)
{
    static const uint8_t roles[MICROS_VM_PAGE_TABLE_FRAME_COUNT] = {
        MICROS_VM_MAPPING_ENTRY_ROOT_PAGE_TABLE,
        MICROS_VM_MAPPING_ENTRY_LEVEL1_PAGE_TABLE,
        MICROS_VM_MAPPING_ENTRY_IMAGE_PAGE_TABLE,
        MICROS_VM_MAPPING_ENTRY_STACK_PAGE_TABLE,
    };
    const struct micros_vm_mapping_entry *entry =
        &record->entries[entry_index];
    uint8_t expected_state =
        record->phase == MICROS_VM_MAPPING_RECORD_LOADING
            ? MICROS_VM_LOCAL_FRAME_LOAD_RESERVED
            : MICROS_VM_LOCAL_FRAME_PROCESS_PAGE_TABLE;

    if (
        entry->virtual_address != 0
        || entry->permissions != 0
        || entry->role != roles[entry_index]
        || state->boot_info->frame_states[frame_index]
            != expected_state
    ) {
        return false;
    }
    return true;
}

static bool validate_data_entry(
    const struct micros_vm_state *state,
    const struct micros_vm_mapping_record *record,
    size_t entry_index,
    uint64_t frame_index
)
{
    const struct micros_vm_mapping_entry *entry =
        &record->entries[entry_index];
    size_t data_index = entry_index - MICROS_VM_PAGE_TABLE_FRAME_COUNT;
    size_t data_count = record->entry_count - MICROS_VM_PAGE_TABLE_FRAME_COUNT;
    uint64_t expected_address = data_index + 1 == data_count
        ? MICROS_APPLICATION_STACK_BASE
        : MICROS_APPLICATION_ENTRY + data_index * MICROS_FRAME_SIZE;
    uint32_t stack_permissions =
        MICROS_SV39_PERMISSION_USER
        | MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE;
    uint8_t expected_state = MICROS_VM_LOCAL_FRAME_PROCESS_WIRED;

    if (
        entry->virtual_address != expected_address
        || entry->role != MICROS_VM_MAPPING_ENTRY_USER_PAGE
        || !vm_core_user_permissions_are_canonical(entry->permissions)
        || (
            data_index + 1 == data_count
            && entry->permissions != stack_permissions
        )
    ) {
        return false;
    }
    if (record->phase == MICROS_VM_MAPPING_RECORD_LOADING) {
        expected_state =
            data_index < state->transaction.completed_page_count
                || state->transaction.scratch_page == data_index + 1
                ? MICROS_VM_LOCAL_FRAME_TRANSFERABLE
                : MICROS_VM_LOCAL_FRAME_LOAD_RESERVED;
    }
    return state->boot_info->frame_states[frame_index] == expected_state;
}

static bool validate_record_entries(
    const struct micros_vm_state *state,
    size_t slot,
    struct vm_validation_counts *counts
)
{
    const struct micros_vm_mapping_record *record =
        &state->mapping_records[slot];
    size_t entry_index;

    for (entry_index = 0; entry_index < record->entry_count; ++entry_index) {
        const struct micros_vm_mapping_entry *entry =
            &record->entries[entry_index];
        uint64_t frame_index;
        bool role_is_valid;

        if (
            entry->physical_address % MICROS_FRAME_SIZE != 0
            || !vm_core_physical_to_frame_index(
                state, entry->physical_address, &frame_index
            )
            || !vm_core_initial_frame_is_free(state, frame_index)
            || !vm_core_frame_availability_is_valid(state, frame_index)
            || !vm_core_bytes_are_zero(entry->reserved, sizeof(entry->reserved))
            || (
                entry_index != 0
                && record->entries[entry_index - 1].physical_address
                    >= entry->physical_address
            )
        ) {
            return false;
        }
        role_is_valid = entry_index < MICROS_VM_PAGE_TABLE_FRAME_COUNT
            ? validate_table_entry(state, record, entry_index, frame_index)
            : validate_data_entry(state, record, entry_index, frame_index);
        if (!role_is_valid) {
            return false;
        }
        if (record->phase != MICROS_VM_MAPPING_RECORD_LOADING) {
            if (entry_index < MICROS_VM_PAGE_TABLE_FRAME_COUNT) {
                ++counts->record_process_table;
            } else {
                ++counts->record_process_wired;
            }
        }
    }
    return vm_core_bytes_are_zero(
        &record->entries[record->entry_count],
        (MICROS_VM_MAPPING_ENTRY_CAPACITY - record->entry_count)
            * sizeof(record->entries[0])
    );
}

static bool finish_mapping_key_check(struct micros_vm_key_view *keys)
{
    bool unique = micros_vm_keys_sort(keys);

    if (unique) {
        for (size_t index = 1; index < keys->count; ++index) {
            if (keys->keys64[index - 1] == keys->keys64[index]) {
                unique = false;
                break;
            }
        }
    }
    return micros_vm_keys_end(keys) && unique;
}

static bool mapping_keys_are_unique(const struct micros_vm_state *state)
{
    struct micros_vm_key_view keys;

    for (unsigned domain = 0; domain < 2; ++domain) {
        if (!micros_vm_keys_begin64(&keys)) {
            return false;
        }
        for (size_t slot = 0; slot < MICROS_VM_MAPPING_RECORD_CAPACITY; ++slot) {
            const struct micros_vm_mapping_record *record = &state->mapping_records[slot];

            if (record->phase == MICROS_VM_MAPPING_RECORD_UNUSED) {
                continue;
            }
            if (domain == 0) {
                for (size_t index = 0; index < record->entry_count; ++index) {
                    if (!micros_vm_keys_append64(&keys, record->entries[index].physical_address)) {
                        goto invalid;
                    }
                }
            } else if (!micros_vm_keys_append64(
                &keys, record->semantic_pid
            )) {
                goto invalid;
            }
        }
        if (!finish_mapping_key_check(&keys)) {
            return false;
        }
    }
    return true;
invalid:
    (void)micros_vm_keys_end(&keys);
    return false;
}

static bool validate_mapping_records(
    const struct micros_vm_state *state,
    struct vm_validation_counts *counts
)
{
    size_t slot;

    for (slot = 0; slot < MICROS_VM_MAPPING_RECORD_CAPACITY; ++slot) {
        const struct micros_vm_mapping_record *record =
            &state->mapping_records[slot];

        if (vm_core_record_is_zero(record)) {
            continue;
        }
        if (
            !vm_core_record_header_is_valid(state, slot)
            || record->phase == MICROS_VM_MAPPING_RECORD_UNUSED
            || !validate_record_entries(state, slot, counts)
        ) {
            return false;
        }
        switch (record->phase) {
        case MICROS_VM_MAPPING_RECORD_LOADING:
            ++counts->loading_records;
            counts->loading_slot = slot;
            break;
        case MICROS_VM_MAPPING_RECORD_FROZEN:
            ++counts->frozen_records;
            counts->frozen_slot = slot;
            break;
        case MICROS_VM_MAPPING_RECORD_ACTIVE:
            break;
        case MICROS_VM_MAPPING_RECORD_EXIT_RELEASING:
            ++counts->exit_records;
            break;
        default:
            return false;
        }
    }
    return mapping_keys_are_unique(state);
}

static bool validate_idle_phase(
    const struct micros_vm_state *state,
    const struct vm_validation_counts *counts,
    bool allow_staged_inputs
)
{
    return (
        counts->loading_records == 0
        && counts->frozen_records == 0
        && counts->exit_records == 0
        && counts->local_load_reserved == 0
        && counts->local_transferable == 0
        && vm_core_bytes_are_zero(&state->transaction, sizeof(state->transaction))
        && (
            allow_staged_inputs
            || (
                vm_core_bytes_are_zero(
                    state->executable_bytes, sizeof(state->executable_bytes)
                )
                && vm_core_bytes_are_zero(
                    state->request_bytes, sizeof(state->request_bytes)
                )
            )
        )
    );
}

static bool validate_input_copied_phase(
    const struct micros_vm_state *state,
    const struct vm_validation_counts *counts
)
{
    return (
        counts->loading_records == 0
        && counts->frozen_records == 0
        && counts->exit_records == 0
        && counts->local_load_reserved == MICROS_VM_PAGE_TABLE_FRAME_COUNT + state->transaction.page_count
        && counts->local_transferable == 0
        && state->transaction.load_token == 0
        && state->transaction.mapping_generation == 0
        && state->transaction.process.slot == 0
        && state->transaction.process.generation == 0
        && state->transaction.completed_page_count == 0
        && state->transaction.scratch_page == 0
        && vm_core_transaction_frames_match(state)
    );
}

static bool validate_loading_phase(
    const struct micros_vm_state *state,
    const struct vm_validation_counts *counts
)
{
    return (
        counts->loading_records == 1
        && counts->frozen_records == 0
        && counts->exit_records == 0
        && counts->local_load_reserved == MICROS_VM_PAGE_TABLE_FRAME_COUNT + state->transaction.page_count
            - state->transaction.completed_page_count - (state->transaction.scratch_page == 0 ? 0 : 1)
        && counts->local_transferable
            == state->transaction.completed_page_count
                + (state->transaction.scratch_page == 0 ? 0 : 1)
        && vm_core_process_is_canonical(state->transaction.process)
        && state->transaction.load_token != 0
        && state->transaction.mapping_generation == 0
        && counts->loading_slot == state->transaction.process.slot
        && vm_core_record_matches_transaction(
            state, &state->mapping_records[counts->loading_slot]
        )
        && vm_core_transaction_frames_match(state)
    );
}

static bool validate_frozen_phase(
    const struct micros_vm_state *state,
    const struct vm_validation_counts *counts
)
{
    return (
        counts->loading_records == 0
        && counts->frozen_records == 1
        && counts->exit_records == 0
        && counts->local_load_reserved == 0
        && counts->local_transferable == 0
        && vm_core_process_is_canonical(state->transaction.process)
        && state->transaction.load_token != 0
        && state->transaction.mapping_generation != 0
        && state->transaction.completed_page_count == state->transaction.page_count
        && state->transaction.scratch_page == 0
        && counts->frozen_slot == state->transaction.process.slot
        && vm_core_record_matches_transaction(
            state, &state->mapping_records[counts->frozen_slot]
        )
        && vm_core_transaction_frames_match(state)
    );
}

static bool validate_exit_releasing_phase(
    const struct micros_vm_state *state,
    const struct vm_validation_counts *counts
)
{
    return (
        counts->loading_records == 0
        && counts->frozen_records == 0
        && counts->exit_records == 1
        && counts->local_load_reserved == 0
        && counts->local_transferable == 0
        && vm_core_bytes_are_zero(&state->transaction, sizeof(state->transaction))
        && vm_core_bytes_are_zero(
            state->executable_bytes, sizeof(state->executable_bytes)
        )
        && vm_core_bytes_are_zero(state->request_bytes, sizeof(state->request_bytes))
    );
}

static bool validate_loader_phase(
    const struct micros_vm_state *state,
    const struct vm_validation_counts *counts,
    bool allow_staged_idle_inputs
)
{
    if (
        counts->local_process_wired != counts->record_process_wired
        || counts->local_process_table != counts->record_process_table
    ) {
        return false;
    }
    switch (state->phase) {
    case MICROS_VM_LOADER_IDLE:
        return validate_idle_phase(state, counts, allow_staged_idle_inputs);
    case MICROS_VM_LOADER_INPUT_COPIED:
        return validate_input_copied_phase(state, counts);
    case MICROS_VM_LOADER_LOADING:
        return validate_loading_phase(state, counts);
    case MICROS_VM_LOADER_FROZEN:
        return validate_frozen_phase(state, counts);
    case MICROS_VM_LOADER_EXIT_RELEASING:
        return validate_exit_releasing_phase(state, counts);
    default:
        return false;
    }
}

static bool cached_frames_are_valid(const struct micros_vm_state *state)
{
    for (size_t index = 0; index < state->free_page_cache_count; ++index) {
        if (!vm_core_initial_frame_is_free(state, state->free_page_cache[index])) {
            return false;
        }
    }
    return true;
}

enum micros_vm_core_error vm_core_validate_state(
    const struct micros_vm_state *state,
    bool allow_staged_idle_inputs
)
{
    MICROS_VM01_WORK_HIT("vm01-service-state", MICROS_WORK_OWNER_VM_SERVICE,
                        MICROS_WORK_KIND_COMPLETE_VALIDATOR_CALL, 1);
    struct vm_validation_counts counts = {
        .loading_slot = MICROS_VM_MAPPING_RECORD_CAPACITY,
        .frozen_slot = MICROS_VM_MAPPING_RECORD_CAPACITY,
    };

    if (state == NULL) {
        return MICROS_VM_CORE_ERROR_ARGUMENT;
    }
    if (
        state->initialization_magic != MICROS_VM_INITIALIZATION_MAGIC
        || state->reserved != 0
        || state->boot_info == NULL
        || vm_core_ranges_overlap(
            state, sizeof(*state), state->boot_info, sizeof(*state->boot_info)
        )
        || !vm_core_boot_header_matches_summary(state)
        || !vm_core_allocator_metadata_is_valid(state)
        || !vm_core_boot_database_is_immutable(state)
        || state->phase < MICROS_VM_LOADER_IDLE
        || state->phase > MICROS_VM_LOADER_EXIT_RELEASING
    ) {
        return MICROS_VM_CORE_ERROR_INVARIANT;
    }
    if (
        (
            state->phase == MICROS_VM_LOADER_INPUT_COPIED
            || state->phase == MICROS_VM_LOADER_LOADING
            || state->phase == MICROS_VM_LOADER_FROZEN
        )
        && (
            !vm_core_transaction_buffers_are_canonical(state)
            || !vm_core_transaction_common_is_canonical(state)
        )
    ) {
        return MICROS_VM_CORE_ERROR_INVARIANT;
    }
    if (
        !validate_local_frame_states(state, &counts)
        || !cached_frames_are_valid(state)
        || !validate_mapping_records(state, &counts)
        || !validate_loader_phase(state, &counts, allow_staged_idle_inputs)
    ) {
        return MICROS_VM_CORE_ERROR_INVARIANT;
    }
    return MICROS_VM_CORE_OK;
}

enum micros_vm_core_error micros_vm_state_validate(
    const struct micros_vm_state *state
)
{
    return vm_core_validate_state(state, false);
}

static bool selected_geometry_is_valid(const struct micros_vm_state *state)
{
    const struct micros_vm_boot_header *header = &state->boot_info->header;
    uint64_t total = 0;
    uint64_t previous_end = 0;

    if (!vm_boot_header_is_valid(state->boot_info)
        || !vm_core_boot_header_matches_summary(state)
        || header->free_frame_count > header->managed_frame_count
        || header->kernel_frame_count > header->managed_frame_count
        || header->vm_self_wired_frame_count == 0
        || header->vm_self_wired_frame_count > header->managed_frame_count
        || header->service_wired_frame_count > header->managed_frame_count
        || header->transferable_frame_count != 0
        || header->free_frame_count + header->kernel_frame_count
            + header->vm_self_wired_frame_count + header->service_wired_frame_count
            != header->managed_frame_count
        || header->mapping_count != header->vm_self_wired_frame_count + header->service_wired_frame_count) {
        return false;
    }
    for (size_t index = 0; index < header->managed_range_count; ++index) {
        const struct micros_vm_managed_range *range = &state->boot_info->managed_ranges[index];

        if (range->base % MICROS_FRAME_SIZE != 0 || range->frame_count == 0
            || range->frame_count > (UINT64_MAX - range->base) / MICROS_FRAME_SIZE
            || range->frame_index != total
            || range->frame_count > header->managed_frame_count - total
            || (index != 0 && range->base < previous_end)) {
            return false;
        }
        total += range->frame_count;
        previous_end = range->base + range->frame_count * MICROS_FRAME_SIZE;
    }
    return total == header->managed_frame_count;
}

bool vm_core_selected_record_is_valid(const struct micros_vm_state *state, size_t slot)
{
    const struct micros_vm_mapping_record *record;

    if (!vm_core_record_header_is_valid(state, slot)) {
        return false;
    }
    record = &state->mapping_records[slot];
    if (record->phase == MICROS_VM_MAPPING_RECORD_UNUSED) {
        return vm_core_record_is_zero(record);
    }
    for (size_t index = 0; index < record->entry_count; ++index) {
        const struct micros_vm_mapping_entry *entry = &record->entries[index];
        uint64_t frame_index;

        if (!vm_core_physical_to_frame_index(state, entry->physical_address, &frame_index)
            || !vm_core_initial_frame_is_free(state, frame_index)
            || !vm_core_frame_availability_is_valid(state, frame_index)
            || !vm_core_bytes_are_zero(entry->reserved, sizeof(entry->reserved))
            || (index != 0 && record->entries[index - 1].physical_address >= entry->physical_address)
            || !(index < MICROS_VM_PAGE_TABLE_FRAME_COUNT
                 ? validate_table_entry(state, record, index, frame_index)
                 : validate_data_entry(state, record, index, frame_index))) {
            return false;
        }
    }
    return vm_core_bytes_are_zero(
        &record->entries[record->entry_count],
        (MICROS_VM_MAPPING_ENTRY_CAPACITY - record->entry_count) * sizeof(record->entries[0])
    );
}

static bool selected_input_frames_are_reserved(const struct micros_vm_state *state)
{
    const struct micros_vm_service_load_transaction *transaction = &state->transaction;

    for (size_t index = 0; index < MICROS_VM_PAGE_TABLE_FRAME_COUNT + transaction->page_count; ++index) {
        uint64_t physical = index < MICROS_VM_PAGE_TABLE_FRAME_COUNT
            ? transaction->table_frames[index]
            : transaction->data_frames[index - MICROS_VM_PAGE_TABLE_FRAME_COUNT];
        uint64_t frame_index;

        if (!vm_core_physical_to_frame_index(state, physical, &frame_index)
            || !vm_core_initial_frame_is_free(state, frame_index)
            || !vm_core_frame_availability_is_valid(state, frame_index)
            || state->boot_info->frame_states[frame_index] != MICROS_VM_LOCAL_FRAME_LOAD_RESERVED) {
            return false;
        }
    }
    return true;
}

static bool retained_segments_are_valid(const struct micros_vm_state *state)
{
    const struct micros_vm_elf_plan *plan = &state->transaction.elf_plan;
    uint64_t virtual_end = MICROS_APPLICATION_ENTRY;
    uint64_t file_end = 0;
    uint64_t pages = 0;

    if (plan->entry != MICROS_APPLICATION_ENTRY
        || plan->segment_count != MICROS_APPLICATION_ELF_SEGMENT_COUNT) {
        return false;
    }
    for (size_t index = 0; index < MICROS_APPLICATION_ELF_SEGMENT_COUNT; ++index) {
        const struct micros_vm_elf_segment *segment = &plan->segments[index];
        uint32_t permissions = MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ
            | (index == 0 ? MICROS_SV39_PERMISSION_EXECUTE : index == 2 ? MICROS_SV39_PERMISSION_WRITE : 0);

        if (segment->reserved != 0 || segment->permissions != permissions
            || segment->virtual_address != virtual_end
            || segment->memory_size == 0 || segment->memory_size % MICROS_FRAME_SIZE != 0
            || segment->memory_size / MICROS_FRAME_SIZE > MICROS_APPLICATION_IMAGE_PAGE_MAX - pages
            || segment->file_size > segment->memory_size
            || segment->file_offset % MICROS_FRAME_SIZE != 0
            || segment->file_offset < file_end
            || segment->file_offset > state->transaction.executable_size
            || segment->file_size > state->transaction.executable_size - segment->file_offset) {
            return false;
        }
        pages += segment->memory_size / MICROS_FRAME_SIZE;
        virtual_end += segment->memory_size;
        file_end = segment->file_offset + segment->file_size;
    }
    return pages == plan->page_count;
}

static bool retained_stack_is_valid(const struct micros_application_stack_plan *plan)
{
    uint32_t start;

    if (plan->occupied_size == 0 || plan->occupied_size > MICROS_APPLICATION_STACK_OCCUPIED_MAX
        || plan->occupied_size % 16 != 0 || plan->frame_size == 0
        || plan->frame_size > plan->occupied_size || plan->occupied_size - plan->frame_size >= 16
        || plan->stack_pointer != MICROS_APPLICATION_STACK_TOP - plan->occupied_size) {
        return false;
    }
    start = MICROS_APPLICATION_STACK_PAGE_SIZE - plan->occupied_size;
    return plan->argc_offset == start
        && plan->arguments_offset == start + sizeof(uint64_t)
        && plan->environment_offset > plan->arguments_offset
        && plan->environment_offset % sizeof(uint64_t) == 0
        && plan->auxiliary_offset > plan->environment_offset
        && plan->auxiliary_offset % 16 == 0
        && plan->auxiliary_offset <= MICROS_APPLICATION_STACK_PAGE_SIZE - MICROS_APPLICATION_AUXV_COUNT * 16
        && plan->strings_offset == plan->auxiliary_offset + MICROS_APPLICATION_AUXV_COUNT * 16
        && plan->strings_offset < start + plan->frame_size;
}

static bool retained_image_page_is_valid(const struct micros_vm_state *state, size_t page_index)
{
    const struct micros_vm_elf_plan *plan = &state->transaction.elf_plan;
    const struct micros_vm_image_page *page = &plan->pages[page_index];
    uint64_t virtual_address = MICROS_APPLICATION_ENTRY + page_index * MICROS_FRAME_SIZE;

    if (page->virtual_address != virtual_address) {
        return false;
    }
    for (size_t index = 0; index < MICROS_APPLICATION_ELF_SEGMENT_COUNT; ++index) {
        const struct micros_vm_elf_segment *segment = &plan->segments[index];
        uint64_t offset, file_offset = 0, file_size = 0;

        if (virtual_address < segment->virtual_address
            || virtual_address - segment->virtual_address >= segment->memory_size) {
            continue;
        }
        offset = virtual_address - segment->virtual_address;
        if (offset < segment->file_size) {
            file_offset = segment->file_offset + offset;
            file_size = segment->file_size - offset;
            if (file_size > MICROS_FRAME_SIZE) {
                file_size = MICROS_FRAME_SIZE;
            }
        }
        return page->permissions == segment->permissions
            && page->file_offset == file_offset && page->file_size == file_size;
    }
    return false;
}

static bool selected_load_page_is_valid(const struct micros_vm_state *state, size_t index)
{
    const struct micros_vm_service_load_transaction *transaction = &state->transaction;
    const struct micros_vm_mapping_record *record = &state->mapping_records[transaction->process.slot];
    size_t entry_index = MICROS_VM_PAGE_TABLE_FRAME_COUNT + index;
    const struct micros_vm_mapping_entry *entry = &record->entries[entry_index];
    uint32_t permissions = MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE;
    uint64_t frame_index;

    if (index < transaction->elf_plan.page_count) {
        if (!retained_image_page_is_valid(state, index)) {
            return false;
        }
        permissions = transaction->elf_plan.pages[index].permissions;
    }
    return entry->physical_address == transaction->data_frames[index]
        && entry->permissions == permissions
        && vm_core_physical_to_frame_index(state, entry->physical_address, &frame_index)
        && vm_core_initial_frame_is_free(state, frame_index)
        && vm_core_frame_availability_is_valid(state, frame_index)
        && vm_core_bytes_are_zero(entry->reserved, sizeof(entry->reserved))
        && validate_data_entry(state, record, entry_index, frame_index);
}

static bool retained_load_set_is_valid(const struct micros_vm_state *state)
{
    if (!vm_core_transaction_frames_are_ordered(state)) {
        return false;
    }
    for (size_t index = 0; index < state->transaction.elf_plan.page_count; ++index) {
        if (!retained_image_page_is_valid(state, index)) {
            return false;
        }
    }
    if (state->phase == MICROS_VM_LOADER_INPUT_COPIED) {
        return selected_input_frames_are_reserved(state);
    }
    return vm_core_selected_record_is_valid(state, state->transaction.process.slot)
        && vm_core_record_matches_transaction(state, &state->mapping_records[state->transaction.process.slot]);
}

enum micros_vm_core_error vm_core_validate_selected_state(const struct micros_vm_state *state)
{
    MICROS_VM01_WORK_HIT("vm01-service-selected", MICROS_WORK_OWNER_VM_SERVICE,
                        MICROS_WORK_KIND_INDEX_PROBE, 1);
    const struct micros_vm_service_load_transaction *transaction;
    const struct micros_vm_mapping_record *record;

    if (state == NULL) {
        return MICROS_VM_CORE_ERROR_ARGUMENT;
    }
    if (state->initialization_magic != MICROS_VM_INITIALIZATION_MAGIC || state->reserved != 0
        || state->boot_info == NULL
        || (uintptr_t)state->boot_info % MICROS_VM_BOOT_INFO_ALIGNMENT != 0
        || vm_core_ranges_overlap(state, sizeof(*state), state->boot_info, sizeof(*state->boot_info))
        || state->phase < MICROS_VM_LOADER_IDLE || state->phase > MICROS_VM_LOADER_EXIT_RELEASING
        || !vm_core_allocator_metadata_is_valid(state)
        || !selected_geometry_is_valid(state)) {
        return MICROS_VM_CORE_ERROR_INVARIANT;
    }
    transaction = &state->transaction;
    if (state->phase == MICROS_VM_LOADER_IDLE || state->phase == MICROS_VM_LOADER_EXIT_RELEASING) {
        return vm_core_bytes_are_zero(transaction, sizeof(*transaction))
            ? MICROS_VM_CORE_OK : MICROS_VM_CORE_ERROR_INVARIANT;
    }
    if (!vm_core_transaction_header_is_canonical(state)
        || !retained_segments_are_valid(state)
        || !retained_stack_is_valid(&transaction->stack_plan)) {
        return MICROS_VM_CORE_ERROR_INVARIANT;
    }
    if (state->phase == MICROS_VM_LOADER_INPUT_COPIED) {
        return transaction->load_token == 0 && transaction->mapping_generation == 0
            && transaction->process.slot == 0 && transaction->process.generation == 0
            && transaction->completed_page_count == 0 && transaction->scratch_page == 0
            && retained_load_set_is_valid(state)
            ? MICROS_VM_CORE_OK : MICROS_VM_CORE_ERROR_INVARIANT;
    }
    if (!vm_core_process_is_canonical(transaction->process) || transaction->load_token == 0) {
        return MICROS_VM_CORE_ERROR_INVARIANT;
    }
    record = &state->mapping_records[transaction->process.slot];
    if (!vm_core_record_header_is_valid(state, transaction->process.slot)
        || record->load_token != transaction->load_token
        || record->mapping_generation != transaction->mapping_generation
        || record->semantic_pid != transaction->semantic_pid
        || record->process_generation != transaction->process.generation
        || record->process_slot != transaction->process.slot
        || record->entry_count != MICROS_VM_PAGE_TABLE_FRAME_COUNT + transaction->page_count
        || (state->phase == MICROS_VM_LOADER_LOADING
            && (transaction->mapping_generation != 0 || record->phase != MICROS_VM_MAPPING_RECORD_LOADING))
        || (state->phase == MICROS_VM_LOADER_FROZEN
            && (transaction->mapping_generation == 0 || record->phase != MICROS_VM_MAPPING_RECORD_FROZEN
                || transaction->completed_page_count != transaction->page_count || transaction->scratch_page != 0))) {
        return MICROS_VM_CORE_ERROR_INVARIANT;
    }
    if (state->phase == MICROS_VM_LOADER_FROZEN) {
        /* The service facade may invoke abort/commit immediately after this admission. */
        return retained_load_set_is_valid(state) ? MICROS_VM_CORE_OK : MICROS_VM_CORE_ERROR_INVARIANT;
    }
    if (transaction->completed_page_count < transaction->page_count) {
        size_t index = transaction->completed_page_count;

        if (!selected_load_page_is_valid(state, index)
            || (index != 0 && (!selected_load_page_is_valid(state, index - 1)
                || transaction->data_frames[index - 1] >= transaction->data_frames[index]))) {
            return MICROS_VM_CORE_ERROR_INVARIANT;
        }
    }
    return MICROS_VM_CORE_OK;
}

enum micros_vm_core_error vm_core_validate_selected_load_plan(const struct micros_vm_state *state)
{
    enum micros_vm_core_error error = vm_core_validate_selected_state(state);

    if (error != MICROS_VM_CORE_OK) {
        return error;
    }
    return state->phase != MICROS_VM_LOADER_LOADING || retained_load_set_is_valid(state)
        ? MICROS_VM_CORE_OK : MICROS_VM_CORE_ERROR_INVARIANT;
}
