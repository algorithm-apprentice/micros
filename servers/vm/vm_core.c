#include "servers/vm/vm_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/endpoint.h"
#include "micros/sv39.h"

void vm_core_zero_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;

    while (size >= 8) {
        bytes[0] = 0;
        bytes[1] = 0;
        bytes[2] = 0;
        bytes[3] = 0;
        bytes[4] = 0;
        bytes[5] = 0;
        bytes[6] = 0;
        bytes[7] = 0;
        bytes += 8;
        size -= 8;
    }
    while (size != 0) {
        *bytes++ = 0;
        --size;
    }
}

void vm_core_copy_bytes(
    void *destination,
    const void *source,
    size_t size
)
{
    uint8_t *output = destination;
    const uint8_t *input = source;

    while (size >= 8) {
        output[0] = input[0];
        output[1] = input[1];
        output[2] = input[2];
        output[3] = input[3];
        output[4] = input[4];
        output[5] = input[5];
        output[6] = input[6];
        output[7] = input[7];
        output += 8;
        input += 8;
        size -= 8;
    }
    while (size != 0) {
        *output++ = *input++;
        --size;
    }
}

bool vm_core_bytes_are_zero(const void *storage, size_t size)
{
    const uint8_t *bytes = storage;

    while (size >= 8) {
        if (
            (
                bytes[0] | bytes[1] | bytes[2] | bytes[3]
                | bytes[4] | bytes[5] | bytes[6] | bytes[7]
            ) != 0
        ) {
            return false;
        }
        bytes += 8;
        size -= 8;
    }
    while (size != 0) {
        if (*bytes++ != 0) {
            return false;
        }
        --size;
    }
    return true;
}

bool vm_core_bytes_equal(
    const void *left,
    const void *right,
    size_t size
)
{
    const uint8_t *left_bytes = left;
    const uint8_t *right_bytes = right;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
            return false;
        }
    }
    return true;
}

bool vm_core_ranges_overlap(
    const void *left,
    size_t left_size,
    const void *right,
    size_t right_size
)
{
    uintptr_t left_start = (uintptr_t)left;
    uintptr_t right_start = (uintptr_t)right;
    uintptr_t left_end;
    uintptr_t right_end;

    if (left_size == 0 || right_size == 0) {
        return false;
    }
    if (
        left_start > UINTPTR_MAX - left_size
        || right_start > UINTPTR_MAX - right_size
    ) {
        return true;
    }
    left_end = left_start + left_size;
    right_end = right_start + right_size;
    return left_start < right_end && right_start < left_end;
}

bool vm_core_endpoint_is_canonical(uint32_t endpoint)
{
    uint32_t slot_mask =
        (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) - 1;
    uint32_t slot = endpoint & slot_mask;
    uint32_t generation = endpoint >> MICROS_ENDPOINT_SLOT_BITS;

    return (
        endpoint != MICROS_ENDPOINT_NONE
        && endpoint != MICROS_ENDPOINT_ANY
        && slot < MICROS_PROCESS_CAPACITY
        && generation != 0
        && generation <= MICROS_ENDPOINT_GENERATION_MAX
    );
}

bool vm_core_process_is_canonical(struct micros_process_handle process)
{
    return (
        process.slot < MICROS_PROCESS_CAPACITY
        && process.generation != 0
        && process.generation <= MICROS_PROCESS_GENERATION_MAX
    );
}

bool vm_core_user_permissions_are_canonical(uint32_t permissions)
{
    const uint32_t defined =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE
        | MICROS_SV39_PERMISSION_EXECUTE
        | MICROS_SV39_PERMISSION_USER;

    return (
        (permissions & ~defined) == 0
        && (
            permissions & MICROS_SV39_PERMISSION_USER
        ) != 0
        && (
            permissions & MICROS_SV39_PERMISSION_READ
        ) != 0
        && (
            (permissions & MICROS_SV39_PERMISSION_WRITE) == 0
            || (
                permissions & MICROS_SV39_PERMISSION_READ
            ) != 0
        )
        && (
            permissions
            & (
                MICROS_SV39_PERMISSION_WRITE
                | MICROS_SV39_PERMISSION_EXECUTE
            )
        ) != (
            MICROS_SV39_PERMISSION_WRITE
            | MICROS_SV39_PERMISSION_EXECUTE
        )
    );
}

