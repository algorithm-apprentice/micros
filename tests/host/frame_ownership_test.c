#include "micros/frame_ownership.h"

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

enum {
    TEST_FRAME_COUNT = 32,
    MODEL_STEPS = 4096,
    MODEL_HANDOFF_STEP = 3072,
    MODEL_FULL_VALIDATION_INTERVAL = 64,
};

static const uint64_t TEST_BASE = UINT64_C(0x00100000);
static struct micros_frame_allocator allocator;
static struct micros_frame_allocator alternate_allocator;
static struct micros_frame_ownership ownership;
static struct micros_frame_ownership ownership_snapshot;
static struct micros_frame_allocator allocator_snapshot;
static uint64_t
    release_bitmap[MICROS_FRAME_ALLOCATOR_BITMAP_WORDS];

_Static_assert(
    sizeof(struct micros_frame_owner) == 8,
    "frame owner records must remain eight bytes"
);

static bool owners_equal(
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

static struct micros_frame_owner raw_kernel_owner(
    enum micros_frame_owner_kind kind
)
{
    struct micros_frame_owner owner = {
        0,
        0,
        (uint8_t)kind,
        0,
    };

    return owner;
}

static struct micros_frame_owner raw_process_owner(
    enum micros_frame_owner_kind kind,
    uint16_t slot,
    uint32_t generation
)
{
    struct micros_frame_owner owner = {
        generation,
        slot,
        (uint8_t)kind,
        0,
    };

    return owner;
}

static bool initialize_fixture(uint64_t frame_count)
{
    struct micros_physical_range range;

    if (
        frame_count == 0
        || frame_count > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES
    ) {
        return false;
    }
    memset(&allocator, 0, sizeof(allocator));
    memset(&ownership, 0, sizeof(ownership));
    range.base = TEST_BASE;
    range.size = frame_count * MICROS_FRAME_SIZE;
    return (
        micros_frame_allocator_initialize(
            &allocator,
            &range,
            1,
            NULL,
            0
        ) == MICROS_FRAME_ALLOCATOR_OK
        && micros_frame_ownership_initialize(
            &ownership,
            &allocator
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_frame_ownership_validate(&ownership)
            == MICROS_FRAME_OWNERSHIP_OK
    );
}

static void clear_release_bitmap(void)
{
    memset(release_bitmap, 0, sizeof(release_bitmap));
}

static void select_release_index(uint64_t frame_index)
{
    release_bitmap[frame_index / 64]
        |= UINT64_C(1) << (frame_index % 64);
}

static bool expect_geometry_allocate_failure(
    struct micros_frame_owner owner
)
{
    uint64_t unchanged = UINT64_C(0x1122334455667788);

    memcpy(
        &ownership_snapshot,
        &ownership,
        sizeof(ownership_snapshot)
    );
    memcpy(
        &allocator_snapshot,
        &allocator,
        sizeof(allocator_snapshot)
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_allocate(
            &ownership,
            owner,
            &unchanged
        )
    );
    EXPECT_TRUE(unchanged == UINT64_C(0x1122334455667788));
    EXPECT_TRUE(
        memcmp(
            &ownership,
            &ownership_snapshot,
            sizeof(ownership)
        ) == 0
    );
    EXPECT_TRUE(
        memcmp(
            &allocator,
            &allocator_snapshot,
            sizeof(allocator)
        ) == 0
    );
    return true;
}

static bool expect_geometry_release_failure(
    struct micros_frame_owner owner,
    uint64_t physical_address
)
{
    memcpy(
        &ownership_snapshot,
        &ownership,
        sizeof(ownership_snapshot)
    );
    memcpy(
        &allocator_snapshot,
        &allocator,
        sizeof(allocator_snapshot)
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_release(
            &ownership,
            owner,
            physical_address
        )
    );
    EXPECT_TRUE(
        memcmp(
            &ownership,
            &ownership_snapshot,
            sizeof(ownership)
        ) == 0
    );
    EXPECT_TRUE(
        memcmp(
            &allocator,
            &allocator_snapshot,
            sizeof(allocator)
        ) == 0
    );
    return true;
}

static bool test_constructs_exact_owner_records(void)
{
    static const enum micros_frame_owner_kind kernel_kinds[] = {
        MICROS_FRAME_OWNER_KERNEL_RETAINED,
        MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE,
        MICROS_FRAME_OWNER_KERNEL_TEMPORARY,
        MICROS_FRAME_OWNER_VM_TRANSFERABLE,
    };
    static const enum micros_frame_owner_kind process_kinds[] = {
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
        MICROS_FRAME_OWNER_PROCESS_USER,
        MICROS_FRAME_OWNER_VM_WIRED,
    };
    static const enum micros_frame_owner_kind invalid_kernel_kinds[] = {
        MICROS_FRAME_OWNER_FREE,
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
        MICROS_FRAME_OWNER_PROCESS_USER,
        MICROS_FRAME_OWNER_VM_WIRED,
        (enum micros_frame_owner_kind)MICROS_FRAME_OWNER_KIND_COUNT,
        (enum micros_frame_owner_kind)UINT8_MAX,
    };
    static const enum micros_frame_owner_kind invalid_process_kinds[] = {
        MICROS_FRAME_OWNER_FREE,
        MICROS_FRAME_OWNER_KERNEL_RETAINED,
        MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE,
        MICROS_FRAME_OWNER_KERNEL_TEMPORARY,
        MICROS_FRAME_OWNER_VM_TRANSFERABLE,
        (enum micros_frame_owner_kind)MICROS_FRAME_OWNER_KIND_COUNT,
        (enum micros_frame_owner_kind)UINT8_MAX,
    };
    static const struct micros_process_handle valid_processes[] = {
        {0, 1},
        {
            MICROS_PROCESS_CAPACITY - 1,
            MICROS_PROCESS_GENERATION_MAX,
        },
    };
    static const struct micros_process_handle invalid_processes[] = {
        {0, 0},
        {MICROS_PROCESS_CAPACITY, 1},
        {0, MICROS_PROCESS_GENERATION_MAX + UINT32_C(1)},
    };
    struct micros_frame_owner owner;
    struct micros_frame_owner owner_before;
    size_t index;
    size_t process_index;

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
            owners_equal(owner, raw_kernel_owner(kernel_kinds[index]))
        );
    }
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
                owners_equal(
                    owner,
                    raw_process_owner(
                        process_kinds[index],
                        valid_processes[process_index].slot,
                        valid_processes[process_index].generation
                    )
                )
            );
        }
    }

    for (
        index = 0;
        index
            < sizeof(invalid_kernel_kinds)
                / sizeof(invalid_kernel_kinds[0]);
        ++index
    ) {
        memset(&owner, 0xa5, sizeof(owner));
        owner_before = owner;
        EXPECT_ERROR(
            MICROS_FRAME_OWNERSHIP_ERROR_OWNER,
            micros_frame_owner_make_kernel(
                invalid_kernel_kinds[index],
                &owner
            )
        );
        EXPECT_TRUE(memcmp(&owner, &owner_before, sizeof(owner)) == 0);
    }
    for (
        index = 0;
        index
            < sizeof(invalid_process_kinds)
                / sizeof(invalid_process_kinds[0]);
        ++index
    ) {
        memset(&owner, 0xa5, sizeof(owner));
        owner_before = owner;
        EXPECT_ERROR(
            MICROS_FRAME_OWNERSHIP_ERROR_OWNER,
            micros_frame_owner_make_process(
                invalid_process_kinds[index],
                valid_processes[0],
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
            valid_processes[0],
            NULL
        )
    );
    return true;
}

