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
static bool expect_handoff_plan_failure(
    struct frame_ownership_test_context *context,
    uint64_t physical_address,
    struct micros_frame_owner expected_owner,
    enum micros_frame_handoff_target target,
    enum micros_frame_ownership_error expected_error
)
{
    frame_ownership_test_snapshot(context);
    EXPECT_ERROR(
        expected_error,
        micros_frame_ownership_prepare_handoff(
            &context->ownership,
            physical_address,
            expected_owner,
            target
        )
    );
    EXPECT_TRUE(frame_ownership_test_snapshot_matches(context));
    return true;
}

static bool expect_handoff_completion_failure(
    struct frame_ownership_test_context *context,
    enum micros_frame_ownership_error expected_error
)
{
    frame_ownership_test_snapshot(context);
    EXPECT_ERROR(
        expected_error,
        micros_frame_ownership_complete_handoff(&context->ownership)
    );
    EXPECT_TRUE(frame_ownership_test_snapshot_matches(context));
    return true;
}

static bool expect_wired_set_failure(
    struct frame_ownership_test_context *context,
    size_t word_count,
    enum micros_frame_ownership_error expected_error
)
{
    frame_ownership_test_snapshot(context);
    EXPECT_ERROR(
        expected_error,
        micros_frame_ownership_prepare_wired_process_user_set(
            &context->ownership,
            context->release_bitmap,
            word_count
        )
    );
    EXPECT_TRUE(frame_ownership_test_snapshot_matches(context));
    return true;
}

struct handoff_fixture {
    struct micros_frame_owner process_table;
    struct micros_frame_owner process_user_one;
    struct micros_frame_owner process_user_two;
    struct micros_frame_owner retained;
    struct micros_frame_owner temporary;
    uint64_t frames[5];
};

static bool initialize_handoff_fixture(
    struct frame_ownership_test_context *context,
    struct handoff_fixture *fixture
)
{
    fixture->process_table = frame_ownership_test_raw_process_owner(
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
        1,
        2
    );
    fixture->process_user_one = frame_ownership_test_raw_process_owner(
        MICROS_FRAME_OWNER_PROCESS_USER,
        1,
        2
    );
    fixture->process_user_two = frame_ownership_test_raw_process_owner(
        MICROS_FRAME_OWNER_PROCESS_USER,
        2,
        1
    );
    fixture->retained = frame_ownership_test_raw_kernel_owner(
        MICROS_FRAME_OWNER_KERNEL_RETAINED
    );
    fixture->temporary = frame_ownership_test_raw_kernel_owner(
        MICROS_FRAME_OWNER_KERNEL_TEMPORARY
    );

    EXPECT_TRUE(frame_ownership_test_initialize_fixture(context, TEST_FRAME_COUNT));
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &context->ownership,
            fixture->process_table,
            &fixture->frames[0]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &context->ownership,
            fixture->process_user_one,
            &fixture->frames[1]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &context->ownership,
            fixture->process_user_two,
            &fixture->frames[2]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &context->ownership,
            fixture->retained,
            &fixture->frames[3]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &context->ownership,
            fixture->temporary,
            &fixture->frames[4]
        )
    );
    return true;
}

static bool forbidden_handoff_plans_are_rejected(
    struct frame_ownership_test_context *context,
    const struct handoff_fixture *fixture
)
{
    EXPECT_TRUE(
        expect_handoff_plan_failure(
            context,
            fixture->frames[0],
            fixture->process_table,
            MICROS_FRAME_HANDOFF_VM_WIRED,
            MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION
        )
    );
    EXPECT_TRUE(
        expect_handoff_plan_failure(
            context,
            fixture->frames[0],
            fixture->process_table,
            MICROS_FRAME_HANDOFF_VM_TRANSFERABLE,
            MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION
        )
    );
    EXPECT_TRUE(
        expect_handoff_plan_failure(
            context,
            fixture->frames[4],
            fixture->temporary,
            MICROS_FRAME_HANDOFF_VM_WIRED,
            MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION
        )
    );
    EXPECT_TRUE(
        expect_handoff_plan_failure(
            context,
            fixture->frames[4],
            fixture->temporary,
            MICROS_FRAME_HANDOFF_VM_TRANSFERABLE,
            MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION
        )
    );
    EXPECT_TRUE(
        expect_handoff_plan_failure(
            context,
            fixture->frames[3],
            fixture->retained,
            MICROS_FRAME_HANDOFF_VM_WIRED,
            MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION
        )
    );
    EXPECT_TRUE(
        expect_handoff_plan_failure(
            context,
            fixture->frames[1],
            fixture->process_user_two,
            MICROS_FRAME_HANDOFF_VM_WIRED,
            MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER
        )
    );
    EXPECT_TRUE(
        expect_handoff_plan_failure(
            context,
            TEST_BASE + (5 * MICROS_FRAME_SIZE),
        fixture->process_user_one,
            MICROS_FRAME_HANDOFF_VM_WIRED,
            MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED
        )
    );
    EXPECT_TRUE(
        expect_handoff_plan_failure(
        context,
        fixture->frames[1],
        fixture->process_user_one,
        (enum micros_frame_handoff_target)
            MICROS_FRAME_HANDOFF_TARGET_COUNT,
        MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION
        )
    );
    return true;
}

