#include "kernel/tests/memory/frame_ownership_test_internal.h"

static struct micros_frame_ownership_test_context test_context;

void micros_frame_ownership_test_copy_bytes(
    void *destination,
    const void *source,
    size_t size
)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

bool micros_frame_ownership_test_bytes_equal(
    const void *left,
    const void *right,
    size_t size
)
{
    const unsigned char *left_bytes = left;
    const unsigned char *right_bytes = right;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
            return false;
        }
    }
    return true;
}

struct micros_frame_ownership_test_context *
micros_frame_ownership_test_context(void)
{
    return &test_context;
}

bool micros_frame_ownership_test_snapshot_runtime_state(
    struct micros_frame_ownership_test_context *context
)
{
    if (
        context == NULL
        || context->ledger == NULL
        || context->ledger->allocator == NULL
        || context->objects == NULL
    ) {
        return false;
    }
    micros_frame_ownership_test_copy_bytes(
        &context->ownership_state_snapshot,
        context->ledger,
        sizeof(context->ownership_state_snapshot)
    );
    micros_frame_ownership_test_copy_bytes(
        &context->allocator_state_snapshot,
        context->ledger->allocator,
        sizeof(context->allocator_state_snapshot)
    );
    micros_frame_ownership_test_copy_bytes(
        &context->object_state_snapshot,
        context->objects,
        sizeof(context->object_state_snapshot)
    );
    return true;
}

bool micros_frame_ownership_test_runtime_state_matches_snapshot(
    const struct micros_frame_ownership_test_context *context
)
{
    return (
        context != NULL
        && context->ledger != NULL
        && context->ledger->allocator != NULL
        && context->objects != NULL
        && micros_frame_ownership_test_bytes_equal(
            context->ledger,
            &context->ownership_state_snapshot,
            sizeof(context->ownership_state_snapshot)
        )
        && micros_frame_ownership_test_bytes_equal(
            context->ledger->allocator,
            &context->allocator_state_snapshot,
            sizeof(context->allocator_state_snapshot)
        )
        && micros_frame_ownership_test_bytes_equal(
            context->objects,
            &context->object_state_snapshot,
            sizeof(context->object_state_snapshot)
        )
    );
}

bool micros_frame_ownership_test_owners_equal(
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

bool micros_frame_ownership_test_frame_index_for_address(
    const struct micros_frame_ownership *ledger,
    uint64_t physical_address,
    uint64_t *frame_index
)
{
    size_t range_index;

    if (
        ledger == NULL
        || frame_index == NULL
        || physical_address % MICROS_FRAME_SIZE != 0
    ) {
        return false;
    }
    for (
        range_index = 0;
        range_index < ledger->managed_range_count;
        ++range_index
    ) {
        const struct micros_frame_range_snapshot *range =
            &ledger->managed_ranges[range_index];
        uint64_t offset;
        uint64_t index_in_range;

        if (physical_address < range->base) {
            continue;
        }
        offset = physical_address - range->base;
        index_in_range = offset / MICROS_FRAME_SIZE;
        if (
            offset % MICROS_FRAME_SIZE == 0
            && index_in_range < range->frame_count
        ) {
            *frame_index = range->bitmap_offset + index_in_range;
            return *frame_index < ledger->managed_frame_count;
        }
    }
    return false;
}

bool micros_frame_ownership_test_frame_state_matches(
    const struct micros_frame_ownership *ledger,
    uint64_t frame_index,
    struct micros_frame_owner expected_owner,
    enum micros_frame_handoff_target expected_target,
    uint64_t expected_owned,
    const uint64_t *expected_counts
)
{
    size_t kind;

    if (
        ledger == NULL
        || expected_counts == NULL
        || frame_index >= ledger->managed_frame_count
        || !micros_frame_ownership_test_owners_equal(
            ledger->owners[frame_index],
            expected_owner
        )
        || ledger->handoff_targets[frame_index] != expected_target
        || ledger->owned_frame_count != expected_owned
    ) {
        return false;
    }
    for (kind = 0; kind < MICROS_FRAME_OWNER_KIND_COUNT; ++kind) {
        if (ledger->owner_counts[kind] != expected_counts[kind]) {
            return false;
        }
    }
    return true;
}
