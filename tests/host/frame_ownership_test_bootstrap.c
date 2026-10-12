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
static bool constructs_valid_kernel_owners(void)
{
    static const enum micros_frame_owner_kind kernel_kinds[] = {
        MICROS_FRAME_OWNER_KERNEL_RETAINED,
        MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE,
        MICROS_FRAME_OWNER_KERNEL_TEMPORARY,
        MICROS_FRAME_OWNER_VM_TRANSFERABLE,
    };
    struct micros_frame_owner owner;
    size_t index;

    for (
        index = 0;
        index < sizeof(kernel_kinds) / sizeof(kernel_kinds[0]);
        ++index
    ) {
        memset(&owner, 0xa5, sizeof(owner));
        EXPECT_ERROR(
            MICROS_FRAME_OWNERSHIP_OK,
            micros_frame_owner_make_kernel(
                kernel_kinds[index],
                &owner
            )
        );
        EXPECT_TRUE(
            frame_ownership_test_owners_equal(
                owner,
                frame_ownership_test_raw_kernel_owner(
                    kernel_kinds[index]
                )
            )
        );
    }
    return true;
}

static bool constructs_valid_process_owners(void)
{
    static const enum micros_frame_owner_kind process_kinds[] = {
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
        MICROS_FRAME_OWNER_PROCESS_USER,
        MICROS_FRAME_OWNER_VM_WIRED,
    };
    static const struct micros_process_handle valid_processes[] = {
        {0, 1},
        {
            MICROS_PROCESS_CAPACITY - 1,
            MICROS_PROCESS_GENERATION_MAX,
        },
    };
    struct micros_frame_owner owner;
    size_t index;
    size_t process_index;

    for (
        process_index = 0;
        process_index
            < sizeof(valid_processes) / sizeof(valid_processes[0]);
        ++process_index
    ) {
        for (
            index = 0;
            index < sizeof(process_kinds) / sizeof(process_kinds[0]);
            ++index
        ) {
            memset(&owner, 0xa5, sizeof(owner));
            EXPECT_ERROR(
                MICROS_FRAME_OWNERSHIP_OK,
                micros_frame_owner_make_process(
                    process_kinds[index],
                    valid_processes[process_index],
                    &owner
                )
            );
            EXPECT_TRUE(
                frame_ownership_test_owners_equal(
                    owner,
                    frame_ownership_test_raw_process_owner(
                        process_kinds[index],
                        valid_processes[process_index].slot,
                        valid_processes[process_index].generation
                    )
                )
            );
        }
    }
    return true;
}

static bool rejects_invalid_kernel_owners(void)
{
    static const enum micros_frame_owner_kind invalid_kinds[] = {
        MICROS_FRAME_OWNER_FREE,
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
        MICROS_FRAME_OWNER_PROCESS_USER,
        MICROS_FRAME_OWNER_VM_WIRED,
        (enum micros_frame_owner_kind)MICROS_FRAME_OWNER_KIND_COUNT,
        (enum micros_frame_owner_kind)UINT8_MAX,
    };
    struct micros_frame_owner owner;
    struct micros_frame_owner owner_before;
    size_t index;

    for (
        index = 0;
        index < sizeof(invalid_kinds) / sizeof(invalid_kinds[0]);
        ++index
    ) {
        memset(&owner, 0xa5, sizeof(owner));
        owner_before = owner;
        EXPECT_ERROR(
            MICROS_FRAME_OWNERSHIP_ERROR_OWNER,
            micros_frame_owner_make_kernel(
                invalid_kinds[index],
                &owner
            )
        );
        EXPECT_TRUE(memcmp(&owner, &owner_before, sizeof(owner)) == 0);
    }
    return true;
}

