#include "micros/bootstrap_memory.h"

#include <stddef.h>
#include <stdint.h>

extern const unsigned char __kernel_start[];
extern const unsigned char __kernel_end[];

static struct micros_frame_allocator bootstrap_allocator;

static bool range_end(
    uint64_t base,
    uint64_t size,
    uint64_t *end
)
{
    if (size == 0 || size > UINT64_MAX - base) {
        return false;
    }
    *end = base + size;
    return true;
}

static bool ranges_intersect(
    uint64_t left_base,
    uint64_t left_end,
    uint64_t right_base,
    uint64_t right_end
)
{
    return left_base < right_end && right_base < left_end;
}

static bool fdt_memory_union_covers(
    const struct micros_fdt_memory_map *memory_map,
    uint64_t base,
    uint64_t end
)
{
    uint64_t cursor = base;

    while (cursor < end) {
        uint64_t furthest = cursor;
        size_t index;

        for (
            index = 0;
            index < memory_map->memory_range_count;
            ++index
        ) {
            uint64_t range_end_address;
            const struct micros_fdt_range *range =
                &memory_map->memory_ranges[index];

            if (
                !range_end(range->base, range->size, &range_end_address)
                || range->base > cursor
                || range_end_address <= cursor
            ) {
                continue;
            }
            if (range_end_address > furthest) {
                furthest = range_end_address;
            }
        }
        if (furthest == cursor) {
            return false;
        }
        cursor = furthest;
    }
    return true;
}

static bool intersects_fdt_reservations(
    const struct micros_fdt_memory_map *memory_map,
    uint64_t base,
    uint64_t end
)
{
    size_t index;

    for (
        index = 0;
        index < memory_map->reservation_range_count;
        ++index
    ) {
        uint64_t reservation_end;
        const struct micros_fdt_range *range =
            &memory_map->reservation_ranges[index];

        if (
            !range_end(range->base, range->size, &reservation_end)
            || ranges_intersect(base, end, range->base, reservation_end)
        ) {
            return true;
        }
    }
    for (
        index = 0;
        index < memory_map->reserved_memory_range_count;
        ++index
    ) {
        uint64_t reservation_end;
        const struct micros_fdt_range *range =
            &memory_map->reserved_memory_ranges[index];

        if (
            !range_end(range->base, range->size, &reservation_end)
            || ranges_intersect(base, end, range->base, reservation_end)
        ) {
            return true;
        }
    }
    return false;
}

static bool managed_frame_is_allowed(
    const struct micros_fdt_memory_map *memory_map,
    uint64_t physical_address
)
{
    uint64_t frame_end;
    uint64_t kernel_end = (uintptr_t)__kernel_end;

    return (
        physical_address >= (uintptr_t)__kernel_start
        && physical_address >= kernel_end
        && range_end(
            physical_address,
            MICROS_FRAME_SIZE,
            &frame_end
        )
        && fdt_memory_union_covers(
            memory_map,
            physical_address,
            frame_end
        )
        && !intersects_fdt_reservations(
            memory_map,
            physical_address,
            frame_end
        )
    );
}

static bool bootstrap_state_is_valid(
    const struct micros_fdt_memory_map *memory_map,
    bool require_all_free
)
{
    uint64_t allocated_count = 0;
    uint64_t expected_bitmap_offset = 0;
    uint64_t previous_end = 0;
    uint64_t bitmap_index;
    size_t index;

    if (
        !bootstrap_allocator.initialized
        || bootstrap_allocator.managed_frame_count == 0
        || bootstrap_allocator.free_frame_count
            > bootstrap_allocator.managed_frame_count
    ) {
        return false;
    }
    for (
        bitmap_index = 0;
        bitmap_index < bootstrap_allocator.managed_frame_count;
        ++bitmap_index
    ) {
        size_t word_index = (size_t)(bitmap_index / 64);
        uint64_t bit = UINT64_C(1) << (bitmap_index % 64);

        if ((bootstrap_allocator.allocated_bitmap[word_index] & bit) != 0) {
            ++allocated_count;
        }
    }
    if (
        allocated_count
            != (
                bootstrap_allocator.managed_frame_count
                - bootstrap_allocator.free_frame_count
            )
        || (require_all_free && allocated_count != 0)
    ) {
        return false;
    }

    for (
        index = 0;
        index < bootstrap_allocator.managed_range_count;
        ++index
    ) {
        const struct micros_managed_frame_range *range =
            &bootstrap_allocator.managed_ranges[index];
        uint64_t range_size;
        uint64_t end;
        uint64_t frame_index;

        if (
            range->frame_count == 0
            || range->frame_count > UINT64_MAX / MICROS_FRAME_SIZE
        ) {
            return false;
        }
        range_size = range->frame_count * MICROS_FRAME_SIZE;
        if (
            !range_end(range->base, range_size, &end)
            || (range->base & (MICROS_FRAME_SIZE - 1)) != 0
            || range->bitmap_offset != expected_bitmap_offset
            || (index != 0 && range->base < previous_end)
            || !fdt_memory_union_covers(
                memory_map,
                range->base,
                end
            )
            || intersects_fdt_reservations(
                memory_map,
                range->base,
                end
            )
            || range->base < (uintptr_t)__kernel_start
            || range->base < (uintptr_t)__kernel_end
        ) {
            return false;
        }
        for (frame_index = 0; frame_index < range->frame_count; ++frame_index) {
            if (
                !managed_frame_is_allowed(
                    memory_map,
                    range->base
                        + (frame_index * MICROS_FRAME_SIZE)
                )
            ) {
                return false;
            }
        }
        expected_bitmap_offset += range->frame_count;
        previous_end = end;
    }
    return expected_bitmap_offset
        == bootstrap_allocator.managed_frame_count;
}

