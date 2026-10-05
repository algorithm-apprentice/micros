#include "micros/frame_allocator.h"

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

static struct micros_frame_allocator allocator;

static bool test_normalizes_and_subtracts_ranges(void)
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
            &allocator,
            memory_ranges,
            sizeof(memory_ranges) / sizeof(memory_ranges[0]),
            reserved_ranges,
            sizeof(reserved_ranges) / sizeof(reserved_ranges[0])
        )
    );
    EXPECT_TRUE(allocator.initialized);
    EXPECT_TRUE(allocator.memory_range_count == 1);
    EXPECT_TRUE(allocator.memory_ranges[0].base == UINT64_C(0x2000));
    EXPECT_TRUE(allocator.memory_ranges[0].size == UINT64_C(0x6000));
    EXPECT_TRUE(allocator.reserved_range_count == 3);
    EXPECT_TRUE(allocator.managed_range_count == 2);
    EXPECT_TRUE(
        allocator.managed_ranges[0].base == UINT64_C(0x4000)
    );
    EXPECT_TRUE(allocator.managed_ranges[0].frame_count == 2);
    EXPECT_TRUE(allocator.managed_ranges[0].bitmap_offset == 0);
    EXPECT_TRUE(
        allocator.managed_ranges[1].base == UINT64_C(0x7000)
    );
    EXPECT_TRUE(allocator.managed_ranges[1].frame_count == 1);
    EXPECT_TRUE(allocator.managed_ranges[1].bitmap_offset == 2);
    EXPECT_TRUE(allocator.managed_frame_count == 3);
    EXPECT_TRUE(allocator.free_frame_count == 3);
    return true;
}

static bool test_allocates_releases_and_reuses_lowest_frame(void)
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
            &allocator,
            &memory_range,
            1,
            NULL,
            0
        )
    );
    for (index = 0; index < 4; ++index) {
        EXPECT_ERROR(
            MICROS_FRAME_ALLOCATOR_OK,
            micros_frame_allocator_allocate(&allocator, &frames[index])
        );
        EXPECT_TRUE(
            frames[index] == UINT64_C(0x1000)
                + (index * MICROS_FRAME_SIZE)
        );
    }
    EXPECT_TRUE(allocator.free_frame_count == 0);
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_EXHAUSTED,
        micros_frame_allocator_allocate(&allocator, &unchanged)
    );
    EXPECT_TRUE(unchanged == UINT64_C(0xfeedfacefeedface));

    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_OK,
        micros_frame_allocator_release(&allocator, frames[1])
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_OK,
        micros_frame_allocator_allocate(&allocator, &unchanged)
    );
    EXPECT_TRUE(unchanged == frames[1]);
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_UNALIGNED,
        micros_frame_allocator_release(&allocator, frames[0] + 1)
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_UNMANAGED,
        micros_frame_allocator_release(&allocator, UINT64_C(0x9000))
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_OK,
        micros_frame_allocator_release(&allocator, frames[3])
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_NOT_ALLOCATED,
        micros_frame_allocator_release(&allocator, frames[3])
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_OK,
        micros_frame_allocator_release(&allocator, frames[0])
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_NOT_ALLOCATED,
        micros_frame_allocator_release(&allocator, frames[0])
    );
    return true;
}

static bool test_merges_reservations_and_handles_full_removal(void)
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
            &allocator,
            &memory_range,
            1,
            reserved_ranges,
            sizeof(reserved_ranges) / sizeof(reserved_ranges[0])
        )
    );
    EXPECT_TRUE(allocator.reserved_range_count == 2);
    EXPECT_TRUE(
        allocator.reserved_ranges[1].base == UINT64_C(0x2000)
    );
    EXPECT_TRUE(
        allocator.reserved_ranges[1].size == UINT64_C(0x4000)
    );
    EXPECT_TRUE(allocator.managed_range_count == 2);
    EXPECT_TRUE(
        allocator.managed_ranges[0].base == UINT64_C(0x1000)
    );
    EXPECT_TRUE(allocator.managed_ranges[0].frame_count == 1);
    EXPECT_TRUE(
        allocator.managed_ranges[1].base == UINT64_C(0x6000)
    );
    EXPECT_TRUE(allocator.managed_ranges[1].frame_count == 1);

    snapshot = allocator;
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_NO_MEMORY,
        micros_frame_allocator_initialize(
            &allocator,
            &memory_range,
            1,
            &remove_all,
            1
        )
    );
    EXPECT_TRUE(memcmp(&allocator, &snapshot, sizeof(allocator)) == 0);
    return true;
}

