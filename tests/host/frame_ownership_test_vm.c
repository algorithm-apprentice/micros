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
struct vm_lifecycle_fixture {
    struct micros_process_handle process;
    uint64_t data_frames[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
    uint64_t table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT];
    uint64_t rollback_frame;
};

static bool initialize_vm_lifecycle_fixture(
    struct frame_ownership_test_context *context,
    struct vm_lifecycle_fixture *fixture
)
{
    size_t index;

    fixture->process = (struct micros_process_handle){3, 5};
    fixture->rollback_frame =
        TEST_BASE
        + (
            MICROS_APPLICATION_RESIDENT_PAGE_MAX
            + MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT
        ) * MICROS_FRAME_SIZE;

    EXPECT_TRUE(frame_ownership_test_initialize_fixture(context, TEST_FRAME_COUNT));
    for (
        index = 0;
        index < MICROS_APPLICATION_RESIDENT_PAGE_MAX;
        ++index
    ) {
        fixture->data_frames[index] =
            TEST_BASE + index * MICROS_FRAME_SIZE;
    }
    for (
        index = 0;
        index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        fixture->table_frames[index] = TEST_BASE
            + (
                MICROS_APPLICATION_RESIDENT_PAGE_MAX
                + index
            ) * MICROS_FRAME_SIZE;
    }
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_complete_handoff(&context->ownership)
    );
    return true;
}

static bool claim_and_release_rollback_frame(
    struct frame_ownership_test_context *context,
    const struct vm_lifecycle_fixture *fixture
)
{
    struct micros_frame_vm_claim_plan claim;
    struct micros_frame_vm_transferable_release_plan rollback;

    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_vm_claim_preflight(
            &context->ownership,
            fixture->rollback_frame,
            &claim
        )
    );
    micros_frame_ownership_vm_claim_commit_prevalidated(
        &context->ownership,
        &claim
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_vm_release_transferable_preflight(
            &context->ownership,
            &fixture->rollback_frame,
            1,
            &rollback
        )
    );
    micros_frame_ownership_vm_release_transferable_commit_prevalidated(
        &context->ownership,
        &rollback
    );
    return true;
}

static bool claim_vm_data_frames(
    struct frame_ownership_test_context *context,
    const struct vm_lifecycle_fixture *fixture
)
{
    struct micros_frame_vm_claim_plan claim;
    size_t index;

    for (
        index = 0;
        index < MICROS_APPLICATION_RESIDENT_PAGE_MAX;
        ++index
    ) {
        EXPECT_ERROR(
            MICROS_FRAME_OWNERSHIP_OK,
            micros_frame_ownership_vm_claim_preflight(
                &context->ownership,
                fixture->data_frames[index],
                &claim
            )
        );
        micros_frame_ownership_vm_claim_commit_prevalidated(
            &context->ownership,
            &claim
        );
    }
    EXPECT_TRUE(
        context->allocator.free_frame_count
            == TEST_FRAME_COUNT
                - MICROS_APPLICATION_RESIDENT_PAGE_MAX
        && context->ownership.owned_frame_count
            == MICROS_APPLICATION_RESIDENT_PAGE_MAX
        && context->ownership.owner_counts[
            MICROS_FRAME_OWNER_VM_TRANSFERABLE
        ] == MICROS_APPLICATION_RESIDENT_PAGE_MAX
    );
    return true;
}

static bool freeze_vm_process_frames(
    struct frame_ownership_test_context *context,
    const struct vm_lifecycle_fixture *fixture
)
{
    struct micros_frame_vm_freeze_plan freeze;

    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_vm_freeze_preflight(
            &context->ownership,
            fixture->process,
            fixture->data_frames,
            MICROS_APPLICATION_RESIDENT_PAGE_MAX,
            fixture->table_frames,
            &freeze
        )
    );
    micros_frame_ownership_vm_freeze_commit_prevalidated(
        &context->ownership,
        &freeze
    );
    EXPECT_TRUE(
        context->allocator.free_frame_count
            == TEST_FRAME_COUNT
                - MICROS_APPLICATION_RESIDENT_PAGE_MAX
                - MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT
        && context->ownership.owned_frame_count
            == MICROS_APPLICATION_RESIDENT_PAGE_MAX
                + MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT
        && context->ownership.owner_counts[
            MICROS_FRAME_OWNER_VM_TRANSFERABLE
        ] == 0
        && context->ownership.owner_counts[MICROS_FRAME_OWNER_VM_WIRED]
            == MICROS_APPLICATION_RESIDENT_PAGE_MAX
        && context->ownership.owner_counts[
            MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
        ] == 4
        && micros_frame_ownership_validate(&context->ownership)
            == MICROS_FRAME_OWNERSHIP_OK
    );
    return true;
}

