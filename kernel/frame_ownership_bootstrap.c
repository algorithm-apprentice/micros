#include "frame_ownership_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool owner_is_allocatable(struct micros_frame_owner owner)
{
    if (!micros_frame_owner_is_valid(owner)) {
        return false;
    }
    return (
        owner.kind == MICROS_FRAME_OWNER_KERNEL_RETAINED
        || owner.kind == MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE
        || owner.kind == MICROS_FRAME_OWNER_KERNEL_TEMPORARY
        || owner.kind == MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
        || owner.kind == MICROS_FRAME_OWNER_PROCESS_USER
    );
}
enum micros_frame_ownership_error micros_frame_ownership_allocate(
    struct micros_frame_ownership *ownership,
    struct micros_frame_owner owner,
    uint64_t *physical_address
)
{
    uint64_t allocated_address;
    uint64_t frame_index;
    enum micros_frame_allocator_error allocator_error;
    enum micros_frame_ownership_error error;

    if (physical_address == NULL) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    error = micros_frame_ownership_require_initialized(ownership);
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    if (ownership->phase != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP) {
        return MICROS_FRAME_OWNERSHIP_ERROR_PHASE;
    }
    if (!owner_is_allocatable(owner)) {
        return MICROS_FRAME_OWNERSHIP_ERROR_OWNER;
    }
    if (!micros_frame_ownership_geometry_matches(ownership)) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }

    allocator_error = micros_frame_allocator_allocate(
        ownership->allocator,
        &allocated_address
    );
    if (allocator_error == MICROS_FRAME_ALLOCATOR_ERROR_EXHAUSTED) {
        return MICROS_FRAME_OWNERSHIP_ERROR_EXHAUSTED;
    }
    if (allocator_error != MICROS_FRAME_ALLOCATOR_OK) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    error = micros_frame_ownership_physical_address_to_index(
        ownership,
        allocated_address,
        &frame_index
    );
    if (
        error != MICROS_FRAME_OWNERSHIP_OK
        || frame_index >= ownership->managed_frame_count
        || ownership->owners[frame_index].kind
            != MICROS_FRAME_OWNER_FREE
        || !micros_frame_owner_is_valid(ownership->owners[frame_index])
        || ownership->handoff_targets[frame_index]
            != MICROS_FRAME_HANDOFF_NONE
        || ownership->owned_frame_count
            >= ownership->managed_frame_count
        || ownership->owner_counts[owner.kind]
            >= ownership->managed_frame_count
    ) {
        (void)micros_frame_allocator_release(
            ownership->allocator,
            allocated_address
        );
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }

    ownership->owners[frame_index] = owner;
    ownership->handoff_targets[frame_index] =
        MICROS_FRAME_HANDOFF_NONE;
    ++ownership->owned_frame_count;
    ++ownership->owner_counts[owner.kind];
    *physical_address = allocated_address;
    return MICROS_FRAME_OWNERSHIP_OK;
}

enum micros_frame_ownership_error micros_frame_ownership_release(
    struct micros_frame_ownership *ownership,
    struct micros_frame_owner expected_owner,
    uint64_t physical_address
)
{
    struct micros_frame_owner stored_owner;
    uint64_t frame_index;
    enum micros_frame_allocator_error allocator_error;
    enum micros_frame_ownership_error error;

    error = micros_frame_ownership_require_initialized(ownership);
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    if (ownership->phase != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP) {
        return MICROS_FRAME_OWNERSHIP_ERROR_PHASE;
    }
    if (!micros_frame_owner_is_valid(expected_owner)) {
        return MICROS_FRAME_OWNERSHIP_ERROR_OWNER;
    }
    if (!micros_frame_ownership_geometry_matches(ownership)) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    error = micros_frame_ownership_physical_address_to_index(
        ownership,
        physical_address,
        &frame_index
    );
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    stored_owner = ownership->owners[frame_index];
    if (stored_owner.kind == MICROS_FRAME_OWNER_FREE) {
        return MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED;
    }
    if (!micros_frame_owner_is_valid(stored_owner)) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    if (!micros_frame_owner_is_equal(stored_owner, expected_owner)) {
        return MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
    }
    if (
        ownership->owned_frame_count == 0
        || ownership->owner_counts[stored_owner.kind] == 0
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }

    allocator_error = micros_frame_allocator_release(
        ownership->allocator,
        physical_address
    );
    if (allocator_error != MICROS_FRAME_ALLOCATOR_OK) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    ownership->owners[frame_index] =
        (struct micros_frame_owner){0, 0, 0, 0};
    ownership->handoff_targets[frame_index] =
        MICROS_FRAME_HANDOFF_NONE;
    --ownership->owned_frame_count;
    --ownership->owner_counts[stored_owner.kind];
    return MICROS_FRAME_OWNERSHIP_OK;
}
struct process_release_summary {
    uint64_t frame_count;
    uint64_t owner_counts[MICROS_FRAME_OWNER_KIND_COUNT];
};