static bool rejects_invalid_process_owners(void)
{
    static const enum micros_frame_owner_kind invalid_kinds[] = {
        MICROS_FRAME_OWNER_FREE,
        MICROS_FRAME_OWNER_KERNEL_RETAINED,
        MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE,
        MICROS_FRAME_OWNER_KERNEL_TEMPORARY,
        MICROS_FRAME_OWNER_VM_TRANSFERABLE,
        (enum micros_frame_owner_kind)MICROS_FRAME_OWNER_KIND_COUNT,
        (enum micros_frame_owner_kind)UINT8_MAX,
    };
    static const struct micros_process_handle invalid_processes[] = {
        {0, 0},
        {MICROS_PROCESS_CAPACITY, 1},
        {0, MICROS_PROCESS_GENERATION_MAX + UINT32_C(1)},
    };
    const struct micros_process_handle valid_process = {0, 1};
    struct micros_frame_owner owner;
    struct micros_frame_owner owner_before;
    size_t index;

    for (
        index = 0;
        index < sizeof(invalid_kinds) / sizeof(invalid_kinds[0]);
        ++index
    ) {
        memset(&owner, 0xa5, sizeof(owner));
        owner_before = owner;
        EXPECT_ERROR(
            MICROS_FRAME_OWNERSHIP_ERROR_OWNER,
            micros_frame_owner_make_process(
                invalid_kinds[index],
                valid_process,
                &owner
            )
        );
        EXPECT_TRUE(memcmp(&owner, &owner_before, sizeof(owner)) == 0);
    }
    for (
        index = 0;
        index
            < sizeof(invalid_processes) / sizeof(invalid_processes[0]);
        ++index
    ) {
        memset(&owner, 0xa5, sizeof(owner));
        owner_before = owner;
        EXPECT_ERROR(
            MICROS_FRAME_OWNERSHIP_ERROR_OWNER,
            micros_frame_owner_make_process(
                MICROS_FRAME_OWNER_PROCESS_USER,
                invalid_processes[index],
                &owner
            )
        );
        EXPECT_TRUE(memcmp(&owner, &owner_before, sizeof(owner)) == 0);
    }
    return true;
}

static bool rejects_null_owner_outputs(void)
{
    const struct micros_process_handle process = {0, 1};

    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT,
        micros_frame_owner_make_kernel(
            MICROS_FRAME_OWNER_KERNEL_RETAINED,
            NULL
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT,
        micros_frame_owner_make_process(
            MICROS_FRAME_OWNER_PROCESS_USER,
            process,
            NULL
        )
    );
    return true;
}

bool frame_ownership_test_constructs_exact_owner_records(
    struct frame_ownership_test_context *context
)
{
    (void)context;

    return (
        constructs_valid_kernel_owners()
        && constructs_valid_process_owners()
        && rejects_invalid_kernel_owners()
        && rejects_invalid_process_owners()
        && rejects_null_owner_outputs()
    );
}

static bool initialize_pristine_ledger(
    struct frame_ownership_test_context *context,
    const struct micros_physical_range *range
)
{
    memset(&context->allocator, 0, sizeof(context->allocator));
    memset(&context->ownership, 0, sizeof(context->ownership));
    EXPECT_TRUE(
        micros_frame_allocator_initialize(
            &context->allocator,
            range,
            1,
            NULL,
            0
        ) == MICROS_FRAME_ALLOCATOR_OK
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_initialize(&context->ownership, &context->allocator)
    );
    EXPECT_TRUE(
        context->ownership.phase == MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
    );
    EXPECT_TRUE(context->ownership.allocator == &context->allocator);
    EXPECT_TRUE(
        context->ownership.allocator_identity == (uintptr_t)&context->allocator
    );
    EXPECT_TRUE(
        context->ownership.managed_frame_count
            == context->allocator.managed_frame_count
    );
    return true;
}

static bool rejects_reinitialization(
    struct frame_ownership_test_context *context
)
{
    frame_ownership_test_snapshot(context);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_ALREADY_INITIALIZED,
        micros_frame_ownership_initialize(&context->ownership, &context->allocator)
    );
    EXPECT_TRUE(frame_ownership_test_snapshot_matches(context));
    return true;
}

static bool rejects_nonzero_ownership_storage(
    struct frame_ownership_test_context *context
)
{
    memset(&context->ownership, 0, sizeof(context->ownership));
    context->ownership.owners[0].reserved = 1;
    frame_ownership_test_snapshot(context);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_STORAGE,
        micros_frame_ownership_initialize(&context->ownership, &context->allocator)
    );
    EXPECT_TRUE(frame_ownership_test_snapshot_matches(context));
    return true;
}

