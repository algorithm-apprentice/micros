#include "micros/frame_allocator.h"

enum {
    FRAME_SHIFT = 12,
};

#define FRAME_MASK ((uint64_t)MICROS_FRAME_SIZE - 1)

_Static_assert(
    MICROS_FRAME_SIZE == (1U << FRAME_SHIFT),
    "frame size and shift must agree"
);
_Static_assert(
    MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES % 64 == 0,
    "allocation bitmap requires complete words"
);

static bool add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (right > UINT64_MAX - left) {
        return false;
    }
    *result = left + right;
    return true;
}

static bool align_up_frame(uint64_t value, uint64_t *result)
{
    uint64_t adjusted;

    if ((value & FRAME_MASK) == 0) {
        *result = value;
        return true;
    }
    if (!add_u64(value, MICROS_FRAME_SIZE, &adjusted)) {
        return false;
    }
    *result = adjusted & ~FRAME_MASK;
    return true;
}

static void sort_ranges(
    struct micros_physical_range *ranges,
    size_t count
)
{
    size_t index;

    for (index = 1; index < count; ++index) {
        struct micros_physical_range value = ranges[index];
        size_t position = index;

        while (
            position > 0
            && (
                ranges[position - 1].base > value.base
                || (
                    ranges[position - 1].base == value.base
                    && ranges[position - 1].size > value.size
                )
            )
        ) {
            ranges[position] = ranges[position - 1];
            --position;
        }
        ranges[position] = value;
    }
}

static size_t merge_ranges(
    struct micros_physical_range *ranges,
    size_t count
)
{
    size_t input_index;
    size_t output_count = 0;

    for (input_index = 0; input_index < count; ++input_index) {
        uint64_t input_end =
            ranges[input_index].base + ranges[input_index].size;

        if (output_count == 0) {
            ranges[output_count] = ranges[input_index];
            ++output_count;
            continue;
        }

        {
            struct micros_physical_range *previous =
                &ranges[output_count - 1];
            uint64_t previous_end = previous->base + previous->size;

            if (ranges[input_index].base <= previous_end) {
                if (input_end > previous_end) {
                    previous->size = input_end - previous->base;
                }
                continue;
            }
        }
        ranges[output_count] = ranges[input_index];
        ++output_count;
    }
    return output_count;
}

static enum micros_frame_allocator_error normalize_ranges(
    const struct micros_physical_range *input_ranges,
    size_t input_count,
    struct micros_physical_range *output_ranges,
    size_t *output_count,
    bool align_inward
)
{
    size_t input_index;
    size_t count = 0;

    for (input_index = 0; input_index < input_count; ++input_index) {
        uint64_t end;
        uint64_t aligned_base;
        uint64_t aligned_end;

        if (
            input_ranges[input_index].size == 0
            || !add_u64(
                input_ranges[input_index].base,
                input_ranges[input_index].size,
                &end
            )
        ) {
            return MICROS_FRAME_ALLOCATOR_ERROR_RANGE;
        }

        if (align_inward) {
            if (
                !align_up_frame(
                    input_ranges[input_index].base,
                    &aligned_base
                )
            ) {
                return MICROS_FRAME_ALLOCATOR_ERROR_RANGE;
            }
            aligned_end = end & ~FRAME_MASK;
            if (aligned_base >= aligned_end) {
                continue;
            }
        } else {
            aligned_base = input_ranges[input_index].base & ~FRAME_MASK;
            if (!align_up_frame(end, &aligned_end)) {
                return MICROS_FRAME_ALLOCATOR_ERROR_RANGE;
            }
        }

        output_ranges[count].base = aligned_base;
        output_ranges[count].size = aligned_end - aligned_base;
        ++count;
    }

    sort_ranges(output_ranges, count);
    *output_count = merge_ranges(output_ranges, count);
    return MICROS_FRAME_ALLOCATOR_OK;
}

static enum micros_frame_allocator_error append_managed_range(
    struct micros_managed_frame_range *managed_ranges,
    size_t *managed_range_count,
    uint64_t *managed_frame_count,
    uint64_t base,
    uint64_t end
)
{
    uint64_t frame_count;

    if (base >= end) {
        return MICROS_FRAME_ALLOCATOR_OK;
    }
    if (
        *managed_range_count
        >= MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES
    ) {
        return MICROS_FRAME_ALLOCATOR_ERROR_CAPACITY;
    }

    frame_count = (end - base) >> FRAME_SHIFT;
    if (
        frame_count
        > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES
            - *managed_frame_count
    ) {
        return MICROS_FRAME_ALLOCATOR_ERROR_CAPACITY;
    }

    managed_ranges[*managed_range_count].base = base;
    managed_ranges[*managed_range_count].frame_count = frame_count;
    managed_ranges[*managed_range_count].bitmap_offset =
        *managed_frame_count;
    ++*managed_range_count;
    *managed_frame_count += frame_count;
    return MICROS_FRAME_ALLOCATOR_OK;
}