bool vm_core_stack_plan_equal(
    const struct micros_application_stack_plan *left,
    const struct micros_application_stack_plan *right
)
{
    return (
        left->stack_pointer == right->stack_pointer
        && left->frame_size == right->frame_size
        && left->occupied_size == right->occupied_size
        && left->argc_offset == right->argc_offset
        && left->arguments_offset == right->arguments_offset
        && left->environment_offset == right->environment_offset
        && left->auxiliary_offset == right->auxiliary_offset
        && left->strings_offset == right->strings_offset
    );
}

bool vm_core_initial_frame_is_free(
    const struct micros_vm_state *state,
    uint64_t frame_index
)
{
    size_t word_index = (size_t)(frame_index / 64);
    uint64_t bit = UINT64_C(1) << (frame_index % 64);

    return (
        state->initial_free_bitmap[word_index] & bit
    ) != 0;
}

static void vm_core_mark_initial_frame_free(
    struct micros_vm_state *state,
    uint64_t frame_index
)
{
    size_t word_index = (size_t)(frame_index / 64);
    uint64_t bit = UINT64_C(1) << (frame_index % 64);

    state->initial_free_bitmap[word_index] |= bit;
}

static uint64_t vm_core_page_digest_byte(uint64_t digest, uint8_t byte)
{
    digest ^= byte;
    return digest * MICROS_VM_PAGE_DIGEST_PRIME;
}

static uint64_t vm_core_page_digest_u32(uint64_t digest, uint32_t value)
{
    size_t index;

    for (index = 0; index < 4; ++index) {
        digest = vm_core_page_digest_byte(
            digest,
            (uint8_t)(value >> (index * 8))
        );
    }
    return digest;
}

static uint64_t vm_core_page_digest_u64(uint64_t digest, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        digest = vm_core_page_digest_byte(
            digest,
            (uint8_t)(value >> (index * 8))
        );
    }
    return digest;
}

uint64_t vm_core_load_page_digest(
    const struct micros_vm_state *state
)
{
    const struct micros_vm_load_transaction *transaction =
        &state->transaction;
    uint64_t digest = MICROS_VM_PAGE_DIGEST_OFFSET;
    size_t index;

    digest = vm_core_page_digest_u32(
        digest,
        transaction->elf_plan.page_count
    );
    digest = vm_core_page_digest_u32(digest, transaction->page_count);
    for (index = 0; index < transaction->page_count; ++index) {
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

        digest = vm_core_page_digest_u64(digest, virtual_address);
        digest = vm_core_page_digest_u64(
            digest,
            transaction->data_frames[index]
        );
        digest = vm_core_page_digest_u32(digest, permissions);
        digest = vm_core_page_digest_u32(digest, 0);
    }
    return digest;
}

static void vm_core_clear_input_buffers(struct micros_vm_state *state)
{
    vm_core_zero_bytes(
        state->executable_bytes,
        sizeof(state->executable_bytes)
    );
    vm_core_zero_bytes(
        state->request_bytes,
        sizeof(state->request_bytes)
    );
}

static void vm_core_clear_transaction(struct micros_vm_state *state)
{
    vm_core_zero_bytes(&state->transaction, sizeof(state->transaction));
    vm_core_clear_input_buffers(state);
}

enum micros_vm_core_error micros_vm_state_initialize(
    struct micros_vm_state *state,
    struct micros_vm_boot_info *boot_info,
    uint32_t expected_service_id,
    uint32_t expected_self_endpoint,
    uint32_t expected_address_space_count
)
{
    struct micros_vm_boot_summary summary;

    if (
        state == NULL
        || boot_info == NULL
        || expected_service_id == 0
        || !vm_core_endpoint_is_canonical(expected_self_endpoint)
        || expected_address_space_count == 0
        || expected_address_space_count
            > MICROS_VM_MAX_STATIC_ADDRESS_SPACES
        || vm_core_ranges_overlap(
            state,
            sizeof(*state),
            boot_info,
            sizeof(*boot_info)
        )
    ) {
        return MICROS_VM_CORE_ERROR_ARGUMENT;
    }
    if (
        micros_vm_boot_validate(boot_info, &summary)
            != MICROS_VM_BOOT_OK
    ) {
        return MICROS_VM_CORE_ERROR_BOOT;
    }
    if (
        boot_info->header.service_id != expected_service_id
        || boot_info->header.self_endpoint != expected_self_endpoint
        || boot_info->header.address_space_count
            != expected_address_space_count
    ) {
        return MICROS_VM_CORE_ERROR_BOOT;
    }
    vm_core_zero_bytes(state, sizeof(*state));
    state->initialization_magic = MICROS_VM_INITIALIZATION_MAGIC;
    state->phase = MICROS_VM_LOADER_IDLE;
    state->boot_info = boot_info;
    state->boot_summary = summary;
    {
        uint64_t frame_index;

        for (
            frame_index = 0;
            frame_index < summary.managed_frame_count;
            ++frame_index
        ) {
            if (
                boot_info->frame_states[frame_index]
                    == MICROS_VM_FRAME_FREE
            ) {
                vm_core_mark_initial_frame_free(
                    state,
                    frame_index
                );
            }
        }
    }
    return MICROS_VM_CORE_OK;
}