static bool rejects_nonpristine_allocator(
    struct frame_ownership_test_context *context
)
{
    uint64_t allocated;

    memset(&context->ownership, 0, sizeof(context->ownership));
    EXPECT_TRUE(
        micros_frame_allocator_allocate(&context->allocator, &allocated)
            == MICROS_FRAME_ALLOCATOR_OK
    );
    frame_ownership_test_snapshot(context);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_ALLOCATOR_STATE,
        micros_frame_ownership_initialize(&context->ownership, &context->allocator)
    );
    EXPECT_TRUE(frame_ownership_test_snapshot_matches(context));
    return true;
}

bool frame_ownership_test_initializes_once_on_pristine_storage(
    struct frame_ownership_test_context *context
)
{
    const struct micros_physical_range range = {
        TEST_BASE,
        TEST_FRAME_COUNT * MICROS_FRAME_SIZE,
    };

    return (
        initialize_pristine_ledger(context, &range)
        && rejects_reinitialization(context)
        && rejects_nonzero_ownership_storage(context)
        && rejects_nonpristine_allocator(context)
    );
}

bool frame_ownership_test_rejects_invalid_raw_owner_records(
    struct frame_ownership_test_context *context
)
{
    static const struct micros_frame_owner invalid_owners[] = {
        {0, 0, MICROS_FRAME_OWNER_FREE, 0},
        {0, 1, MICROS_FRAME_OWNER_KERNEL_RETAINED, 0},
        {1, 0, MICROS_FRAME_OWNER_KERNEL_RETAINED, 0},
        {0, 0, MICROS_FRAME_OWNER_PROCESS_USER, 0},
        {
            MICROS_PROCESS_GENERATION_MAX + UINT32_C(1),
            0,
            MICROS_FRAME_OWNER_PROCESS_USER,
            0,
        },
        {
            1,
            MICROS_PROCESS_CAPACITY,
            MICROS_FRAME_OWNER_PROCESS_USER,
            0,
        },
        {1, 0, MICROS_FRAME_OWNER_PROCESS_USER, 1},
        {1, 0, MICROS_FRAME_OWNER_VM_TRANSFERABLE, 0},
        {0, 0, MICROS_FRAME_OWNER_VM_TRANSFERABLE, 0},
        {1, 0, MICROS_FRAME_OWNER_VM_WIRED, 0},
        {0, 0, UINT8_MAX, 0},
    };
    uint64_t unchanged;
    size_t index;

    EXPECT_TRUE(frame_ownership_test_initialize_fixture(context, TEST_FRAME_COUNT));
    for (
        index = 0;
        index < sizeof(invalid_owners) / sizeof(invalid_owners[0]);
        ++index
    ) {
        unchanged = UINT64_C(0x123456789abcdef0);
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
            MICROS_FRAME_OWNERSHIP_ERROR_OWNER,
            micros_frame_ownership_allocate(
                &context->ownership,
                invalid_owners[index],
                &unchanged
            )
        );
        EXPECT_TRUE(unchanged == UINT64_C(0x123456789abcdef0));
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
    }
    return true;
}

struct exact_owner_fixture {
    struct micros_frame_owner process_one;
    struct micros_frame_owner process_two;
    struct micros_frame_owner temporary;
    struct micros_frame_owner observed;
    uint64_t process_count;
    uint64_t frames[3];
    uint64_t reused_frame;
};

static bool initialize_exact_owner_fixture(
    struct frame_ownership_test_context *context,
    struct exact_owner_fixture *fixture
)
{
    fixture->process_one = frame_ownership_test_raw_process_owner(
        MICROS_FRAME_OWNER_PROCESS_USER,
        1,
        3
    );
    fixture->process_two = frame_ownership_test_raw_process_owner(
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
        2,
        5
    );
    fixture->temporary = frame_ownership_test_raw_kernel_owner(
        MICROS_FRAME_OWNER_KERNEL_TEMPORARY
    );
    fixture->observed = frame_ownership_test_raw_kernel_owner(
        MICROS_FRAME_OWNER_KERNEL_RETAINED
    );
    fixture->process_count = UINT64_MAX;
    fixture->reused_frame = UINT64_C(0xfeedfacefeedface);

    EXPECT_TRUE(frame_ownership_test_initialize_fixture(context, TEST_FRAME_COUNT));
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &context->ownership,
            fixture->process_one,
            &fixture->frames[0]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &context->ownership,
            fixture->temporary,
            &fixture->frames[1]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &context->ownership,
            fixture->process_two,
            &fixture->frames[2]
        )
    );
    EXPECT_TRUE(fixture->frames[0] == TEST_BASE);
    EXPECT_TRUE(fixture->frames[1] == TEST_BASE + MICROS_FRAME_SIZE);
    EXPECT_TRUE(
        fixture->frames[2] == TEST_BASE + (2 * MICROS_FRAME_SIZE)
    );
    return true;
}