enum micros_frame_allocator_error micros_bootstrap_memory_initialize(
    const struct micros_fdt_memory_map *memory_map
)
{
    struct micros_physical_range
        memory_ranges[MICROS_FRAME_ALLOCATOR_MAX_MEMORY_RANGES];
    struct micros_physical_range
        reserved_ranges[MICROS_FRAME_ALLOCATOR_MAX_RESERVED_RANGES];
    size_t memory_index;
    size_t reserved_count = 0;
    enum micros_frame_allocator_error error;

    if (memory_map == NULL) {
        return MICROS_FRAME_ALLOCATOR_ERROR_ARGUMENT;
    }
    if (
        memory_map->memory_range_count
            > MICROS_FRAME_ALLOCATOR_MAX_MEMORY_RANGES
        || memory_map->reservation_range_count
            > MICROS_FDT_MAX_RESERVATION_RANGES
        || memory_map->reserved_memory_range_count
            > MICROS_FDT_MAX_RESERVED_MEMORY_RANGES
    ) {
        return MICROS_FRAME_ALLOCATOR_ERROR_CAPACITY;
    }

    for (
        memory_index = 0;
        memory_index < memory_map->memory_range_count;
        ++memory_index
    ) {
        memory_ranges[memory_index].base =
            memory_map->memory_ranges[memory_index].base;
        memory_ranges[memory_index].size =
            memory_map->memory_ranges[memory_index].size;
    }
    for (
        memory_index = 0;
        memory_index < memory_map->reservation_range_count;
        ++memory_index
    ) {
        reserved_ranges[reserved_count].base =
            memory_map->reservation_ranges[memory_index].base;
        reserved_ranges[reserved_count].size =
            memory_map->reservation_ranges[memory_index].size;
        ++reserved_count;
    }
    for (
        memory_index = 0;
        memory_index < memory_map->reserved_memory_range_count;
        ++memory_index
    ) {
        reserved_ranges[reserved_count].base =
            memory_map->reserved_memory_ranges[memory_index].base;
        reserved_ranges[reserved_count].size =
            memory_map->reserved_memory_ranges[memory_index].size;
        ++reserved_count;
    }
    if (
        reserved_count >= MICROS_FRAME_ALLOCATOR_MAX_RESERVED_RANGES
        || (uintptr_t)__kernel_end == 0
    ) {
        return MICROS_FRAME_ALLOCATOR_ERROR_CAPACITY;
    }
    reserved_ranges[reserved_count].base = 0;
    reserved_ranges[reserved_count].size = (uintptr_t)__kernel_end;
    ++reserved_count;

    error = micros_frame_allocator_initialize(
        &bootstrap_allocator,
        memory_ranges,
        memory_map->memory_range_count,
        reserved_ranges,
        reserved_count
    );
    if (error != MICROS_FRAME_ALLOCATOR_OK) {
        return error;
    }
    if (!bootstrap_state_is_valid(memory_map, true)) {
        bootstrap_allocator.initialized = false;
        return MICROS_FRAME_ALLOCATOR_ERROR_INVARIANT;
    }
    return MICROS_FRAME_ALLOCATOR_OK;
}

const struct micros_frame_allocator *micros_bootstrap_frame_allocator(void)
{
    if (!bootstrap_allocator.initialized) {
        return NULL;
    }
    return &bootstrap_allocator;
}

enum micros_frame_allocator_error micros_bootstrap_frame_allocate(
    uint64_t *physical_address
)
{
    return micros_frame_allocator_allocate(
        &bootstrap_allocator,
        physical_address
    );
}

enum micros_frame_allocator_error micros_bootstrap_frame_release(
    uint64_t physical_address
)
{
    return micros_frame_allocator_release(
        &bootstrap_allocator,
        physical_address
    );
}

#ifdef MICROS_BUILD_FRAME_ALLOCATOR_TEST
bool micros_bootstrap_memory_run_self_test(
    const struct micros_fdt_memory_map *memory_map
)
{
    uint64_t frames[4];
    uint64_t reused;
    uint64_t initial_free;
    size_t index;
    static const size_t release_order[] = {1, 3, 0, 2};

    if (
        memory_map == NULL
        || !bootstrap_state_is_valid(memory_map, false)
        || bootstrap_allocator.free_frame_count < 4
    ) {
        return false;
    }
    initial_free = bootstrap_allocator.free_frame_count;

    for (index = 0; index < 4; ++index) {
        if (
            micros_bootstrap_frame_allocate(&frames[index])
                != MICROS_FRAME_ALLOCATOR_OK
            || !managed_frame_is_allowed(memory_map, frames[index])
            || (
                index != 0
                && frames[index - 1] >= frames[index]
            )
        ) {
            return false;
        }
    }
    for (index = 0; index < 4; ++index) {
        if (
            micros_bootstrap_frame_release(frames[release_order[index]])
            != MICROS_FRAME_ALLOCATOR_OK
        ) {
            return false;
        }
    }
    if (bootstrap_allocator.free_frame_count != initial_free) {
        return false;
    }
    if (
        micros_bootstrap_frame_allocate(&reused)
            != MICROS_FRAME_ALLOCATOR_OK
        || reused != frames[0]
        || micros_bootstrap_frame_release(reused)
            != MICROS_FRAME_ALLOCATOR_OK
        || bootstrap_allocator.free_frame_count != initial_free
    ) {
        return false;
    }
    return bootstrap_state_is_valid(memory_map, false);
}
#endif
