#include "kernel/memory/frame_ownership_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static struct micros_frame_owner resulting_owner(
    struct micros_frame_owner owner,
    uint8_t target
)
{
    if (target == MICROS_FRAME_HANDOFF_VM_WIRED) {
        owner.kind = MICROS_FRAME_OWNER_VM_WIRED;
    } else if (target == MICROS_FRAME_HANDOFF_VM_TRANSFERABLE) {
        owner = (struct micros_frame_owner){
            0,
            0,
            MICROS_FRAME_OWNER_VM_TRANSFERABLE,
            0,
        };
    }
    return owner;
}

enum micros_frame_ownership_error
micros_frame_ownership_prepare_handoff(
    struct micros_frame_ownership *ownership,
    uint64_t physical_address,
    struct micros_frame_owner expected_owner,
    enum micros_frame_handoff_target target
)
{
    struct micros_frame_owner stored_owner;
    uint64_t frame_index;
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
    error = micros_frame_ownership_validate_metadata(ownership);
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    error = micros_frame_ownership_physical_address_to_index(
        ownership,
        physical_address,
        &frame_index
    );
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    error = micros_frame_ownership_selected_record(ownership, frame_index, &stored_owner);
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
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
        target >= MICROS_FRAME_HANDOFF_TARGET_COUNT
        || !micros_frame_ownership_handoff_target_is_valid(stored_owner, (uint8_t)target)
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION;
    }
    ownership->handoff_targets[frame_index] = (uint8_t)target;
    return MICROS_FRAME_OWNERSHIP_OK;
}

enum micros_frame_ownership_error
micros_frame_ownership_prepare_wired_process_user_set(
    struct micros_frame_ownership *ownership,
    const struct micros_frame_selection *selection
)
{
    uint64_t selected_count = 0;
    struct micros_frame_owner owner;
    uint64_t frame_index;
    uint64_t word;
    size_t position;
    size_t word_index;
    unsigned bit_index;
    enum micros_frame_ownership_error error;

    error = micros_frame_ownership_require_initialized(ownership);
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    if (ownership->phase != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP) {
        return MICROS_FRAME_OWNERSHIP_ERROR_PHASE;
    }
    if (
        selection == NULL
        || selection->word_count > MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        || selection->frame_count > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES
        || (selection->word_count == 0) != (selection->frame_count == 0)
        || selection->word_count > selection->frame_count
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    error = micros_frame_ownership_validate_active(ownership);
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    error = micros_frame_ownership_check_selection(selection, ownership->managed_frame_count);
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }

    for (position = 0; position < selection->word_count; ++position) {
        word_index = selection->words[position];
        word = selection->bitmap[word_index];
        for (bit_index = 0; bit_index < 64; ++bit_index) {
            if ((word & (UINT64_C(1) << bit_index)) == 0) {
                continue;
            }
            frame_index = (uint64_t)word_index * 64 + bit_index;
            owner = ownership->owners[frame_index];
            if (owner.kind == MICROS_FRAME_OWNER_FREE) {
                return MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED;
            }
            if (owner.kind != MICROS_FRAME_OWNER_PROCESS_USER) {
                return MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
            }
            if (ownership->handoff_targets[frame_index] != MICROS_FRAME_HANDOFF_NONE) {
                return MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION;
            }
            ++selected_count;
        }
    }
    if (selected_count != ownership->owner_counts[MICROS_FRAME_OWNER_PROCESS_USER]) {
        return MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
    }

    for (position = 0; position < selection->word_count; ++position) {
        word_index = selection->words[position];
        word = selection->bitmap[word_index];
        for (bit_index = 0; bit_index < 64; ++bit_index) {
            if ((word & (UINT64_C(1) << bit_index)) == 0) {
                continue;
            }
            frame_index = (uint64_t)word_index * 64 + bit_index;
            ownership->handoff_targets[frame_index] = MICROS_FRAME_HANDOFF_VM_WIRED;
        }
    }
    return MICROS_FRAME_OWNERSHIP_OK;
}

enum micros_frame_ownership_error
micros_frame_ownership_complete_handoff(
    struct micros_frame_ownership *ownership
)
{
    uint64_t resulting_counts[MICROS_FRAME_OWNER_KIND_COUNT];
    uint64_t resulting_owned_count = 0;
    uint64_t frame_index;
    enum micros_frame_ownership_error error;

    micros_frame_ownership_clear_bytes(resulting_counts, sizeof(resulting_counts));
    error = micros_frame_ownership_require_initialized(ownership);
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    if (ownership->phase != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP) {
        return MICROS_FRAME_OWNERSHIP_ERROR_PHASE;
    }
    error = micros_frame_ownership_validate_active(ownership);
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }

    for (
        frame_index = 0;
        frame_index < ownership->managed_frame_count;
        ++frame_index
    ) {
        struct micros_frame_owner owner =
            ownership->owners[frame_index];
        uint8_t target = ownership->handoff_targets[frame_index];
        struct micros_frame_owner result;

        if (
            owner.kind == MICROS_FRAME_OWNER_KERNEL_TEMPORARY
            || (
                owner.kind == MICROS_FRAME_OWNER_PROCESS_USER
                && target == MICROS_FRAME_HANDOFF_NONE
            )
        ) {
            return MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION;
        }
        result = resulting_owner(owner, target);
        if (!micros_frame_owner_is_valid(result)) {
            return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
        }
        if (result.kind != MICROS_FRAME_OWNER_FREE) {
            if (
                resulting_owned_count
                    == ownership->managed_frame_count
                || resulting_counts[result.kind]
                    == ownership->managed_frame_count
            ) {
                return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
            }
            ++resulting_owned_count;
            ++resulting_counts[result.kind];
        }
    }
    if (resulting_owned_count != ownership->owned_frame_count) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }

    for (
        frame_index = 0;
        frame_index < ownership->managed_frame_count;
        ++frame_index
    ) {
        if (micros_frame_owner_kind_is_process_bound(ownership->owners[frame_index].kind)
            && ownership->handoff_targets[frame_index] == MICROS_FRAME_HANDOFF_VM_TRANSFERABLE) {
            micros_frame_ownership_index_remove_prevalidated(ownership, frame_index);
        }
        ownership->owners[frame_index] = resulting_owner(
            ownership->owners[frame_index],
            ownership->handoff_targets[frame_index]
        );
        ownership->handoff_targets[frame_index] =
            MICROS_FRAME_HANDOFF_NONE;
    }
    for (
        frame_index = 0;
        frame_index < MICROS_FRAME_OWNER_KIND_COUNT;
        ++frame_index
    ) {
        ownership->owner_counts[frame_index] =
            resulting_counts[frame_index];
    }
    ownership->owned_frame_count = resulting_owned_count;
    ownership->phase = MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF;
    return MICROS_FRAME_OWNERSHIP_OK;
}