static bool frozen_vm_frame_owners_are_exact(
    struct frame_ownership_test_context *context,
    const struct vm_lifecycle_fixture *fixture
)
{
    struct micros_frame_owner observed;
    size_t index;

    for (
        index = 0;
        index < MICROS_APPLICATION_RESIDENT_PAGE_MAX;
        ++index
    ) {
        EXPECT_ERROR(
            MICROS_FRAME_OWNERSHIP_OK,
            micros_frame_ownership_lookup(
                &context->ownership,
                fixture->data_frames[index],
                &observed
            )
        );
        EXPECT_TRUE(
            frame_ownership_test_owners_equal(
                observed,
                frame_ownership_test_raw_process_owner(
                    MICROS_FRAME_OWNER_VM_WIRED,
                    fixture->process.slot,
                    fixture->process.generation
                )
            )
        );
    }
    for (
        index = 0;
        index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        EXPECT_ERROR(
            MICROS_FRAME_OWNERSHIP_OK,
            micros_frame_ownership_lookup(
                &context->ownership,
                fixture->table_frames[index],
                &observed
            )
        );
        EXPECT_TRUE(
            frame_ownership_test_owners_equal(
                observed,
                frame_ownership_test_raw_process_owner(
                    MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
                    fixture->process.slot,
                    fixture->process.generation
                )
            )
        );
    }
    return true;
}

static bool release_vm_process_frames(
    struct frame_ownership_test_context *context,
    const struct vm_lifecycle_fixture *fixture
)
{
    struct micros_frame_vm_process_release_plan release;

    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_vm_release_process_preflight(
            &context->ownership,
            fixture->process,
            fixture->data_frames,
            MICROS_APPLICATION_RESIDENT_PAGE_MAX,
            fixture->table_frames,
            &release
        )
    );
    micros_frame_ownership_vm_release_process_commit_prevalidated(
        &context->ownership,
        &release
    );
    EXPECT_TRUE(
        context->allocator.free_frame_count == TEST_FRAME_COUNT
        && context->ownership.owned_frame_count == 0
        && micros_frame_ownership_validate(&context->ownership)
            == MICROS_FRAME_OWNERSHIP_OK
    );
    return true;
}

bool frame_ownership_test_commits_post_handoff_vm_ownership_lifecycle(
    struct frame_ownership_test_context *context
)
{
    struct vm_lifecycle_fixture fixture;

    return (
        initialize_vm_lifecycle_fixture(context, &fixture)
        && claim_and_release_rollback_frame(context, &fixture)
        && claim_vm_data_frames(context, &fixture)
        && freeze_vm_process_frames(context, &fixture)
        && frozen_vm_frame_owners_are_exact(context, &fixture)
        && release_vm_process_frames(context, &fixture)
    );
}

struct vm_atomic_fixture {
    struct micros_process_handle process;
    struct micros_process_handle wrong_process;
    uint64_t data_frames[2];
    uint64_t duplicate_frames[2];
    uint64_t table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT];
};

