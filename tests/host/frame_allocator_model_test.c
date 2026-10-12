#include "frame_allocator_test_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

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

static uint32_t next_random(uint32_t *state)
{
    *state = (*state * UINT32_C(1664525)) + UINT32_C(1013904223);
    return *state;
}

enum {
    MODEL_FRAME_COUNT = 32,
    MODEL_STEPS = 2000,
};

struct frame_allocator_model {
    bool managed[MODEL_FRAME_COUNT];
    bool allocated[MODEL_FRAME_COUNT];
    size_t expected_free;
    size_t allocation_steps;
    size_t release_steps;
    size_t successful_releases;
};

static void initialize_reference_model(
    struct frame_allocator_model *model
)
{
    size_t index;

    for (index = 0; index < MODEL_FRAME_COUNT; ++index) {
        model->managed[index] = !(
            index == 4 || index == 5 || index == 16
        );
        if (model->managed[index]) {
            ++model->expected_free;
        }
    }
}

static bool run_model_allocation(
    struct frame_allocator_test_context *context,
    struct frame_allocator_model *model,
    const struct micros_physical_range *memory_range,
    uint32_t seed,
    size_t step
)
{
    uint64_t actual_address = 0;
    size_t expected_index = MODEL_FRAME_COUNT;
    enum micros_frame_allocator_error expected_error;
    enum micros_frame_allocator_error actual_error;
    size_t index;

    ++model->allocation_steps;
    for (index = 0; index < MODEL_FRAME_COUNT; ++index) {
        if (model->managed[index] && !model->allocated[index]) {
            expected_index = index;
            break;
        }
    }
    expected_error = expected_index == MODEL_FRAME_COUNT
        ? MICROS_FRAME_ALLOCATOR_ERROR_EXHAUSTED
        : MICROS_FRAME_ALLOCATOR_OK;
    actual_error = micros_frame_allocator_allocate(
        &context->allocator,
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
    if (expected_error != MICROS_FRAME_ALLOCATOR_OK) {
        return true;
    }
    if (
        actual_address
        != memory_range->base + (expected_index * MICROS_FRAME_SIZE)
    ) {
        fprintf(
            stderr,
            "seed=0x%08x step=%zu allocation mismatch\n",
            seed,
            step
        );
        return false;
    }
    model->allocated[expected_index] = true;
    --model->expected_free;
    return true;
}

static bool run_model_release(
    struct frame_allocator_test_context *context,
    struct frame_allocator_model *model,
    const struct micros_physical_range *memory_range,
    uint32_t *random_state,
    uint32_t seed,
    size_t step
)
{
    size_t release_index =
        (next_random(random_state) >> 16) % MODEL_FRAME_COUNT;
    uint64_t address = memory_range->base
        + (release_index * MICROS_FRAME_SIZE);
    enum micros_frame_allocator_error expected_error;

    ++model->release_steps;
    if (!model->managed[release_index]) {
        expected_error = MICROS_FRAME_ALLOCATOR_ERROR_UNMANAGED;
    } else if (!model->allocated[release_index]) {
        expected_error = MICROS_FRAME_ALLOCATOR_ERROR_NOT_ALLOCATED;
    } else {
        expected_error = MICROS_FRAME_ALLOCATOR_OK;
        model->allocated[release_index] = false;
        ++model->expected_free;
        ++model->successful_releases;
    }
    if (
        micros_frame_allocator_release(&context->allocator, address)
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
    return true;
}

static bool model_free_count_matches(
    const struct frame_allocator_test_context *context,
    const struct frame_allocator_model *model,
    uint32_t seed,
    size_t step
)
{
    if (context->allocator.free_frame_count == model->expected_free) {
        return true;
    }
    fprintf(
        stderr,
        "seed=0x%08x step=%zu free-count mismatch\n",
        seed,
        step
    );
    return false;
}

static bool model_coverage_is_complete(
    const struct frame_allocator_model *model,
    uint32_t seed
)
{
    if (
        model->allocation_steps != 0
        && model->release_steps != 0
        && model->successful_releases != 0
    ) {
        return true;
    }
    fprintf(
        stderr,
        "seed=0x%08x incomplete trace allocations=%zu "
        "releases=%zu successful-releases=%zu\n",
        seed,
        model->allocation_steps,
        model->release_steps,
        model->successful_releases
    );
    return false;
}

bool frame_allocator_test_matches_seeded_reference_model(
    struct frame_allocator_test_context *context
)
{
    const uint32_t seed = UINT32_C(0x17a110c);
    const struct micros_physical_range memory_range = {
        UINT64_C(0x100000),
        MODEL_FRAME_COUNT * MICROS_FRAME_SIZE,
    };
    const struct micros_physical_range reserved_ranges[] = {
        {UINT64_C(0x104000), UINT64_C(0x2000)},
        {UINT64_C(0x110000), UINT64_C(0x1000)},
    };
    struct frame_allocator_model model = {0};
    uint32_t random_state = seed;
    size_t step;

    initialize_reference_model(&model);
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

    for (step = 0; step < MODEL_STEPS; ++step) {
        uint32_t operation = next_random(&random_state);
        bool succeeded;

        if ((operation & UINT32_C(0x80000000)) == 0) {
            succeeded = run_model_allocation(
                context,
                &model,
                &memory_range,
                seed,
                step
            );
        } else {
            succeeded = run_model_release(
                context,
                &model,
                &memory_range,
                &random_state,
                seed,
                step
            );
        }
        if (
            !succeeded
            || !model_free_count_matches(
                context,
                &model,
                seed,
                step
            )
        ) {
            return false;
        }
    }
    return model_coverage_is_complete(&model, seed);
}