static bool incomplete_handoff_is_rejected(
    struct frame_ownership_test_context *context,
    const struct handoff_fixture *fixture
)
{
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_prepare_handoff(
        &context->ownership,
        fixture->frames[1],
        fixture->process_user_one,
        MICROS_FRAME_HANDOFF_VM_WIRED
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_prepare_handoff(
            &context->ownership,
            fixture->frames[3],
            fixture->retained,
            MICROS_FRAME_HANDOFF_VM_TRANSFERABLE
        )
    );
    EXPECT_TRUE(
        expect_handoff_completion_failure(
            context,
            MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION
        )
    );

    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_release(
            &context->ownership,
            fixture->temporary,
            fixture->frames[4]
        )
    );
    EXPECT_TRUE(
        expect_handoff_completion_failure(
            context,
            MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION
        )
    );
    return true;
}

static bool complete_handoff_plan(
    struct frame_ownership_test_context *context,
    const struct handoff_fixture *fixture
)
{
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_prepare_handoff(
            &context->ownership,
            fixture->frames[2],
            fixture->process_user_two,
            MICROS_FRAME_HANDOFF_VM_TRANSFERABLE
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_prepare_handoff(
            &context->ownership,
            fixture->frames[1],
            fixture->process_user_one,
            MICROS_FRAME_HANDOFF_NONE
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_prepare_handoff(
            &context->ownership,
            fixture->frames[1],
            fixture->process_user_one,
            MICROS_FRAME_HANDOFF_VM_WIRED
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_complete_handoff(&context->ownership)
    );
    EXPECT_TRUE(
        context->ownership.phase == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
    );
    return true;
}

static bool completed_handoff_frame_owners_are_exact(
    struct frame_ownership_test_context *context,
    const struct handoff_fixture *fixture
)
{
    struct micros_frame_owner observed;

    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_lookup(
            &context->ownership,
            fixture->frames[0],
            &observed
        )
    );
    EXPECT_TRUE(
        frame_ownership_test_owners_equal(
            observed,
            fixture->process_table
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_lookup(
            &context->ownership,
            fixture->frames[1],
            &observed
        )
    );
    EXPECT_TRUE(
        frame_ownership_test_owners_equal(
            observed,
            frame_ownership_test_raw_process_owner(
                MICROS_FRAME_OWNER_VM_WIRED,
                1,
                2
            )
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_lookup(
            &context->ownership,
            fixture->frames[2],
            &observed
        )
    );
    EXPECT_TRUE(
        frame_ownership_test_owners_equal(
            observed,
            frame_ownership_test_raw_kernel_owner(
                MICROS_FRAME_OWNER_VM_TRANSFERABLE
            )
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_lookup(
            &context->ownership,
            fixture->frames[3],
            &observed
        )
    );
    EXPECT_TRUE(
        frame_ownership_test_owners_equal(
            observed,
            frame_ownership_test_raw_kernel_owner(
                MICROS_FRAME_OWNER_VM_TRANSFERABLE
            )
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_lookup(
            &context->ownership,
            fixture->frames[4],
            &observed
        )
    );
    EXPECT_TRUE(
        frame_ownership_test_owners_equal(
            observed,
            frame_ownership_test_raw_kernel_owner(MICROS_FRAME_OWNER_FREE)
        )
    );
    return true;
}

static bool completed_handoff_counts_are_exact(
    struct frame_ownership_test_context *context
)
{
    uint64_t count = UINT64_MAX;
    uint64_t expected_counts[MICROS_FRAME_OWNER_KIND_COUNT] = {0};
    size_t index;

    for (index = 0; index < TEST_FRAME_COUNT; ++index) {
        EXPECT_TRUE(
            context->ownership.handoff_targets[index]
                == MICROS_FRAME_HANDOFF_NONE
        );
    }
    expected_counts[MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE] = 1;
    expected_counts[MICROS_FRAME_OWNER_VM_WIRED] = 1;
    expected_counts[MICROS_FRAME_OWNER_VM_TRANSFERABLE] = 2;
    EXPECT_TRUE(context->ownership.owned_frame_count == 4);
    EXPECT_TRUE(context->allocator.free_frame_count == TEST_FRAME_COUNT - 4);
    for (
        index = 0;
        index < MICROS_FRAME_OWNER_KIND_COUNT;
        ++index
    ) {
        EXPECT_TRUE(
            context->ownership.owner_counts[index] == expected_counts[index]
        );
    }
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_count_process(
            &context->ownership,
            (struct micros_process_handle){1, 2},
            &count
        )
    );
    EXPECT_TRUE(count == 2);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_count_process(
            &context->ownership,
            (struct micros_process_handle){2, 1},
            &count
        )
    );
    EXPECT_TRUE(count == 0);
    return true;
}

static bool post_handoff_bootstrap_operations_are_rejected(
    struct frame_ownership_test_context *context,
    const struct handoff_fixture *fixture
)
{
    uint64_t unchanged = UINT64_C(0xa5a5a5a5a5a5a5a5);

    frame_ownership_test_snapshot(context);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_PHASE,
        micros_frame_ownership_allocate(
            &context->ownership,
            fixture->temporary,
            &unchanged
        )
    );
    EXPECT_TRUE(unchanged == UINT64_C(0xa5a5a5a5a5a5a5a5));
    EXPECT_TRUE(frame_ownership_test_snapshot_matches(context));
    frame_ownership_test_snapshot(context);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_PHASE,
        micros_frame_ownership_release(
            &context->ownership,
            fixture->process_table,
            fixture->frames[0]
        )
    );
    EXPECT_TRUE(frame_ownership_test_snapshot_matches(context));
    EXPECT_TRUE(
        expect_handoff_plan_failure(
            context,
            fixture->frames[0],
            fixture->process_table,
            MICROS_FRAME_HANDOFF_NONE,
            MICROS_FRAME_OWNERSHIP_ERROR_PHASE
        )
    );
    EXPECT_TRUE(
        expect_handoff_completion_failure(
            context,
            MICROS_FRAME_OWNERSHIP_ERROR_PHASE
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_validate(&context->ownership)
    );
    return true;
}

bool frame_ownership_test_stages_and_atomically_commits_handoff(
    struct frame_ownership_test_context *context
)
{
    struct handoff_fixture fixture;

    return (
        initialize_handoff_fixture(context, &fixture)
        && forbidden_handoff_plans_are_rejected(context, &fixture)
        && incomplete_handoff_is_rejected(context, &fixture)
        && complete_handoff_plan(context, &fixture)
        && completed_handoff_frame_owners_are_exact(context, &fixture)
        && completed_handoff_counts_are_exact(context)
        && post_handoff_bootstrap_operations_are_rejected(
            context,
            &fixture
        )
    );
}

struct wired_set_fixture {
    struct micros_frame_owner process_user_one;
    struct micros_frame_owner process_user_two;
    struct micros_frame_owner process_table;
    struct micros_frame_owner retained;
    uint64_t frames[4];
};

static bool initialize_wired_set_fixture(
    struct frame_ownership_test_context *context,
    struct wired_set_fixture *fixture
)
{
    fixture->process_user_one = frame_ownership_test_raw_process_owner(
        MICROS_FRAME_OWNER_PROCESS_USER,
        1,
        2
    );
    fixture->process_user_two = frame_ownership_test_raw_process_owner(
        MICROS_FRAME_OWNER_PROCESS_USER,
        2,
        3
    );
    fixture->process_table = frame_ownership_test_raw_process_owner(
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
        1,
        2
    );
    fixture->retained = frame_ownership_test_raw_kernel_owner(
        MICROS_FRAME_OWNER_KERNEL_RETAINED
    );

    EXPECT_TRUE(frame_ownership_test_initialize_fixture(context, TEST_FRAME_COUNT));
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &context->ownership,
            fixture->process_user_one,
            &fixture->frames[0]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &context->ownership,
            fixture->process_user_two,
            &fixture->frames[1]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &context->ownership,
            fixture->process_table,
            &fixture->frames[2]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &context->ownership,
            fixture->retained,
            &fixture->frames[3]
        )
    );
    return true;
}

