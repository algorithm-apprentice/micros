#ifndef MICROS_FRAME_OWNERSHIP_RUNTIME_H
#define MICROS_FRAME_OWNERSHIP_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "micros/frame_ownership.h"

enum micros_frame_ownership_error
micros_frame_ownership_runtime_initialize(void);

const struct micros_frame_ownership *
micros_frame_ownership_runtime_ledger(void);

enum micros_frame_ownership_error
micros_frame_ownership_runtime_allocate(
    struct micros_frame_owner owner,
    uint64_t *physical_address
);

enum micros_frame_ownership_error
micros_frame_ownership_runtime_release(
    struct micros_frame_owner expected_owner,
    uint64_t physical_address
);

enum micros_frame_ownership_error
micros_frame_ownership_runtime_prepare_handoff(
    uint64_t physical_address,
    struct micros_frame_owner expected_owner,
    enum micros_frame_handoff_target target
);

enum micros_frame_ownership_error
micros_frame_ownership_runtime_prepare_wired_process_user_set(
    const uint64_t *selected_bitmap,
    size_t selected_word_count
);

enum micros_frame_ownership_error
micros_frame_ownership_runtime_release_process_set(
    struct micros_process_handle process,
    const uint64_t *release_bitmap,
    size_t release_word_count
);

enum micros_frame_ownership_error
micros_frame_ownership_runtime_complete_handoff(
    const struct micros_kernel_objects *objects
);

enum micros_frame_ownership_error
micros_frame_ownership_runtime_validate(
    const struct micros_kernel_objects *objects
);

enum micros_frame_ownership_error
micros_frame_ownership_runtime_vm_claim_preflight(
    uint64_t physical_address,
    struct micros_frame_vm_claim_plan *plan
);

void
micros_frame_ownership_runtime_vm_claim_commit_prevalidated(
    const struct micros_frame_vm_claim_plan *plan
);

enum micros_frame_ownership_error
micros_frame_ownership_runtime_vm_release_transferable_preflight(
    const uint64_t *physical_addresses,
    size_t physical_address_count,
    struct micros_frame_vm_transferable_release_plan *plan
);

void
micros_frame_ownership_runtime_vm_release_transferable_commit_prevalidated(
    const struct micros_frame_vm_transferable_release_plan *plan
);

enum micros_frame_ownership_error
micros_frame_ownership_runtime_vm_freeze_preflight(
    struct micros_process_handle process,
    const uint64_t *data_physical_addresses,
    size_t data_physical_address_count,
    const uint64_t
        table_physical_addresses[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
    struct micros_frame_vm_freeze_plan *plan
);

void
micros_frame_ownership_runtime_vm_freeze_commit_prevalidated(
    const struct micros_frame_vm_freeze_plan *plan
);

enum micros_frame_ownership_error
micros_frame_ownership_runtime_vm_release_process_preflight(
    struct micros_process_handle process,
    const uint64_t *data_physical_addresses,
    size_t data_physical_address_count,
    const uint64_t
        table_physical_addresses[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
    struct micros_frame_vm_process_release_plan *plan
);

void
micros_frame_ownership_runtime_vm_release_process_commit_prevalidated(
    const struct micros_frame_vm_process_release_plan *plan
);

enum micros_kernel_object_error
micros_frame_ownership_runtime_release_process(
    struct micros_kernel_objects *objects,
    struct micros_process_handle process
);

#endif