static bool initialize_vm_atomic_fixture(
    struct frame_ownership_test_context *context,
    struct vm_atomic_fixture *fixture
)
{
    struct micros_frame_vm_claim_plan claim;
    struct micros_frame_vm_claim_plan claim_sentinel;
    size_t index;

    fixture->process = (struct micros_process_handle){4, 7};
    fixture->wrong_process = (struct micros_process_handle){4, 8};
    fixture->data_frames[0] = TEST_BASE;
    fixture->data_frames[1] = TEST_BASE + MICROS_FRAME_SIZE;
    fixture->duplicate_frames[0] = TEST_BASE;
    fixture->duplicate_frames[1] = TEST_BASE;
    fixture->table_frames[0] =
        TEST_BASE + (2 * MICROS_FRAME_SIZE);
    fixture->table_frames[1] =
        TEST_BASE + (3 * MICROS_FRAME_SIZE);
    fixture->table_frames[2] =
        TEST_BASE + (4 * MICROS_FRAME_SIZE);
    fixture->table_frames[3] =
        TEST_BASE + (5 * MICROS_FRAME_SIZE);

    EXPECT_TRUE(frame_ownership_test_initialize_fixture(context, TEST_FRAME_COUNT));
    memset(&claim_sentinel, 0xa5, sizeof(claim_sentinel));
    claim = claim_sentinel;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_PHASE,
        micros_frame_ownership_vm_claim_preflight(
            &context->ownership,
            fixture->data_frames[0],
            &claim
        )
    );
    EXPECT_TRUE(
        memcmp(&claim, &claim_sentinel, sizeof(claim)) == 0
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_complete_handoff(&context->ownership)
    );
    for (
        index = 0;
        index < sizeof(fixture->data_frames)
            / sizeof(fixture->data_frames[0]);
        ++index
    ) {
        EXPECT_ERROR(
            MICROS_FRAME_OWNERSHIP_OK,
            micros_frame_ownership_vm_claim_preflight(
                &context->ownership,
                fixture->data_frames[index],
                &claim
            )
        );
        micros_frame_ownership_vm_claim_commit_prevalidated(
            &context->ownership,
            &claim
        );
    }
    return true;
}

static bool duplicate_transferable_release_is_rejected(
    struct frame_ownership_test_context *context,
    const struct vm_atomic_fixture *fixture
)
{
    struct micros_frame_vm_transferable_release_plan rollback;

    frame_ownership_test_snapshot(context);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION,
        micros_frame_ownership_vm_release_transferable_preflight(
            &context->ownership,
            fixture->duplicate_frames,
            2,
            &rollback
        )
    );
    EXPECT_TRUE(frame_ownership_test_snapshot_matches(context));
    return true;
}

static bool duplicate_freeze_frames_are_rejected(
    struct frame_ownership_test_context *context,
    struct vm_atomic_fixture *fixture
)
{
    struct micros_frame_vm_freeze_plan freeze;

    fixture->table_frames[3] = fixture->table_frames[2];
    frame_ownership_test_snapshot(context);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION,
        micros_frame_ownership_vm_freeze_preflight(
            &context->ownership,
            fixture->process,
            fixture->data_frames,
            sizeof(fixture->data_frames)
                / sizeof(fixture->data_frames[0]),
            fixture->table_frames,
            &freeze
        )
    );
    EXPECT_TRUE(frame_ownership_test_snapshot_matches(context));
    fixture->table_frames[3] =
        TEST_BASE + (5 * MICROS_FRAME_SIZE);
    return true;
}

static bool wrong_process_release_is_rejected(
    struct frame_ownership_test_context *context,
    const struct vm_atomic_fixture *fixture
)
{
    struct micros_frame_vm_freeze_plan freeze;
    struct micros_frame_vm_process_release_plan release;

    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_vm_freeze_preflight(
            &context->ownership,
            fixture->process,
            fixture->data_frames,
            sizeof(fixture->data_frames)
                / sizeof(fixture->data_frames[0]),
            fixture->table_frames,
            &freeze
        )
    );
    micros_frame_ownership_vm_freeze_commit_prevalidated(
        &context->ownership,
        &freeze
    );
    frame_ownership_test_snapshot(context);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER,
        micros_frame_ownership_vm_release_process_preflight(
            &context->ownership,
            fixture->wrong_process,
            fixture->data_frames,
            sizeof(fixture->data_frames)
                / sizeof(fixture->data_frames[0]),
            fixture->table_frames,
            &release
        )
    );
    EXPECT_TRUE(frame_ownership_test_snapshot_matches(context));
    return true;
}

bool frame_ownership_test_post_handoff_vm_ownership_preflight_is_atomic(
    struct frame_ownership_test_context *context
)
{
    struct vm_atomic_fixture fixture;

    return (
        initialize_vm_atomic_fixture(context, &fixture)
        && duplicate_transferable_release_is_rejected(
            context,
            &fixture
        )
        && duplicate_freeze_frames_are_rejected(context, &fixture)
        && wrong_process_release_is_rejected(context, &fixture)
    );
}
