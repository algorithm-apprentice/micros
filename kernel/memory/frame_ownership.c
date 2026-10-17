#include "kernel/memory/frame_ownership_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MICROS_FRAME_OWNERSHIP_MAGIC UINT64_C(0x4d4943524f53464f)

void micros_frame_ownership_clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

void micros_frame_ownership_copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *output = destination;
    const unsigned char *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
    }
}

static bool bytes_are_zero(const void *storage, size_t size)
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

bool micros_frame_owner_is_equal(
    struct micros_frame_owner left,
    struct micros_frame_owner right
)
{
    return (
        left.generation == right.generation
        && left.slot == right.slot
        && left.kind == right.kind
        && left.reserved == right.reserved
    );
}

bool micros_frame_process_handle_is_valid(
    struct micros_process_handle process
)
{
    return (
        process.slot < MICROS_PROCESS_CAPACITY
        && process.generation != 0
        && process.generation <= MICROS_PROCESS_GENERATION_MAX
    );
}

bool micros_frame_owner_kind_is_process_bound(uint8_t kind)
{
    return (
        kind == MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
        || kind == MICROS_FRAME_OWNER_PROCESS_USER
        || kind == MICROS_FRAME_OWNER_VM_WIRED
    );
}

bool micros_frame_owner_is_valid(struct micros_frame_owner owner)
{
    if (
        owner.kind >= MICROS_FRAME_OWNER_KIND_COUNT
        || owner.reserved != 0
    ) {
        return false;
    }
    if (micros_frame_owner_kind_is_process_bound(owner.kind)) {
        return micros_frame_process_handle_is_valid(
            (struct micros_process_handle){
                owner.slot,
                owner.generation,
            }
        );
    }
    return owner.slot == 0 && owner.generation == 0;
}

static bool range_snapshot_is_zero(
    const struct micros_frame_range_snapshot *range
)
{
    return (
        range->base == 0
        && range->frame_count == 0
        && range->bitmap_offset == 0
    );
}

static bool managed_range_is_zero(
    const struct micros_managed_frame_range *range
)
{
    return (
        range->base == 0
        && range->frame_count == 0
        && range->bitmap_offset == 0
    );
}

static bool allocator_geometry_is_valid(
    const struct micros_frame_allocator *allocator
)
{
    uint64_t expected_bitmap_offset = 0;
    uint64_t previous_end = 0;
    size_t index;

    if (
        allocator == NULL
        || !allocator->initialized
        || allocator->managed_range_count == 0
        || allocator->managed_range_count
            > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES
        || allocator->managed_frame_count == 0
        || allocator->managed_frame_count
            > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES
        || allocator->free_frame_count
            > allocator->managed_frame_count
    ) {
        return false;
    }
    for (
        index = 0;
        index < allocator->managed_range_count;
        ++index
    ) {
        const struct micros_managed_frame_range *range =
            &allocator->managed_ranges[index];
        uint64_t range_size;
        uint64_t range_end;

        if (
            range->frame_count == 0
            || range->frame_count
                > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES
                    - expected_bitmap_offset
            || range->base % MICROS_FRAME_SIZE != 0
            || range->bitmap_offset != expected_bitmap_offset
            || range->frame_count
                > UINT64_MAX / MICROS_FRAME_SIZE
        ) {
            return false;
        }
        range_size = range->frame_count * MICROS_FRAME_SIZE;
        if (
            range_size > UINT64_MAX - range->base
            || (index != 0 && range->base < previous_end)
        ) {
            return false;
        }
        range_end = range->base + range_size;
        expected_bitmap_offset += range->frame_count;
        previous_end = range_end;
    }
    if (expected_bitmap_offset != allocator->managed_frame_count) {
        return false;
    }
    for (
        index = allocator->managed_range_count;
        index < MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES;
        ++index
    ) {
        if (!managed_range_is_zero(&allocator->managed_ranges[index])) {
            return false;
        }
    }
    return true;
}

static bool allocator_is_pristine(
    const struct micros_frame_allocator *allocator
)
{
    size_t index;

    if (
        !allocator_geometry_is_valid(allocator)
        || allocator->free_frame_count
            != allocator->managed_frame_count
    ) {
        return false;
    }
    for (
        index = 0;
        index < MICROS_FRAME_ALLOCATOR_BITMAP_WORDS;
        ++index
    ) {
        if (allocator->allocated_bitmap[index] != 0) {
            return false;
        }
    }
    return true;
}