static bool test_rejects_invalid_inputs_without_changing_state(void)
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
    struct micros_frame_allocator snapshot;

    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_OK,
        micros_frame_allocator_initialize(
            &allocator,
            &valid_memory,
            1,
            NULL,
            0
        )
    );
    snapshot = allocator;

    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_ARGUMENT,
        micros_frame_allocator_initialize(
            NULL,
            &valid_memory,
            1,
            NULL,
            0
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_ARGUMENT,
        micros_frame_allocator_initialize(
            &allocator,
            NULL,
            1,
            NULL,
            0
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_CAPACITY,
        micros_frame_allocator_initialize(
            &allocator,
            &valid_memory,
            MICROS_FRAME_ALLOCATOR_MAX_MEMORY_RANGES + 1,
            NULL,
            0
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_CAPACITY,
        micros_frame_allocator_initialize(
            &allocator,
            &valid_memory,
            1,
            &valid_memory,
            MICROS_FRAME_ALLOCATOR_MAX_RESERVED_RANGES + 1
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_RANGE,
        micros_frame_allocator_initialize(
            &allocator,
            &overflow,
            1,
            NULL,
            0
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_RANGE,
        micros_frame_allocator_initialize(
            &allocator,
            &zero_size,
            1,
            NULL,
            0
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_RANGE,
        micros_frame_allocator_initialize(
            &allocator,
            &valid_memory,
            1,
            &reservation_alignment_overflow,
            1
        )
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_NO_MEMORY,
        micros_frame_allocator_initialize(
            &allocator,
            &partial,
            1,
            NULL,
            0
        )
    );
    EXPECT_TRUE(memcmp(&allocator, &snapshot, sizeof(allocator)) == 0);
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_ARGUMENT,
        micros_frame_allocator_allocate(NULL, NULL)
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_ARGUMENT,
        micros_frame_allocator_allocate(&allocator, NULL)
    );
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_ARGUMENT,
        micros_frame_allocator_release(NULL, UINT64_C(0x1000))
    );
    return true;
}

static bool test_accepts_capacity_boundaries(void)
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
            &allocator,
            memory_ranges,
            MICROS_FRAME_ALLOCATOR_MAX_MEMORY_RANGES,
            reserved_ranges,
            MICROS_FRAME_ALLOCATOR_MAX_RESERVED_RANGES
        )
    );
    EXPECT_TRUE(
        allocator.managed_range_count
        == MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES
    );
    EXPECT_TRUE(allocator.managed_frame_count == 96);

    memory_ranges[0].base = 0;
    memory_ranges[0].size =
        (uint64_t)MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES
        * MICROS_FRAME_SIZE;
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_OK,
        micros_frame_allocator_initialize(
            &allocator,
            memory_ranges,
            1,
            NULL,
            0
        )
    );
    EXPECT_TRUE(
        allocator.managed_frame_count
        == MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES
    );

    memory_ranges[0].size += MICROS_FRAME_SIZE;
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_ERROR_CAPACITY,
        micros_frame_allocator_initialize(
            &allocator,
            memory_ranges,
            1,
            NULL,
            0
        )
    );
    return true;
}

static uint32_t next_random(uint32_t *state)
{
    *state = (*state * UINT32_C(1664525)) + UINT32_C(1013904223);
    return *state;
}