static bool verify_exact_owner_lookup(
    struct frame_ownership_test_context *context,
    struct exact_owner_fixture *fixture
)
{
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_lookup(
            &context->ownership,
            fixture->frames[0],
            &fixture->observed
        )
    );
    EXPECT_TRUE(
        frame_ownership_test_owners_equal(
            fixture->observed,
            fixture->process_one
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_count_process(
            &context->ownership,
            (struct micros_process_handle){1, 3},
            &fixture->process_count
        )
    );
    EXPECT_TRUE(fixture->process_count == 1);
    return true;
}

static bool rejects_wrong_generation_release(
    struct frame_ownership_test_context *context,
    const struct exact_owner_fixture *fixture
)
{
    frame_ownership_test_snapshot(context);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER,
        micros_frame_ownership_release(
            &context->ownership,
            frame_ownership_test_raw_process_owner(
                MICROS_FRAME_OWNER_PROCESS_USER,
                1,
                4
            ),
            fixture->frames[0]
        )
    );
    EXPECT_TRUE(frame_ownership_test_snapshot_matches(context));
    return true;
}

static bool release_reuse_and_cleanup(
    struct frame_ownership_test_context *context,
    struct exact_owner_fixture *fixture
)
{
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_release(
            &context->ownership,
            fixture->temporary,
            fixture->frames[1]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &context->ownership,
            fixture->temporary,
            &fixture->reused_frame
        )
    );
    EXPECT_TRUE(fixture->reused_frame == fixture->frames[1]);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_UNALIGNED,
        micros_frame_ownership_release(
            &context->ownership,
            fixture->temporary,
            fixture->reused_frame + 1
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_UNMANAGED,
        micros_frame_ownership_lookup(
            &context->ownership,
            UINT64_C(0x90000000),
            &fixture->observed
        )
    );
    EXPECT_TRUE(
        frame_ownership_test_owners_equal(
            fixture->observed,
            fixture->process_one
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_release(
            &context->ownership,
            fixture->process_one,
            fixture->frames[0]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_release(
            &context->ownership,
            fixture->temporary,
            fixture->reused_frame
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_release(
            &context->ownership,
            fixture->process_two,
            fixture->frames[2]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED,
        micros_frame_ownership_release(
            &context->ownership,
            fixture->process_two,
            fixture->frames[2]
        )
    );
    EXPECT_TRUE(context->ownership.owned_frame_count == 0);
    EXPECT_TRUE(context->allocator.free_frame_count == TEST_FRAME_COUNT);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_validate(&context->ownership)
    );
    return true;
}

bool frame_ownership_test_allocates_looks_up_and_releases_exact_owners(
    struct frame_ownership_test_context *context
)
{
    struct exact_owner_fixture fixture;

    return (
        initialize_exact_owner_fixture(context, &fixture)
        && verify_exact_owner_lookup(context, &fixture)
        && rejects_wrong_generation_release(context, &fixture)
        && release_reuse_and_cleanup(context, &fixture)
    );
}

bool frame_ownership_test_rolls_back_defensive_post_allocation_failure(
    struct frame_ownership_test_context *context
)
{
    const struct micros_frame_owner temporary = frame_ownership_test_raw_kernel_owner(
        MICROS_FRAME_OWNER_KERNEL_TEMPORARY
    );
    uint64_t unchanged = UINT64_C(0x8899aabbccddeeff);

    EXPECT_TRUE(frame_ownership_test_initialize_fixture(context, TEST_FRAME_COUNT));
    context->ownership.owners[0] = temporary;
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
            temporary,
            &unchanged
        )
    );
    EXPECT_TRUE(unchanged == UINT64_C(0x8899aabbccddeeff));
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

static bool expect_release_set_failure(
    struct frame_ownership_test_context *context,
    struct micros_process_handle process,
    size_t word_count,
    enum micros_frame_ownership_error expected_error
)
{
    frame_ownership_test_snapshot(context);
    EXPECT_ERROR(
        expected_error,
        micros_frame_ownership_release_process_set(
            &context->ownership,
            process,
            context->release_bitmap,
            word_count
        )
    );
    EXPECT_TRUE(frame_ownership_test_snapshot_matches(context));
    return true;
}

struct process_release_fixture {
    struct micros_process_handle process_one;
    struct micros_process_handle process_two;
    struct micros_frame_owner process_table;
    struct micros_frame_owner process_user;
    struct micros_frame_owner foreign_user;
    struct micros_frame_owner temporary;
    struct micros_frame_owner observed;
    uint64_t process_count;
    uint64_t frames[4];
};

static bool initialize_process_release_fixture(
    struct frame_ownership_test_context *context,
    struct process_release_fixture *fixture
)
{
    fixture->process_one = (struct micros_process_handle){1, 2};
    fixture->process_two = (struct micros_process_handle){2, 3};
    fixture->process_table = frame_ownership_test_raw_process_owner(
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
        fixture->process_one.slot,
        fixture->process_one.generation
    );
    fixture->process_user = frame_ownership_test_raw_process_owner(
        MICROS_FRAME_OWNER_PROCESS_USER,
        fixture->process_one.slot,
        fixture->process_one.generation
    );
    fixture->foreign_user = frame_ownership_test_raw_process_owner(
        MICROS_FRAME_OWNER_PROCESS_USER,
        fixture->process_two.slot,
        fixture->process_two.generation
    );
    fixture->temporary = frame_ownership_test_raw_kernel_owner(
        MICROS_FRAME_OWNER_KERNEL_TEMPORARY
    );
    fixture->process_count = UINT64_MAX;

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
            fixture->process_user,
            &fixture->frames[1]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &context->ownership,
            fixture->foreign_user,
            &fixture->frames[2]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &context->ownership,
            fixture->temporary,
            &fixture->frames[3]
        )
    );
    return true;
}