static bool invalid_wired_sets_are_rejected(
    struct frame_ownership_test_context *context
)
{
    frame_ownership_test_snapshot(context);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT,
        micros_frame_ownership_prepare_wired_process_user_set(
            &context->ownership,
            NULL,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        )
    );
    EXPECT_TRUE(frame_ownership_test_snapshot_matches(context));
    frame_ownership_test_clear_release_bitmap(context);
    frame_ownership_test_select_release_index(context, 0);
    EXPECT_TRUE(
        expect_wired_set_failure(
            context,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER
        )
    );
    frame_ownership_test_select_release_index(context, 1);
    frame_ownership_test_select_release_index(context, 2);
    EXPECT_TRUE(
        expect_wired_set_failure(
            context,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER
        )
    );
    frame_ownership_test_clear_release_bitmap(context);
    frame_ownership_test_select_release_index(context, 0);
    frame_ownership_test_select_release_index(context, 1);
    frame_ownership_test_select_release_index(context, 4);
    EXPECT_TRUE(
        expect_wired_set_failure(
            context,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED
        )
    );
    frame_ownership_test_clear_release_bitmap(context);
    frame_ownership_test_select_release_index(context, 0);
    frame_ownership_test_select_release_index(context, 1);
    frame_ownership_test_select_release_index(context, TEST_FRAME_COUNT);
    EXPECT_TRUE(
        expect_wired_set_failure(
            context,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_UNMANAGED
        )
    );
    frame_ownership_test_clear_release_bitmap(context);
    frame_ownership_test_select_release_index(context, 0);
    frame_ownership_test_select_release_index(context, 1);
    EXPECT_TRUE(
        expect_wired_set_failure(
            context,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS - 1,
            MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT
        )
    );
    return true;
}