static enum micros_vm_core_error vm_core_prepare_failure(
    struct micros_vm_state *state,
    enum micros_vm_core_error error
)
{
    vm_core_clear_input_buffers(state);
    return error;
}

enum micros_vm_core_error micros_vm_load_prepare(
    struct micros_vm_state *state,
    uint64_t process_reservation_token,
    uint64_t semantic_pid,
    const uint8_t *executable_bytes,
    size_t executable_size,
    const uint8_t *request_bytes,
    size_t request_size,
    const struct micros_application_stack_values *stack_values
)
{
    struct micros_vm_load_transaction candidate;
    struct micros_vm_elf_plan elf_plan;
    struct micros_application_stack_plan stack_plan;
    uint64_t table_frames[MICROS_VM_PAGE_TABLE_FRAME_COUNT] = {0};
    uint64_t data_frames[MICROS_APPLICATION_RESIDENT_PAGE_MAX] = {0};
    bool executable_is_staged;
    bool request_is_staged;
    enum micros_vm_core_error state_error;
    size_t page_count;

    if (
        state == NULL
        || executable_bytes == NULL
        || request_bytes == NULL
        || stack_values == NULL
    ) {
        return MICROS_VM_CORE_ERROR_ARGUMENT;
    }
    executable_is_staged =
        executable_bytes == state->executable_bytes;
    request_is_staged = request_bytes == state->request_bytes;
    state_error = vm_core_validate_state(
        state,
        executable_is_staged || request_is_staged
    );
    if (state_error != MICROS_VM_CORE_OK) {
        return state_error;
    }
    if (state->phase != MICROS_VM_LOADER_IDLE) {
        return MICROS_VM_CORE_ERROR_STATE;
    }
    if (
        process_reservation_token == 0
        || semantic_pid == 0
        || stack_values->pid != semantic_pid
        || !vm_core_endpoint_is_canonical(stack_values->pm_endpoint)
        || !vm_core_endpoint_is_canonical(stack_values->vfs_endpoint)
        || executable_size == 0
        || request_size < MICROS_SPAWN_REQUEST_SIZE_MIN
        || (
            executable_is_staged
            && executable_size > sizeof(state->executable_bytes)
        )
        || (
            request_is_staged
            && request_size > sizeof(state->request_bytes)
        )
        || (
            !executable_is_staged
            && vm_core_ranges_overlap(
                executable_bytes,
                executable_size,
                state,
                sizeof(*state)
            )
        )
        || (
            !request_is_staged
            && vm_core_ranges_overlap(
                request_bytes,
                request_size,
                state,
                sizeof(*state)
            )
        )
        || vm_core_ranges_overlap(
            executable_bytes,
            executable_size,
            request_bytes,
            request_size
        )
    ) {
        return MICROS_VM_CORE_ERROR_ARGUMENT;
    }
    if (executable_size > MICROS_APPLICATION_EXECUTABLE_SIZE_MAX) {
        return vm_core_prepare_failure(state, MICROS_VM_CORE_ERROR_TOO_BIG);
    }
    if (request_size > MICROS_SPAWN_REQUEST_SIZE_MAX) {
        return vm_core_prepare_failure(state, MICROS_VM_CORE_ERROR_REQUEST);
    }
    switch (micros_vm_elf_plan(
        executable_bytes,
        executable_size,
        &elf_plan
    )) {
    case MICROS_VM_ELF_OK:
        break;
    case MICROS_VM_ELF_ERROR_TOO_BIG:
        return vm_core_prepare_failure(state, MICROS_VM_CORE_ERROR_TOO_BIG);
    case MICROS_VM_ELF_ERROR_ARGUMENT:
        return MICROS_VM_CORE_ERROR_ARGUMENT;
    case MICROS_VM_ELF_ERROR_EXECUTABLE:
    default:
        return vm_core_prepare_failure(
            state,
            MICROS_VM_CORE_ERROR_EXECUTABLE
        );
    }
    switch (micros_application_stack_plan(
        request_bytes,
        request_size,
        &stack_plan
    )) {
    case MICROS_APPLICATION_STACK_OK:
        break;
    case MICROS_APPLICATION_STACK_ERROR_TOO_BIG:
        return vm_core_prepare_failure(state, MICROS_VM_CORE_ERROR_TOO_BIG);
    case MICROS_APPLICATION_STACK_ERROR_ARGUMENT:
        return MICROS_VM_CORE_ERROR_ARGUMENT;
    case MICROS_APPLICATION_STACK_ERROR_MALFORMED:
    default:
        return vm_core_prepare_failure(
            state,
            MICROS_VM_CORE_ERROR_REQUEST
        );
    }
    page_count = elf_plan.page_count + 1;
    if (!vm_core_select_lowest_frames(
        state,
        MICROS_VM_MAPPING_RECORD_CAPACITY,
        page_count,
        table_frames,
        data_frames
    )) {
        return vm_core_prepare_failure(
            state,
            MICROS_VM_CORE_ERROR_NO_SPACE
        );
    }

    vm_core_zero_bytes(&candidate, sizeof(candidate));
    candidate.process_reservation_token = process_reservation_token;
    candidate.semantic_pid = semantic_pid;
    candidate.stack_values = *stack_values;
    candidate.executable_size = (uint32_t)executable_size;
    candidate.request_size = (uint32_t)request_size;
    candidate.page_count = (uint32_t)page_count;
    candidate.elf_plan = elf_plan;
    candidate.stack_plan = stack_plan;
    vm_core_copy_bytes(
        candidate.table_frames,
        table_frames,
        sizeof(candidate.table_frames)
    );
    vm_core_copy_bytes(
        candidate.data_frames,
        data_frames,
        page_count * sizeof(candidate.data_frames[0])
    );

    if (executable_is_staged) {
        vm_core_zero_bytes(
            &state->executable_bytes[executable_size],
            sizeof(state->executable_bytes) - executable_size
        );
    } else {
        vm_core_zero_bytes(
            state->executable_bytes,
            sizeof(state->executable_bytes)
        );
        vm_core_copy_bytes(
            state->executable_bytes,
            executable_bytes,
            executable_size
        );
    }
    if (request_is_staged) {
        vm_core_zero_bytes(
            &state->request_bytes[request_size],
            sizeof(state->request_bytes) - request_size
        );
    } else {
        vm_core_zero_bytes(
            state->request_bytes,
            sizeof(state->request_bytes)
        );
        vm_core_copy_bytes(
            state->request_bytes,
            request_bytes,
            request_size
        );
    }
    state->transaction = candidate;
    state->phase = MICROS_VM_LOADER_INPUT_COPIED;
    return vm_core_validate_state(state, false);
}

