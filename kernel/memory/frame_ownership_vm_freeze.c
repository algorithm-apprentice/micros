#include "kernel/memory/frame_ownership_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum micros_frame_ownership_error
micros_frame_ownership_vm_freeze_preflight(
    const struct micros_frame_ownership *ownership,
    struct micros_process_handle process,
    const uint64_t *data_physical_addresses,
    size_t data_physical_address_count,
    const uint64_t
        table_physical_addresses[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
    struct micros_frame_vm_freeze_plan *plan
)
{
    struct micros_frame_vm_freeze_plan candidate;
    enum micros_frame_ownership_error error;

    if (plan == NULL) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    micros_frame_ownership_clear_bytes(&candidate, sizeof(candidate));
    error = micros_frame_ownership_vm_process_set_preflight(
        ownership,
        process,
        data_physical_addresses,
        data_physical_address_count,
        table_physical_addresses,
        MICROS_FRAME_OWNER_VM_TRANSFERABLE,
        MICROS_FRAME_OWNER_FREE,
        candidate.data_frame_indices,
        candidate.table_frame_indices
    );
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    if (
        data_physical_address_count
            > ownership->owner_counts[
                MICROS_FRAME_OWNER_VM_TRANSFERABLE
            ]
        || MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT
            > ownership->allocator->free_frame_count
        || MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT
            > ownership->managed_frame_count
                - ownership->owned_frame_count
        || data_physical_address_count
            > ownership->managed_frame_count
                - ownership->owner_counts[
                    MICROS_FRAME_OWNER_VM_WIRED
                ]
        || MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT
            > ownership->managed_frame_count
                - ownership->owner_counts[
                    MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
                ]
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    candidate.process = process;
    candidate.data_frame_count =
        (uint32_t)data_physical_address_count;
    micros_frame_ownership_copy_bytes(plan, &candidate, sizeof(candidate));
    return MICROS_FRAME_OWNERSHIP_OK;
}

void micros_frame_ownership_vm_freeze_commit_prevalidated(
    struct micros_frame_ownership *ownership,
    const struct micros_frame_vm_freeze_plan *plan
)
{
    const struct micros_frame_owner wired = {
        plan->process.generation,
        plan->process.slot,
        MICROS_FRAME_OWNER_VM_WIRED,
        0,
    };
    const struct micros_frame_owner table = {
        plan->process.generation,
        plan->process.slot,
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
        0,
    };
    size_t index;

    for (index = 0; index < plan->data_frame_count; ++index) {
        ownership->owners[plan->data_frame_indices[index]] = wired;
    }
    for (
        index = 0;
        index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        uint64_t frame_index = plan->table_frame_indices[index];

        micros_frame_ownership_allocator_mark_allocated(ownership->allocator, frame_index);
        ownership->owners[frame_index] = table;
        ownership->handoff_targets[frame_index] =
            MICROS_FRAME_HANDOFF_NONE;
    }
    ownership->allocator->free_frame_count -=
        MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
    ownership->owned_frame_count +=
        MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
    ownership->owner_counts[
        MICROS_FRAME_OWNER_VM_TRANSFERABLE
    ] -= plan->data_frame_count;
    ownership->owner_counts[MICROS_FRAME_OWNER_VM_WIRED] +=
        plan->data_frame_count;
    ownership->owner_counts[
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
    ] += MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
}
