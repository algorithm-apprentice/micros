#include "micros/vm_bootstrap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static void copy_bytes(void *destination, const void *source, size_t size)
{
    uint8_t *output = destination;
    const uint8_t *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
    }
}

static void clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static bool physical_to_frame_index(
    const struct micros_vm_boot_info *info,
    uint64_t physical_address,
    uint64_t *frame_index
)
{
    size_t index;

    if (
        physical_address % MICROS_FRAME_SIZE != 0
        || frame_index == NULL
    ) {
        return false;
    }
    for (
        index = 0;
        index < info->header.managed_range_count;
        ++index
    ) {
        const struct micros_vm_managed_range *range =
            &info->managed_ranges[index];
        uint64_t size = range->frame_count * MICROS_FRAME_SIZE;

        if (
            physical_address >= range->base
            && physical_address - range->base < size
        ) {
            *frame_index = range->frame_index
                + (physical_address - range->base)
                    / MICROS_FRAME_SIZE;
            return true;
        }
    }
    return false;
}

enum micros_vm_boot_error micros_vm_boot_finalize(
    struct micros_vm_boot_info *info,
    struct micros_vm_boot_summary *summary
)
{
    uint64_t digest;
    enum micros_vm_boot_error error;

    if (
        info == NULL
        || summary == NULL
        || (uintptr_t)info % MICROS_VM_BOOT_INFO_ALIGNMENT != 0
    ) {
        return MICROS_VM_BOOT_ERROR_ARGUMENT;
    }
    if (info->header.digest != 0) {
        return MICROS_VM_BOOT_ERROR_STATE;
    }
    error = micros_vm_boot_digest(info, &digest);
    if (error != MICROS_VM_BOOT_OK) {
        return error;
    }
    info->header.digest = digest;
    error = micros_vm_boot_validate(info, summary);
    if (error != MICROS_VM_BOOT_OK) {
        info->header.digest = 0;
    }
    return error;
}

static enum micros_vm_boot_error clear_build_failure(
    struct micros_vm_boot_info *info,
    enum micros_vm_boot_error error
)
{
    clear_bytes(info, sizeof(*info));
    return error;
}

static bool allocator_bit_is_set(
    const struct micros_frame_allocator *allocator,
    uint64_t frame_index
)
{
    size_t word_index = (size_t)(frame_index / 64);
    uint64_t bit = UINT64_C(1) << (frame_index % 64);

    return (allocator->allocated_bitmap[word_index] & bit) != 0;
}

static bool owner_matches_process(
    struct micros_frame_owner owner,
    enum micros_frame_owner_kind kind,
    struct micros_process_handle process
)
{
    return (
        owner.kind == kind
        && owner.slot == process.slot
        && owner.generation == process.generation
        && owner.reserved == 0
    );
}

static bool process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

static void initialize_build_header(
    struct micros_vm_boot_info *info,
    const struct micros_frame_allocator *allocator,
    uint32_t vm_service_id,
    uint32_t vm_endpoint,
    size_t address_space_count,
    size_t mapping_count
)
{
    info->header.version = MICROS_VM_BOOT_INFO_VERSION;
    info->header.header_size = MICROS_VM_BOOT_HEADER_SIZE;
    info->header.total_size = MICROS_VM_BOOT_INFO_SIZE;
    info->header.physical_range_size = MICROS_VM_PHYSICAL_RANGE_SIZE;
    info->header.managed_range_size = MICROS_VM_MANAGED_RANGE_SIZE;
    info->header.memory_range_capacity =
        MICROS_VM_MAX_MEMORY_RANGES;
    info->header.memory_range_count =
        (uint32_t)allocator->memory_range_count;
    info->header.reserved_range_capacity =
        MICROS_VM_MAX_RESERVED_RANGES;
    info->header.reserved_range_count =
        (uint32_t)allocator->reserved_range_count;
    info->header.managed_range_capacity =
        MICROS_VM_MAX_MANAGED_RANGES;
    info->header.managed_range_count =
        (uint32_t)allocator->managed_range_count;
    info->header.frame_state_capacity =
        MICROS_VM_MAX_MANAGED_FRAMES;
    info->header.frame_state_size = 1;
    info->header.service_id = vm_service_id;
    info->header.self_endpoint = vm_endpoint;
    info->header.managed_frame_count =
        allocator->managed_frame_count;
    info->header.ownership_phase =
        MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP;
    info->header.address_space_entry_size =
        MICROS_VM_ADDRESS_SPACE_SIZE;
    info->header.address_space_capacity =
        MICROS_VM_MAX_STATIC_ADDRESS_SPACES;
    info->header.address_space_count = (uint32_t)address_space_count;
    info->header.mapping_entry_size = MICROS_VM_MAPPING_SIZE;
    info->header.mapping_capacity = MICROS_VM_MAX_STATIC_MAPPINGS;
    info->header.mapping_count = (uint32_t)mapping_count;
}

