#include "frame_ownership_test_internal.h"

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
        enum micros_frame_ownership_error actual_error = (expression); \
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
static bool expect_geometry_allocate_failure(
    struct frame_ownership_test_context *context,
    struct micros_frame_owner owner
)
{
    uint64_t unchanged = UINT64_C(0x1122334455667788);

    memcpy(
        &context->ownership_snapshot,
        &context->ownership,
        sizeof(context->ownership_snapshot)
    );
    memcpy(
        &context->allocator_snapshot,
        &context->allocator,
        sizeof(context->allocator_snapshot)
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_allocate(
            &context->ownership,
            owner,
            &unchanged
        )
    );
    EXPECT_TRUE(unchanged == UINT64_C(0x1122334455667788));
    EXPECT_TRUE(
        memcmp(
            &context->ownership,
            &context->ownership_snapshot,
            sizeof(context->ownership)
        ) == 0
    );
    EXPECT_TRUE(
        memcmp(
            &context->allocator,
            &context->allocator_snapshot,
            sizeof(context->allocator)
        ) == 0
    );
    return true;
}

static bool expect_geometry_release_failure(
    struct frame_ownership_test_context *context,
    struct micros_frame_owner owner,
    uint64_t physical_address
)
{
    memcpy(
        &context->ownership_snapshot,
        &context->ownership,
        sizeof(context->ownership_snapshot)
    );
    memcpy(
        &context->allocator_snapshot,
        &context->allocator,
        sizeof(context->allocator_snapshot)
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_release(
            &context->ownership,
            owner,
            physical_address
        )
    );
    EXPECT_TRUE(
        memcmp(
            &context->ownership,
            &context->ownership_snapshot,
            sizeof(context->ownership)
        ) == 0
    );
    EXPECT_TRUE(
        memcmp(
            &context->allocator,
            &context->allocator_snapshot,
            sizeof(context->allocator)
        ) == 0
    );
    return true;
}

static bool expect_inactive_owner_corruption(
    struct frame_ownership_test_context *context,
    struct micros_frame_owner owner
)
{
    context->ownership.owners[TEST_FRAME_COUNT] = owner;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&context->ownership)
    );
    memset(
        &context->ownership.owners[TEST_FRAME_COUNT],
        0,
        sizeof(context->ownership.owners[TEST_FRAME_COUNT])
    );
    return true;
}

bool frame_ownership_test_rejects_geometry_changes_before_mutation(
    struct frame_ownership_test_context *context
)
{
    const struct micros_frame_owner temporary = frame_ownership_test_raw_kernel_owner(
        MICROS_FRAME_OWNER_KERNEL_TEMPORARY
    );
    uint64_t frame;
    size_t original_range_count;
    uint64_t original_managed_count;

    EXPECT_TRUE(frame_ownership_test_initialize_fixture(context, TEST_FRAME_COUNT));

    original_range_count = context->allocator.managed_range_count;
    context->allocator.managed_range_count = 0;
    EXPECT_TRUE(expect_geometry_allocate_failure(context, temporary));
    context->allocator.managed_range_count = original_range_count;

    context->allocator.managed_range_count =
        MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES + 1;
    EXPECT_TRUE(expect_geometry_allocate_failure(context, temporary));
    context->allocator.managed_range_count = original_range_count;

    original_managed_count = context->allocator.managed_frame_count;
    --context->allocator.managed_frame_count;
    EXPECT_TRUE(expect_geometry_allocate_failure(context, temporary));
    context->allocator.managed_frame_count = original_managed_count;

    ++context->allocator.managed_ranges[0].base;
    EXPECT_TRUE(expect_geometry_allocate_failure(context, temporary));
    --context->allocator.managed_ranges[0].base;
    ++context->allocator.managed_ranges[0].frame_count;
    EXPECT_TRUE(expect_geometry_allocate_failure(context, temporary));
    --context->allocator.managed_ranges[0].frame_count;
    ++context->allocator.managed_ranges[0].bitmap_offset;
    EXPECT_TRUE(expect_geometry_allocate_failure(context, temporary));
    --context->allocator.managed_ranges[0].bitmap_offset;

    context->alternate_allocator = context->allocator;
    context->ownership.allocator = &context->alternate_allocator;
    EXPECT_TRUE(expect_geometry_allocate_failure(context, temporary));
    EXPECT_TRUE(
        memcmp(
            &context->alternate_allocator,
            &context->allocator,
            sizeof(context->allocator)
        ) == 0
    );
    context->ownership.allocator = &context->allocator;

    context->allocator.managed_ranges[1].base = UINT64_C(0xdead0000);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&context->ownership)
    );
    context->allocator.managed_ranges[1].base = 0;

    context->allocator.allocated_bitmap[0] |= UINT64_C(1) << TEST_FRAME_COUNT;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&context->ownership)
    );
    context->allocator.allocated_bitmap[0] &=
        ~(UINT64_C(1) << TEST_FRAME_COUNT);

    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &context->ownership,
            temporary,
            &frame
        )
    );
    context->allocator.managed_range_count = 0;
    EXPECT_TRUE(expect_geometry_release_failure(context, temporary, frame));
    context->allocator.managed_range_count = original_range_count;
    --context->allocator.managed_frame_count;
    EXPECT_TRUE(expect_geometry_release_failure(context, temporary, frame));
    context->allocator.managed_frame_count = original_managed_count;

    ++context->allocator.managed_ranges[0].base;
    EXPECT_TRUE(expect_geometry_release_failure(context, temporary, frame));
    --context->allocator.managed_ranges[0].base;
    ++context->allocator.managed_ranges[0].frame_count;
    EXPECT_TRUE(expect_geometry_release_failure(context, temporary, frame));
    --context->allocator.managed_ranges[0].frame_count;
    ++context->allocator.managed_ranges[0].bitmap_offset;
    EXPECT_TRUE(expect_geometry_release_failure(context, temporary, frame));
    --context->allocator.managed_ranges[0].bitmap_offset;

    context->alternate_allocator = context->allocator;
    context->ownership.allocator = &context->alternate_allocator;
    EXPECT_TRUE(expect_geometry_release_failure(context, temporary, frame));
    EXPECT_TRUE(
        memcmp(
            &context->alternate_allocator,
            &context->allocator,
            sizeof(context->allocator)
        ) == 0
    );
    context->ownership.allocator = &context->allocator;

    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_release(
            &context->ownership,
            temporary,
            frame
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_validate(&context->ownership)
    );
    return true;
}

