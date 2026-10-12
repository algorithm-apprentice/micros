#include "frame_allocator_test_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define EXPECT_TRUE(expression) \
    do { \
        if (!(expression)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %s\n", \
                __FILE__, \
                __LINE__, \
                #expression \
            ); \
            return false; \
        } \
    } while (false)

#define EXPECT_ERROR(expected, expression) \
    do { \
        enum micros_frame_allocator_error actual_error = (expression); \
        if (actual_error != (expected)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected error %d, got %d\n", \
                __FILE__, \
                __LINE__, \
                (int)(expected), \
                (int)actual_error \
            ); \
            return false; \
        } \
    } while (false)

bool frame_allocator_test_normalizes_and_subtracts_ranges(
    struct frame_allocator_test_context *context
)
{
    const struct micros_physical_range memory_ranges[] = {
        {UINT64_C(0x5000), UINT64_C(0x3000)},
        {UINT64_C(0x1003), UINT64_C(0x2ffd)},
        {UINT64_C(0x4000), UINT64_C(0x1000)},
        {UINT64_C(0x5000), UINT64_C(0x1000)},
    };
    const struct micros_physical_range reserved_ranges[] = {
        {UINT64_C(0x6000), UINT64_C(0x1000)},
        {UINT64_C(0x2fff), UINT64_C(0x0002)},
        {UINT64_C(0x9000), UINT64_C(0x1000)},
    };

    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_OK,
        micros_frame_allocator_initialize(
            &context->allocator,
            memory_ranges,
            sizeof(memory_ranges) / sizeof(memory_ranges[0]),
            reserved_ranges,
            sizeof(reserved_ranges) / sizeof(reserved_ranges[0])
        )
    );
    EXPECT_TRUE(context->allocator.initialized);
    EXPECT_TRUE(context->allocator.memory_range_count == 1);
    EXPECT_TRUE(context->allocator.memory_ranges[0].base == UINT64_C(0x2000));
    EXPECT_TRUE(context->allocator.memory_ranges[0].size == UINT64_C(0x6000));
    EXPECT_TRUE(context->allocator.reserved_range_count == 3);
    EXPECT_TRUE(context->allocator.managed_range_count == 2);
    EXPECT_TRUE(
        context->allocator.managed_ranges[0].base == UINT64_C(0x4000)
    );
    EXPECT_TRUE(context->allocator.managed_ranges[0].frame_count == 2);
    EXPECT_TRUE(context->allocator.managed_ranges[0].bitmap_offset == 0);
    EXPECT_TRUE(
        context->allocator.managed_ranges[1].base == UINT64_C(0x7000)
    );
    EXPECT_TRUE(context->allocator.managed_ranges[1].frame_count == 1);
    EXPECT_TRUE(context->allocator.managed_ranges[1].bitmap_offset == 2);
    EXPECT_TRUE(context->allocator.managed_frame_count == 3);
    EXPECT_TRUE(context->allocator.free_frame_count == 3);
    return true;
}

bool frame_allocator_test_allocates_releases_and_reuses_lowest_frame(
    struct frame_allocator_test_context *context
)
{
    const struct micros_physical_range memory_range = {
        UINT64_C(0x1000),
        UINT64_C(0x4000),
    };
    uint64_t frames[4];
    uint64_t unchanged = UINT64_C(0xfeedfacefeedface);
    size_t index;

    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_OK,
        micros_frame_allocator_initialize(
            &context->allocator,
            &memory_range,
            1,
            NULL,
            0
        )
    );
    for (index = 0; index < 4; ++index) {
        EXPECT_ERROR(
            MICROS_FRAME_ALLOCATOR_OK,
            micros_frame_allocator_allocate(&context->allocator, &frames[index])
        );
        EXPECT_TRUE(
            frames[index] == UINT64_C(0x1000)
                + (index * MICROS_FRAME_SIZE)
        );
    }
    EXPECT_TRUE(context->allocator.free_frame_count == 0);
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_EXHAUSTED,
        micros_frame_allocator_allocate(&context->allocator, &unchanged)
    );
    EXPECT_TRUE(unchanged == UINT64_C(0xfeedfacefeedface));

    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_OK,
        micros_frame_allocator_release(&context->allocator, frames[1])
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_OK,
        micros_frame_allocator_allocate(&context->allocator, &unchanged)
    );
    EXPECT_TRUE(unchanged == frames[1]);
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_UNALIGNED,
        micros_frame_allocator_release(&context->allocator, frames[0] + 1)
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_UNMANAGED,
        micros_frame_allocator_release(&context->allocator, UINT64_C(0x9000))
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_OK,
        micros_frame_allocator_release(&context->allocator, frames[3])
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_NOT_ALLOCATED,
        micros_frame_allocator_release(&context->allocator, frames[3])
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_OK,
        micros_frame_allocator_release(&context->allocator, frames[0])
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_NOT_ALLOCATED,
        micros_frame_allocator_release(&context->allocator, frames[0])
    );
    return true;
}