static bool copy_build_ranges(
    struct micros_vm_boot_info *info,
    const struct micros_frame_allocator *allocator
)
{
    size_t index;

    if (
        allocator->memory_range_count > MICROS_VM_MAX_MEMORY_RANGES
        || allocator->reserved_range_count
            > MICROS_VM_MAX_RESERVED_RANGES
        || allocator->managed_range_count
            > MICROS_VM_MAX_MANAGED_RANGES
    ) {
        return false;
    }
    for (
        index = 0;
        index < allocator->memory_range_count;
        ++index
    ) {
        info->memory_ranges[index] =
            (struct micros_vm_physical_range){
                .base = allocator->memory_ranges[index].base,
                .size = allocator->memory_ranges[index].size,
            };
    }
    for (
        index = 0;
        index < allocator->reserved_range_count;
        ++index
    ) {
        info->reserved_ranges[index] =
            (struct micros_vm_physical_range){
                .base = allocator->reserved_ranges[index].base,
                .size = allocator->reserved_ranges[index].size,
            };
    }
    for (
        index = 0;
        index < allocator->managed_range_count;
        ++index
    ) {
        info->managed_ranges[index] =
            (struct micros_vm_managed_range){
                .base = allocator->managed_ranges[index].base,
                .frame_count =
                    allocator->managed_ranges[index].frame_count,
                .frame_index =
                    allocator->managed_ranges[index].bitmap_offset,
            };
    }
    return true;
}

static enum micros_vm_boot_error authoritative_frame_state(
    const struct micros_frame_allocator *allocator,
    const struct micros_frame_ownership *ownership,
    struct micros_process_handle vm_process,
    uint64_t frame_index,
    uint8_t *state
)
{
    struct micros_frame_owner owner =
        ownership->owners[frame_index];
    uint8_t target = ownership->handoff_targets[frame_index];
    bool allocated = allocator_bit_is_set(allocator, frame_index);

    switch (owner.kind) {
    case MICROS_FRAME_OWNER_FREE:
        if (allocated || target != MICROS_FRAME_HANDOFF_NONE) {
            return MICROS_VM_BOOT_ERROR_STATE;
        }
        *state = MICROS_VM_FRAME_FREE;
        return MICROS_VM_BOOT_OK;
    case MICROS_FRAME_OWNER_KERNEL_RETAINED:
    case MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE:
    case MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE:
        if (!allocated || target != MICROS_FRAME_HANDOFF_NONE) {
            return MICROS_VM_BOOT_ERROR_STATE;
        }
        *state = MICROS_VM_FRAME_KERNEL;
        return MICROS_VM_BOOT_OK;
    case MICROS_FRAME_OWNER_PROCESS_USER:
        if (
            !allocated
            || target != MICROS_FRAME_HANDOFF_VM_WIRED
        ) {
            return MICROS_VM_BOOT_ERROR_STATE;
        }
        *state = (
            owner.slot == vm_process.slot
            && owner.generation == vm_process.generation
        )
            ? MICROS_VM_FRAME_SELF_WIRED
            : MICROS_VM_FRAME_SERVICE_WIRED;
        return MICROS_VM_BOOT_OK;
    case MICROS_FRAME_OWNER_KERNEL_TEMPORARY:
    case MICROS_FRAME_OWNER_VM_WIRED:
    case MICROS_FRAME_OWNER_VM_TRANSFERABLE:
    default:
        return MICROS_VM_BOOT_ERROR_STATE;
    }
}