static bool test_initializes_once_on_pristine_storage(void)
{
    const struct micros_physical_range range = {
        TEST_BASE,
        TEST_FRAME_COUNT * MICROS_FRAME_SIZE,
    };
    uint64_t allocated;

    memset(&allocator, 0, sizeof(allocator));
    memset(&ownership, 0, sizeof(ownership));
    EXPECT_TRUE(
        micros_frame_allocator_initialize(
            &allocator,
            &range,
            1,
            NULL,
            0
        ) == MICROS_FRAME_ALLOCATOR_OK
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_initialize(&ownership, &allocator)
    );
    EXPECT_TRUE(
        ownership.phase == MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
    );
    EXPECT_TRUE(ownership.allocator == &allocator);
    EXPECT_TRUE(
        ownership.allocator_identity == (uintptr_t)&allocator
    );
    EXPECT_TRUE(
        ownership.managed_frame_count
            == allocator.managed_frame_count
    );
    memcpy(
        &ownership_snapshot,
        &ownership,
        sizeof(ownership_snapshot)
    );
    memcpy(
        &allocator_snapshot,
        &allocator,
        sizeof(allocator_snapshot)
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_ALREADY_INITIALIZED,
        micros_frame_ownership_initialize(&ownership, &allocator)
    );
    EXPECT_TRUE(
        memcmp(
            &ownership,
            &ownership_snapshot,
            sizeof(ownership)
        ) == 0
    );
    EXPECT_TRUE(
        memcmp(
            &allocator,
            &allocator_snapshot,
            sizeof(allocator)
        ) == 0
    );

    memset(&ownership, 0, sizeof(ownership));
    ownership.owners[0].reserved = 1;
    memcpy(
        &ownership_snapshot,
        &ownership,
        sizeof(ownership_snapshot)
    );
    memcpy(
        &allocator_snapshot,
        &allocator,
        sizeof(allocator_snapshot)
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_STORAGE,
        micros_frame_ownership_initialize(&ownership, &allocator)
    );
    EXPECT_TRUE(
        memcmp(
            &ownership,
            &ownership_snapshot,
            sizeof(ownership)
        ) == 0
    );
    EXPECT_TRUE(
        memcmp(
            &allocator,
            &allocator_snapshot,
            sizeof(allocator)
        ) == 0
    );

    memset(&ownership, 0, sizeof(ownership));
    EXPECT_TRUE(
        micros_frame_allocator_allocate(&allocator, &allocated)
            == MICROS_FRAME_ALLOCATOR_OK
    );
    memcpy(
        &allocator_snapshot,
        &allocator,
        sizeof(allocator_snapshot)
    );
    memcpy(
        &ownership_snapshot,
        &ownership,
        sizeof(ownership_snapshot)
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_ALLOCATOR_STATE,
        micros_frame_ownership_initialize(&ownership, &allocator)
    );
    EXPECT_TRUE(
        memcmp(
            &allocator,
            &allocator_snapshot,
            sizeof(allocator)
        ) == 0
    );
    EXPECT_TRUE(
        memcmp(
            &ownership,
            &ownership_snapshot,
            sizeof(ownership)
        ) == 0
    );
    return true;
}

static bool test_rejects_invalid_raw_owner_records(void)
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

    EXPECT_TRUE(initialize_fixture(TEST_FRAME_COUNT));
    for (
        index = 0;
        index < sizeof(invalid_owners) / sizeof(invalid_owners[0]);
        ++index
    ) {
        unchanged = UINT64_C(0x123456789abcdef0);
        memcpy(
            &ownership_snapshot,
            &ownership,
            sizeof(ownership_snapshot)
        );
        memcpy(
            &allocator_snapshot,
            &allocator,
            sizeof(allocator_snapshot)
        );
        EXPECT_ERROR(
            MICROS_FRAME_OWNERSHIP_ERROR_OWNER,
            micros_frame_ownership_allocate(
                &ownership,
                invalid_owners[index],
                &unchanged
            )
        );
        EXPECT_TRUE(unchanged == UINT64_C(0x123456789abcdef0));
        EXPECT_TRUE(
            memcmp(
                &ownership,
                &ownership_snapshot,
                sizeof(ownership)
            ) == 0
        );
        EXPECT_TRUE(
            memcmp(
                &allocator,
                &allocator_snapshot,
                sizeof(allocator)
            ) == 0
        );
    }
    return true;
}

static bool test_allocates_looks_up_and_releases_exact_owners(void)
{
    const struct micros_frame_owner process_one = raw_process_owner(
        MICROS_FRAME_OWNER_PROCESS_USER,
        1,
        3
    );
    const struct micros_frame_owner process_two = raw_process_owner(
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
        2,
        5
    );
    const struct micros_frame_owner temporary = raw_kernel_owner(
        MICROS_FRAME_OWNER_KERNEL_TEMPORARY
    );
    struct micros_frame_owner observed = raw_kernel_owner(
        MICROS_FRAME_OWNER_KERNEL_RETAINED
    );
    uint64_t process_count = UINT64_MAX;
    uint64_t frames[3];
    uint64_t unchanged = UINT64_C(0xfeedfacefeedface);

    EXPECT_TRUE(initialize_fixture(TEST_FRAME_COUNT));
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &ownership,
            process_one,
            &frames[0]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &ownership,
            temporary,
            &frames[1]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &ownership,
            process_two,
            &frames[2]
        )
    );
    EXPECT_TRUE(frames[0] == TEST_BASE);
    EXPECT_TRUE(frames[1] == TEST_BASE + MICROS_FRAME_SIZE);
    EXPECT_TRUE(frames[2] == TEST_BASE + (2 * MICROS_FRAME_SIZE));

    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_lookup(
            &ownership,
            frames[0],
            &observed
        )
    );
    EXPECT_TRUE(owners_equal(observed, process_one));
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_count_process(
            &ownership,
            (struct micros_process_handle){1, 3},
            &process_count
        )
    );
    EXPECT_TRUE(process_count == 1);

    memcpy(
        &ownership_snapshot,
        &ownership,
        sizeof(ownership_snapshot)
    );
    memcpy(
        &allocator_snapshot,
        &allocator,
        sizeof(allocator_snapshot)
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER,
        micros_frame_ownership_release(
            &ownership,
            raw_process_owner(
                MICROS_FRAME_OWNER_PROCESS_USER,
                1,
                4
            ),
            frames[0]
        )
    );
    EXPECT_TRUE(
        memcmp(
            &ownership,
            &ownership_snapshot,
            sizeof(ownership)
        ) == 0
    );
    EXPECT_TRUE(
        memcmp(
            &allocator,
            &allocator_snapshot,
            sizeof(allocator)
        ) == 0
    );

    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_release(
            &ownership,
            temporary,
            frames[1]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &ownership,
            temporary,
            &unchanged
        )
    );
    EXPECT_TRUE(unchanged == frames[1]);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_UNALIGNED,
        micros_frame_ownership_release(
            &ownership,
            temporary,
            unchanged + 1
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_UNMANAGED,
        micros_frame_ownership_lookup(
            &ownership,
            UINT64_C(0x90000000),
            &observed
        )
    );
    EXPECT_TRUE(owners_equal(observed, process_one));
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_release(
            &ownership,
            process_one,
            frames[0]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_release(
            &ownership,
            temporary,
            unchanged
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_release(
            &ownership,
            process_two,
            frames[2]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED,
        micros_frame_ownership_release(
            &ownership,
            process_two,
            frames[2]
        )
    );
    EXPECT_TRUE(ownership.owned_frame_count == 0);
    EXPECT_TRUE(allocator.free_frame_count == TEST_FRAME_COUNT);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_validate(&ownership)
    );
    return true;
}

static bool test_rejects_geometry_changes_before_mutation(void)
{
    const struct micros_frame_owner temporary = raw_kernel_owner(
        MICROS_FRAME_OWNER_KERNEL_TEMPORARY
    );
    uint64_t frame;
    size_t original_range_count;
    uint64_t original_managed_count;

    EXPECT_TRUE(initialize_fixture(TEST_FRAME_COUNT));

    original_range_count = allocator.managed_range_count;
    allocator.managed_range_count = 0;
    EXPECT_TRUE(expect_geometry_allocate_failure(temporary));
    allocator.managed_range_count = original_range_count;

    allocator.managed_range_count =
        MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES + 1;
    EXPECT_TRUE(expect_geometry_allocate_failure(temporary));
    allocator.managed_range_count = original_range_count;

    original_managed_count = allocator.managed_frame_count;
    --allocator.managed_frame_count;
    EXPECT_TRUE(expect_geometry_allocate_failure(temporary));
    allocator.managed_frame_count = original_managed_count;

    ++allocator.managed_ranges[0].base;
    EXPECT_TRUE(expect_geometry_allocate_failure(temporary));
    --allocator.managed_ranges[0].base;
    ++allocator.managed_ranges[0].frame_count;
    EXPECT_TRUE(expect_geometry_allocate_failure(temporary));
    --allocator.managed_ranges[0].frame_count;
    ++allocator.managed_ranges[0].bitmap_offset;
    EXPECT_TRUE(expect_geometry_allocate_failure(temporary));
    --allocator.managed_ranges[0].bitmap_offset;

    alternate_allocator = allocator;
    ownership.allocator = &alternate_allocator;
    EXPECT_TRUE(expect_geometry_allocate_failure(temporary));
    EXPECT_TRUE(
        memcmp(
            &alternate_allocator,
            &allocator,
            sizeof(allocator)
        ) == 0
    );
    ownership.allocator = &allocator;

    allocator.managed_ranges[1].base = UINT64_C(0xdead0000);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&ownership)
    );
    allocator.managed_ranges[1].base = 0;

    allocator.allocated_bitmap[0] |= UINT64_C(1) << TEST_FRAME_COUNT;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&ownership)
    );
    allocator.allocated_bitmap[0] &=
        ~(UINT64_C(1) << TEST_FRAME_COUNT);

    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &ownership,
            temporary,
            &frame
        )
    );
    allocator.managed_range_count = 0;
    EXPECT_TRUE(expect_geometry_release_failure(temporary, frame));
    allocator.managed_range_count = original_range_count;
    --allocator.managed_frame_count;
    EXPECT_TRUE(expect_geometry_release_failure(temporary, frame));
    allocator.managed_frame_count = original_managed_count;

    ++allocator.managed_ranges[0].base;
    EXPECT_TRUE(expect_geometry_release_failure(temporary, frame));
    --allocator.managed_ranges[0].base;
    ++allocator.managed_ranges[0].frame_count;
    EXPECT_TRUE(expect_geometry_release_failure(temporary, frame));
    --allocator.managed_ranges[0].frame_count;
    ++allocator.managed_ranges[0].bitmap_offset;
    EXPECT_TRUE(expect_geometry_release_failure(temporary, frame));
    --allocator.managed_ranges[0].bitmap_offset;

    alternate_allocator = allocator;
    ownership.allocator = &alternate_allocator;
    EXPECT_TRUE(expect_geometry_release_failure(temporary, frame));
    EXPECT_TRUE(
        memcmp(
            &alternate_allocator,
            &allocator,
            sizeof(allocator)
        ) == 0
    );
    ownership.allocator = &allocator;

    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_release(
            &ownership,
            temporary,
            frame
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_validate(&ownership)
    );
    return true;
}