bool frame_allocator_test_merges_reservations_and_handles_full_removal(
    struct frame_allocator_test_context *context
)
{
    const struct micros_physical_range memory_range = {
        UINT64_C(0x1000),
        UINT64_C(0x6000),
    };
    const struct micros_physical_range reserved_ranges[] = {
        {UINT64_C(0x4000), UINT64_C(0x2000)},
        {UINT64_C(0x2001), UINT64_C(0x1fff)},
        {UINT64_C(0x0000), UINT64_C(0x1000)},
    };
    const struct micros_physical_range remove_all = {
        UINT64_C(0x0000),
        UINT64_C(0x8000),
    };
    struct micros_frame_allocator snapshot;

    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_OK,
        micros_frame_allocator_initialize(
            &context->allocator,
            &memory_range,
            1,
            reserved_ranges,
            sizeof(reserved_ranges) / sizeof(reserved_ranges[0])
        )
    );
    EXPECT_TRUE(context->allocator.reserved_range_count == 2);
    EXPECT_TRUE(
        context->allocator.reserved_ranges[1].base == UINT64_C(0x2000)
    );
    EXPECT_TRUE(
        context->allocator.reserved_ranges[1].size == UINT64_C(0x4000)
    );
    EXPECT_TRUE(context->allocator.managed_range_count == 2);
    EXPECT_TRUE(
        context->allocator.managed_ranges[0].base == UINT64_C(0x1000)
    );
    EXPECT_TRUE(context->allocator.managed_ranges[0].frame_count == 1);
    EXPECT_TRUE(
        context->allocator.managed_ranges[1].base == UINT64_C(0x6000)
    );
    EXPECT_TRUE(context->allocator.managed_ranges[1].frame_count == 1);

    snapshot = context->allocator;
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_NO_MEMORY,
        micros_frame_allocator_initialize(
            &context->allocator,
            &memory_range,
            1,
            &remove_all,
            1
        )
    );
    EXPECT_TRUE(memcmp(&context->allocator, &snapshot, sizeof(context->allocator)) == 0);
    return true;
}

static bool allocator_operation_arguments_are_rejected(
    struct frame_allocator_test_context *context
)
{
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_ARGUMENT,
        micros_frame_allocator_allocate(NULL, NULL)
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_ARGUMENT,
        micros_frame_allocator_allocate(&context->allocator, NULL)
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_ARGUMENT,
        micros_frame_allocator_release(NULL, UINT64_C(0x1000))
    );
    return true;
}