static enum micros_frame_ownership_error
collect_process_release_summary(
    const struct micros_frame_ownership *ownership,
    struct micros_process_handle process,
    const uint64_t *release_bitmap,
    struct process_release_summary *summary
)
{
    uint64_t frame_index;

    for (
        frame_index = 0;
        frame_index < MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES;
        ++frame_index
    ) {
        size_t word_index = (size_t)(frame_index / 64);
        uint64_t bit = UINT64_C(1) << (frame_index % 64);
        struct micros_frame_owner owner;

        if ((release_bitmap[word_index] & bit) == 0) {
            continue;
        }
        if (frame_index >= ownership->managed_frame_count) {
            return MICROS_FRAME_OWNERSHIP_ERROR_UNMANAGED;
        }
        owner = ownership->owners[frame_index];
        if (owner.kind == MICROS_FRAME_OWNER_FREE) {
            return MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED;
        }
        if (
            (
                owner.kind
                    != MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
                && owner.kind != MICROS_FRAME_OWNER_PROCESS_USER
            )
            || owner.slot != process.slot
            || owner.generation != process.generation
        ) {
            return MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
        }
        if (
            summary->frame_count == ownership->managed_frame_count
            || summary->owner_counts[owner.kind]
                == ownership->managed_frame_count
        ) {
            return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
        }
        ++summary->frame_count;
        ++summary->owner_counts[owner.kind];
    }
    return MICROS_FRAME_OWNERSHIP_OK;
}

static bool process_release_counts_are_valid(
    const struct micros_frame_ownership *ownership,
    const struct process_release_summary *summary
)
{
    uint64_t owner_kind;

    if (
        summary->frame_count > ownership->owned_frame_count
        || summary->frame_count
            > ownership->managed_frame_count
                - ownership->allocator->free_frame_count
    ) {
        return false;
    }
    for (
        owner_kind = 0;
        owner_kind < MICROS_FRAME_OWNER_KIND_COUNT;
        ++owner_kind
    ) {
        if (
            summary->owner_counts[owner_kind]
                > ownership->owner_counts[owner_kind]
        ) {
            return false;
        }
    }
    return true;
}

static void commit_process_release(
    struct micros_frame_ownership *ownership,
    const uint64_t *release_bitmap,
    const struct process_release_summary *summary
)
{
    uint64_t frame_index;

    for (
        frame_index = 0;
        frame_index < ownership->managed_frame_count;
        ++frame_index
    ) {
        size_t word_index = (size_t)(frame_index / 64);
        uint64_t bit = UINT64_C(1) << (frame_index % 64);

        if ((release_bitmap[word_index] & bit) == 0) {
            continue;
        }
        ownership->allocator->allocated_bitmap[word_index] &= ~bit;
        ownership->owners[frame_index] =
            (struct micros_frame_owner){0, 0, 0, 0};
        ownership->handoff_targets[frame_index] =
            MICROS_FRAME_HANDOFF_NONE;
    }
    ownership->allocator->free_frame_count += summary->frame_count;
    ownership->owned_frame_count -= summary->frame_count;
    for (
        frame_index = 0;
        frame_index < MICROS_FRAME_OWNER_KIND_COUNT;
        ++frame_index
    ) {
        ownership->owner_counts[frame_index] -=
            summary->owner_counts[frame_index];
    }
}

enum micros_frame_ownership_error
micros_frame_ownership_release_process_set(
    struct micros_frame_ownership *ownership,
    struct micros_process_handle process,
    const uint64_t *release_bitmap,
    size_t release_word_count
)
{
    struct process_release_summary summary;
    enum micros_frame_ownership_error error;

    micros_frame_ownership_clear_bytes(&summary, sizeof(summary));
    error = micros_frame_ownership_require_initialized(ownership);
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    if (ownership->phase != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP) {
        return MICROS_FRAME_OWNERSHIP_ERROR_PHASE;
    }
    if (
        release_bitmap == NULL
        || release_word_count
            != MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    if (!micros_frame_process_handle_is_valid(process)) {
        return MICROS_FRAME_OWNERSHIP_ERROR_OWNER;
    }
    error = micros_frame_ownership_validate(ownership);
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    error = collect_process_release_summary(
        ownership,
        process,
        release_bitmap,
        &summary
    );
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    if (!process_release_counts_are_valid(ownership, &summary)) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    commit_process_release(ownership, release_bitmap, &summary);
    return MICROS_FRAME_OWNERSHIP_OK;
}