enum micros_vm_core_error micros_vm_load_bind(
    struct micros_vm_state *state,
    struct micros_process_handle process,
    uint64_t load_token
)
{
    static const uint8_t table_roles[MICROS_VM_PAGE_TABLE_FRAME_COUNT] = {
        MICROS_VM_MAPPING_ENTRY_ROOT_PAGE_TABLE,
        MICROS_VM_MAPPING_ENTRY_LEVEL1_PAGE_TABLE,
        MICROS_VM_MAPPING_ENTRY_IMAGE_PAGE_TABLE,
        MICROS_VM_MAPPING_ENTRY_STACK_PAGE_TABLE,
    };
    struct micros_vm_mapping_record candidate;
    enum micros_vm_core_error error = vm_core_validate_state(state, false);
    size_t slot;
    size_t index;

    if (error != MICROS_VM_CORE_OK) {
        return error;
    }
    if (
        state->phase != MICROS_VM_LOADER_INPUT_COPIED
        || !vm_core_process_is_canonical(process)
        || load_token == 0
        || !vm_core_record_is_zero(&state->mapping_records[process.slot])
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
            !vm_core_record_is_zero(record)
            && (
                record->load_token == load_token
                || record->semantic_pid
                    == state->transaction.semantic_pid
            )
        ) {
            return MICROS_VM_CORE_ERROR_STATE;
        }
    }
    vm_core_zero_bytes(&candidate, sizeof(candidate));
    candidate.load_token = load_token;
    candidate.semantic_pid = state->transaction.semantic_pid;
    candidate.process_generation = process.generation;
    candidate.process_slot = process.slot;
    candidate.phase = MICROS_VM_MAPPING_RECORD_LOADING;
    candidate.entry_count = (uint8_t)(
        MICROS_VM_PAGE_TABLE_FRAME_COUNT
        + state->transaction.page_count
    );
    for (
        index = 0;
        index < MICROS_VM_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        uint64_t frame_index;

        if (
            !vm_core_physical_to_frame_index(
                state,
                state->transaction.table_frames[index],
                &frame_index
            )
            || state->boot_info->frame_states[frame_index]
                != MICROS_VM_LOCAL_FRAME_FREE
            || vm_core_physical_is_in_record_other_than(
                state,
                state->transaction.table_frames[index],
                MICROS_VM_MAPPING_RECORD_CAPACITY
            )
        ) {
            return MICROS_VM_CORE_ERROR_INVARIANT;
        }
        candidate.entries[index].physical_address =
            state->transaction.table_frames[index];
        candidate.entries[index].role = table_roles[index];
    }
    for (index = 0; index < state->transaction.page_count; ++index) {
        struct micros_vm_mapping_entry *entry =
            &candidate.entries[
                MICROS_VM_PAGE_TABLE_FRAME_COUNT + index
            ];
        uint64_t frame_index;

        if (
            !vm_core_physical_to_frame_index(
                state,
                state->transaction.data_frames[index],
                &frame_index
            )
            || state->boot_info->frame_states[frame_index]
                != MICROS_VM_LOCAL_FRAME_FREE
            || vm_core_physical_is_in_record_other_than(
                state,
                state->transaction.data_frames[index],
                MICROS_VM_MAPPING_RECORD_CAPACITY
            )
        ) {
            return MICROS_VM_CORE_ERROR_INVARIANT;
        }
        entry->virtual_address = index
                < state->transaction.elf_plan.page_count
            ? state->transaction.elf_plan.pages[index]
                .virtual_address
            : MICROS_APPLICATION_STACK_BASE;
        entry->physical_address =
            state->transaction.data_frames[index];
        entry->permissions = index
                < state->transaction.elf_plan.page_count
            ? state->transaction.elf_plan.pages[index].permissions
            : MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE;
        entry->role = MICROS_VM_MAPPING_ENTRY_USER_PAGE;
    }

    for (
        index = 0;
        index < MICROS_VM_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        uint64_t frame_index;

        (void)vm_core_physical_to_frame_index(
            state,
            state->transaction.table_frames[index],
            &frame_index
        );
        state->boot_info->frame_states[frame_index] =
            MICROS_VM_LOCAL_FRAME_LOAD_RESERVED;
    }
    state->mapping_records[process.slot] = candidate;
    state->transaction.process.slot = process.slot;
    state->transaction.process.generation = process.generation;
    state->transaction.load_token = load_token;
    state->phase = MICROS_VM_LOADER_LOADING;
    return vm_core_validate_state(state, false);
}