static bool invalid_initialization_inputs_are_rejected(
    struct frame_allocator_test_context *context,
    const struct micros_physical_range *valid_memory,
    const struct micros_physical_range *overflow,
    const struct micros_physical_range *partial,
    const struct micros_physical_range *zero_size,
    const struct micros_physical_range *reservation_alignment_overflow
)
{
    struct micros_frame_allocator snapshot = context->allocator;

    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_ARGUMENT,
        micros_frame_allocator_initialize(
            NULL,
            valid_memory,
            1,
            NULL,
            0
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_ARGUMENT,
        micros_frame_allocator_initialize(
            &context->allocator,
            NULL,
            1,
            NULL,
            0
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_CAPACITY,
        micros_frame_allocator_initialize(
            &context->allocator,
            valid_memory,
            MICROS_FRAME_ALLOCATOR_MAX_MEMORY_RANGES + 1,
            NULL,
            0
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_CAPACITY,
        micros_frame_allocator_initialize(
            &context->allocator,
            valid_memory,
            1,
            valid_memory,
            MICROS_FRAME_ALLOCATOR_MAX_RESERVED_RANGES + 1
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_RANGE,
        micros_frame_allocator_initialize(
            &context->allocator,
            overflow,
            1,
            NULL,
            0
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_RANGE,
        micros_frame_allocator_initialize(
            &context->allocator,
            zero_size,
            1,
            NULL,
            0
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_RANGE,
        micros_frame_allocator_initialize(
            &context->allocator,
            valid_memory,
            1,
            reservation_alignment_overflow,
            1
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_NO_MEMORY,
        micros_frame_allocator_initialize(
            &context->allocator,
            partial,
            1,
            NULL,
            0
        )
    );
    EXPECT_TRUE(
        memcmp(
            &context->allocator,
            &snapshot,
            sizeof(context->allocator)
        ) == 0
    );
    return true;
}

bool frame_allocator_test_rejects_invalid_inputs_without_changing_state(
    struct frame_allocator_test_context *context
)
{
    const struct micros_physical_range valid_memory = {
        UINT64_C(0x1000),
        UINT64_C(0x2000),
    };
    const struct micros_physical_range overflow = {
        UINT64_MAX - UINT64_C(0x100),
        UINT64_C(0x200),
    };
    const struct micros_physical_range partial = {
        UINT64_C(0x1001),
        UINT64_C(0x0ffe),
    };
    const struct micros_physical_range zero_size = {
        UINT64_C(0x1000),
        0,
    };
    const struct micros_physical_range reservation_alignment_overflow = {
        UINT64_MAX - UINT64_C(0x100),
        UINT64_C(0x100),
    };

    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_OK,
        micros_frame_allocator_initialize(
            &context->allocator,
            &valid_memory,
            1,
            NULL,
            0
        )
    );
    return (
        invalid_initialization_inputs_are_rejected(
            context,
            &valid_memory,
            &overflow,
            &partial,
            &zero_size,
            &reservation_alignment_overflow
        )
        && allocator_operation_arguments_are_rejected(context)
    );
}

bool frame_allocator_test_accepts_capacity_boundaries(
    struct frame_allocator_test_context *context
)
{
    struct micros_physical_range
        memory_ranges[MICROS_FRAME_ALLOCATOR_MAX_MEMORY_RANGES];
    struct micros_physical_range
        reserved_ranges[MICROS_FRAME_ALLOCATOR_MAX_RESERVED_RANGES];
    size_t memory_index;
    size_t reservation_index = 0;

    for (
        memory_index = 0;
        memory_index < MICROS_FRAME_ALLOCATOR_MAX_MEMORY_RANGES;
        ++memory_index
    ) {
        uint64_t base = UINT64_C(0x100000)
            + (UINT64_C(0x100000) * memory_index);
        size_t split_index;

        memory_ranges[memory_index].base = base;
        memory_ranges[memory_index].size =
            UINT64_C(11) * MICROS_FRAME_SIZE;
        for (split_index = 0; split_index < 5; ++split_index) {
            reserved_ranges[reservation_index].base = base
                + ((UINT64_C(2) * split_index + 1)
                    * MICROS_FRAME_SIZE);
            reserved_ranges[reservation_index].size =
                MICROS_FRAME_SIZE;
            ++reservation_index;
        }
    }

    EXPECT_TRUE(
        reservation_index
        == MICROS_FRAME_ALLOCATOR_MAX_RESERVED_RANGES
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_OK,
        micros_frame_allocator_initialize(
            &context->allocator,
            memory_ranges,
            MICROS_FRAME_ALLOCATOR_MAX_MEMORY_RANGES,
            reserved_ranges,
            MICROS_FRAME_ALLOCATOR_MAX_RESERVED_RANGES
        )
    );
    EXPECT_TRUE(
        context->allocator.managed_range_count
        == MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES
    );
    EXPECT_TRUE(context->allocator.managed_frame_count == 96);

    memory_ranges[0].base = 0;
    memory_ranges[0].size =
        (uint64_t)MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES
        * MICROS_FRAME_SIZE;
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_OK,
        micros_frame_allocator_initialize(
            &context->allocator,
            memory_ranges,
            1,
            NULL,
            0
        )
    );
    EXPECT_TRUE(
        context->allocator.managed_frame_count
        == MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES
    );

    memory_ranges[0].size += MICROS_FRAME_SIZE;
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_CAPACITY,
        micros_frame_allocator_initialize(
            &context->allocator,
            memory_ranges,
            1,
            NULL,
            0
        )
    );
    return true;
}
