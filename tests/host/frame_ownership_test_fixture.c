#include "frame_ownership_test_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static struct frame_ownership_test_context test_context;

struct frame_ownership_test_context *
frame_ownership_test_context(void)
{
    return &test_context;
}

bool frame_ownership_test_owners_equal(
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

struct micros_frame_owner frame_ownership_test_raw_kernel_owner(
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

struct micros_frame_owner frame_ownership_test_raw_process_owner(
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

bool frame_ownership_test_initialize_fixture(
    struct frame_ownership_test_context *context,
    uint64_t frame_count
)
{
    struct micros_physical_range range;

    if (
        frame_count == 0
        || frame_count > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES
    ) {
        return false;
    }
    memset(&context->allocator, 0, sizeof(context->allocator));
    memset(&context->ownership, 0, sizeof(context->ownership));
    range.base = TEST_BASE;
    range.size = frame_count * MICROS_FRAME_SIZE;
    return (
        micros_frame_allocator_initialize(
            &context->allocator,
            &range,
            1,
            NULL,
            0
        ) == MICROS_FRAME_ALLOCATOR_OK
        && micros_frame_ownership_initialize(
            &context->ownership,
            &context->allocator
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_frame_ownership_validate(&context->ownership)
            == MICROS_FRAME_OWNERSHIP_OK
    );
}

void frame_ownership_test_clear_release_bitmap(
    struct frame_ownership_test_context *context
)
{
    memset(context->release_bitmap, 0, sizeof(context->release_bitmap));
}

void frame_ownership_test_select_release_index(
    struct frame_ownership_test_context *context,
    uint64_t frame_index
)
{
    context->release_bitmap[frame_index / 64]
        |= UINT64_C(1) << (frame_index % 64);
}

void frame_ownership_test_snapshot(
    struct frame_ownership_test_context *context
)
{
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
}

bool frame_ownership_test_snapshot_matches(
    const struct frame_ownership_test_context *context
)
{
    return (
        memcmp(
            &context->ownership,
            &context->ownership_snapshot,
            sizeof(context->ownership)
        ) == 0
        && memcmp(
            &context->allocator,
            &context->allocator_snapshot,
            sizeof(context->allocator)
        ) == 0
    );
}