static bool test_matches_seeded_reference_model(void)
{
    enum {
        MODEL_FRAME_COUNT = 32,
        MODEL_STEPS = 2000,
    };
    const uint32_t seed = UINT32_C(0x17a110c);
    const struct micros_physical_range memory_range = {
        UINT64_C(0x100000),
        MODEL_FRAME_COUNT * MICROS_FRAME_SIZE,
    };
    const struct micros_physical_range reserved_ranges[] = {
        {UINT64_C(0x104000), UINT64_C(0x2000)},
        {UINT64_C(0x110000), UINT64_C(0x1000)},
    };
    bool managed[MODEL_FRAME_COUNT];
    bool allocated[MODEL_FRAME_COUNT] = {false};
    uint32_t random_state = seed;
    size_t expected_free = 0;
    size_t allocation_steps = 0;
    size_t release_steps = 0;
    size_t successful_releases = 0;
    size_t step;
    size_t index;

    for (index = 0; index < MODEL_FRAME_COUNT; ++index) {
        managed[index] = !(index == 4 || index == 5 || index == 16);
        if (managed[index]) {
            ++expected_free;
        }
    }
    EXPECT_ERROR(
        MICROS_FRAME_ALLOCATOR_OK,
        micros_frame_allocator_initialize(
            &allocator,
            &memory_range,
            1,
            reserved_ranges,
            sizeof(reserved_ranges) / sizeof(reserved_ranges[0])
        )
    );

    for (step = 0; step < MODEL_STEPS; ++step) {
        uint32_t operation = next_random(&random_state);

        if ((operation & UINT32_C(0x80000000)) == 0) {
            uint64_t actual_address = 0;
            size_t expected_index = MODEL_FRAME_COUNT;
            enum micros_frame_allocator_error expected_error;
            enum micros_frame_allocator_error actual_error;

            ++allocation_steps;
            for (index = 0; index < MODEL_FRAME_COUNT; ++index) {
                if (managed[index] && !allocated[index]) {
                    expected_index = index;
                    break;
                }
            }
            if (expected_index == MODEL_FRAME_COUNT) {
                expected_error =
                    MICROS_FRAME_ALLOCATOR_ERROR_EXHAUSTED;
            } else {
                expected_error = MICROS_FRAME_ALLOCATOR_OK;
            }
            actual_error = micros_frame_allocator_allocate(
                &allocator,
                &actual_address
            );
            if (actual_error != expected_error) {
                fprintf(
                    stderr,
                    "seed=0x%08x step=%zu allocation error mismatch\n",
                    seed,
                    step
                );
                return false;
            }
            if (expected_error == MICROS_FRAME_ALLOCATOR_OK) {
                if (
                    actual_address
                    != memory_range.base
                        + (expected_index * MICROS_FRAME_SIZE)
                ) {
                    fprintf(
                        stderr,
                        "seed=0x%08x step=%zu allocation mismatch\n",
                        seed,
                        step
                    );
                    return false;
                }
                allocated[expected_index] = true;
                --expected_free;
            }
        } else {
            size_t release_index =
                (next_random(&random_state) >> 16)
                % MODEL_FRAME_COUNT;
            uint64_t address = memory_range.base
                + (release_index * MICROS_FRAME_SIZE);
            enum micros_frame_allocator_error expected_error;

            ++release_steps;
            if (!managed[release_index]) {
                expected_error =
                    MICROS_FRAME_ALLOCATOR_ERROR_UNMANAGED;
            } else if (!allocated[release_index]) {
                expected_error =
                    MICROS_FRAME_ALLOCATOR_ERROR_NOT_ALLOCATED;
            } else {
                expected_error = MICROS_FRAME_ALLOCATOR_OK;
                allocated[release_index] = false;
                ++expected_free;
                ++successful_releases;
            }
            if (
                micros_frame_allocator_release(&allocator, address)
                != expected_error
            ) {
                fprintf(
                    stderr,
                    "seed=0x%08x step=%zu release mismatch\n",
                    seed,
                    step
                );
                return false;
            }
        }
        if (allocator.free_frame_count != expected_free) {
            fprintf(
                stderr,
                "seed=0x%08x step=%zu free-count mismatch\n",
                seed,
                step
            );
            return false;
        }
    }
    if (
        allocation_steps == 0
        || release_steps == 0
        || successful_releases == 0
    ) {
        fprintf(
            stderr,
            "seed=0x%08x incomplete trace allocations=%zu "
            "releases=%zu successful-releases=%zu\n",
            seed,
            allocation_steps,
            release_steps,
            successful_releases
        );
        return false;
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
            "normalizes and subtracts ranges",
            test_normalizes_and_subtracts_ranges,
        },
        {
            "allocates, releases, and reuses lowest frame",
            test_allocates_releases_and_reuses_lowest_frame,
        },
        {
            "merges reservations and handles full removal",
            test_merges_reservations_and_handles_full_removal,
        },
        {
            "rejects invalid inputs without state changes",
            test_rejects_invalid_inputs_without_changing_state,
        },
        {
            "accepts capacity boundaries",
            test_accepts_capacity_boundaries,
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
