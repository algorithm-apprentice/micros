#ifndef MICROS_FRAME_OWNERSHIP_TEST_INTERNAL_H
#define MICROS_FRAME_OWNERSHIP_TEST_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/kernel_address_space.h"
#include "micros/kernel_object_runtime.h"
#include "micros/frame_ownership_runtime.h"

struct micros_frame_ownership_test_context {
    struct micros_frame_ownership ownership_state_snapshot;
    struct micros_frame_allocator allocator_state_snapshot;
    struct micros_kernel_objects object_state_snapshot;
    struct micros_kernel_objects substitute_objects;
    struct micros_kernel_objects substitute_state_snapshot;
    struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *address_space;
    const struct micros_frame_ownership *ledger;
    struct micros_process_handle stale_process;
    struct micros_process_handle replacement_process;
    struct micros_process_handle second_process;
    struct micros_process_handle substitute_stale_process;
    struct micros_process_handle substitute_replacement_process;
    struct micros_process_handle substitute_second_process;
    struct micros_frame_owner stale_user;
    struct micros_frame_owner replacement_table;
    struct micros_frame_owner replacement_user;
    struct micros_frame_owner second_table;
    struct micros_frame_owner second_user;
    struct micros_frame_owner observed;
    uint64_t replacement_table_frame;
    uint64_t replacement_user_frame;
    uint64_t second_table_frame;
    uint64_t second_user_frame;
    uint64_t replacement_table_index;
    uint64_t replacement_user_index;
    uint64_t second_table_index;
    uint64_t second_user_index;
    uint64_t baseline_table_count;
    uint64_t counts_before[MICROS_FRAME_OWNER_KIND_COUNT];
    uint64_t owned_before;
    uint64_t unchanged;
};

struct micros_frame_ownership_test_context *
micros_frame_ownership_test_context(void);

void micros_frame_ownership_test_copy_bytes(
    void *destination,
    const void *source,
    size_t size
);

bool micros_frame_ownership_test_bytes_equal(
    const void *left,
    const void *right,
    size_t size
);

bool micros_frame_ownership_test_snapshot_runtime_state(
    struct micros_frame_ownership_test_context *context
);

bool micros_frame_ownership_test_runtime_state_matches_snapshot(
    const struct micros_frame_ownership_test_context *context
);

bool micros_frame_ownership_test_owners_equal(
    struct micros_frame_owner left,
    struct micros_frame_owner right
);

bool micros_frame_ownership_test_frame_index_for_address(
    const struct micros_frame_ownership *ledger,
    uint64_t physical_address,
    uint64_t *frame_index
);

bool micros_frame_ownership_test_frame_state_matches(
    const struct micros_frame_ownership *ledger,
    uint64_t frame_index,
    struct micros_frame_owner expected_owner,
    enum micros_frame_handoff_target expected_target,
    uint64_t expected_owned,
    const uint64_t *expected_counts
);

bool micros_frame_ownership_test_prepare_runtime_baseline(
    struct micros_frame_ownership_test_context *context
);

bool micros_frame_ownership_test_prepare_process_generations(
    struct micros_frame_ownership_test_context *context
);

bool micros_frame_ownership_test_prepare_owner_records(
    struct micros_frame_ownership_test_context *context
);

bool micros_frame_ownership_test_allocate_owned_frames(
    struct micros_frame_ownership_test_context *context
);

bool micros_frame_ownership_test_verify_bootstrap_rejections(
    struct micros_frame_ownership_test_context *context
);

bool micros_frame_ownership_test_verify_owner_lookups(
    struct micros_frame_ownership_test_context *context
);

bool micros_frame_ownership_test_verify_registry_mismatch_rejections(
    struct micros_frame_ownership_test_context *context
);

bool micros_frame_ownership_test_verify_handoff_rejections(
    struct micros_frame_ownership_test_context *context
);

bool micros_frame_ownership_test_commit_handoff(
    struct micros_frame_ownership_test_context *context
);

bool micros_frame_ownership_test_verify_handoff_state(
    struct micros_frame_ownership_test_context *context
);

bool micros_frame_ownership_test_verify_sealed_rejections(
    struct micros_frame_ownership_test_context *context
);

bool micros_frame_ownership_test_verify_final_state(
    struct micros_frame_ownership_test_context *context
);

#endif