bool frame_ownership_test_validator_rejects_independent_corruption(
    struct frame_ownership_test_context *context
)
{
    const struct micros_frame_owner process_user = frame_ownership_test_raw_process_owner(
        MICROS_FRAME_OWNER_PROCESS_USER,
        3,
        4
    );
    uint64_t frame;
    uint64_t frame_index;

    EXPECT_TRUE(frame_ownership_test_initialize_fixture(context, TEST_FRAME_COUNT));
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &context->ownership,
            process_user,
            &frame
        )
    );
    frame_index = (frame - TEST_BASE) / MICROS_FRAME_SIZE;

    ++context->ownership.owned_frame_count;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&context->ownership)
    );
    --context->ownership.owned_frame_count;
    ++context->ownership.owner_counts[MICROS_FRAME_OWNER_PROCESS_USER];
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&context->ownership)
    );
    --context->ownership.owner_counts[MICROS_FRAME_OWNER_PROCESS_USER];

    context->ownership.owners[frame_index].reserved = 1;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&context->ownership)
    );
    context->ownership.owners[frame_index].reserved = 0;
    context->ownership.owners[frame_index].slot = MICROS_PROCESS_CAPACITY;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&context->ownership)
    );
    context->ownership.owners[frame_index].slot = process_user.slot;
    context->ownership.owners[frame_index].generation = 0;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&context->ownership)
    );
    context->ownership.owners[frame_index].generation =
        MICROS_PROCESS_GENERATION_MAX + UINT32_C(1);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&context->ownership)
    );
    context->ownership.owners[frame_index].generation = process_user.generation;
    context->ownership.owners[frame_index].kind = UINT8_MAX;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&context->ownership)
    );
    context->ownership.owners[frame_index] = process_user;

    context->ownership.handoff_targets[frame_index] = UINT8_MAX;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&context->ownership)
    );
    context->ownership.handoff_targets[frame_index] = MICROS_FRAME_HANDOFF_NONE;

    context->allocator.allocated_bitmap[0] &= ~UINT64_C(1);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&context->ownership)
    );
    context->allocator.allocated_bitmap[0] |= UINT64_C(1);

    context->ownership.phase = (enum micros_frame_ownership_phase)99;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&context->ownership)
    );
    context->ownership.phase = MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP;

    ++context->ownership.allocator_identity;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&context->ownership)
    );
    --context->ownership.allocator_identity;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_validate(&context->ownership)
    );
    return true;
}