static enum micros_vm_boot_error derive_build_frame_states(
    struct micros_vm_boot_info *info,
    const struct micros_frame_allocator *allocator,
    const struct micros_frame_ownership *ownership,
    struct micros_process_handle vm_process
)
{
    uint64_t frame_index;

    for (
        frame_index = 0;
        frame_index < ownership->managed_frame_count;
        ++frame_index
    ) {
        uint8_t state;
        enum micros_vm_boot_error error =
            authoritative_frame_state(
                allocator,
                ownership,
                vm_process,
                frame_index,
                &state
            );

        if (error != MICROS_VM_BOOT_OK) {
            return error;
        }
        info->frame_states[frame_index] = state;
        switch (state) {
        case MICROS_VM_FRAME_FREE:
            ++info->header.free_frame_count;
            break;
        case MICROS_VM_FRAME_KERNEL:
            ++info->header.kernel_frame_count;
            break;
        case MICROS_VM_FRAME_SELF_WIRED:
            ++info->header.vm_self_wired_frame_count;
            break;
        case MICROS_VM_FRAME_SERVICE_WIRED:
            ++info->header.service_wired_frame_count;
            break;
        default:
            return MICROS_VM_BOOT_ERROR_STATE;
        }
    }
    return MICROS_VM_BOOT_OK;
}

static enum micros_vm_boot_error bind_build_inventory(
    struct micros_vm_boot_info *info,
    const struct micros_frame_allocator *allocator,
    const struct micros_frame_ownership *ownership,
    uint32_t vm_service_id,
    uint32_t vm_endpoint,
    struct micros_process_handle vm_process
)
{
    bool found_vm = false;
    size_t space_index;

    for (
        space_index = 0;
        space_index < info->header.address_space_count;
        ++space_index
    ) {
        struct micros_vm_address_space *space =
            &info->address_spaces[space_index];
        struct micros_process_handle process = {
            .slot = space->process_slot,
            .generation = space->process_generation,
        };
        uint64_t root_index;
        size_t mapping_index;
        size_t mapping_end;
        bool is_vm = (
            space->service_id == vm_service_id
            && space->endpoint == vm_endpoint
            && process_handles_equal(process, vm_process)
        );

        if (
            space->mapping_index > info->header.mapping_count
            || space->mapping_count
                > info->header.mapping_count - space->mapping_index
            || !physical_to_frame_index(
                info,
                space->root_physical_address,
                &root_index
            )
            || !owner_matches_process(
                ownership->owners[root_index],
                MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
                process
            )
            || !allocator_bit_is_set(allocator, root_index)
            || ownership->handoff_targets[root_index]
                != MICROS_FRAME_HANDOFF_NONE
        ) {
            return MICROS_VM_BOOT_ERROR_IDENTITY;
        }
        if (is_vm) {
            if (found_vm) {
                return MICROS_VM_BOOT_ERROR_IDENTITY;
            }
            found_vm = true;
        } else if (
            space->service_id == vm_service_id
            || space->endpoint == vm_endpoint
            || process_handles_equal(process, vm_process)
        ) {
            return MICROS_VM_BOOT_ERROR_IDENTITY;
        }
        mapping_end = space->mapping_index + space->mapping_count;
        for (
            mapping_index = space->mapping_index;
            mapping_index < mapping_end;
            ++mapping_index
        ) {
            struct micros_vm_mapping *mapping =
                &info->mappings[mapping_index];
            uint64_t frame_index;

            if (
                mapping->role != 0
                || !physical_to_frame_index(
                    info,
                    mapping->physical_address,
                    &frame_index
                )
                || !owner_matches_process(
                    ownership->owners[frame_index],
                    MICROS_FRAME_OWNER_PROCESS_USER,
                    process
                )
                || !allocator_bit_is_set(allocator, frame_index)
                || ownership->handoff_targets[frame_index]
                    != MICROS_FRAME_HANDOFF_VM_WIRED
            ) {
                return MICROS_VM_BOOT_ERROR_MAPPING;
            }
            mapping->role = is_vm
                ? MICROS_VM_MAPPING_SELF_WIRED
                : MICROS_VM_MAPPING_SERVICE_WIRED;
        }
    }
    return found_vm
        ? MICROS_VM_BOOT_OK
        : MICROS_VM_BOOT_ERROR_IDENTITY;
}