enum micros_vm_core_error micros_vm_load_next_page(
    const struct micros_vm_state *state,
    struct micros_vm_load_page_action *action
)
{
    struct micros_vm_load_page_action candidate;
    enum micros_vm_core_error error = vm_core_validate_state(state, false);
    size_t page_index;

    if (action == NULL) {
        return MICROS_VM_CORE_ERROR_ARGUMENT;
    }
    if (error != MICROS_VM_CORE_OK) {
        return error;
    }
    if (
        state->phase != MICROS_VM_LOADER_LOADING
        || state->transaction.scratch_page != 0
        || state->transaction.completed_page_count
            >= state->transaction.page_count
    ) {
        return MICROS_VM_CORE_ERROR_STATE;
    }
    page_index = state->transaction.completed_page_count;
    vm_core_zero_bytes(&candidate, sizeof(candidate));
    candidate.virtual_address = page_index
            < state->transaction.elf_plan.page_count
        ? state->transaction.elf_plan.pages[page_index]
            .virtual_address
        : MICROS_APPLICATION_STACK_BASE;
    candidate.physical_address =
        state->transaction.data_frames[page_index];
    candidate.permissions = page_index
            < state->transaction.elf_plan.page_count
        ? state->transaction.elf_plan.pages[page_index].permissions
        : MICROS_SV39_PERMISSION_USER
            | MICROS_SV39_PERMISSION_READ
            | MICROS_SV39_PERMISSION_WRITE;
    candidate.page_index = (uint32_t)page_index;
    candidate.kind = page_index
            < state->transaction.elf_plan.page_count
        ? MICROS_VM_LOAD_PAGE_IMAGE
        : MICROS_VM_LOAD_PAGE_STACK;
    *action = candidate;
    return MICROS_VM_CORE_OK;
}

