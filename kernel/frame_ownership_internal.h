#ifndef MICROS_FRAME_OWNERSHIP_INTERNAL_H
#define MICROS_FRAME_OWNERSHIP_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/frame_ownership.h"

void micros_frame_ownership_clear_bytes(void *storage, size_t size);

void micros_frame_ownership_copy_bytes(
    void *destination,
    const void *source,
    size_t size
);

bool micros_frame_owner_is_equal(
    struct micros_frame_owner left,
    struct micros_frame_owner right
);

bool micros_frame_process_handle_is_valid(
    struct micros_process_handle process
);

bool micros_frame_owner_kind_is_process_bound(uint8_t kind);

bool micros_frame_owner_is_valid(struct micros_frame_owner owner);

enum micros_frame_ownership_error
micros_frame_ownership_require_initialized(
    const struct micros_frame_ownership *ownership
);

bool micros_frame_ownership_geometry_matches(
    const struct micros_frame_ownership *ownership
);

enum micros_frame_ownership_error
micros_frame_ownership_physical_address_to_index(
    const struct micros_frame_ownership *ownership,
    uint64_t physical_address,
    uint64_t *frame_index
);

bool micros_frame_ownership_allocator_bit_is_set(
    const struct micros_frame_allocator *allocator,
    uint64_t frame_index
);

bool micros_frame_ownership_handoff_target_is_valid(
    struct micros_frame_owner owner,
    uint8_t target
);

void micros_frame_ownership_allocator_mark_allocated(
    struct micros_frame_allocator *allocator,
    uint64_t frame_index
);

void micros_frame_ownership_allocator_mark_free(
    struct micros_frame_allocator *allocator,
    uint64_t frame_index
);

enum micros_frame_ownership_error
micros_frame_ownership_vm_transition_preflight(
    const struct micros_frame_ownership *ownership
);

bool micros_frame_ownership_frame_index_is_duplicate(
    const uint64_t *frame_indices,
    size_t frame_index_count,
    uint64_t frame_index
);

enum micros_frame_ownership_error
micros_frame_ownership_vm_process_set_preflight(
    const struct micros_frame_ownership *ownership,
    struct micros_process_handle process,
    const uint64_t *data_physical_addresses,
    size_t data_physical_address_count,
    const uint64_t
        table_physical_addresses[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
    enum micros_frame_owner_kind data_owner_kind,
    enum micros_frame_owner_kind table_owner_kind,
    uint64_t
        data_frame_indices[MICROS_APPLICATION_RESIDENT_PAGE_MAX],
    uint64_t
        table_frame_indices[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT]
);

#endif