static enum micros_frame_allocator_error subtract_reserved_ranges(
    const struct micros_physical_range *memory_ranges,
    size_t memory_range_count,
    const struct micros_physical_range *reserved_ranges,
    size_t reserved_range_count,
    struct micros_managed_frame_range *managed_ranges,
    size_t *managed_range_count,
    uint64_t *managed_frame_count
)
{
    size_t memory_index;

    *managed_range_count = 0;
    *managed_frame_count = 0;
    for (memory_index = 0; memory_index < memory_range_count; ++memory_index) {
        uint64_t cursor = memory_ranges[memory_index].base;
        uint64_t memory_end =
            memory_ranges[memory_index].base
            + memory_ranges[memory_index].size;
        size_t reserved_index;

        for (
            reserved_index = 0;
            reserved_index < reserved_range_count;
            ++reserved_index
        ) {
            uint64_t reserved_base = reserved_ranges[reserved_index].base;
            uint64_t reserved_end =
                reserved_base + reserved_ranges[reserved_index].size;
            enum micros_frame_allocator_error error;

            if (reserved_end <= cursor) {
                continue;
            }
            if (reserved_base >= memory_end) {
                break;
            }
            if (reserved_base > cursor) {
                uint64_t available_end = reserved_base < memory_end
                    ? reserved_base
                    : memory_end;

                error = append_managed_range(
                    managed_ranges,
                    managed_range_count,
                    managed_frame_count,
                    cursor,
                    available_end
                );
                if (error != MICROS_FRAME_ALLOCATOR_OK) {
                    return error;
                }
            }
            if (reserved_end > cursor) {
                cursor = reserved_end;
            }
            if (cursor >= memory_end) {
                break;
            }
        }

        if (cursor < memory_end) {
            enum micros_frame_allocator_error error =
                append_managed_range(
                    managed_ranges,
                    managed_range_count,
                    managed_frame_count,
                    cursor,
                    memory_end
                );

            if (error != MICROS_FRAME_ALLOCATOR_OK) {
                return error;
            }
        }
    }
    if (*managed_frame_count == 0) {
        return MICROS_FRAME_ALLOCATOR_ERROR_NO_MEMORY;
    }
    return MICROS_FRAME_ALLOCATOR_OK;
}

static void commit_initialized_state(
    struct micros_frame_allocator *allocator,
    const struct micros_physical_range *memory_ranges,
    size_t memory_range_count,
    const struct micros_physical_range *reserved_ranges,
    size_t reserved_range_count,
    const struct micros_managed_frame_range *managed_ranges,
    size_t managed_range_count,
    uint64_t managed_frame_count
)
{
    size_t index;

    allocator->initialized = false;
    allocator->memory_range_count = memory_range_count;
    allocator->reserved_range_count = reserved_range_count;
    allocator->managed_range_count = managed_range_count;
    allocator->managed_frame_count = managed_frame_count;
    allocator->free_frame_count = managed_frame_count;

    for (
        index = 0;
        index < MICROS_FRAME_ALLOCATOR_MAX_MEMORY_RANGES;
        ++index
    ) {
        allocator->memory_ranges[index] =
            (struct micros_physical_range){0, 0};
    }
    for (index = 0; index < memory_range_count; ++index) {
        allocator->memory_ranges[index] = memory_ranges[index];
    }

    for (
        index = 0;
        index < MICROS_FRAME_ALLOCATOR_MAX_RESERVED_RANGES;
        ++index
    ) {
        allocator->reserved_ranges[index] =
            (struct micros_physical_range){0, 0};
    }
    for (index = 0; index < reserved_range_count; ++index) {
        allocator->reserved_ranges[index] = reserved_ranges[index];
    }

    for (
        index = 0;
        index < MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES;
        ++index
    ) {
        allocator->managed_ranges[index] =
            (struct micros_managed_frame_range){0, 0, 0};
    }
    for (index = 0; index < managed_range_count; ++index) {
        allocator->managed_ranges[index] = managed_ranges[index];
    }

    for (
        index = 0;
        index < MICROS_FRAME_ALLOCATOR_BITMAP_WORDS;
        ++index
    ) {
        allocator->allocated_bitmap[index] = 0;
    }
    allocator->initialized = true;
}

