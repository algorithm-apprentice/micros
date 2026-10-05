#ifndef MICROS_FRAME_ALLOCATOR_H
#define MICROS_FRAME_ALLOCATOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    MICROS_FRAME_SIZE = 4096,
    MICROS_FRAME_ALLOCATOR_MAX_MEMORY_RANGES = 16,
    MICROS_FRAME_ALLOCATOR_MAX_RESERVED_RANGES = 80,
    MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES = 96,
    MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES = 262144,
    MICROS_FRAME_ALLOCATOR_BITMAP_WORDS =
        MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES / 64,
};

struct micros_physical_range {
    uint64_t base;
    uint64_t size;
};

struct micros_managed_frame_range {
    uint64_t base;
    uint64_t frame_count;
    uint64_t bitmap_offset;
};

struct micros_frame_allocator {
    bool initialized;
    size_t memory_range_count;
    size_t reserved_range_count;
    size_t managed_range_count;
    uint64_t managed_frame_count;
    uint64_t free_frame_count;
    struct micros_physical_range
        memory_ranges[MICROS_FRAME_ALLOCATOR_MAX_MEMORY_RANGES];
    struct micros_physical_range
        reserved_ranges[MICROS_FRAME_ALLOCATOR_MAX_RESERVED_RANGES];
    struct micros_managed_frame_range
        managed_ranges[MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES];
    uint64_t allocated_bitmap[MICROS_FRAME_ALLOCATOR_BITMAP_WORDS];
};

enum micros_frame_allocator_error {
    MICROS_FRAME_ALLOCATOR_OK = 0,
    MICROS_FRAME_ALLOCATOR_ERROR_ARGUMENT,
    MICROS_FRAME_ALLOCATOR_ERROR_RANGE,
    MICROS_FRAME_ALLOCATOR_ERROR_CAPACITY,
    MICROS_FRAME_ALLOCATOR_ERROR_NO_MEMORY,
    MICROS_FRAME_ALLOCATOR_ERROR_EXHAUSTED,
    MICROS_FRAME_ALLOCATOR_ERROR_UNALIGNED,
    MICROS_FRAME_ALLOCATOR_ERROR_UNMANAGED,
    MICROS_FRAME_ALLOCATOR_ERROR_NOT_ALLOCATED,
    MICROS_FRAME_ALLOCATOR_ERROR_INVARIANT,
};

enum micros_frame_allocator_error micros_frame_allocator_initialize(
    struct micros_frame_allocator *allocator,
    const struct micros_physical_range *memory_ranges,
    size_t memory_range_count,
    const struct micros_physical_range *reserved_ranges,
    size_t reserved_range_count
);

enum micros_frame_allocator_error micros_frame_allocator_allocate(
    struct micros_frame_allocator *allocator,
    uint64_t *physical_address
);

enum micros_frame_allocator_error micros_frame_allocator_release(
    struct micros_frame_allocator *allocator,
    uint64_t physical_address
);

#endif