static bool test_rolls_back_defensive_post_allocation_failure(void)
{
    const struct micros_frame_owner temporary = raw_kernel_owner(
        MICROS_FRAME_OWNER_KERNEL_TEMPORARY
    );
    uint64_t unchanged = UINT64_C(0x8899aabbccddeeff);

    EXPECT_TRUE(initialize_fixture(TEST_FRAME_COUNT));
    ownership.owners[0] = temporary;
    memcpy(
        &ownership_snapshot,
        &ownership,
        sizeof(ownership_snapshot)
    );
    memcpy(
        &allocator_snapshot,
        &allocator,
        sizeof(allocator_snapshot)
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_allocate(
            &ownership,
            temporary,
            &unchanged
        )
    );
    EXPECT_TRUE(unchanged == UINT64_C(0x8899aabbccddeeff));
    EXPECT_TRUE(
        memcmp(
            &ownership,
            &ownership_snapshot,
            sizeof(ownership)
        ) == 0
    );
    EXPECT_TRUE(
        memcmp(
            &allocator,
            &allocator_snapshot,
            sizeof(allocator)
        ) == 0
    );
    return true;
}

static bool expect_release_set_failure(
    struct micros_process_handle process,
    size_t word_count,
    enum micros_frame_ownership_error expected_error
)
{
    memcpy(
        &ownership_snapshot,
        &ownership,
        sizeof(ownership_snapshot)
    );
    memcpy(
        &allocator_snapshot,
        &allocator,
        sizeof(allocator_snapshot)
    );
    EXPECT_ERROR(
        expected_error,
        micros_frame_ownership_release_process_set(
            &ownership,
            process,
            release_bitmap,
            word_count
        )
    );
    EXPECT_TRUE(
        memcmp(
            &ownership,
            &ownership_snapshot,
            sizeof(ownership)
        ) == 0
    );
    EXPECT_TRUE(
        memcmp(
            &allocator,
            &allocator_snapshot,
            sizeof(allocator)
        ) == 0
    );
    return true;
}

static bool expect_handoff_plan_failure(
    uint64_t physical_address,
    struct micros_frame_owner expected_owner,
    enum micros_frame_handoff_target target,
    enum micros_frame_ownership_error expected_error
)
{
    memcpy(
        &ownership_snapshot,
        &ownership,
        sizeof(ownership_snapshot)
    );
    memcpy(
        &allocator_snapshot,
        &allocator,
        sizeof(allocator_snapshot)
    );
    EXPECT_ERROR(
        expected_error,
        micros_frame_ownership_prepare_handoff(
            &ownership,
            physical_address,
            expected_owner,
            target
        )
    );
    EXPECT_TRUE(
        memcmp(
            &ownership,
            &ownership_snapshot,
            sizeof(ownership)
        ) == 0
    );
    EXPECT_TRUE(
        memcmp(
            &allocator,
            &allocator_snapshot,
            sizeof(allocator)
        ) == 0
    );
    return true;
}

static bool expect_handoff_completion_failure(
    enum micros_frame_ownership_error expected_error
)
{
    memcpy(
        &ownership_snapshot,
        &ownership,
        sizeof(ownership_snapshot)
    );
    memcpy(
        &allocator_snapshot,
        &allocator,
        sizeof(allocator_snapshot)
    );
    EXPECT_ERROR(
        expected_error,
        micros_frame_ownership_complete_handoff(&ownership)
    );
    EXPECT_TRUE(
        memcmp(
            &ownership,
            &ownership_snapshot,
            sizeof(ownership)
        ) == 0
    );
    EXPECT_TRUE(
        memcmp(
            &allocator,
            &allocator_snapshot,
            sizeof(allocator)
        ) == 0
    );
    return true;
}

static bool expect_wired_set_failure(
    size_t word_count,
    enum micros_frame_ownership_error expected_error
)
{
    memcpy(
        &ownership_snapshot,
        &ownership,
        sizeof(ownership_snapshot)
    );
    memcpy(
        &allocator_snapshot,
        &allocator,
        sizeof(allocator_snapshot)
    );
    EXPECT_ERROR(
        expected_error,
        micros_frame_ownership_prepare_wired_process_user_set(
            &ownership,
            release_bitmap,
            word_count
        )
    );
    EXPECT_TRUE(
        memcmp(
            &ownership,
            &ownership_snapshot,
            sizeof(ownership)
        ) == 0
    );
    EXPECT_TRUE(
        memcmp(
            &allocator,
            &allocator_snapshot,
            sizeof(allocator)
        ) == 0
    );
    return true;
}

static bool test_atomically_releases_process_owned_sets(void)
{
    const struct micros_process_handle process_one = {1, 2};
    const struct micros_process_handle process_two = {2, 3};
    const struct micros_frame_owner process_table = raw_process_owner(
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
        process_one.slot,
        process_one.generation
    );
    const struct micros_frame_owner process_user = raw_process_owner(
        MICROS_FRAME_OWNER_PROCESS_USER,
        process_one.slot,
        process_one.generation
    );
    const struct micros_frame_owner foreign_user = raw_process_owner(
        MICROS_FRAME_OWNER_PROCESS_USER,
        process_two.slot,
        process_two.generation
    );
    const struct micros_frame_owner temporary = raw_kernel_owner(
        MICROS_FRAME_OWNER_KERNEL_TEMPORARY
    );
    struct micros_frame_owner observed;
    uint64_t process_count = UINT64_MAX;
    uint64_t frames[4];

    EXPECT_TRUE(initialize_fixture(TEST_FRAME_COUNT));
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &ownership,
            process_table,
            &frames[0]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &ownership,
            process_user,
            &frames[1]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &ownership,
            foreign_user,
            &frames[2]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &ownership,
            temporary,
            &frames[3]
        )
    );

    clear_release_bitmap();
    memcpy(
        &ownership_snapshot,
        &ownership,
        sizeof(ownership_snapshot)
    );
    memcpy(
        &allocator_snapshot,
        &allocator,
        sizeof(allocator_snapshot)
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_release_process_set(
            &ownership,
            process_one,
            release_bitmap,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        )
    );
    EXPECT_TRUE(
        memcmp(
            &ownership,
            &ownership_snapshot,
            sizeof(ownership)
        ) == 0
    );
    EXPECT_TRUE(
        memcmp(
            &allocator,
            &allocator_snapshot,
            sizeof(allocator)
        ) == 0
    );

    EXPECT_TRUE(
        expect_release_set_failure(
            process_one,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS - 1,
            MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT
        )
    );
    clear_release_bitmap();
    select_release_index(TEST_FRAME_COUNT);
    EXPECT_TRUE(
        expect_release_set_failure(
            process_one,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_UNMANAGED
        )
    );
    clear_release_bitmap();
    select_release_index(5);
    EXPECT_TRUE(
        expect_release_set_failure(
            process_one,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED
        )
    );
    clear_release_bitmap();
    select_release_index(2);
    EXPECT_TRUE(
        expect_release_set_failure(
            process_one,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER
        )
    );
    clear_release_bitmap();
    select_release_index(0);
    EXPECT_TRUE(
        expect_release_set_failure(
            (struct micros_process_handle){
                process_one.slot,
                process_one.generation + 1,
            },
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER
        )
    );
    clear_release_bitmap();
    select_release_index(3);
    EXPECT_TRUE(
        expect_release_set_failure(
            process_one,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER
        )
    );
    clear_release_bitmap();
    select_release_index(0);
    EXPECT_TRUE(
        expect_release_set_failure(
            (struct micros_process_handle){0, 0},
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_OWNER
        )
    );

    allocator.allocated_bitmap[0] &= ~UINT64_C(1);
    EXPECT_TRUE(
        expect_release_set_failure(
            process_one,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT
        )
    );
    allocator.allocated_bitmap[0] |= UINT64_C(1);
    ++ownership.owned_frame_count;
    EXPECT_TRUE(
        expect_release_set_failure(
            process_one,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT
        )
    );
    --ownership.owned_frame_count;
    ++ownership.owner_counts[
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
    ];
    EXPECT_TRUE(
        expect_release_set_failure(
            process_one,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT
        )
    );
    --ownership.owner_counts[
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
    ];
    ++allocator.free_frame_count;
    EXPECT_TRUE(
        expect_release_set_failure(
            process_one,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT
        )
    );
    --allocator.free_frame_count;

    clear_release_bitmap();
    select_release_index(0);
    select_release_index(1);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_release_process_set(
            &ownership,
            process_one,
            release_bitmap,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_lookup(
            &ownership,
            frames[0],
            &observed
        )
    );
    EXPECT_TRUE(
        owners_equal(
            observed,
            raw_kernel_owner(MICROS_FRAME_OWNER_FREE)
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_lookup(
            &ownership,
            frames[1],
            &observed
        )
    );
    EXPECT_TRUE(
        owners_equal(
            observed,
            raw_kernel_owner(MICROS_FRAME_OWNER_FREE)
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_count_process(
            &ownership,
            process_one,
            &process_count
        )
    );
    EXPECT_TRUE(process_count == 0);
    EXPECT_TRUE(ownership.owned_frame_count == 2);
    EXPECT_TRUE(allocator.free_frame_count == TEST_FRAME_COUNT - 2);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_validate(&ownership)
    );

    EXPECT_TRUE(initialize_fixture(TEST_FRAME_COUNT));
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &ownership,
            process_table,
            &frames[0]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_complete_handoff(&ownership)
    );
    clear_release_bitmap();
    select_release_index(0);
    EXPECT_TRUE(
        expect_release_set_failure(
            process_one,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_PHASE
        )
    );
    EXPECT_TRUE(
        expect_release_set_failure(
            process_one,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS - 1,
            MICROS_FRAME_OWNERSHIP_ERROR_PHASE
        )
    );
    return true;
}