enum micros_frame_allocator_error micros_frame_allocator_initialize(
    struct micros_frame_allocator *allocator,
    const struct micros_physical_range *memory_ranges,
    size_t memory_range_count,
    const struct micros_physical_range *reserved_ranges,
    size_t reserved_range_count
)
{
    struct micros_physical_range
        canonical_memory[MICROS_FRAME_ALLOCATOR_MAX_MEMORY_RANGES];
    struct micros_physical_range
        canonical_reserved[MICROS_FRAME_ALLOCATOR_MAX_RESERVED_RANGES];
    struct micros_managed_frame_range
        managed_ranges[MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES];
    size_t canonical_memory_count;
    size_t canonical_reserved_count;
    size_t managed_range_count;
    uint64_t managed_frame_count;
    enum micros_frame_allocator_error error;

    if (allocator == NULL) {
        return MICROS_FRAME_ALLOCATOR_ERROR_ARGUMENT;
    }
    if (
        memory_range_count > MICROS_FRAME_ALLOCATOR_MAX_MEMORY_RANGES
        || reserved_range_count
            > MICROS_FRAME_ALLOCATOR_MAX_RESERVED_RANGES
    ) {
        return MICROS_FRAME_ALLOCATOR_ERROR_CAPACITY;
    }
    if (
        memory_range_count == 0
        || memory_ranges == NULL
        || (reserved_range_count != 0 && reserved_ranges == NULL)
    ) {
        return MICROS_FRAME_ALLOCATOR_ERROR_ARGUMENT;
    }

    error = normalize_ranges(
        memory_ranges,
        memory_range_count,
        canonical_memory,
        &canonical_memory_count,
        true
    );
    if (error != MICROS_FRAME_ALLOCATOR_OK) {
        return error;
    }
    if (canonical_memory_count == 0) {
        return MICROS_FRAME_ALLOCATOR_ERROR_NO_MEMORY;
    }
    error = normalize_ranges(
        reserved_ranges,
        reserved_range_count,
        canonical_reserved,
        &canonical_reserved_count,
        false
    );
    if (error != MICROS_FRAME_ALLOCATOR_OK) {
        return error;
    }
    error = subtract_reserved_ranges(
        canonical_memory,
        canonical_memory_count,
        canonical_reserved,
        canonical_reserved_count,
        managed_ranges,
        &managed_range_count,
        &managed_frame_count
    );
    if (error != MICROS_FRAME_ALLOCATOR_OK) {
        return error;
    }

    commit_initialized_state(
        allocator,
        canonical_memory,
        canonical_memory_count,
        canonical_reserved,
        canonical_reserved_count,
        managed_ranges,
        managed_range_count,
        managed_frame_count
    );
    return MICROS_FRAME_ALLOCATOR_OK;
}

enum micros_frame_allocator_error micros_frame_allocator_allocate(
    struct micros_frame_allocator *allocator,
    uint64_t *physical_address
)
{
    size_t range_index;

    if (
        allocator == NULL
        || physical_address == NULL
        || !allocator->initialized
    ) {
        return MICROS_FRAME_ALLOCATOR_ERROR_ARGUMENT;
    }
    if (allocator->free_frame_count == 0) {
        return MICROS_FRAME_ALLOCATOR_ERROR_EXHAUSTED;
    }

    for (
        range_index = 0;
        range_index < allocator->managed_range_count;
        ++range_index
    ) {
        const struct micros_managed_frame_range *range =
            &allocator->managed_ranges[range_index];
        uint64_t frame_index;

        for (frame_index = 0; frame_index < range->frame_count; ++frame_index) {
            uint64_t bitmap_index = range->bitmap_offset + frame_index;
            size_t word_index = (size_t)(bitmap_index / 64);
            uint64_t bit = UINT64_C(1) << (bitmap_index % 64);

            if ((allocator->allocated_bitmap[word_index] & bit) == 0) {
                allocator->allocated_bitmap[word_index] |= bit;
                --allocator->free_frame_count;
                *physical_address = range->base
                    + (frame_index << FRAME_SHIFT);
                return MICROS_FRAME_ALLOCATOR_OK;
            }
        }
    }
    return MICROS_FRAME_ALLOCATOR_ERROR_EXHAUSTED;
}

enum micros_frame_allocator_error micros_frame_allocator_release(
    struct micros_frame_allocator *allocator,
    uint64_t physical_address
)
{
    size_t range_index;

    if (allocator == NULL || !allocator->initialized) {
        return MICROS_FRAME_ALLOCATOR_ERROR_ARGUMENT;
    }
    if ((physical_address & FRAME_MASK) != 0) {
        return MICROS_FRAME_ALLOCATOR_ERROR_UNALIGNED;
    }

    for (
        range_index = 0;
        range_index < allocator->managed_range_count;
        ++range_index
    ) {
        const struct micros_managed_frame_range *range =
            &allocator->managed_ranges[range_index];
        uint64_t range_size = range->frame_count << FRAME_SHIFT;
        uint64_t frame_index;
        uint64_t bitmap_index;
        size_t word_index;
        uint64_t bit;

        if (physical_address < range->base) {
            break;
        }
        if (physical_address - range->base >= range_size) {
            continue;
        }

        frame_index =
            (physical_address - range->base) >> FRAME_SHIFT;
        bitmap_index = range->bitmap_offset + frame_index;
        word_index = (size_t)(bitmap_index / 64);
        bit = UINT64_C(1) << (bitmap_index % 64);
        if ((allocator->allocated_bitmap[word_index] & bit) == 0) {
            return MICROS_FRAME_ALLOCATOR_ERROR_NOT_ALLOCATED;
        }
        allocator->allocated_bitmap[word_index] &= ~bit;
        ++allocator->free_frame_count;
        return MICROS_FRAME_ALLOCATOR_OK;
    }
    return MICROS_FRAME_ALLOCATOR_ERROR_UNMANAGED;
}
