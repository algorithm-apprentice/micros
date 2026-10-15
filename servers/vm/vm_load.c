#include "servers/vm/vm_internal.h"

#include "micros/endpoint.h"
#include "micros/sv39.h"

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

static enum micros_vm_core_error vm_core_prepare_failure(
    struct micros_vm_state *state,
    enum micros_vm_core_error error
)
{
    vm_core_clear_input_buffers(state);
    return error;
}

static bool prepare_source_shape_is_valid(
    const struct micros_vm_state *state,
    uint64_t process_reservation_token,
    uint64_t semantic_pid,
    const uint8_t *executable_bytes,
    size_t executable_size,
    const uint8_t *request_bytes,
    size_t request_size,
    const struct micros_application_stack_values *stack_values
)
{
    bool executable_is_staged = executable_bytes == state->executable_bytes;
    bool request_is_staged = request_bytes == state->request_bytes;

    return (
        process_reservation_token != 0
        && semantic_pid != 0
        && stack_values->pid == semantic_pid
        && vm_core_endpoint_is_canonical(stack_values->pm_endpoint)
        && vm_core_endpoint_is_canonical(stack_values->vfs_endpoint)
        && executable_size != 0
        && request_size >= MICROS_SPAWN_REQUEST_SIZE_MIN
        && (!executable_is_staged || executable_size <= sizeof(state->executable_bytes))
        && (!request_is_staged || request_size <= sizeof(state->request_bytes))
        && (
            executable_is_staged
            || !vm_core_ranges_overlap(
                executable_bytes, executable_size, state, sizeof(*state)
            )
        )
        && (
            request_is_staged
            || !vm_core_ranges_overlap(request_bytes, request_size, state, sizeof(*state))
        )
        && !vm_core_ranges_overlap(
            executable_bytes, executable_size, request_bytes, request_size
        )
    );
}

static enum micros_vm_core_error prepare_binary_plans(
    struct micros_vm_state *state,
    const uint8_t *executable_bytes,
    size_t executable_size,
    const uint8_t *request_bytes,
    size_t request_size,
    struct micros_vm_elf_plan *elf_plan,
    struct micros_application_stack_plan *stack_plan
)
{
    if (executable_size > MICROS_APPLICATION_EXECUTABLE_SIZE_MAX) {
        return vm_core_prepare_failure(state, MICROS_VM_CORE_ERROR_TOO_BIG);
    }
    if (request_size > MICROS_SPAWN_REQUEST_SIZE_MAX) {
        return vm_core_prepare_failure(state, MICROS_VM_CORE_ERROR_REQUEST);
    }
    switch (micros_vm_elf_plan(executable_bytes, executable_size, elf_plan)) {
    case MICROS_VM_ELF_OK:
        break;
    case MICROS_VM_ELF_ERROR_TOO_BIG:
        return vm_core_prepare_failure(state, MICROS_VM_CORE_ERROR_TOO_BIG);
    case MICROS_VM_ELF_ERROR_ARGUMENT:
        return MICROS_VM_CORE_ERROR_ARGUMENT;
    case MICROS_VM_ELF_ERROR_EXECUTABLE:
    default:
        return vm_core_prepare_failure(state, MICROS_VM_CORE_ERROR_EXECUTABLE);
    }
    switch (micros_application_stack_plan(request_bytes, request_size, stack_plan)) {
    case MICROS_APPLICATION_STACK_OK:
        return MICROS_VM_CORE_OK;
    case MICROS_APPLICATION_STACK_ERROR_TOO_BIG:
        return vm_core_prepare_failure(state, MICROS_VM_CORE_ERROR_TOO_BIG);
    case MICROS_APPLICATION_STACK_ERROR_ARGUMENT:
        return MICROS_VM_CORE_ERROR_ARGUMENT;
    case MICROS_APPLICATION_STACK_ERROR_MALFORMED:
    default:
        return vm_core_prepare_failure(state, MICROS_VM_CORE_ERROR_REQUEST);
    }
}