static bool test_stages_and_atomically_commits_handoff(void)
{
    const struct micros_frame_owner process_table = raw_process_owner(
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
        1,
        2
    );
    const struct micros_frame_owner process_user_one = raw_process_owner(
        MICROS_FRAME_OWNER_PROCESS_USER,
        1,
        2
    );
    const struct micros_frame_owner process_user_two = raw_process_owner(
        MICROS_FRAME_OWNER_PROCESS_USER,
        2,
        1
    );
    const struct micros_frame_owner retained = raw_kernel_owner(
        MICROS_FRAME_OWNER_KERNEL_RETAINED
    );
    const struct micros_frame_owner temporary = raw_kernel_owner(
        MICROS_FRAME_OWNER_KERNEL_TEMPORARY
    );
    uint64_t frames[5];
    uint64_t unchanged = UINT64_C(0xa5a5a5a5a5a5a5a5);
    struct micros_frame_owner observed;
    uint64_t count = UINT64_MAX;
    uint64_t expected_counts[MICROS_FRAME_OWNER_KIND_COUNT] = {0};
    size_t index;

    EXPECT_TRUE(initialize_fixture(TEST_FRAME_COUNT));
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &ownership,
            process_table,
            &frames[0]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &ownership,
            process_user_one,
            &frames[1]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &ownership,
            process_user_two,
            &frames[2]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &ownership,
            retained,
            &frames[3]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &ownership,
            temporary,
            &frames[4]
        )
    );

    EXPECT_TRUE(
        expect_handoff_plan_failure(
            frames[0],
            process_table,
            MICROS_FRAME_HANDOFF_VM_WIRED,
            MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION
        )
    );
    EXPECT_TRUE(
        expect_handoff_plan_failure(
            frames[0],
            process_table,
            MICROS_FRAME_HANDOFF_VM_TRANSFERABLE,
            MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION
        )
    );
    EXPECT_TRUE(
        expect_handoff_plan_failure(
            frames[4],
            temporary,
            MICROS_FRAME_HANDOFF_VM_WIRED,
            MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION
        )
    );
    EXPECT_TRUE(
        expect_handoff_plan_failure(
            frames[4],
            temporary,
            MICROS_FRAME_HANDOFF_VM_TRANSFERABLE,
            MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION
        )
    );
    EXPECT_TRUE(
        expect_handoff_plan_failure(
            frames[3],
            retained,
            MICROS_FRAME_HANDOFF_VM_WIRED,
            MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION
        )
    );
    EXPECT_TRUE(
        expect_handoff_plan_failure(
            frames[1],
            process_user_two,
            MICROS_FRAME_HANDOFF_VM_WIRED,
            MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER
        )
    );
    EXPECT_TRUE(
        expect_handoff_plan_failure(
            TEST_BASE + (5 * MICROS_FRAME_SIZE),
            process_user_one,
            MICROS_FRAME_HANDOFF_VM_WIRED,
            MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED
        )
    );
    EXPECT_TRUE(
        expect_handoff_plan_failure(
            frames[1],
            process_user_one,
            (enum micros_frame_handoff_target)
                MICROS_FRAME_HANDOFF_TARGET_COUNT,
            MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION
        )
    );

    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_prepare_handoff(
            &ownership,
            frames[1],
            process_user_one,
            MICROS_FRAME_HANDOFF_VM_WIRED
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_prepare_handoff(
            &ownership,
            frames[3],
            retained,
            MICROS_FRAME_HANDOFF_VM_TRANSFERABLE
        )
    );
    EXPECT_TRUE(
        expect_handoff_completion_failure(
            MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION
        )
    );

    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_release(
            &ownership,
            temporary,
            frames[4]
        )
    );
    EXPECT_TRUE(
        expect_handoff_completion_failure(
            MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_prepare_handoff(
            &ownership,
            frames[2],
            process_user_two,
            MICROS_FRAME_HANDOFF_VM_TRANSFERABLE
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_prepare_handoff(
            &ownership,
            frames[1],
            process_user_one,
            MICROS_FRAME_HANDOFF_NONE
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_prepare_handoff(
            &ownership,
            frames[1],
            process_user_one,
            MICROS_FRAME_HANDOFF_VM_WIRED
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_complete_handoff(&ownership)
    );
    EXPECT_TRUE(
        ownership.phase == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_lookup(
            &ownership,
            frames[0],
            &observed
        )
    );
    EXPECT_TRUE(owners_equal(observed, process_table));
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_lookup(
            &ownership,
            frames[1],
            &observed
        )
    );
    EXPECT_TRUE(
        owners_equal(
            observed,
            raw_process_owner(MICROS_FRAME_OWNER_VM_WIRED, 1, 2)
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_lookup(
            &ownership,
            frames[2],
            &observed
        )
    );
    EXPECT_TRUE(
        owners_equal(
            observed,
            raw_kernel_owner(MICROS_FRAME_OWNER_VM_TRANSFERABLE)
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_lookup(
            &ownership,
            frames[3],
            &observed
        )
    );
    EXPECT_TRUE(
        owners_equal(
            observed,
            raw_kernel_owner(MICROS_FRAME_OWNER_VM_TRANSFERABLE)
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_lookup(
            &ownership,
            frames[4],
            &observed
        )
    );
    EXPECT_TRUE(
        owners_equal(
            observed,
            raw_kernel_owner(MICROS_FRAME_OWNER_FREE)
        )
    );
    for (index = 0; index < TEST_FRAME_COUNT; ++index) {
        EXPECT_TRUE(
            ownership.handoff_targets[index]
                == MICROS_FRAME_HANDOFF_NONE
        );
    }
    expected_counts[MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE] = 1;
    expected_counts[MICROS_FRAME_OWNER_VM_WIRED] = 1;
    expected_counts[MICROS_FRAME_OWNER_VM_TRANSFERABLE] = 2;
    EXPECT_TRUE(ownership.owned_frame_count == 4);
    EXPECT_TRUE(allocator.free_frame_count == TEST_FRAME_COUNT - 4);
    for (
        index = 0;
        index < MICROS_FRAME_OWNER_KIND_COUNT;
        ++index
    ) {
        EXPECT_TRUE(
            ownership.owner_counts[index] == expected_counts[index]
        );
    }
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_count_process(
            &ownership,
            (struct micros_process_handle){1, 2},
            &count
        )
    );
    EXPECT_TRUE(count == 2);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_count_process(
            &ownership,
            (struct micros_process_handle){2, 1},
            &count
        )
    );
    EXPECT_TRUE(count == 0);

    memcpy(
        &ownership_snapshot,
        &ownership,
        sizeof(ownership_snapshot)
    );
    memcpy(
        &allocator_snapshot,
        &allocator,
        sizeof(allocator_snapshot)
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_PHASE,
        micros_frame_ownership_allocate(
            &ownership,
            temporary,
            &unchanged
        )
    );
    EXPECT_TRUE(unchanged == UINT64_C(0xa5a5a5a5a5a5a5a5));
    EXPECT_TRUE(
        memcmp(
            &ownership,
            &ownership_snapshot,
            sizeof(ownership)
        ) == 0
    );
    EXPECT_TRUE(
        memcmp(
            &allocator,
            &allocator_snapshot,
            sizeof(allocator)
        ) == 0
    );
    memcpy(
        &ownership_snapshot,
        &ownership,
        sizeof(ownership_snapshot)
    );
    memcpy(
        &allocator_snapshot,
        &allocator,
        sizeof(allocator_snapshot)
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_PHASE,
        micros_frame_ownership_release(
            &ownership,
            process_table,
            frames[0]
        )
    );
    EXPECT_TRUE(
        memcmp(
            &ownership,
            &ownership_snapshot,
            sizeof(ownership)
        ) == 0
    );
    EXPECT_TRUE(
        memcmp(
            &allocator,
            &allocator_snapshot,
            sizeof(allocator)
        ) == 0
    );
    EXPECT_TRUE(
        expect_handoff_plan_failure(
            frames[0],
            process_table,
            MICROS_FRAME_HANDOFF_NONE,
            MICROS_FRAME_OWNERSHIP_ERROR_PHASE
        )
    );
    EXPECT_TRUE(
        expect_handoff_completion_failure(
            MICROS_FRAME_OWNERSHIP_ERROR_PHASE
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_validate(&ownership)
    );
    return true;
}

static bool test_atomically_stages_complete_wired_set(void)
{
    const struct micros_frame_owner process_user_one =
        raw_process_owner(MICROS_FRAME_OWNER_PROCESS_USER, 1, 2);
    const struct micros_frame_owner process_user_two =
        raw_process_owner(MICROS_FRAME_OWNER_PROCESS_USER, 2, 3);
    const struct micros_frame_owner process_table =
        raw_process_owner(MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE, 1, 2);
    const struct micros_frame_owner retained =
        raw_kernel_owner(MICROS_FRAME_OWNER_KERNEL_RETAINED);
    uint64_t frames[4];

    EXPECT_TRUE(initialize_fixture(TEST_FRAME_COUNT));
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &ownership,
            process_user_one,
            &frames[0]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &ownership,
            process_user_two,
            &frames[1]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &ownership,
            process_table,
            &frames[2]
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &ownership,
            retained,
            &frames[3]
        )
    );

    memcpy(
        &ownership_snapshot,
        &ownership,
        sizeof(ownership_snapshot)
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT,
        micros_frame_ownership_prepare_wired_process_user_set(
            &ownership,
            NULL,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        )
    );
    EXPECT_TRUE(
        memcmp(
            &ownership,
            &ownership_snapshot,
            sizeof(ownership)
        ) == 0
    );
    clear_release_bitmap();
    select_release_index(0);
    EXPECT_TRUE(
        expect_wired_set_failure(
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER
        )
    );
    select_release_index(1);
    select_release_index(2);
    EXPECT_TRUE(
        expect_wired_set_failure(
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER
        )
    );
    clear_release_bitmap();
    select_release_index(0);
    select_release_index(1);
    select_release_index(4);
    EXPECT_TRUE(
        expect_wired_set_failure(
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED
        )
    );
    clear_release_bitmap();
    select_release_index(0);
    select_release_index(1);
    select_release_index(TEST_FRAME_COUNT);
    EXPECT_TRUE(
        expect_wired_set_failure(
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_UNMANAGED
        )
    );
    clear_release_bitmap();
    select_release_index(0);
    select_release_index(1);
    EXPECT_TRUE(
        expect_wired_set_failure(
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS - 1,
            MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_prepare_handoff(
            &ownership,
            frames[0],
            process_user_one,
            MICROS_FRAME_HANDOFF_VM_TRANSFERABLE
        )
    );
    EXPECT_TRUE(
        expect_wired_set_failure(
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS,
            MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_prepare_handoff(
            &ownership,
            frames[0],
            process_user_one,
            MICROS_FRAME_HANDOFF_NONE
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_prepare_wired_process_user_set(
            &ownership,
            release_bitmap,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        )
    );
    EXPECT_TRUE(
        ownership.handoff_targets[0]
            == MICROS_FRAME_HANDOFF_VM_WIRED
        && ownership.handoff_targets[1]
            == MICROS_FRAME_HANDOFF_VM_WIRED
        && ownership.handoff_targets[2]
            == MICROS_FRAME_HANDOFF_NONE
        && ownership.handoff_targets[3]
            == MICROS_FRAME_HANDOFF_NONE
        && micros_frame_ownership_validate(&ownership)
            == MICROS_FRAME_OWNERSHIP_OK
    );
    memcpy(
        &ownership_snapshot,
        &ownership,
        sizeof(ownership_snapshot)
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION,
        micros_frame_ownership_prepare_wired_process_user_set(
            &ownership,
            release_bitmap,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        )
    );
    EXPECT_TRUE(
        memcmp(
            &ownership,
            &ownership_snapshot,
            sizeof(ownership)
        ) == 0
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_complete_handoff(&ownership)
    );
    memcpy(
        &ownership_snapshot,
        &ownership,
        sizeof(ownership_snapshot)
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_PHASE,
        micros_frame_ownership_prepare_wired_process_user_set(
            &ownership,
            release_bitmap,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        )
    );
    EXPECT_TRUE(
        memcmp(
            &ownership,
            &ownership_snapshot,
            sizeof(ownership)
        ) == 0
    );
    return true;
}

static bool test_validator_rejects_independent_corruption(void)
{
    const struct micros_frame_owner process_user = raw_process_owner(
        MICROS_FRAME_OWNER_PROCESS_USER,
        3,
        4
    );
    uint64_t frame;
    uint64_t frame_index;

    EXPECT_TRUE(initialize_fixture(TEST_FRAME_COUNT));
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &ownership,
            process_user,
            &frame
        )
    );
    frame_index = (frame - TEST_BASE) / MICROS_FRAME_SIZE;

    ++ownership.owned_frame_count;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&ownership)
    );
    --ownership.owned_frame_count;
    ++ownership.owner_counts[MICROS_FRAME_OWNER_PROCESS_USER];
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&ownership)
    );
    --ownership.owner_counts[MICROS_FRAME_OWNER_PROCESS_USER];

    ownership.owners[frame_index].reserved = 1;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&ownership)
    );
    ownership.owners[frame_index].reserved = 0;
    ownership.owners[frame_index].slot = MICROS_PROCESS_CAPACITY;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&ownership)
    );
    ownership.owners[frame_index].slot = process_user.slot;
    ownership.owners[frame_index].generation = 0;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&ownership)
    );
    ownership.owners[frame_index].generation =
        MICROS_PROCESS_GENERATION_MAX + UINT32_C(1);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&ownership)
    );
    ownership.owners[frame_index].generation = process_user.generation;
    ownership.owners[frame_index].kind = UINT8_MAX;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&ownership)
    );
    ownership.owners[frame_index] = process_user;

    ownership.handoff_targets[frame_index] = UINT8_MAX;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&ownership)
    );
    ownership.handoff_targets[frame_index] = MICROS_FRAME_HANDOFF_NONE;

    allocator.allocated_bitmap[0] &= ~UINT64_C(1);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&ownership)
    );
    allocator.allocated_bitmap[0] |= UINT64_C(1);

    ownership.owners[TEST_FRAME_COUNT] = process_user;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&ownership)
    );
    memset(
        &ownership.owners[TEST_FRAME_COUNT],
        0,
        sizeof(ownership.owners[TEST_FRAME_COUNT])
    );
    ownership.handoff_targets[TEST_FRAME_COUNT] =
        MICROS_FRAME_HANDOFF_VM_WIRED;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&ownership)
    );
    ownership.handoff_targets[TEST_FRAME_COUNT] =
        MICROS_FRAME_HANDOFF_NONE;

    ownership.phase = (enum micros_frame_ownership_phase)99;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&ownership)
    );
    ownership.phase = MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP;

    ++ownership.allocator_identity;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
        micros_frame_ownership_validate(&ownership)
    );
    --ownership.allocator_identity;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_validate(&ownership)
    );
    return true;
}

