#ifndef MICROS_FRAME_ALLOCATOR_TEST_INTERNAL_H
#define MICROS_FRAME_ALLOCATOR_TEST_INTERNAL_H

#include <stdbool.h>

#include "micros/frame_allocator.h"

struct frame_allocator_test_context {
    struct micros_frame_allocator allocator;
};

bool frame_allocator_test_normalizes_and_subtracts_ranges(
    struct frame_allocator_test_context *context
);

bool frame_allocator_test_allocates_releases_and_reuses_lowest_frame(
    struct frame_allocator_test_context *context
);

bool frame_allocator_test_merges_reservations_and_handles_full_removal(
    struct frame_allocator_test_context *context
);

bool frame_allocator_test_rejects_invalid_inputs_without_changing_state(
    struct frame_allocator_test_context *context
);

bool frame_allocator_test_accepts_capacity_boundaries(
    struct frame_allocator_test_context *context
);

bool frame_allocator_test_matches_seeded_reference_model(
    struct frame_allocator_test_context *context
);

#endif