bool frame_ownership_test_rejects_inactive_corruption(
    struct frame_ownership_test_context *context
)
{
    EXPECT_TRUE(
        frame_ownership_test_initialize_fixture(
            context,
            TEST_FRAME_COUNT
        )
    );
    EXPECT_TRUE(
        expect_inactive_owner_corruption(
            context,
            (struct micros_frame_owner){.generation = 1}
        )
        && expect_inactive_owner_corruption(
            context,
            (struct micros_frame_owner){.slot = 1}
        )
        && expect_inactive_owner_corruption(
            context,
            (struct micros_frame_owner){
                .kind = MICROS_FRAME_OWNER_KERNEL_RETAINED,
            }
        )
        && expect_inactive_owner_corruption(
            context,
            (struct micros_frame_owner){.reserved = 1}
        )
    );
    context->ownership.handoff_targets[TEST_FRAME_COUNT] =
        MICROS_FRAME_HANDOFF_VM_WIRED;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&context->ownership)
    );
    context->ownership.handoff_targets[TEST_FRAME_COUNT] =
        MICROS_FRAME_HANDOFF_NONE;
    return true;
}

bool frame_ownership_test_validates_bitmap_word_boundaries(
    struct frame_ownership_test_context *context
)
{
    static const uint64_t frame_counts[] = {
        1,
        63,
        64,
        65,
        UINT64_C(28904),
        MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES - 1,
        MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES,
    };
    size_t index;

    for (index = 0; index < sizeof(frame_counts) / sizeof(frame_counts[0]); ++index) {
        uint64_t frame_count = frame_counts[index];

        EXPECT_TRUE(frame_ownership_test_initialize_fixture(context, frame_count));
        EXPECT_ERROR(
            MICROS_FRAME_OWNERSHIP_OK,
            micros_frame_ownership_validate(&context->ownership)
        );
        if (frame_count < MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES) {
            context->allocator.allocated_bitmap[frame_count / 64]
                |= UINT64_C(1) << (frame_count % 64);
            EXPECT_ERROR(
                MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
                micros_frame_ownership_validate(&context->ownership)
            );
            context->allocator.allocated_bitmap[frame_count / 64]
                &= ~(UINT64_C(1) << (frame_count % 64));
        }
    }
    return true;
}

bool frame_ownership_test_operates_at_maximum_managed_capacity(
    struct frame_ownership_test_context *context
)
{
    const struct micros_frame_owner temporary = frame_ownership_test_raw_kernel_owner(
        MICROS_FRAME_OWNER_KERNEL_TEMPORARY
    );
    const struct micros_process_handle boundary_process = {1, 1};
    const struct micros_frame_owner boundary_owner = frame_ownership_test_raw_process_owner(
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
        boundary_process.slot,
        boundary_process.generation
    );
    const uint64_t frame_count =
        MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES;
    const uint64_t last_address =
        TEST_BASE + ((frame_count - 1) * MICROS_FRAME_SIZE);
    struct micros_frame_owner observed;
    uint64_t output;
    size_t index;

    EXPECT_TRUE(
        frame_ownership_test_initialize_fixture(context, frame_count)
    );
    EXPECT_TRUE(
        context->ownership.managed_frame_count == frame_count
    );
    for (index = 0; index < (size_t)(frame_count - 1); ++index) {
        context->allocator.allocated_bitmap[index / 64]
            |= UINT64_C(1) << (index % 64);
        context->ownership.owners[index] = temporary;
    }
    context->allocator.free_frame_count = 1;
    context->ownership.owned_frame_count = frame_count - 1;
    context->ownership.owner_counts[MICROS_FRAME_OWNER_KERNEL_TEMPORARY] =
        frame_count - 1;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_validate(&context->ownership)
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &context->ownership,
            boundary_owner,
            &output
        )
    );
    EXPECT_TRUE(output == last_address);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_lookup(
            &context->ownership,
            last_address,
            &observed
        )
    );
    EXPECT_TRUE(frame_ownership_test_owners_equal(observed, boundary_owner));

    output = UINT64_C(0xfeedfacefeedface);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_EXHAUSTED,
        micros_frame_ownership_allocate(
            &context->ownership,
            temporary,
            &output
        )
    );
    EXPECT_TRUE(output == UINT64_C(0xfeedfacefeedface));
    frame_ownership_test_clear_release_bitmap(context);
    frame_ownership_test_select_release_index(context, frame_count - 1);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_release_process_set(
            &context->ownership,
            boundary_process,
            context->release_bitmap,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        )
    );
    output = 0;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &context->ownership,
            boundary_owner,
            &output
        )
    );
    EXPECT_TRUE(output == last_address);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_validate(&context->ownership)
    );
    return true;
}