static bool test_operates_at_maximum_managed_capacity(void)
{
    const struct micros_frame_owner temporary = raw_kernel_owner(
        MICROS_FRAME_OWNER_KERNEL_TEMPORARY
    );
    const struct micros_process_handle boundary_process = {1, 1};
    const struct micros_frame_owner boundary_owner = raw_process_owner(
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
        initialize_fixture(frame_count)
    );
    EXPECT_TRUE(
        ownership.managed_frame_count == frame_count
    );
    for (index = 0; index < (size_t)(frame_count - 1); ++index) {
        allocator.allocated_bitmap[index / 64]
            |= UINT64_C(1) << (index % 64);
        ownership.owners[index] = temporary;
    }
    allocator.free_frame_count = 1;
    ownership.owned_frame_count = frame_count - 1;
    ownership.owner_counts[MICROS_FRAME_OWNER_KERNEL_TEMPORARY] =
        frame_count - 1;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_validate(&ownership)
    );
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &ownership,
            boundary_owner,
            &output
        )
    );
    EXPECT_TRUE(output == last_address);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_lookup(
            &ownership,
            last_address,
            &observed
        )
    );
    EXPECT_TRUE(owners_equal(observed, boundary_owner));

    output = UINT64_C(0xfeedfacefeedface);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_ERROR_EXHAUSTED,
        micros_frame_ownership_allocate(
            &ownership,
            temporary,
            &output
        )
    );
    EXPECT_TRUE(output == UINT64_C(0xfeedfacefeedface));
    clear_release_bitmap();
    select_release_index(frame_count - 1);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_release_process_set(
            &ownership,
            boundary_process,
            release_bitmap,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        )
    );
    output = 0;
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_allocate(
            &ownership,
            boundary_owner,
            &output
        )
    );
    EXPECT_TRUE(output == last_address);
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_validate(&ownership)
    );
    return true;
}