static void publish_load_input(
    uint8_t *destination,
    size_t capacity,
    const uint8_t *source,
    size_t size
)
{
    if (source == destination) {
        vm_core_zero_bytes(&destination[size], capacity - size);
    } else {
        vm_core_zero_bytes(destination, capacity);
        vm_core_copy_bytes(destination, source, size);
    }
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
    struct micros_vm_service_load_transaction candidate;
    struct micros_vm_elf_plan elf_plan;
    struct micros_application_stack_plan stack_plan;
    uint64_t table_frames[MICROS_VM_PAGE_TABLE_FRAME_COUNT] = {0};
    uint64_t data_frames[MICROS_APPLICATION_RESIDENT_PAGE_MAX] = {0};
    enum micros_vm_core_error error;
    size_t page_count;

    if (
        state == NULL || executable_bytes == NULL
        || request_bytes == NULL || stack_values == NULL
    ) {
        return MICROS_VM_CORE_ERROR_ARGUMENT;
    }
    error = vm_core_validate_state(
        state,
        executable_bytes == state->executable_bytes
            || request_bytes == state->request_bytes
    );
    if (error != MICROS_VM_CORE_OK) {
        return error;
    }
    if (state->phase != MICROS_VM_LOADER_IDLE) {
        return MICROS_VM_CORE_ERROR_STATE;
    }
    if (!prepare_source_shape_is_valid(
        state, process_reservation_token, semantic_pid,
        executable_bytes, executable_size, request_bytes, request_size, stack_values
    )) {
        return MICROS_VM_CORE_ERROR_ARGUMENT;
    }
    error = prepare_binary_plans(
        state, executable_bytes, executable_size,
        request_bytes, request_size, &elf_plan, &stack_plan
    );
    if (error != MICROS_VM_CORE_OK) {
        return error;
    }
    page_count = elf_plan.page_count + 1;
    if (!vm_core_select_lowest_frames(
        state, MICROS_VM_MAPPING_RECORD_CAPACITY, page_count, table_frames, data_frames
    )) {
        return vm_core_prepare_failure(state, MICROS_VM_CORE_ERROR_NO_SPACE);
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
    vm_core_copy_bytes(candidate.table_frames, table_frames, sizeof(candidate.table_frames));
    vm_core_copy_bytes(
        candidate.data_frames, data_frames, page_count * sizeof(candidate.data_frames[0])
    );
    publish_load_input(
        state->executable_bytes, sizeof(state->executable_bytes),
        executable_bytes, executable_size
    );
    publish_load_input(
        state->request_bytes, sizeof(state->request_bytes), request_bytes, request_size
    );
    state->transaction = candidate;
    state->phase = MICROS_VM_LOADER_INPUT_COPIED;
    return vm_core_validate_state(state, false);
}

static bool load_record_identity_is_available(
    const struct micros_vm_state *state,
    uint64_t load_token
)
{
    size_t slot;

    for (slot = 0; slot < MICROS_VM_MAPPING_RECORD_CAPACITY; ++slot) {
        const struct micros_vm_mapping_record *record = &state->mapping_records[slot];

        if (
            !vm_core_record_is_zero(record)
            && (
                record->load_token == load_token
                || record->semantic_pid == state->transaction.semantic_pid
            )
        ) {
            return false;
        }
    }
    return true;
}

static bool selected_frame_is_available(
    const struct micros_vm_state *state,
    uint64_t physical_address
)
{
    uint64_t frame_index;

    return (
        vm_core_physical_to_frame_index(state, physical_address, &frame_index)
        && state->boot_info->frame_states[frame_index] == MICROS_VM_LOCAL_FRAME_FREE
        && !vm_core_physical_is_in_record_other_than(
            state, physical_address, MICROS_VM_MAPPING_RECORD_CAPACITY
        )
    );
}

static bool build_load_table_entries(
    const struct micros_vm_state *state,
    struct micros_vm_mapping_record *candidate
)
{
    static const uint8_t roles[MICROS_VM_PAGE_TABLE_FRAME_COUNT] = {
        MICROS_VM_MAPPING_ENTRY_ROOT_PAGE_TABLE,
        MICROS_VM_MAPPING_ENTRY_LEVEL1_PAGE_TABLE,
        MICROS_VM_MAPPING_ENTRY_IMAGE_PAGE_TABLE,
        MICROS_VM_MAPPING_ENTRY_STACK_PAGE_TABLE,
    };
    size_t index;

    for (index = 0; index < MICROS_VM_PAGE_TABLE_FRAME_COUNT; ++index) {
        uint64_t physical = state->transaction.table_frames[index];

        if (!selected_frame_is_available(state, physical)) {
            return false;
        }
        candidate->entries[index].physical_address = physical;
        candidate->entries[index].role = roles[index];
    }
    return true;
}

static bool build_load_data_entries(
    const struct micros_vm_state *state,
    struct micros_vm_mapping_record *candidate
)
{
    size_t index;

    for (index = 0; index < state->transaction.page_count; ++index) {
        struct micros_vm_mapping_entry *entry =
            &candidate->entries[MICROS_VM_PAGE_TABLE_FRAME_COUNT + index];
        uint64_t physical = state->transaction.data_frames[index];

        if (!selected_frame_is_available(state, physical)) {
            return false;
        }
        entry->virtual_address = index < state->transaction.elf_plan.page_count
            ? state->transaction.elf_plan.pages[index].virtual_address
            : MICROS_APPLICATION_STACK_BASE;
        entry->physical_address = physical;
        entry->permissions = index < state->transaction.elf_plan.page_count
            ? state->transaction.elf_plan.pages[index].permissions
            : MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE;
        entry->role = MICROS_VM_MAPPING_ENTRY_USER_PAGE;
    }
    return true;
}

enum micros_vm_core_error micros_vm_load_bind(
    struct micros_vm_state *state,
    struct micros_process_handle process,
    uint64_t load_token
)
{
    struct micros_vm_mapping_record candidate;
    enum micros_vm_core_error error = vm_core_validate_state(state, false);
    size_t index;

    if (error != MICROS_VM_CORE_OK) {
        return error;
    }
    if (
        state->phase != MICROS_VM_LOADER_INPUT_COPIED
        || !vm_core_process_is_canonical(process)
        || load_token == 0
        || !vm_core_record_is_zero(&state->mapping_records[process.slot])
        || !load_record_identity_is_available(state, load_token)
    ) {
        return MICROS_VM_CORE_ERROR_STATE;
    }
    vm_core_zero_bytes(&candidate, sizeof(candidate));
    candidate.load_token = load_token;
    candidate.semantic_pid = state->transaction.semantic_pid;
    candidate.process_generation = process.generation;
    candidate.process_slot = process.slot;
    candidate.phase = MICROS_VM_MAPPING_RECORD_LOADING;
    candidate.entry_count = (uint8_t)(
        MICROS_VM_PAGE_TABLE_FRAME_COUNT + state->transaction.page_count
    );
    if (
        !build_load_table_entries(state, &candidate)
        || !build_load_data_entries(state, &candidate)
    ) {
        return MICROS_VM_CORE_ERROR_INVARIANT;
    }
    for (index = 0; index < MICROS_VM_PAGE_TABLE_FRAME_COUNT; ++index) {
        uint64_t frame_index;

        (void)vm_core_physical_to_frame_index(
            state, state->transaction.table_frames[index], &frame_index
        );
        state->boot_info->frame_states[frame_index] = MICROS_VM_LOCAL_FRAME_LOAD_RESERVED;
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
