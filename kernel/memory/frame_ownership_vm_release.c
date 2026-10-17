#include "kernel/memory/frame_ownership_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum micros_frame_ownership_error
micros_frame_ownership_vm_release_transferable_preflight(
    const struct micros_frame_ownership *ownership,
    const uint64_t *physical_addresses,
    size_t physical_address_count,
    struct micros_frame_vm_transferable_release_plan *plan
)
{
    struct micros_frame_vm_transferable_release_plan candidate;
    enum micros_frame_ownership_error error;
    size_t index;

    if (
        plan == NULL
        || physical_address_count
            > MICROS_APPLICATION_RESIDENT_PAGE_MAX
        || (
            physical_address_count != 0
            && physical_addresses == NULL
        )
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    micros_frame_ownership_clear_bytes(&candidate, sizeof(candidate));
    error = micros_frame_ownership_vm_transition_preflight(ownership);
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    for (index = 0; index < physical_address_count; ++index) {
        uint64_t frame_index;
        struct micros_frame_owner owner;

        error = micros_frame_ownership_physical_address_to_index(
            ownership,
            physical_addresses[index],
            &frame_index
        );
        if (error != MICROS_FRAME_OWNERSHIP_OK) {
            return error;
        }
        if (micros_frame_ownership_frame_index_is_duplicate(
            candidate.frame_indices,
            index,
            frame_index
        )) {
            return MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION;
        }
        owner = ownership->owners[frame_index];
        if (owner.kind == MICROS_FRAME_OWNER_FREE) {
            return MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED;
        }
        if (
            !micros_frame_owner_is_equal(
                owner,
                (struct micros_frame_owner){
                    0,
                    0,
                    MICROS_FRAME_OWNER_VM_TRANSFERABLE,
                    0,
                }
            )
        ) {
            return MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
        }
        candidate.frame_indices[index] = frame_index;
    }
    if (
        physical_address_count > ownership->owned_frame_count
        || physical_address_count
            > ownership->owner_counts[
                MICROS_FRAME_OWNER_VM_TRANSFERABLE
            ]
        || physical_address_count
            > ownership->managed_frame_count
                - ownership->allocator->free_frame_count
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    candidate.frame_count = (uint32_t)physical_address_count;
    micros_frame_ownership_copy_bytes(plan, &candidate, sizeof(candidate));
    return MICROS_FRAME_OWNERSHIP_OK;
}

void micros_frame_ownership_vm_release_transferable_commit_prevalidated(
    struct micros_frame_ownership *ownership,
    const struct micros_frame_vm_transferable_release_plan *plan
)
{
    size_t index;

    for (index = 0; index < plan->frame_count; ++index) {
        uint64_t frame_index = plan->frame_indices[index];

        micros_frame_ownership_allocator_mark_free(ownership->allocator, frame_index);
        ownership->owners[frame_index] =
            (struct micros_frame_owner){0, 0, 0, 0};
        ownership->handoff_targets[frame_index] =
            MICROS_FRAME_HANDOFF_NONE;
    }
    ownership->allocator->free_frame_count += plan->frame_count;
    ownership->owned_frame_count -= plan->frame_count;
    ownership->owner_counts[
        MICROS_FRAME_OWNER_VM_TRANSFERABLE
    ] -= plan->frame_count;
}

enum micros_frame_ownership_error
micros_frame_ownership_vm_release_process_preflight(
    const struct micros_frame_ownership *ownership,
    struct micros_process_handle process,
    const uint64_t *data_physical_addresses,
    size_t data_physical_address_count,
    const uint64_t
        table_physical_addresses[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
    struct micros_frame_vm_process_release_plan *plan
)
{
    struct micros_frame_vm_process_release_plan candidate;
    enum micros_frame_ownership_error error;
    size_t release_count;

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
        MICROS_FRAME_OWNER_VM_WIRED,
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
        candidate.data_frame_indices,
        candidate.table_frame_indices
    );
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    release_count = data_physical_address_count
        + MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
    if (
        data_physical_address_count
            > ownership->owner_counts[
                MICROS_FRAME_OWNER_VM_WIRED
            ]
        || MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT
            > ownership->owner_counts[
                MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
            ]
        || release_count > ownership->owned_frame_count
        || release_count
            > ownership->managed_frame_count
                - ownership->allocator->free_frame_count
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    candidate.process = process;
    candidate.data_frame_count =
        (uint32_t)data_physical_address_count;
    micros_frame_ownership_copy_bytes(plan, &candidate, sizeof(candidate));
    return MICROS_FRAME_OWNERSHIP_OK;
}

void micros_frame_ownership_vm_release_process_commit_prevalidated(
    struct micros_frame_ownership *ownership,
    const struct micros_frame_vm_process_release_plan *plan
)
{
    size_t release_count = plan->data_frame_count
        + MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
    size_t index;

    for (index = 0; index < plan->data_frame_count; ++index) {
        uint64_t frame_index = plan->data_frame_indices[index];

        micros_frame_ownership_allocator_mark_free(ownership->allocator, frame_index);
        ownership->owners[frame_index] =
            (struct micros_frame_owner){0, 0, 0, 0};
        ownership->handoff_targets[frame_index] =
            MICROS_FRAME_HANDOFF_NONE;
    }
    for (
        index = 0;
        index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        uint64_t frame_index = plan->table_frame_indices[index];

        micros_frame_ownership_allocator_mark_free(ownership->allocator, frame_index);
        ownership->owners[frame_index] =
            (struct micros_frame_owner){0, 0, 0, 0};
        ownership->handoff_targets[frame_index] =
            MICROS_FRAME_HANDOFF_NONE;
    }
    ownership->allocator->free_frame_count += release_count;
    ownership->owned_frame_count -= release_count;
    ownership->owner_counts[MICROS_FRAME_OWNER_VM_WIRED] -=
        plan->data_frame_count;
    ownership->owner_counts[
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
    ] -= MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
}