static bool existing_wired_plan_is_rejected(
    struct frame_ownership_test_context *context,
    const struct wired_set_fixture *fixture
)
{
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_prepare_handoff(
            &context->ownership,
            fixture->frames[0],
            fixture->process_user_one,
            MICROS_FRAME_HANDOFF_VM_TRANSFERABLE
        )
    );
    EXPECT_TRUE(
        expect_wired_set_failure(
            context,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_prepare_handoff(
            &context->ownership,
            fixture->frames[0],
            fixture->process_user_one,
            MICROS_FRAME_HANDOFF_NONE
        )
    );
    return true;
}

static bool complete_wired_set_is_staged(
    struct frame_ownership_test_context *context
)
{
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_prepare_wired_process_user_set(
            &context->ownership,
            context->release_bitmap,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        )
    );
    EXPECT_TRUE(
        context->ownership.handoff_targets[0]
            == MICROS_FRAME_HANDOFF_VM_WIRED
        && context->ownership.handoff_targets[1]
            == MICROS_FRAME_HANDOFF_VM_WIRED
        && context->ownership.handoff_targets[2]
            == MICROS_FRAME_HANDOFF_NONE
        && context->ownership.handoff_targets[3]
            == MICROS_FRAME_HANDOFF_NONE
        && micros_frame_ownership_validate(&context->ownership)
            == MICROS_FRAME_OWNERSHIP_OK
    );
    return true;
}

static bool repeated_and_post_handoff_wired_sets_are_rejected(
    struct frame_ownership_test_context *context
)
{
    frame_ownership_test_snapshot(context);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION,
        micros_frame_ownership_prepare_wired_process_user_set(
            &context->ownership,
            context->release_bitmap,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        )
    );
    EXPECT_TRUE(frame_ownership_test_snapshot_matches(context));
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_complete_handoff(&context->ownership)
    );
    frame_ownership_test_snapshot(context);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_PHASE,
        micros_frame_ownership_prepare_wired_process_user_set(
            &context->ownership,
            context->release_bitmap,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        )
    );
    EXPECT_TRUE(frame_ownership_test_snapshot_matches(context));
    return true;
}

bool frame_ownership_test_atomically_stages_complete_wired_set(
    struct frame_ownership_test_context *context
)
{
    struct wired_set_fixture fixture;

    return (
        initialize_wired_set_fixture(context, &fixture)
        && invalid_wired_sets_are_rejected(context)
        && existing_wired_plan_is_rejected(context, &fixture)
        && complete_wired_set_is_staged(context)
        && repeated_and_post_handoff_wired_sets_are_rejected(context)
    );
}
