#include "micros/vm_bootstrap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FNV_OFFSET UINT64_C(14695981039346656037)
#define FNV_PRIME UINT64_C(1099511628211)

enum {
    VM_SERVICE_ID_MAX = 63,
};

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

static bool bytes_are_zero(const void *storage, size_t size)
{
    const uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool range_is_valid(uint64_t base, uint64_t size)
{
    return (
        size != 0
        && UINT64_MAX - base >= size
    );
}

static bool physical_ranges_are_canonical(
    const struct micros_vm_physical_range *ranges,
    size_t count
)
{
    size_t index;

    for (index = 0; index < count; ++index) {
        if (
            !range_is_valid(ranges[index].base, ranges[index].size)
            || ranges[index].base % MICROS_FRAME_SIZE != 0
            || ranges[index].size % MICROS_FRAME_SIZE != 0
            || (
                index != 0
                && ranges[index - 1].base
                    + ranges[index - 1].size
                    >= ranges[index].base
            )
        ) {
            return false;
        }
    }
    return true;
}

static bool managed_range_matches(
    const struct micros_vm_boot_info *info,
    size_t managed_index,
    uint64_t base,
    uint64_t end,
    uint64_t frame_index
)
{
    const struct micros_vm_managed_range *range;

    if (
        base >= end
        || managed_index >= info->header.managed_range_count
    ) {
        return false;
    }
    range = &info->managed_ranges[managed_index];
    return (
        range->base == base
        && range->frame_count == (end - base) / MICROS_FRAME_SIZE
        && range->frame_index == frame_index
    );
}

static bool managed_ranges_match_physical_database(
    const struct micros_vm_boot_info *info
)
{
    uint64_t frame_index = 0;
    size_t managed_index = 0;
    size_t memory_index;

    for (
        memory_index = 0;
        memory_index < info->header.memory_range_count;
        ++memory_index
    ) {
        const struct micros_vm_physical_range *memory =
            &info->memory_ranges[memory_index];
        uint64_t cursor = memory->base;
        uint64_t memory_end = memory->base + memory->size;
        size_t reserved_index;

        for (
            reserved_index = 0;
            reserved_index < info->header.reserved_range_count;
            ++reserved_index
        ) {
            const struct micros_vm_physical_range *reserved =
                &info->reserved_ranges[reserved_index];
            uint64_t reserved_end = reserved->base + reserved->size;
            uint64_t available_end;
            uint64_t frame_count;

            if (reserved_end <= cursor) {
                continue;
            }
            if (reserved->base >= memory_end) {
                break;
            }
            available_end = reserved->base < memory_end
                ? reserved->base
                : memory_end;
            if (cursor < available_end) {
                frame_count =
                    (available_end - cursor) / MICROS_FRAME_SIZE;
                if (
                    frame_count
                        > MICROS_VM_MAX_MANAGED_FRAMES - frame_index
                    || !managed_range_matches(
                        info,
                        managed_index,
                        cursor,
                        available_end,
                        frame_index
                    )
                ) {
                    return false;
                }
                ++managed_index;
                frame_index += frame_count;
            }
            if (reserved_end > cursor) {
                cursor = reserved_end;
            }
            if (cursor >= memory_end) {
                break;
            }
        }
        if (cursor < memory_end) {
            uint64_t frame_count =
                (memory_end - cursor) / MICROS_FRAME_SIZE;

            if (
                frame_count > MICROS_VM_MAX_MANAGED_FRAMES - frame_index
                || !managed_range_matches(
                    info,
                    managed_index,
                    cursor,
                    memory_end,
                    frame_index
                )
            ) {
                return false;
            }
            ++managed_index;
            frame_index += frame_count;
        }
    }
    return (
        managed_index == info->header.managed_range_count
        && frame_index == info->header.managed_frame_count
    );
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

static bool permissions_are_valid(uint8_t permissions)
{
    return (
        permissions != 0
        && (
            permissions & ~MICROS_VM_PERMISSION_DEFINED_MASK
        ) == 0
        && (
            (permissions & MICROS_VM_PERMISSION_WRITE) == 0
            || (
                permissions & MICROS_VM_PERMISSION_READ
            ) != 0
        )
        && (
            permissions
            & (
                MICROS_VM_PERMISSION_WRITE
                | MICROS_VM_PERMISSION_EXECUTE
            )
        ) != (
            MICROS_VM_PERMISSION_WRITE
            | MICROS_VM_PERMISSION_EXECUTE
        )
    );
}

static bool endpoint_is_valid(uint32_t endpoint)
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

static bool endpoint_matches_process(
    uint32_t endpoint,
    uint16_t process_slot,
    uint32_t process_generation
)
{
    return endpoint
        == (
            (process_generation << MICROS_ENDPOINT_SLOT_BITS)
            | process_slot
        );
}

enum micros_vm_boot_error micros_vm_boot_digest(
    const struct micros_vm_boot_info *info,
    uint64_t *digest
)
{
    const uint8_t *bytes = (const uint8_t *)info;
    size_t digest_offset =
        offsetof(struct micros_vm_boot_info, header.digest);
    uint64_t value = FNV_OFFSET;
    size_t index;

    if (
        info == NULL
        || digest == NULL
        || (uintptr_t)info % MICROS_VM_BOOT_INFO_ALIGNMENT != 0
    ) {
        return MICROS_VM_BOOT_ERROR_ARGUMENT;
    }
    for (index = 0; index < sizeof(*info); ++index) {
        uint8_t byte = (
            index >= digest_offset
            && index < digest_offset + sizeof(info->header.digest)
        )
            ? 0
            : bytes[index];

        value ^= byte;
        value *= FNV_PRIME;
    }
    *digest = value;
    return MICROS_VM_BOOT_OK;
}

enum micros_vm_boot_error micros_vm_boot_validate(
    const struct micros_vm_boot_info *info,
    struct micros_vm_boot_summary *summary
)
{
    struct micros_vm_boot_summary candidate;
    uint64_t free_count = 0;
    uint64_t kernel_count = 0;
    uint64_t self_count = 0;
    uint64_t service_count = 0;
    uint64_t transferable_count = 0;
    uint64_t digest;
    uint64_t wired_mapping_count = 0;
    uint32_t expected_mapping_index = 0;
    bool found_self = false;
    size_t index;

    if (
        info == NULL
        || summary == NULL
        || (uintptr_t)info % MICROS_VM_BOOT_INFO_ALIGNMENT != 0
    ) {
        return MICROS_VM_BOOT_ERROR_ARGUMENT;
    }
    if (
        info->header.version != MICROS_VM_BOOT_INFO_VERSION
        || info->header.header_size != MICROS_VM_BOOT_HEADER_SIZE
        || info->header.total_size != MICROS_VM_BOOT_INFO_SIZE
        || info->header.physical_range_size
            != MICROS_VM_PHYSICAL_RANGE_SIZE
        || info->header.managed_range_size
            != MICROS_VM_MANAGED_RANGE_SIZE
        || info->header.address_space_entry_size
            != MICROS_VM_ADDRESS_SPACE_SIZE
        || info->header.mapping_entry_size != MICROS_VM_MAPPING_SIZE
        || info->header.memory_range_capacity
            != MICROS_VM_MAX_MEMORY_RANGES
        || info->header.reserved_range_capacity
            != MICROS_VM_MAX_RESERVED_RANGES
        || info->header.managed_range_capacity
            != MICROS_VM_MAX_MANAGED_RANGES
        || info->header.address_space_capacity
            != MICROS_VM_MAX_STATIC_ADDRESS_SPACES
        || info->header.mapping_capacity
            != MICROS_VM_MAX_STATIC_MAPPINGS
        || info->header.frame_state_capacity
            != MICROS_VM_MAX_MANAGED_FRAMES
        || info->header.frame_state_size != 1
        || info->header.service_id == 0
        || info->header.service_id > VM_SERVICE_ID_MAX
        || !endpoint_is_valid(info->header.self_endpoint)
        || info->header.ownership_phase
            != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
        || info->header.flags != MICROS_VM_BOOT_FLAG_NONE
        || info->header.reserved0 != 0
        || !bytes_are_zero(
            info->header.reserved,
            sizeof(info->header.reserved)
        )
        || info->header.memory_range_count == 0
        || info->header.memory_range_count
            > MICROS_VM_MAX_MEMORY_RANGES
        || info->header.reserved_range_count
            > MICROS_VM_MAX_RESERVED_RANGES
        || info->header.managed_range_count == 0
        || info->header.managed_range_count
            > MICROS_VM_MAX_MANAGED_RANGES
        || info->header.address_space_count == 0
        || info->header.address_space_count
            > MICROS_VM_MAX_STATIC_ADDRESS_SPACES
        || info->header.mapping_count == 0
        || info->header.mapping_count
            > MICROS_VM_MAX_STATIC_MAPPINGS
        || info->header.managed_frame_count == 0
        || info->header.managed_frame_count
            > MICROS_VM_MAX_MANAGED_FRAMES
    ) {
        return MICROS_VM_BOOT_ERROR_SHAPE;
    }
    if (
        !physical_ranges_are_canonical(
            info->memory_ranges,
            info->header.memory_range_count
        )
        || !physical_ranges_are_canonical(
            info->reserved_ranges,
            info->header.reserved_range_count
        )
        || !managed_ranges_match_physical_database(info)
        || !bytes_are_zero(
            &info->memory_ranges[info->header.memory_range_count],
            (
                MICROS_VM_MAX_MEMORY_RANGES
                - info->header.memory_range_count
            ) * sizeof(info->memory_ranges[0])
        )
        || !bytes_are_zero(
            &info->reserved_ranges[info->header.reserved_range_count],
            (
                MICROS_VM_MAX_RESERVED_RANGES
                - info->header.reserved_range_count
            ) * sizeof(info->reserved_ranges[0])
        )
        || !bytes_are_zero(
            &info->managed_ranges[info->header.managed_range_count],
            (
                MICROS_VM_MAX_MANAGED_RANGES
                - info->header.managed_range_count
            ) * sizeof(info->managed_ranges[0])
        )
    ) {
        return MICROS_VM_BOOT_ERROR_RANGE;
    }
    for (
        index = 0;
        index < info->header.address_space_count;
        ++index
    ) {
        const struct micros_vm_address_space *space =
            &info->address_spaces[index];
        uint64_t root_frame_index;
        uint32_t mapping_end;
        size_t other;

        if (
            space->service_id == 0
            || space->service_id > VM_SERVICE_ID_MAX
            || !endpoint_is_valid(space->endpoint)
            || space->process_slot >= MICROS_PROCESS_CAPACITY
            || space->process_generation == 0
            || space->process_generation > MICROS_PROCESS_GENERATION_MAX
            || !endpoint_matches_process(
                space->endpoint,
                space->process_slot,
                space->process_generation
            )
            || space->mapping_count == 0
            || space->mapping_index != expected_mapping_index
            || space->flags != 0
            || space->root_physical_address % MICROS_FRAME_SIZE != 0
            || !physical_to_frame_index(
                info,
                space->root_physical_address,
                &root_frame_index
            )
            || info->frame_states[root_frame_index]
                != MICROS_VM_FRAME_KERNEL
            || space->mapping_index > UINT32_MAX - space->mapping_count
        ) {
            return MICROS_VM_BOOT_ERROR_IDENTITY;
        }
        mapping_end = space->mapping_index + space->mapping_count;
        if (mapping_end > info->header.mapping_count) {
            return MICROS_VM_BOOT_ERROR_RANGE;
        }
        for (other = 0; other < index; ++other) {
            if (
                info->address_spaces[other].service_id
                    == space->service_id
                || info->address_spaces[other].endpoint
                    == space->endpoint
                || info->address_spaces[other].process_slot
                    == space->process_slot
                || info->address_spaces[other].root_physical_address
                    == space->root_physical_address
            ) {
                return MICROS_VM_BOOT_ERROR_IDENTITY;
            }
        }
        if (space->service_id == info->header.service_id) {
            if (
                found_self
                || space->endpoint != info->header.self_endpoint
            ) {
                return MICROS_VM_BOOT_ERROR_IDENTITY;
            }
            found_self = true;
        }
        expected_mapping_index = mapping_end;
    }
    if (
        !found_self
        || expected_mapping_index != info->header.mapping_count
        || !bytes_are_zero(
            &info->address_spaces[info->header.address_space_count],
            (
                MICROS_VM_MAX_STATIC_ADDRESS_SPACES
                - info->header.address_space_count
            ) * sizeof(info->address_spaces[0])
        )
    ) {
        return MICROS_VM_BOOT_ERROR_IDENTITY;
    }
    for (index = 0; index < info->header.mapping_count; ++index) {
        const struct micros_vm_mapping *mapping = &info->mappings[index];
        const struct micros_vm_address_space *space = NULL;
        uint64_t frame_index;
        uint8_t expected_state;
        size_t space_index;
        size_t other;

        for (
            space_index = 0;
            space_index < info->header.address_space_count;
            ++space_index
        ) {
            uint32_t first =
                info->address_spaces[space_index].mapping_index;
            uint32_t end = first
                + info->address_spaces[space_index].mapping_count;

            if (index >= first && index < end) {
                space = &info->address_spaces[space_index];
                break;
            }
        }
        if (
            space == NULL
            || mapping->process_slot != space->process_slot
            || mapping->process_generation
                != space->process_generation
            || mapping->process_generation
                > MICROS_PROCESS_GENERATION_MAX
            || !permissions_are_valid(mapping->permissions)
            || mapping->virtual_address % MICROS_FRAME_SIZE != 0
            || mapping->virtual_address < MICROS_USER_VIRTUAL_BASE
            || mapping->virtual_address >= MICROS_USER_VIRTUAL_END
            || !physical_to_frame_index(
                info,
                mapping->physical_address,
                &frame_index
            )
        ) {
            return MICROS_VM_BOOT_ERROR_MAPPING;
        }
        expected_state = space->service_id
                == info->header.service_id
            ? MICROS_VM_FRAME_SELF_WIRED
            : MICROS_VM_FRAME_SERVICE_WIRED;
        if (
            mapping->role
                != (
                    expected_state == MICROS_VM_FRAME_SELF_WIRED
                    ? MICROS_VM_MAPPING_SELF_WIRED
                    : MICROS_VM_MAPPING_SERVICE_WIRED
                )
            || info->frame_states[frame_index] != expected_state
            || (
                index != space->mapping_index
                && info->mappings[index - 1].virtual_address
                    >= mapping->virtual_address
            )
        ) {
            return MICROS_VM_BOOT_ERROR_MAPPING;
        }
        for (other = 0; other < index; ++other) {
            if (
                info->mappings[other].physical_address
                    == mapping->physical_address
            ) {
                return MICROS_VM_BOOT_ERROR_MAPPING;
            }
        }
        ++wired_mapping_count;
    }
    if (
        !bytes_are_zero(
            &info->mappings[info->header.mapping_count],
            (
                MICROS_VM_MAX_STATIC_MAPPINGS
                - info->header.mapping_count
            ) * sizeof(info->mappings[0])
        )
    ) {
        return MICROS_VM_BOOT_ERROR_MAPPING;
    }
    for (
        index = 0;
        index < info->header.managed_frame_count;
        ++index
    ) {
        switch (info->frame_states[index]) {
        case MICROS_VM_FRAME_FREE:
            ++free_count;
            break;
        case MICROS_VM_FRAME_KERNEL:
            ++kernel_count;
            break;
        case MICROS_VM_FRAME_SELF_WIRED:
            ++self_count;
            break;
        case MICROS_VM_FRAME_SERVICE_WIRED:
            ++service_count;
            break;
        case MICROS_VM_FRAME_TRANSFERABLE:
            ++transferable_count;
            break;
        default:
            return MICROS_VM_BOOT_ERROR_MAPPING;
        }
    }
    if (
        !bytes_are_zero(
            &info->frame_states[info->header.managed_frame_count],
            MICROS_VM_MAX_MANAGED_FRAMES
                - info->header.managed_frame_count
        )
        || free_count != info->header.free_frame_count
        || kernel_count != info->header.kernel_frame_count
        || self_count != info->header.vm_self_wired_frame_count
        || service_count != info->header.service_wired_frame_count
        || transferable_count
            != info->header.transferable_frame_count
        || self_count == 0
        || transferable_count != 0
        || wired_mapping_count != self_count + service_count
        || free_count + kernel_count + self_count
                + service_count + transferable_count
            != info->header.managed_frame_count
    ) {
        return MICROS_VM_BOOT_ERROR_COUNT;
    }
    if (
        micros_vm_boot_digest(info, &digest) != MICROS_VM_BOOT_OK
        || digest != info->header.digest
    ) {
        return MICROS_VM_BOOT_ERROR_DIGEST;
    }
    candidate.version = info->header.version;
    candidate.managed_range_count =
        info->header.managed_range_count;
    candidate.address_space_count =
        info->header.address_space_count;
    candidate.mapping_count = info->header.mapping_count;
    candidate.managed_frame_count =
        info->header.managed_frame_count;
    candidate.free_frame_count = info->header.free_frame_count;
    candidate.vm_self_wired_frame_count =
        info->header.vm_self_wired_frame_count;
    candidate.digest = digest;
    copy_bytes(summary, &candidate, sizeof(*summary));
    return MICROS_VM_BOOT_OK;
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
        struct micros_frame_owner owner =
            ownership->owners[frame_index];
        uint8_t target = ownership->handoff_targets[frame_index];
        bool allocated = allocator_bit_is_set(allocator, frame_index);

        switch (owner.kind) {
        case MICROS_FRAME_OWNER_FREE:
            if (allocated || target != MICROS_FRAME_HANDOFF_NONE) {
                return MICROS_VM_BOOT_ERROR_STATE;
            }
            info->frame_states[frame_index] = MICROS_VM_FRAME_FREE;
            ++info->header.free_frame_count;
            break;
        case MICROS_FRAME_OWNER_KERNEL_RETAINED:
        case MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE:
        case MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE:
            if (!allocated || target != MICROS_FRAME_HANDOFF_NONE) {
                return MICROS_VM_BOOT_ERROR_STATE;
            }
            info->frame_states[frame_index] = MICROS_VM_FRAME_KERNEL;
            ++info->header.kernel_frame_count;
            break;
        case MICROS_FRAME_OWNER_PROCESS_USER:
            if (
                !allocated
                || target != MICROS_FRAME_HANDOFF_VM_WIRED
            ) {
                return MICROS_VM_BOOT_ERROR_STATE;
            }
            if (
                owner.slot == vm_process.slot
                && owner.generation == vm_process.generation
            ) {
                info->frame_states[frame_index] =
                    MICROS_VM_FRAME_SELF_WIRED;
                ++info->header.vm_self_wired_frame_count;
            } else {
                info->frame_states[frame_index] =
                    MICROS_VM_FRAME_SERVICE_WIRED;
                ++info->header.service_wired_frame_count;
            }
            break;
        case MICROS_FRAME_OWNER_KERNEL_TEMPORARY:
        case MICROS_FRAME_OWNER_VM_WIRED:
        case MICROS_FRAME_OWNER_VM_TRANSFERABLE:
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