enum micros_frame_ownership_error micros_frame_ownership_require_initialized(
    const struct micros_frame_ownership *ownership
)
{
    if (ownership == NULL) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    if (
        ownership->initialization_magic
            != MICROS_FRAME_OWNERSHIP_MAGIC
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_NOT_INITIALIZED;
    }
    if (
        ownership->allocator == NULL
        || ownership->allocator_identity
            != (uintptr_t)ownership->allocator
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    return MICROS_FRAME_OWNERSHIP_OK;
}

bool micros_frame_ownership_geometry_matches(
    const struct micros_frame_ownership *ownership
)
{
    const struct micros_frame_allocator *allocator =
        ownership->allocator;
    uint64_t expected_bitmap_offset = 0;
    uint64_t previous_end = 0;
    size_t index;

    if (
        ownership->allocator_identity != (uintptr_t)allocator
        || !allocator_geometry_is_valid(allocator)
        || ownership->managed_range_count == 0
        || ownership->managed_range_count
            > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES
        || ownership->managed_frame_count == 0
        || ownership->managed_frame_count
            > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES
        || allocator->managed_range_count
            != ownership->managed_range_count
        || allocator->managed_frame_count
            != ownership->managed_frame_count
    ) {
        return false;
    }
    for (
        index = 0;
        index < ownership->managed_range_count;
        ++index
    ) {
        const struct micros_frame_range_snapshot *snapshot =
            &ownership->managed_ranges[index];
        const struct micros_managed_frame_range *current =
            &allocator->managed_ranges[index];
        uint64_t range_size;
        uint64_t range_end;

        if (
            snapshot->frame_count == 0
            || snapshot->frame_count
                > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES
                    - expected_bitmap_offset
            || snapshot->base % MICROS_FRAME_SIZE != 0
            || snapshot->bitmap_offset != expected_bitmap_offset
            || snapshot->frame_count
                > UINT64_MAX / MICROS_FRAME_SIZE
            || snapshot->base != current->base
            || snapshot->frame_count != current->frame_count
            || snapshot->bitmap_offset != current->bitmap_offset
        ) {
            return false;
        }
        range_size = snapshot->frame_count * MICROS_FRAME_SIZE;
        if (
            range_size > UINT64_MAX - snapshot->base
            || (index != 0 && snapshot->base < previous_end)
        ) {
            return false;
        }
        range_end = snapshot->base + range_size;
        expected_bitmap_offset += snapshot->frame_count;
        previous_end = range_end;
    }
    if (expected_bitmap_offset != ownership->managed_frame_count) {
        return false;
    }
    for (
        index = ownership->managed_range_count;
        index < MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES;
        ++index
    ) {
        if (!range_snapshot_is_zero(&ownership->managed_ranges[index])) {
            return false;
        }
    }
    return true;
}

enum micros_frame_ownership_error micros_frame_ownership_physical_address_to_index(
    const struct micros_frame_ownership *ownership,
    uint64_t physical_address,
    uint64_t *frame_index
)
{
    size_t range_index;

    if (physical_address % MICROS_FRAME_SIZE != 0) {
        return MICROS_FRAME_OWNERSHIP_ERROR_UNALIGNED;
    }
    for (
        range_index = 0;
        range_index < ownership->managed_range_count;
        ++range_index
    ) {
        const struct micros_frame_range_snapshot *range =
            &ownership->managed_ranges[range_index];
        uint64_t offset;
        uint64_t index_in_range;

        if (physical_address < range->base) {
            break;
        }
        offset = physical_address - range->base;
        index_in_range = offset / MICROS_FRAME_SIZE;
        if (index_in_range < range->frame_count) {
            *frame_index = range->bitmap_offset + index_in_range;
            return MICROS_FRAME_OWNERSHIP_OK;
        }
    }
    return MICROS_FRAME_OWNERSHIP_ERROR_UNMANAGED;
}

bool micros_frame_ownership_allocator_bit_is_set(
    const struct micros_frame_allocator *allocator,
    uint64_t frame_index
)
{
    size_t word_index = (size_t)(frame_index / 64);
    uint64_t bit = UINT64_C(1) << (frame_index % 64);

    return (allocator->allocated_bitmap[word_index] & bit) != 0;
}

bool micros_frame_ownership_handoff_target_is_valid(
    struct micros_frame_owner owner,
    uint8_t target
)
{
    if (target == MICROS_FRAME_HANDOFF_NONE) {
        return true;
    }
    if (owner.kind == MICROS_FRAME_OWNER_PROCESS_USER) {
        return (
            target == MICROS_FRAME_HANDOFF_VM_WIRED
            || target == MICROS_FRAME_HANDOFF_VM_TRANSFERABLE
        );
    }
    return (
        owner.kind == MICROS_FRAME_OWNER_KERNEL_RETAINED
        && target == MICROS_FRAME_HANDOFF_VM_TRANSFERABLE
    );
}

enum micros_frame_ownership_error micros_frame_owner_make_kernel(
    enum micros_frame_owner_kind kind,
    struct micros_frame_owner *owner
)
{
    struct micros_frame_owner result;

    if (owner == NULL) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    result = (struct micros_frame_owner){
        0,
        0,
        (uint8_t)kind,
        0,
    };
    if (
        kind != MICROS_FRAME_OWNER_KERNEL_RETAINED
        && kind != MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE
        && kind != MICROS_FRAME_OWNER_KERNEL_TEMPORARY
        && kind != MICROS_FRAME_OWNER_VM_TRANSFERABLE
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_OWNER;
    }
    *owner = result;
    return MICROS_FRAME_OWNERSHIP_OK;
}

enum micros_frame_ownership_error micros_frame_owner_make_process(
    enum micros_frame_owner_kind kind,
    struct micros_process_handle process,
    struct micros_frame_owner *owner
)
{
    struct micros_frame_owner result;

    if (owner == NULL) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    if (
        (
            kind != MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
            && kind != MICROS_FRAME_OWNER_PROCESS_USER
            && kind != MICROS_FRAME_OWNER_VM_WIRED
        )
        || !micros_frame_process_handle_is_valid(process)
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_OWNER;
    }
    result = (struct micros_frame_owner){
        process.generation,
        process.slot,
        (uint8_t)kind,
        0,
    };
    *owner = result;
    return MICROS_FRAME_OWNERSHIP_OK;
}

enum micros_frame_ownership_error micros_frame_ownership_initialize(
    struct micros_frame_ownership *ownership,
    struct micros_frame_allocator *allocator
)
{
    size_t index;

    if (ownership == NULL || allocator == NULL) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    if (
        ownership->initialization_magic
            == MICROS_FRAME_OWNERSHIP_MAGIC
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ALREADY_INITIALIZED;
    }
    if (!bytes_are_zero(ownership, sizeof(*ownership))) {
        return MICROS_FRAME_OWNERSHIP_ERROR_STORAGE;
    }
    if (!allocator_is_pristine(allocator)) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ALLOCATOR_STATE;
    }

    ownership->allocator = allocator;
    ownership->allocator_identity = (uintptr_t)allocator;
    ownership->phase = MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP;
    ownership->managed_range_count = allocator->managed_range_count;
    ownership->managed_frame_count = allocator->managed_frame_count;
    for (index = 0; index < allocator->managed_range_count; ++index) {
        ownership->managed_ranges[index].base =
            allocator->managed_ranges[index].base;
        ownership->managed_ranges[index].frame_count =
            allocator->managed_ranges[index].frame_count;
        ownership->managed_ranges[index].bitmap_offset =
            allocator->managed_ranges[index].bitmap_offset;
    }
    ownership->initialization_magic =
        MICROS_FRAME_OWNERSHIP_MAGIC;
    return MICROS_FRAME_OWNERSHIP_OK;
}

void micros_frame_ownership_allocator_mark_allocated(
    struct micros_frame_allocator *allocator,
    uint64_t frame_index
)
{
    size_t word_index = (size_t)(frame_index / 64);
    uint64_t bit = UINT64_C(1) << (frame_index % 64);

    allocator->allocated_bitmap[word_index] |= bit;
}

void micros_frame_ownership_allocator_mark_free(
    struct micros_frame_allocator *allocator,
    uint64_t frame_index
)
{
    size_t word_index = (size_t)(frame_index / 64);
    uint64_t bit = UINT64_C(1) << (frame_index % 64);

    allocator->allocated_bitmap[word_index] &= ~bit;
}
