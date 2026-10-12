#include "frame_ownership_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum micros_frame_ownership_error
micros_frame_ownership_vm_claim_preflight(
    const struct micros_frame_ownership *ownership,
    uint64_t physical_address,
    struct micros_frame_vm_claim_plan *plan
)
{
    struct micros_frame_vm_claim_plan candidate;
    uint64_t frame_index;
    enum micros_frame_ownership_error error;

    if (plan == NULL) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    error = micros_frame_ownership_vm_transition_preflight(ownership);
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
    if (
        ownership->owners[frame_index].kind
            != MICROS_FRAME_OWNER_FREE
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
    }
    if (
        ownership->allocator->free_frame_count == 0
        || ownership->owned_frame_count
            == ownership->managed_frame_count
        || ownership->owner_counts[
            MICROS_FRAME_OWNER_VM_TRANSFERABLE
        ] == ownership->managed_frame_count
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    candidate.frame_index = frame_index;
    micros_frame_ownership_copy_bytes(plan, &candidate, sizeof(candidate));
    return MICROS_FRAME_OWNERSHIP_OK;
}

void micros_frame_ownership_vm_claim_commit_prevalidated(
    struct micros_frame_ownership *ownership,
    const struct micros_frame_vm_claim_plan *plan
)
{
    const struct micros_frame_owner transferable = {
        0,
        0,
        MICROS_FRAME_OWNER_VM_TRANSFERABLE,
        0,
    };

    micros_frame_ownership_allocator_mark_allocated(
        ownership->allocator,
        plan->frame_index
    );
    ownership->owners[plan->frame_index] = transferable;
    ownership->handoff_targets[plan->frame_index] =
        MICROS_FRAME_HANDOFF_NONE;
    --ownership->allocator->free_frame_count;
    ++ownership->owned_frame_count;
    ++ownership->owner_counts[
        MICROS_FRAME_OWNER_VM_TRANSFERABLE
    ];
}