static uint32_t next_random(uint32_t *state)
{
    *state = (*state * UINT32_C(1664525)) + UINT32_C(1013904223);
    return *state;
}

static struct micros_frame_owner model_owner_from_selector(
    uint32_t selector
)
{
    switch (selector % 4) {
    case 0:
        return raw_kernel_owner(MICROS_FRAME_OWNER_KERNEL_TEMPORARY);
    case 1:
        return raw_kernel_owner(MICROS_FRAME_OWNER_KERNEL_RETAINED);
    case 2:
        return raw_process_owner(
            MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
            1,
            2
        );
    default:
        return raw_process_owner(
            MICROS_FRAME_OWNER_PROCESS_USER,
            2,
            3
        );
    }
}

static bool model_owner_is_free(struct micros_frame_owner owner)
{
    return owner.kind == MICROS_FRAME_OWNER_FREE;
}

static struct micros_frame_owner model_different_valid_owner(
    struct micros_frame_owner owner
)
{
    if (
        owner.kind == MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
        || owner.kind == MICROS_FRAME_OWNER_PROCESS_USER
        || owner.kind == MICROS_FRAME_OWNER_VM_WIRED
    ) {
        owner.generation = owner.generation
                == MICROS_PROCESS_GENERATION_MAX
            ? owner.generation - UINT32_C(1)
            : owner.generation + UINT32_C(1);
        return owner;
    }
    if (owner.kind == MICROS_FRAME_OWNER_KERNEL_RETAINED) {
        return raw_kernel_owner(MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE);
    }
    return raw_kernel_owner(MICROS_FRAME_OWNER_KERNEL_RETAINED);
}