enum micros_vm_core_error micros_vm_load_scratch_mapped(
    struct micros_vm_state *state,
    uint32_t page_index
)
{
    enum micros_vm_core_error error = vm_core_validate_state(state, false);
    uint64_t frame_index;

    if (error != MICROS_VM_CORE_OK) {
        return error;
    }
    if (
        state->phase != MICROS_VM_LOADER_LOADING
        || state->transaction.scratch_page != 0
        || page_index != state->transaction.completed_page_count
        || page_index >= state->transaction.page_count
        || !vm_core_physical_to_frame_index(
            state,
            state->transaction.data_frames[page_index],
            &frame_index
        )
        || state->boot_info->frame_states[frame_index]
            != MICROS_VM_LOCAL_FRAME_FREE
    ) {
        return MICROS_VM_CORE_ERROR_STATE;
    }
    state->boot_info->frame_states[frame_index] =
        MICROS_VM_LOCAL_FRAME_TRANSFERABLE;
    state->transaction.scratch_page = page_index + 1;
    return vm_core_validate_state(state, false);
}

enum micros_vm_core_error micros_vm_load_fill_scratch(
    struct micros_vm_state *state,
    uint32_t page_index,
    uint8_t *page,
    size_t page_size
)
{
    struct micros_application_stack_plan stack_plan;
    enum micros_vm_core_error error = vm_core_validate_state(state, false);

    if (
        state == NULL
        || page == NULL
        || page_size != MICROS_APPLICATION_STACK_PAGE_SIZE
        || vm_core_ranges_overlap(page, page_size, state, sizeof(*state))
        || vm_core_ranges_overlap(
            page,
            page_size,
            state->boot_info,
            sizeof(*state->boot_info)
        )
    ) {
        return MICROS_VM_CORE_ERROR_ARGUMENT;
    }
    if (error != MICROS_VM_CORE_OK) {
        return error;
    }
    if (
        state->phase != MICROS_VM_LOADER_LOADING
        || state->transaction.scratch_page != page_index + 1
        || state->transaction.scratch_filled != 0
        || page_index != state->transaction.completed_page_count
        || page_index >= state->transaction.page_count
    ) {
        return MICROS_VM_CORE_ERROR_STATE;
    }
    if (page_index < state->transaction.elf_plan.page_count) {
        const struct micros_vm_image_page *image_page =
            &state->transaction.elf_plan.pages[page_index];

        if (
            image_page->file_offset
                > state->transaction.executable_size
            || image_page->file_size
                > state->transaction.executable_size
                    - image_page->file_offset
        ) {
            return MICROS_VM_CORE_ERROR_INVARIANT;
        }
        vm_core_zero_bytes(page, page_size);
        vm_core_copy_bytes(
            page,
            &state->executable_bytes[image_page->file_offset],
            image_page->file_size
        );
    } else {
        if (
            micros_application_stack_plan(
                state->request_bytes,
                state->transaction.request_size,
                &stack_plan
            ) != MICROS_APPLICATION_STACK_OK
            || !vm_core_stack_plan_equal(
                &stack_plan,
                &state->transaction.stack_plan
            )
        ) {
            return MICROS_VM_CORE_ERROR_INVARIANT;
        }
        if (
            micros_application_stack_build(
                state->request_bytes,
                state->transaction.request_size,
                &state->transaction.stack_values,
                page,
                page_size,
                &stack_plan
            ) != MICROS_APPLICATION_STACK_OK
            || !vm_core_stack_plan_equal(
                &stack_plan,
                &state->transaction.stack_plan
            )
        ) {
            return MICROS_VM_CORE_ERROR_INVARIANT;
        }
    }
    state->transaction.scratch_filled = 1;
    return vm_core_validate_state(state, false);
}