enum micros_vm_boot_error micros_vm_boot_build(
    struct micros_vm_boot_info *info,
    size_t address_space_count,
    size_t mapping_count,
    const struct micros_frame_allocator *allocator,
    const struct micros_frame_ownership *ownership,
    uint32_t vm_service_id,
    uint32_t vm_endpoint,
    struct micros_process_handle vm_process,
    struct micros_vm_boot_summary *summary
)
{
    struct micros_vm_boot_summary candidate;
    enum micros_vm_boot_error error;

    if (
        info == NULL
        || allocator == NULL
        || ownership == NULL
        || summary == NULL
        || (uintptr_t)info % MICROS_VM_BOOT_INFO_ALIGNMENT != 0
        || (uintptr_t)allocator
            % _Alignof(struct micros_frame_allocator) != 0
        || (uintptr_t)ownership
            % _Alignof(struct micros_frame_ownership) != 0
        || address_space_count == 0
        || address_space_count > MICROS_VM_MAX_STATIC_ADDRESS_SPACES
        || mapping_count == 0
        || mapping_count > MICROS_VM_MAX_STATIC_MAPPINGS
    ) {
        return MICROS_VM_BOOT_ERROR_ARGUMENT;
    }
    if (
        ownership->allocator != allocator
        || ownership->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
        || micros_frame_ownership_validate(ownership)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        return clear_build_failure(
            info,
            MICROS_VM_BOOT_ERROR_STATE
        );
    }

    clear_bytes(&info->header, sizeof(info->header));
    clear_bytes(info->memory_ranges, sizeof(info->memory_ranges));
    clear_bytes(info->reserved_ranges, sizeof(info->reserved_ranges));
    clear_bytes(info->managed_ranges, sizeof(info->managed_ranges));
    clear_bytes(info->frame_states, sizeof(info->frame_states));
    initialize_build_header(
        info,
        allocator,
        vm_service_id,
        vm_endpoint,
        address_space_count,
        mapping_count
    );
    if (!copy_build_ranges(info, allocator)) {
        return clear_build_failure(
            info,
            MICROS_VM_BOOT_ERROR_RANGE
        );
    }
    error = derive_build_frame_states(
        info,
        allocator,
        ownership,
        vm_process
    );
    if (error != MICROS_VM_BOOT_OK) {
        return clear_build_failure(info, error);
    }
    error = bind_build_inventory(
        info,
        allocator,
        ownership,
        vm_service_id,
        vm_endpoint,
        vm_process
    );
    if (error != MICROS_VM_BOOT_OK) {
        return clear_build_failure(info, error);
    }
    error = micros_vm_boot_finalize(info, &candidate);
    if (error != MICROS_VM_BOOT_OK) {
        return clear_build_failure(info, error);
    }
    copy_bytes(summary, &candidate, sizeof(candidate));
    return MICROS_VM_BOOT_OK;
}

static bool authority_ranges_match(
    const struct micros_vm_boot_info *info,
    const struct micros_frame_allocator *allocator
)
{
    size_t index;

    if (
        allocator->memory_range_count
            != info->header.memory_range_count
        || allocator->reserved_range_count
            != info->header.reserved_range_count
        || allocator->managed_range_count
            != info->header.managed_range_count
        || allocator->managed_frame_count
            != info->header.managed_frame_count
        || allocator->free_frame_count
            != info->header.free_frame_count
    ) {
        return false;
    }
    for (
        index = 0;
        index < allocator->memory_range_count;
        ++index
    ) {
        if (
            info->memory_ranges[index].base
                != allocator->memory_ranges[index].base
            || info->memory_ranges[index].size
                != allocator->memory_ranges[index].size
        ) {
            return false;
        }
    }
    for (
        index = 0;
        index < allocator->reserved_range_count;
        ++index
    ) {
        if (
            info->reserved_ranges[index].base
                != allocator->reserved_ranges[index].base
            || info->reserved_ranges[index].size
                != allocator->reserved_ranges[index].size
        ) {
            return false;
        }
    }
    for (
        index = 0;
        index < allocator->managed_range_count;
        ++index
    ) {
        if (
            info->managed_ranges[index].base
                != allocator->managed_ranges[index].base
            || info->managed_ranges[index].frame_count
                != allocator->managed_ranges[index].frame_count
            || info->managed_ranges[index].frame_index
                != allocator->managed_ranges[index].bitmap_offset
        ) {
            return false;
        }
    }
    return true;
}