static bool empty_process_release_is_noop(
    struct frame_ownership_test_context *context,
    const struct process_release_fixture *fixture
)
{
    frame_ownership_test_clear_release_bitmap(context);
    frame_ownership_test_snapshot(context);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_release_process_set(
            &context->ownership,
            fixture->process_one,
            context->release_bitmap,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        )
    );
    EXPECT_TRUE(frame_ownership_test_snapshot_matches(context));
    return true;
}

static bool invalid_process_release_sets_are_rejected(
    struct frame_ownership_test_context *context,
    const struct process_release_fixture *fixture
)
{
    EXPECT_TRUE(
        expect_release_set_failure(
            context,
            fixture->process_one,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS - 1,
            MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT
        )
    );
    frame_ownership_test_clear_release_bitmap(context);
    frame_ownership_test_select_release_index(context, TEST_FRAME_COUNT);
    EXPECT_TRUE(
        expect_release_set_failure(
            context,
            fixture->process_one,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_UNMANAGED
        )
    );
    frame_ownership_test_clear_release_bitmap(context);
    frame_ownership_test_select_release_index(context, 5);
    EXPECT_TRUE(
        expect_release_set_failure(
            context,
            fixture->process_one,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED
        )
    );
    frame_ownership_test_clear_release_bitmap(context);
    frame_ownership_test_select_release_index(context, 2);
    EXPECT_TRUE(
        expect_release_set_failure(
            context,
            fixture->process_one,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER
        )
    );
    frame_ownership_test_clear_release_bitmap(context);
    frame_ownership_test_select_release_index(context, 0);
    EXPECT_TRUE(
        expect_release_set_failure(
            context,
            (struct micros_process_handle){
                fixture->process_one.slot,
                fixture->process_one.generation + 1,
            },
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER
        )
    );
    frame_ownership_test_clear_release_bitmap(context);
    frame_ownership_test_select_release_index(context, 3);
    EXPECT_TRUE(
        expect_release_set_failure(
            context,
            fixture->process_one,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER
        )
    );
    frame_ownership_test_clear_release_bitmap(context);
    frame_ownership_test_select_release_index(context, 0);
    EXPECT_TRUE(
        expect_release_set_failure(
            context,
            (struct micros_process_handle){0, 0},
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_OWNER
        )
    );
    return true;
}