enum micros_vm_core_error micros_vm_load_scratch_committed(
    struct micros_vm_state *state,
    uint32_t page_index
)
{
    enum micros_vm_core_error error = vm_core_validate_state(state, false);
    uint64_t frame_index;

    if (error != MICROS_VM_CORE_OK) {
        return error;
    }
    if (
        state->phase != MICROS_VM_LOADER_LOADING
        || state->transaction.scratch_page != page_index + 1
        || state->transaction.scratch_filled != 1
        || page_index != state->transaction.completed_page_count
        || !vm_core_physical_to_frame_index(
            state,
            state->transaction.data_frames[page_index],
            &frame_index
        )
        || state->boot_info->frame_states[frame_index]
            != MICROS_VM_LOCAL_FRAME_TRANSFERABLE
    ) {
        return MICROS_VM_CORE_ERROR_STATE;
    }
    state->transaction.scratch_page = 0;
    state->transaction.scratch_filled = 0;
    ++state->transaction.completed_page_count;
    return vm_core_validate_state(state, false);
}

enum micros_vm_core_error micros_vm_load_freeze_plan(
    const struct micros_vm_state *state,
    struct micros_vm_freeze_plan *plan
)
{
    struct micros_vm_freeze_plan candidate;
    enum micros_vm_core_error error = vm_core_validate_state(state, false);

    if (plan == NULL) {
        return MICROS_VM_CORE_ERROR_ARGUMENT;
    }
    if (error != MICROS_VM_CORE_OK) {
        return error;
    }
    if (
        state->phase != MICROS_VM_LOADER_LOADING
        || state->transaction.scratch_page != 0
        || state->transaction.scratch_filled != 0
        || state->transaction.completed_page_count
            != state->transaction.page_count
    ) {
        return MICROS_VM_CORE_ERROR_STATE;
    }
    vm_core_zero_bytes(&candidate, sizeof(candidate));
    candidate.load_token = state->transaction.load_token;
    vm_core_copy_bytes(
        candidate.table_frames,
        state->transaction.table_frames,
        sizeof(candidate.table_frames)
    );
    candidate.entry = state->transaction.elf_plan.entry;
    candidate.stack_pointer =
        state->transaction.stack_plan.stack_pointer;
    candidate.image_page_count =
        state->transaction.elf_plan.page_count;
    candidate.data_page_count = state->transaction.page_count;
    candidate.page_plan_digest = vm_core_load_page_digest(state);
    *plan = candidate;
    return MICROS_VM_CORE_OK;
}

enum micros_vm_core_error micros_vm_load_frozen(
    struct micros_vm_state *state,
    uint64_t mapping_generation
)
{
    enum micros_vm_core_error error = vm_core_validate_state(state, false);
    struct micros_vm_mapping_record *record;
    size_t index;

    if (error != MICROS_VM_CORE_OK) {
        return error;
    }
    if (
        state->phase != MICROS_VM_LOADER_LOADING
        || mapping_generation == 0
        || state->transaction.scratch_page != 0
        || state->transaction.completed_page_count
            != state->transaction.page_count
    ) {
        return MICROS_VM_CORE_ERROR_STATE;
    }
    record =
        &state->mapping_records[state->transaction.process.slot];
    for (
        index = 0;
        index < MICROS_VM_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        uint64_t frame_index;

        if (
            !vm_core_physical_to_frame_index(
                state,
                state->transaction.table_frames[index],
                &frame_index
            )
            || state->boot_info->frame_states[frame_index]
                != MICROS_VM_LOCAL_FRAME_LOAD_RESERVED
        ) {
            return MICROS_VM_CORE_ERROR_INVARIANT;
        }
    }
    for (index = 0; index < state->transaction.page_count; ++index) {
        uint64_t frame_index;

        if (
            !vm_core_physical_to_frame_index(
                state,
                state->transaction.data_frames[index],
                &frame_index
            )
            || state->boot_info->frame_states[frame_index]
                != MICROS_VM_LOCAL_FRAME_TRANSFERABLE
        ) {
            return MICROS_VM_CORE_ERROR_INVARIANT;
        }
    }
    for (
        index = 0;
        index < MICROS_VM_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        uint64_t frame_index;

        (void)vm_core_physical_to_frame_index(
            state,
            state->transaction.table_frames[index],
            &frame_index
        );
        state->boot_info->frame_states[frame_index] =
            MICROS_VM_LOCAL_FRAME_PROCESS_PAGE_TABLE;
    }
    for (index = 0; index < state->transaction.page_count; ++index) {
        uint64_t frame_index;

        (void)vm_core_physical_to_frame_index(
            state,
            state->transaction.data_frames[index],
            &frame_index
        );
        state->boot_info->frame_states[frame_index] =
            MICROS_VM_LOCAL_FRAME_PROCESS_WIRED;
    }
    state->transaction.mapping_generation = mapping_generation;
    record->mapping_generation = mapping_generation;
    record->phase = MICROS_VM_MAPPING_RECORD_FROZEN;
    state->phase = MICROS_VM_LOADER_FROZEN;
    return vm_core_validate_state(state, false);
}