static bool model_plan_is_valid(
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

static enum micros_frame_ownership_error
model_prepare_wired_set_error(
    const struct micros_frame_owner *model_owners,
    const uint8_t *model_targets,
    enum micros_frame_ownership_phase model_phase,
    size_t frame_count,
    const uint64_t *selected_bitmap
)
{
    size_t index;

    if (model_phase == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF) {
        return MICROS_FRAME_OWNERSHIP_ERROR_PHASE;
    }
    for (
        index = 0;
        index < MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES;
        ++index
    ) {
        bool selected = (
            selected_bitmap[index / 64]
            & (UINT64_C(1) << (index % 64))
        ) != 0;

        if (index >= frame_count) {
            if (selected) {
                return MICROS_FRAME_OWNERSHIP_ERROR_UNMANAGED;
            }
            continue;
        }
        if (
            model_owners[index].kind
                == MICROS_FRAME_OWNER_PROCESS_USER
        ) {
            if (!selected) {
                return MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
            }
            if (
                model_targets[index]
                    != MICROS_FRAME_HANDOFF_NONE
            ) {
                return MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION;
            }
        } else if (selected) {
            return model_owner_is_free(model_owners[index])
                ? MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED
                : MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
        }
    }
    return MICROS_FRAME_OWNERSHIP_OK;
}

static void model_apply_wired_set(
    const struct micros_frame_owner *model_owners,
    uint8_t *model_targets,
    size_t frame_count
)
{
    size_t index;

    for (index = 0; index < frame_count; ++index) {
        if (
            model_owners[index].kind
                == MICROS_FRAME_OWNER_PROCESS_USER
        ) {
            model_targets[index] =
                MICROS_FRAME_HANDOFF_VM_WIRED;
        }
    }
}

static bool model_handoff_is_ready(
    const struct micros_frame_owner *model_owners,
    const uint8_t *model_targets,
    size_t frame_count
)
{
    size_t index;

    for (index = 0; index < frame_count; ++index) {
        if (
            model_owners[index].kind
                == MICROS_FRAME_OWNER_KERNEL_TEMPORARY
            || (
                model_owners[index].kind
                    == MICROS_FRAME_OWNER_PROCESS_USER
                && model_targets[index]
                    == MICROS_FRAME_HANDOFF_NONE
            )
            || !model_plan_is_valid(
                model_owners[index],
                model_targets[index]
            )
        ) {
            return false;
        }
    }
    return true;
}

static void model_apply_handoff(
    struct micros_frame_owner *model_owners,
    uint8_t *model_targets,
    size_t frame_count
)
{
    size_t index;

    for (index = 0; index < frame_count; ++index) {
        if (
            model_targets[index]
                == MICROS_FRAME_HANDOFF_VM_WIRED
        ) {
            model_owners[index].kind = MICROS_FRAME_OWNER_VM_WIRED;
        } else if (
            model_targets[index]
                == MICROS_FRAME_HANDOFF_VM_TRANSFERABLE
        ) {
            model_owners[index] = raw_kernel_owner(
                MICROS_FRAME_OWNER_VM_TRANSFERABLE
            );
        }
        model_targets[index] = MICROS_FRAME_HANDOFF_NONE;
    }
}

static bool compare_model(
    const struct micros_frame_owner *model_owners,
    const uint8_t *model_targets,
    enum micros_frame_ownership_phase model_phase,
    size_t frame_count,
    size_t step,
    uint32_t seed
)
{
    uint64_t counts[MICROS_FRAME_OWNER_KIND_COUNT] = {0};
    uint64_t owned = 0;
    size_t index;

    if (ownership.phase != model_phase) {
        fprintf(
            stderr,
            "seed=0x%08x step=%zu phase mismatch\n",
            seed,
            step
        );
        return false;
    }
    for (index = 0; index < frame_count; ++index) {
        bool allocated =
            (
                allocator.allocated_bitmap[index / 64]
                & (UINT64_C(1) << (index % 64))
            ) != 0;

        if (
            !owners_equal(model_owners[index], ownership.owners[index])
            || model_targets[index] != ownership.handoff_targets[index]
            || allocated == model_owner_is_free(model_owners[index])
            || (
                model_phase
                    == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
                && (
                    model_targets[index]
                        != MICROS_FRAME_HANDOFF_NONE
                    || model_owners[index].kind
                        == MICROS_FRAME_OWNER_KERNEL_TEMPORARY
                    || model_owners[index].kind
                        == MICROS_FRAME_OWNER_PROCESS_USER
                )
            )
        ) {
            fprintf(
                stderr,
                "seed=0x%08x step=%zu frame=%zu model mismatch\n",
                seed,
                step,
                index
            );
            return false;
        }
        ++counts[model_owners[index].kind];
        if (!model_owner_is_free(model_owners[index])) {
            ++owned;
        }
    }
    if (
        ownership.owned_frame_count != owned
        || allocator.free_frame_count != frame_count - owned
    ) {
        fprintf(
            stderr,
            "seed=0x%08x step=%zu total mismatch\n",
            seed,
            step
        );
        return false;
    }
    for (index = 0; index < MICROS_FRAME_OWNER_KIND_COUNT; ++index) {
        uint64_t expected = index == MICROS_FRAME_OWNER_FREE
            ? 0
            : counts[index];

        if (ownership.owner_counts[index] != expected) {
            fprintf(
                stderr,
                "seed=0x%08x step=%zu kind=%zu count mismatch\n",
                seed,
                step,
                index
            );
            return false;
        }
    }
    if (
        (
            step % MODEL_FULL_VALIDATION_INTERVAL == 0
            || step + 1 == MODEL_STEPS
        )
        && micros_frame_ownership_validate(&ownership)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        fprintf(
            stderr,
            "seed=0x%08x step=%zu validator mismatch\n",
            seed,
            step
        );
        return false;
    }
    return true;
}

static bool transition_model_to_handed_off(
    struct micros_frame_owner *model_owners,
    uint8_t *model_targets,
    enum micros_frame_ownership_phase *model_phase,
    size_t frame_count,
    size_t step,
    uint32_t seed
)
{
    const struct micros_frame_owner temporary = raw_kernel_owner(
        MICROS_FRAME_OWNER_KERNEL_TEMPORARY
    );
    size_t index;

    if (
        model_owners == NULL
        || model_targets == NULL
        || model_phase == NULL
        || *model_phase != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
    ) {
        return false;
    }

    if (model_handoff_is_ready(model_owners, model_targets, frame_count)) {
        uint64_t physical_address = UINT64_MAX;
        size_t free_index = frame_count;

        for (index = 0; index < frame_count; ++index) {
            if (model_owner_is_free(model_owners[index])) {
                free_index = index;
                break;
            }
        }
        if (free_index == frame_count) {
            if (
                micros_frame_ownership_release(
                    &ownership,
                    model_owners[0],
                    TEST_BASE
                ) != MICROS_FRAME_OWNERSHIP_OK
            ) {
                return false;
            }
            memset(&model_owners[0], 0, sizeof(model_owners[0]));
            model_targets[0] = MICROS_FRAME_HANDOFF_NONE;
            free_index = 0;
        }
        if (
            micros_frame_ownership_allocate(
                &ownership,
                temporary,
                &physical_address
            ) != MICROS_FRAME_OWNERSHIP_OK
            || physical_address
                != TEST_BASE + (free_index * MICROS_FRAME_SIZE)
        ) {
            return false;
        }
        model_owners[free_index] = temporary;
        model_targets[free_index] = MICROS_FRAME_HANDOFF_NONE;
    }

    memcpy(
        &ownership_snapshot,
        &ownership,
        sizeof(ownership_snapshot)
    );
    memcpy(
        &allocator_snapshot,
        &allocator,
        sizeof(allocator_snapshot)
    );
    if (
        micros_frame_ownership_complete_handoff(&ownership)
            != MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION
        || memcmp(
            &ownership,
            &ownership_snapshot,
            sizeof(ownership)
        ) != 0
        || memcmp(
            &allocator,
            &allocator_snapshot,
            sizeof(allocator)
        ) != 0
        || !compare_model(
            model_owners,
            model_targets,
            *model_phase,
            frame_count,
            step,
            seed
        )
    ) {
        return false;
    }

    for (index = 0; index < frame_count; ++index) {
        if (
            model_owners[index].kind
                == MICROS_FRAME_OWNER_KERNEL_TEMPORARY
        ) {
            if (
                micros_frame_ownership_release(
                    &ownership,
                    model_owners[index],
                    TEST_BASE + (index * MICROS_FRAME_SIZE)
                ) != MICROS_FRAME_OWNERSHIP_OK
            ) {
                return false;
            }
            memset(
                &model_owners[index],
                0,
                sizeof(model_owners[index])
            );
            model_targets[index] = MICROS_FRAME_HANDOFF_NONE;
        } else if (
            model_owners[index].kind
                == MICROS_FRAME_OWNER_PROCESS_USER
            && model_targets[index] == MICROS_FRAME_HANDOFF_NONE
        ) {
            enum micros_frame_handoff_target target =
                index % 2 == 0
                ? MICROS_FRAME_HANDOFF_VM_WIRED
                : MICROS_FRAME_HANDOFF_VM_TRANSFERABLE;

            if (
                micros_frame_ownership_prepare_handoff(
                    &ownership,
                    TEST_BASE + (index * MICROS_FRAME_SIZE),
                    model_owners[index],
                    target
                ) != MICROS_FRAME_OWNERSHIP_OK
            ) {
                return false;
            }
            model_targets[index] = (uint8_t)target;
        }
    }
    if (
        !model_handoff_is_ready(
            model_owners,
            model_targets,
            frame_count
        )
        || micros_frame_ownership_complete_handoff(&ownership)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        return false;
    }
    model_apply_handoff(model_owners, model_targets, frame_count);
    *model_phase = MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF;
    return compare_model(
        model_owners,
        model_targets,
        *model_phase,
        frame_count,
        step,
        seed
    );
}

static bool test_matches_seeded_reference_model(void)
{
    const uint32_t seed = UINT32_C(0x20f0a11c);
    struct micros_frame_owner model_owners[TEST_FRAME_COUNT];
    uint8_t model_targets[TEST_FRAME_COUNT];
    enum micros_frame_ownership_phase model_phase =
        MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP;
    uint32_t random_state = seed;
    size_t step;
    size_t index;

    EXPECT_TRUE(initialize_fixture(TEST_FRAME_COUNT));
    memset(model_owners, 0, sizeof(model_owners));
    memset(model_targets, 0, sizeof(model_targets));

    for (step = 0; step < MODEL_STEPS; ++step) {
        uint32_t value = next_random(&random_state);
        uint32_t operation;

        if (
            step == MODEL_HANDOFF_STEP
            && !transition_model_to_handed_off(
                model_owners,
                model_targets,
                &model_phase,
                TEST_FRAME_COUNT,
                step,
                seed
            )
        ) {
            fprintf(
                stderr,
                "seed=0x%08x step=%zu handoff transition mismatch\n",
                seed,
                step
            );
            return false;
        }
        operation = value % (
            model_phase == MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
                ? 7U
                : 8U
        );
        if (
            model_phase == MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
            && operation == 4
            && ((value >> 24) & 7U) != 0
        ) {
            operation = 5;
        }
        if (
            model_phase == MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
            && operation == 6
            && ((value >> 24) & 31U) != 0
        ) {
            operation = 5;
        }

        if (operation == 0) {
            struct micros_frame_owner owner =
                model_owner_from_selector(value >> 8);
            uint64_t output = UINT64_C(0xdeadbeefdeadbeef);
            size_t expected_index = TEST_FRAME_COUNT;
            enum micros_frame_ownership_error expected_error;

            for (index = 0; index < TEST_FRAME_COUNT; ++index) {
                if (model_owner_is_free(model_owners[index])) {
                    expected_index = index;
                    break;
                }
            }
            if (
                model_phase
                    == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
            ) {
                expected_error = MICROS_FRAME_OWNERSHIP_ERROR_PHASE;
            } else {
                expected_error = expected_index == TEST_FRAME_COUNT
                    ? MICROS_FRAME_OWNERSHIP_ERROR_EXHAUSTED
                    : MICROS_FRAME_OWNERSHIP_OK;
            }
            if (
                micros_frame_ownership_allocate(
                    &ownership,
                    owner,
                    &output
                ) != expected_error
            ) {
                fprintf(
                    stderr,
                    "seed=0x%08x step=%zu allocation error mismatch\n",
                    seed,
                    step
                );
                return false;
            }
            if (expected_error == MICROS_FRAME_OWNERSHIP_OK) {
                if (
                    output
                    != TEST_BASE
                        + (expected_index * MICROS_FRAME_SIZE)
                ) {
                    fprintf(
                        stderr,
                        "seed=0x%08x step=%zu allocation address mismatch\n",
                        seed,
                        step
                    );
                    return false;
                }
                model_owners[expected_index] = owner;
                model_targets[expected_index] =
                    MICROS_FRAME_HANDOFF_NONE;
            } else if (output != UINT64_C(0xdeadbeefdeadbeef)) {
                return false;
            }
        } else if (operation == 1) {
            index = (value >> 8) % TEST_FRAME_COUNT;
            {
                struct micros_frame_owner expected =
                    model_owners[index];
                bool use_wrong = ((value >> 16) & 1U) != 0;
                enum micros_frame_ownership_error expected_error;

                if (
                    model_phase
                        == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
                ) {
                    expected_error =
                        MICROS_FRAME_OWNERSHIP_ERROR_PHASE;
                } else if (model_owner_is_free(expected)) {
                    expected_error =
                        MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED;
                } else if (use_wrong) {
                    expected = model_different_valid_owner(expected);
                    expected_error =
                        MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
                } else {
                    expected_error = MICROS_FRAME_OWNERSHIP_OK;
                }
                if (
                    micros_frame_ownership_release(
                        &ownership,
                        expected,
                        TEST_BASE + (index * MICROS_FRAME_SIZE)
                    ) != expected_error
                ) {
                    fprintf(
                        stderr,
                        "seed=0x%08x step=%zu release mismatch\n",
                        seed,
                        step
                    );
                    return false;
                }
                if (expected_error == MICROS_FRAME_OWNERSHIP_OK) {
                    memset(
                        &model_owners[index],
                        0,
                        sizeof(model_owners[index])
                    );
                    model_targets[index] = MICROS_FRAME_HANDOFF_NONE;
                }
            }
        } else if (operation == 2) {
            index = (value >> 8) % TEST_FRAME_COUNT;
            {
                struct micros_frame_owner output =
                    raw_kernel_owner(
                        MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE
                    );

                EXPECT_ERROR(
                    MICROS_FRAME_OWNERSHIP_OK,
                    micros_frame_ownership_lookup(
                        &ownership,
                        TEST_BASE + (index * MICROS_FRAME_SIZE),
                        &output
                    )
                );
                if (!owners_equal(output, model_owners[index])) {
                    fprintf(
                        stderr,
                        "seed=0x%08x step=%zu lookup mismatch\n",
                        seed,
                        step
                    );
                    return false;
                }
            }
        } else if (operation == 3) {
            index = (value >> 8) % TEST_FRAME_COUNT;
            {
                struct micros_frame_owner expected =
                    model_owners[index];
                bool use_wrong = ((value >> 20) & 1U) != 0;
                enum micros_frame_handoff_target target =
                    (enum micros_frame_handoff_target)(
                        (value >> 16)
                        % MICROS_FRAME_HANDOFF_TARGET_COUNT
                    );
                enum micros_frame_ownership_error expected_error;

                if (
                    model_phase
                        == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
                ) {
                    expected_error =
                        MICROS_FRAME_OWNERSHIP_ERROR_PHASE;
                } else if (model_owner_is_free(expected)) {
                    expected_error =
                        MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED;
                } else if (use_wrong) {
                    expected = model_different_valid_owner(expected);
                    expected_error =
                        MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
                } else if (model_plan_is_valid(expected, target)) {
                    expected_error = MICROS_FRAME_OWNERSHIP_OK;
                } else {
                    expected_error =
                        MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION;
                }
                if (
                    micros_frame_ownership_prepare_handoff(
                        &ownership,
                        TEST_BASE + (index * MICROS_FRAME_SIZE),
                        expected,
                        target
                    ) != expected_error
                ) {
                    fprintf(
                        stderr,
                        "seed=0x%08x step=%zu plan mismatch\n",
                        seed,
                        step
                    );
                    return false;
                }
                if (expected_error == MICROS_FRAME_OWNERSHIP_OK) {
                    model_targets[index] = (uint8_t)target;
                }
            }
        } else if (operation == 4) {
            struct micros_process_handle process = (
                ((value >> 8) & 1U) == 0
                    ? (struct micros_process_handle){1, 2}
                    : (struct micros_process_handle){2, 3}
            );
            enum micros_frame_ownership_error expected_error =
                MICROS_FRAME_OWNERSHIP_OK;
            size_t selection;

            if (((value >> 9) & 1U) != 0) {
                ++process.generation;
            }
            clear_release_bitmap();
            for (selection = 0; selection < 4; ++selection) {
                size_t selected_index =
                    (
                        value
                        >> (selection * 5)
                    ) % TEST_FRAME_COUNT;

                select_release_index(selected_index);
            }
            if (
                model_phase
                    == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
            ) {
                expected_error = MICROS_FRAME_OWNERSHIP_ERROR_PHASE;
            } else {
                for (
                    index = 0;
                    index < TEST_FRAME_COUNT;
                    ++index
                ) {
                    struct micros_frame_owner owner =
                        model_owners[index];

                    if (
                        (
                            release_bitmap[index / 64]
                            & (
                                UINT64_C(1)
                                << (index % 64)
                            )
                        ) == 0
                    ) {
                        continue;
                    }
                    if (model_owner_is_free(owner)) {
                        expected_error =
                            MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED;
                        break;
                    }
                    if (
                        (
                            owner.kind
                                != MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
                            && owner.kind
                                != MICROS_FRAME_OWNER_PROCESS_USER
                        )
                        || owner.slot != process.slot
                        || owner.generation != process.generation
                    ) {
                        expected_error =
                            MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
                        break;
                    }
                }
            }
            if (
                micros_frame_ownership_release_process_set(
                    &ownership,
                    process,
                    release_bitmap,
                    MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
                ) != expected_error
            ) {
                fprintf(
                    stderr,
                    "seed=0x%08x step=%zu release-set mismatch\n",
                    seed,
                    step
                );
                return false;
            }
            if (expected_error == MICROS_FRAME_OWNERSHIP_OK) {
                for (
                    index = 0;
                    index < TEST_FRAME_COUNT;
                    ++index
                ) {
                    if (
                        (
                            release_bitmap[index / 64]
                            & (
                                UINT64_C(1)
                                << (index % 64)
                            )
                        ) != 0
                    ) {
                        memset(
                            &model_owners[index],
                            0,
                            sizeof(model_owners[index])
                        );
                        model_targets[index] =
                            MICROS_FRAME_HANDOFF_NONE;
                    }
                }
            }
        } else if (operation == 5) {
            const struct micros_process_handle process = {
                (uint16_t)((value >> 8) & 3U),
                ((value >> 10) & 1U) != 0 ? 2U : 3U,
            };
            uint64_t expected_count = 0;
            uint64_t actual_count = UINT64_MAX;

            for (index = 0; index < TEST_FRAME_COUNT; ++index) {
                struct micros_frame_owner owner = model_owners[index];

                if (
                    (
                        owner.kind
                            == MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
                        || owner.kind
                            == MICROS_FRAME_OWNER_PROCESS_USER
                        || owner.kind == MICROS_FRAME_OWNER_VM_WIRED
                    )
                    && owner.slot == process.slot
                    && owner.generation == process.generation
                ) {
                    ++expected_count;
                }
            }
            EXPECT_ERROR(
                MICROS_FRAME_OWNERSHIP_OK,
                micros_frame_ownership_count_process(
                    &ownership,
                    process,
                    &actual_count
                )
            );
            if (actual_count != expected_count) {
                fprintf(
                    stderr,
                    "seed=0x%08x step=%zu process count mismatch\n",
                    seed,
                    step
                );
                return false;
            }
        } else if (operation == 6) {
            enum micros_frame_ownership_error expected_error;
            size_t mutation_index =
                (value >> 8) % (TEST_FRAME_COUNT + 1);

            clear_release_bitmap();
            for (index = 0; index < TEST_FRAME_COUNT; ++index) {
                if (
                    model_owners[index].kind
                        == MICROS_FRAME_OWNER_PROCESS_USER
                ) {
                    select_release_index(index);
                }
            }
            if (((value >> 20) & 1U) != 0) {
                release_bitmap[mutation_index / 64]
                    ^= UINT64_C(1) << (mutation_index % 64);
            }
            expected_error = model_prepare_wired_set_error(
                model_owners,
                model_targets,
                model_phase,
                TEST_FRAME_COUNT,
                release_bitmap
            );
            if (
                micros_frame_ownership_prepare_wired_process_user_set(
                    &ownership,
                    release_bitmap,
                    MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
                ) != expected_error
            ) {
                fprintf(
                    stderr,
                    "seed=0x%08x step=%zu wired-set mismatch\n",
                    seed,
                    step
                );
                return false;
            }
            if (expected_error == MICROS_FRAME_OWNERSHIP_OK) {
                model_apply_wired_set(
                    model_owners,
                    model_targets,
                    TEST_FRAME_COUNT
                );
            }
        } else {
            enum micros_frame_ownership_error expected_error =
                model_phase
                    == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
                ? MICROS_FRAME_OWNERSHIP_ERROR_PHASE
                : (
                    model_handoff_is_ready(
                        model_owners,
                        model_targets,
                        TEST_FRAME_COUNT
                    )
                    ? MICROS_FRAME_OWNERSHIP_OK
                    : MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION
                );

            if (
                micros_frame_ownership_complete_handoff(&ownership)
                    != expected_error
            ) {
                fprintf(
                    stderr,
                    "seed=0x%08x step=%zu completion mismatch\n",
                    seed,
                    step
                );
                return false;
            }
            if (expected_error == MICROS_FRAME_OWNERSHIP_OK) {
                model_apply_handoff(
                    model_owners,
                    model_targets,
                    TEST_FRAME_COUNT
                );
                model_phase =
                    MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF;
            }
        }
        if (
            !compare_model(
                model_owners,
                model_targets,
                model_phase,
                TEST_FRAME_COUNT,
                step,
                seed
            )
        ) {
            return false;
        }
    }
    return true;
}

struct test_case {
    const char *name;
    bool (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        {
            "constructs exact owner records",
            test_constructs_exact_owner_records,
        },
        {
            "initializes once on pristine storage",
            test_initializes_once_on_pristine_storage,
        },
        {
            "rejects invalid raw owner records",
            test_rejects_invalid_raw_owner_records,
        },
        {
            "allocates, looks up, and releases exact owners",
            test_allocates_looks_up_and_releases_exact_owners,
        },
        {
            "rejects geometry changes before mutation",
            test_rejects_geometry_changes_before_mutation,
        },
        {
            "rolls back defensive post-allocation failure",
            test_rolls_back_defensive_post_allocation_failure,
        },
        {
            "atomically releases process-owned sets",
            test_atomically_releases_process_owned_sets,
        },
        {
            "stages and atomically commits handoff",
            test_stages_and_atomically_commits_handoff,
        },
        {
            "atomically stages complete wired set",
            test_atomically_stages_complete_wired_set,
        },
        {
            "rejects independent corruption",
            test_validator_rejects_independent_corruption,
        },
        {
            "operates at maximum managed capacity",
            test_operates_at_maximum_managed_capacity,
        },
        {
            "matches seeded reference model",
            test_matches_seeded_reference_model,
        },
    };
    size_t index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run()) {
            fprintf(stderr, "not ok %zu - %s\n", index + 1, tests[index].name);
            return 1;
        }
        printf("ok %zu - %s\n", index + 1, tests[index].name);
    }
    return 0;
}