static enum micros_vm_boot_error
validate_authoritative_inventory(
    const struct micros_vm_boot_info *info,
    const struct micros_frame_allocator *allocator,
    const struct micros_frame_ownership *ownership,
    struct micros_process_handle vm_process
)
{
    bool found_vm = false;
    size_t space_index;

    for (
        space_index = 0;
        space_index < info->header.address_space_count;
        ++space_index
    ) {
        const struct micros_vm_address_space *space =
            &info->address_spaces[space_index];
        struct micros_process_handle process = {
            .slot = space->process_slot,
            .generation = space->process_generation,
        };
        uint64_t root_index;
        size_t mapping_index;
        size_t mapping_end =
            space->mapping_index + space->mapping_count;
        bool is_vm = (
            space->service_id == info->header.service_id
            && space->endpoint == info->header.self_endpoint
            && process_handles_equal(process, vm_process)
        );

        if (
            !physical_to_frame_index(
                info,
                space->root_physical_address,
                &root_index
            )
            || !owner_matches_process(
                ownership->owners[root_index],
                MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
                process
            )
            || !allocator_bit_is_set(allocator, root_index)
            || ownership->handoff_targets[root_index]
                != MICROS_FRAME_HANDOFF_NONE
        ) {
            return MICROS_VM_BOOT_ERROR_IDENTITY;
        }
        if (is_vm) {
            if (found_vm) {
                return MICROS_VM_BOOT_ERROR_IDENTITY;
            }
            found_vm = true;
        } else if (
            space->service_id == info->header.service_id
            || space->endpoint == info->header.self_endpoint
            || process_handles_equal(process, vm_process)
        ) {
            return MICROS_VM_BOOT_ERROR_IDENTITY;
        }
        for (
            mapping_index = space->mapping_index;
            mapping_index < mapping_end;
            ++mapping_index
        ) {
            const struct micros_vm_mapping *mapping =
                &info->mappings[mapping_index];
            uint64_t frame_index;
            uint8_t expected_role = is_vm
                ? MICROS_VM_MAPPING_SELF_WIRED
                : MICROS_VM_MAPPING_SERVICE_WIRED;

            if (
                mapping->role != expected_role
                || !physical_to_frame_index(
                    info,
                    mapping->physical_address,
                    &frame_index
                )
                || !owner_matches_process(
                    ownership->owners[frame_index],
                    MICROS_FRAME_OWNER_PROCESS_USER,
                    process
                )
                || !allocator_bit_is_set(allocator, frame_index)
                || ownership->handoff_targets[frame_index]
                    != MICROS_FRAME_HANDOFF_VM_WIRED
            ) {
                return MICROS_VM_BOOT_ERROR_MAPPING;
            }
        }
    }
    return found_vm
        ? MICROS_VM_BOOT_OK
        : MICROS_VM_BOOT_ERROR_IDENTITY;
}

enum micros_vm_boot_error micros_vm_boot_validate_authority(
    const struct micros_vm_boot_info *info,
    const struct micros_frame_allocator *allocator,
    const struct micros_frame_ownership *ownership,
    struct micros_process_handle vm_process
)
{
    struct micros_vm_boot_summary summary;
    uint64_t frame_index;
    enum micros_vm_boot_error error;

    if (
        info == NULL
        || allocator == NULL
        || ownership == NULL
        || (uintptr_t)info % MICROS_VM_BOOT_INFO_ALIGNMENT != 0
        || (uintptr_t)allocator
            % _Alignof(struct micros_frame_allocator) != 0
        || (uintptr_t)ownership
            % _Alignof(struct micros_frame_ownership) != 0
    ) {
        return MICROS_VM_BOOT_ERROR_ARGUMENT;
    }
    if (
        ownership->allocator != allocator
        || ownership->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
        || micros_frame_ownership_validate(ownership)
            != MICROS_FRAME_OWNERSHIP_OK
        || micros_vm_boot_validate(info, &summary)
            != MICROS_VM_BOOT_OK
        || !authority_ranges_match(info, allocator)
    ) {
        return MICROS_VM_BOOT_ERROR_STATE;
    }
    for (
        frame_index = 0;
        frame_index < ownership->managed_frame_count;
        ++frame_index
    ) {
        uint8_t state;

        error = authoritative_frame_state(
            allocator,
            ownership,
            vm_process,
            frame_index,
            &state
        );
        if (
            error != MICROS_VM_BOOT_OK
            || info->frame_states[frame_index] != state
        ) {
            return MICROS_VM_BOOT_ERROR_STATE;
        }
    }
    return validate_authoritative_inventory(
        info,
        allocator,
        ownership,
        vm_process
    );
}
