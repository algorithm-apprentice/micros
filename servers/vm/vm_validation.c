#include "servers/vm/vm_internal.h"

#include "micros/endpoint.h"
#include "micros/sv39.h"

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
    size_t digest_offset =
        offsetof(struct micros_vm_boot_info, header.digest);
    size_t frame_state_offset =
        offsetof(struct micros_vm_boot_info, frame_states);
    uint64_t digest = MICROS_VM_PAGE_DIGEST_OFFSET;
    size_t index;

    for (index = 0; index < sizeof(*state->boot_info); ++index) {
        uint8_t byte;

        if (
            index >= digest_offset
            && index
                < digest_offset
                    + sizeof(state->boot_info->header.digest)
        ) {
            byte = 0;
        } else if (
            index >= frame_state_offset
            && index
                < frame_state_offset
                    + sizeof(state->boot_info->frame_states)
            && vm_core_initial_frame_is_free(
                state,
                index - frame_state_offset
            )
        ) {
            byte = MICROS_VM_FRAME_FREE;
        } else {
            byte = bytes[index];
        }
        digest ^= byte;
        digest *= MICROS_VM_PAGE_DIGEST_PRIME;
    }
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

static bool vm_core_transaction_common_is_canonical(
    const struct micros_vm_state *state
)
{
    const struct micros_vm_service_load_transaction *transaction =
        &state->transaction;
    size_t index;

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
        || !vm_core_transaction_buffers_are_canonical(state)
        || !vm_core_transaction_plans_are_canonical(state)
    ) {
        return false;
    }
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
    uint64_t frame_index,
    struct vm_validation_counts *counts
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
    if (record->phase != MICROS_VM_MAPPING_RECORD_LOADING) {
        ++counts->record_process_table;
    }
    return true;
}

static bool validate_data_entry(
    const struct micros_vm_state *state,
    const struct micros_vm_mapping_record *record,
    size_t entry_index,
    uint64_t frame_index,
    struct vm_validation_counts *counts
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
                : MICROS_VM_LOCAL_FRAME_FREE;
    } else {
        ++counts->record_process_wired;
    }
    return state->boot_info->frame_states[frame_index] == expected_state;
}

static bool record_entry_is_unique(
    const struct micros_vm_state *state,
    size_t slot,
    size_t entry_index
)
{
    uint64_t physical =
        state->mapping_records[slot].entries[entry_index].physical_address;
    size_t prior_slot;

    for (prior_slot = 0; prior_slot <= slot; ++prior_slot) {
        const struct micros_vm_mapping_record *prior =
            &state->mapping_records[prior_slot];
        size_t limit = prior_slot == slot
            ? entry_index
            : (vm_core_record_is_zero(prior) ? 0 : prior->entry_count);
        size_t prior_entry;

        for (prior_entry = 0; prior_entry < limit; ++prior_entry) {
            if (prior->entries[prior_entry].physical_address == physical) {
                return false;
            }
        }
    }
    return true;
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
            ? validate_table_entry(state, record, entry_index, frame_index, counts)
            : validate_data_entry(state, record, entry_index, frame_index, counts);
        if (!role_is_valid || !record_entry_is_unique(state, slot, entry_index)) {
            return false;
        }
    }
    return vm_core_bytes_are_zero(
        &record->entries[record->entry_count],
        (MICROS_VM_MAPPING_ENTRY_CAPACITY - record->entry_count)
            * sizeof(record->entries[0])
    );
}

static bool record_identity_is_unique(
    const struct micros_vm_state *state,
    size_t slot
)
{
    const struct micros_vm_mapping_record *record =
        &state->mapping_records[slot];
    size_t other_slot;

    for (other_slot = 0; other_slot < slot; ++other_slot) {
        const struct micros_vm_mapping_record *other =
            &state->mapping_records[other_slot];

        if (
            !vm_core_record_is_zero(other)
            && (
                other->load_token == record->load_token
                || other->semantic_pid == record->semantic_pid
                || (
                    other->process_generation == record->process_generation
                    && other->process_slot == record->process_slot
                )
            )
        ) {
            return false;
        }
    }
    return true;
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
            record->load_token == 0
            || record->semantic_pid == 0
            || record->process_slot != slot
            || record->process_generation == 0
            || record->process_generation > MICROS_PROCESS_GENERATION_MAX
            || record->entry_count
                < MICROS_VM_PAGE_TABLE_FRAME_COUNT
                    + MICROS_APPLICATION_IMAGE_PAGE_MIN + 1
            || record->entry_count > MICROS_VM_MAPPING_ENTRY_CAPACITY
            || record->phase < MICROS_VM_MAPPING_RECORD_LOADING
            || record->phase > MICROS_VM_MAPPING_RECORD_EXIT_RELEASING
            || (
                record->phase == MICROS_VM_MAPPING_RECORD_LOADING
                && record->mapping_generation != 0
            )
            || (
                record->phase != MICROS_VM_MAPPING_RECORD_LOADING
                && record->mapping_generation == 0
            )
            || !validate_record_entries(state, slot, counts)
            || !record_identity_is_unique(state, slot)
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
    return true;
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
        && counts->local_load_reserved == 0
        && counts->local_transferable == 0
        && state->transaction.load_token == 0
        && state->transaction.mapping_generation == 0
        && state->transaction.process.slot == 0
        && state->transaction.process.generation == 0
        && state->transaction.completed_page_count == 0
        && state->transaction.scratch_page == 0
        && vm_core_selected_frames_match(state, MICROS_VM_MAPPING_RECORD_CAPACITY)
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
        && counts->local_load_reserved == MICROS_VM_PAGE_TABLE_FRAME_COUNT
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
        && vm_core_selected_frames_match(state, counts->loading_slot)
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
        && vm_core_selected_frames_match(state, counts->frozen_slot)
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

enum micros_vm_core_error vm_core_validate_state(
    const struct micros_vm_state *state,
    bool allow_staged_idle_inputs
)
{
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
        && !vm_core_transaction_common_is_canonical(state)
    ) {
        return MICROS_VM_CORE_ERROR_INVARIANT;
    }
    if (
        !validate_local_frame_states(state, &counts)
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