enum micros_vm_core_error micros_vm_load_aborted(
    struct micros_vm_state *state,
    uint64_t load_token,
    uint64_t mapping_generation
)
{
    enum micros_vm_core_error error = vm_core_validate_state(state, false);
    size_t index;

    if (error != MICROS_VM_CORE_OK) {
        return error;
    }
    if (state->phase == MICROS_VM_LOADER_INPUT_COPIED) {
        if (load_token != 0 || mapping_generation != 0) {
            return MICROS_VM_CORE_ERROR_STATE;
        }
        vm_core_clear_transaction(state);
        state->phase = MICROS_VM_LOADER_IDLE;
        return vm_core_validate_state(state, false);
    }
    if (
        (
            state->phase != MICROS_VM_LOADER_LOADING
            && state->phase != MICROS_VM_LOADER_FROZEN
        )
        || load_token != state->transaction.load_token
        || mapping_generation
            != state->transaction.mapping_generation
    ) {
        return MICROS_VM_CORE_ERROR_STATE;
    }
    for (
        index = 0;
        index < MICROS_VM_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        uint64_t frame_index;

        if (
            !vm_core_physical_to_frame_index(
                state,
                state->transaction.table_frames[index],
                &frame_index
            )
        ) {
            return MICROS_VM_CORE_ERROR_INVARIANT;
        }
        state->boot_info->frame_states[frame_index] =
            MICROS_VM_LOCAL_FRAME_FREE;
    }
    for (index = 0; index < state->transaction.page_count; ++index) {
        uint64_t frame_index;

        if (
            !vm_core_physical_to_frame_index(
                state,
                state->transaction.data_frames[index],
                &frame_index
            )
        ) {
            return MICROS_VM_CORE_ERROR_INVARIANT;
        }
        state->boot_info->frame_states[frame_index] =
            MICROS_VM_LOCAL_FRAME_FREE;
    }
    vm_core_zero_bytes(
        &state->mapping_records[state->transaction.process.slot],
        sizeof(state->mapping_records[0])
    );
    vm_core_clear_transaction(state);
    state->phase = MICROS_VM_LOADER_IDLE;
    return vm_core_validate_state(state, false);
}

enum micros_vm_core_error micros_vm_load_committed(
    struct micros_vm_state *state,
    uint64_t load_token,
    uint64_t mapping_generation,
    uint64_t semantic_pid
)
{
    enum micros_vm_core_error error = vm_core_validate_state(state, false);
    struct micros_vm_mapping_record *record;

    if (error != MICROS_VM_CORE_OK) {
        return error;
    }
    if (
        state->phase != MICROS_VM_LOADER_FROZEN
        || load_token != state->transaction.load_token
        || mapping_generation
            != state->transaction.mapping_generation
        || semantic_pid != state->transaction.semantic_pid
    ) {
        return MICROS_VM_CORE_ERROR_STATE;
    }
    record =
        &state->mapping_records[state->transaction.process.slot];
    record->phase = MICROS_VM_MAPPING_RECORD_ACTIVE;
    vm_core_clear_transaction(state);
    state->phase = MICROS_VM_LOADER_IDLE;
    return vm_core_validate_state(state, false);
}

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
