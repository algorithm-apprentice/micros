#ifndef MICROS_FRAME_OWNERSHIP_HOST_TEST_INTERNAL_H
#define MICROS_FRAME_OWNERSHIP_HOST_TEST_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "micros/frame_ownership.h"

enum {
    TEST_FRAME_COUNT = 32,
    MODEL_STEPS = 4096,
    MODEL_HANDOFF_STEP = 3072,
    MODEL_FULL_VALIDATION_INTERVAL = 64,
};

#define TEST_BASE UINT64_C(0x00100000)

struct frame_ownership_test_context {
    struct micros_frame_allocator allocator;
    struct micros_frame_allocator alternate_allocator;
    struct micros_frame_ownership ownership;
    struct micros_frame_ownership ownership_snapshot;
    struct micros_frame_allocator allocator_snapshot;
    uint64_t release_bitmap[MICROS_FRAME_ALLOCATOR_BITMAP_WORDS];
};

struct frame_ownership_test_context *
frame_ownership_test_context(void);

bool frame_ownership_test_owners_equal(
    struct micros_frame_owner left,
    struct micros_frame_owner right
);

struct micros_frame_owner frame_ownership_test_raw_kernel_owner(
    enum micros_frame_owner_kind kind
);

struct micros_frame_owner frame_ownership_test_raw_process_owner(
    enum micros_frame_owner_kind kind,
    uint16_t slot,
    uint32_t generation
);

bool frame_ownership_test_initialize_fixture(
    struct frame_ownership_test_context *context,
    uint64_t frame_count
);

void frame_ownership_test_clear_release_bitmap(
    struct frame_ownership_test_context *context
);

void frame_ownership_test_select_release_index(
    struct frame_ownership_test_context *context,
    uint64_t frame_index
);

void frame_ownership_test_snapshot(
    struct frame_ownership_test_context *context
);

bool frame_ownership_test_snapshot_matches(
    const struct frame_ownership_test_context *context
);

bool frame_ownership_test_constructs_exact_owner_records(
    struct frame_ownership_test_context *context
);

bool frame_ownership_test_initializes_once_on_pristine_storage(
    struct frame_ownership_test_context *context
);

bool frame_ownership_test_rejects_invalid_raw_owner_records(
    struct frame_ownership_test_context *context
);

bool frame_ownership_test_allocates_looks_up_and_releases_exact_owners(
    struct frame_ownership_test_context *context
);

bool frame_ownership_test_rejects_geometry_changes_before_mutation(
    struct frame_ownership_test_context *context
);

bool frame_ownership_test_rolls_back_defensive_post_allocation_failure(
    struct frame_ownership_test_context *context
);

bool frame_ownership_test_atomically_releases_process_owned_sets(
    struct frame_ownership_test_context *context
);

bool frame_ownership_test_stages_and_atomically_commits_handoff(
    struct frame_ownership_test_context *context
);

bool frame_ownership_test_atomically_stages_complete_wired_set(
    struct frame_ownership_test_context *context
);

bool frame_ownership_test_validator_rejects_independent_corruption(
    struct frame_ownership_test_context *context
);

bool frame_ownership_test_operates_at_maximum_managed_capacity(
    struct frame_ownership_test_context *context
);

bool frame_ownership_test_matches_seeded_reference_model(
    struct frame_ownership_test_context *context
);

bool frame_ownership_test_commits_post_handoff_vm_ownership_lifecycle(
    struct frame_ownership_test_context *context
);

bool frame_ownership_test_post_handoff_vm_ownership_preflight_is_atomic(
    struct frame_ownership_test_context *context
);

#endif