static bool corrupted_process_release_state_is_rejected(
    struct frame_ownership_test_context *context,
    const struct process_release_fixture *fixture
)
{
    context->allocator.allocated_bitmap[0] &= ~UINT64_C(1);
    EXPECT_TRUE(
        expect_release_set_failure(
            context,
            fixture->process_one,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT
        )
    );
    context->allocator.allocated_bitmap[0] |= UINT64_C(1);
    ++context->ownership.owned_frame_count;
    EXPECT_TRUE(
        expect_release_set_failure(
            context,
            fixture->process_one,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT
        )
    );
    --context->ownership.owned_frame_count;
    ++context->ownership.owner_counts[
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
    ];
    EXPECT_TRUE(
        expect_release_set_failure(
            context,
            fixture->process_one,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT
        )
    );
    --context->ownership.owner_counts[
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
    ];
    ++context->allocator.free_frame_count;
    EXPECT_TRUE(
        expect_release_set_failure(
            context,
            fixture->process_one,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT
        )
    );
    --context->allocator.free_frame_count;
    return true;
}

static bool selected_process_frames_are_released(
    struct frame_ownership_test_context *context,
    struct process_release_fixture *fixture
)
{
    frame_ownership_test_clear_release_bitmap(context);
    frame_ownership_test_select_release_index(context, 0);
    frame_ownership_test_select_release_index(context, 1);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_release_process_set(
            &context->ownership,
            fixture->process_one,
            context->release_bitmap,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_lookup(
            &context->ownership,
            fixture->frames[0],
            &fixture->observed
        )
    );
    EXPECT_TRUE(
        frame_ownership_test_owners_equal(
            fixture->observed,
            frame_ownership_test_raw_kernel_owner(MICROS_FRAME_OWNER_FREE)
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_lookup(
            &context->ownership,
            fixture->frames[1],
            &fixture->observed
        )
    );
    EXPECT_TRUE(
        frame_ownership_test_owners_equal(
            fixture->observed,
            frame_ownership_test_raw_kernel_owner(MICROS_FRAME_OWNER_FREE)
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_count_process(
            &context->ownership,
            fixture->process_one,
            &fixture->process_count
        )
    );
    EXPECT_TRUE(fixture->process_count == 0);
    EXPECT_TRUE(context->ownership.owned_frame_count == 2);
    EXPECT_TRUE(context->allocator.free_frame_count == TEST_FRAME_COUNT - 2);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_validate(&context->ownership)
    );
    return true;
}

static bool post_handoff_process_release_is_rejected(
    struct frame_ownership_test_context *context,
    struct process_release_fixture *fixture
)
{
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
        micros_frame_ownership_complete_handoff(&context->ownership)
    );
    frame_ownership_test_clear_release_bitmap(context);
    frame_ownership_test_select_release_index(context, 0);
    EXPECT_TRUE(
        expect_release_set_failure(
            context,
            fixture->process_one,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_PHASE
        )
    );
    EXPECT_TRUE(
        expect_release_set_failure(
            context,
            fixture->process_one,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS - 1,
            MICROS_FRAME_OWNERSHIP_ERROR_PHASE
        )
    );
    return true;
}

bool frame_ownership_test_atomically_releases_process_owned_sets(
    struct frame_ownership_test_context *context
)
{
    struct process_release_fixture fixture;

    return (
        initialize_process_release_fixture(context, &fixture)
        && empty_process_release_is_noop(context, &fixture)
        && invalid_process_release_sets_are_rejected(context, &fixture)
        && corrupted_process_release_state_is_rejected(context, &fixture)
        && selected_process_frames_are_released(context, &fixture)
        && post_handoff_process_release_is_rejected(context, &fixture)
    );
}
