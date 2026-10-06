#ifndef MICROS_FRAME_OWNERSHIP_H
#define MICROS_FRAME_OWNERSHIP_H

#include <stddef.h>
#include <stdint.h>

#include "micros/frame_allocator.h"
#include "micros/kernel_objects.h"

enum {
    MICROS_FRAME_OWNER_KIND_COUNT = 8,
    MICROS_FRAME_HANDOFF_TARGET_COUNT = 3,
};

enum micros_frame_owner_kind {
    MICROS_FRAME_OWNER_FREE = 0,
    MICROS_FRAME_OWNER_KERNEL_RETAINED,
    MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE,
    MICROS_FRAME_OWNER_KERNEL_TEMPORARY,
    MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
    MICROS_FRAME_OWNER_PROCESS_USER,
    MICROS_FRAME_OWNER_VM_WIRED,
    MICROS_FRAME_OWNER_VM_TRANSFERABLE,
};

enum micros_frame_ownership_phase {
    MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP = 1,
    MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF,
};

enum micros_frame_handoff_target {
    MICROS_FRAME_HANDOFF_NONE = 0,
    MICROS_FRAME_HANDOFF_VM_WIRED,
    MICROS_FRAME_HANDOFF_VM_TRANSFERABLE,
};

struct micros_frame_owner {
    uint32_t generation;
    uint16_t slot;
    uint8_t kind;
    uint8_t reserved;
};

_Static_assert(
    sizeof(struct micros_frame_owner) == 8,
    "frame ownership records must remain exactly eight bytes"
);

struct micros_frame_range_snapshot {
    uint64_t base;
    uint64_t frame_count;
    uint64_t bitmap_offset;
};

struct micros_frame_ownership {
    uint64_t initialization_magic;
    struct micros_frame_allocator *allocator;
    uintptr_t allocator_identity;
    enum micros_frame_ownership_phase phase;
    size_t managed_range_count;
    uint64_t managed_frame_count;
    uint64_t owned_frame_count;
    uint64_t owner_counts[MICROS_FRAME_OWNER_KIND_COUNT];
    struct micros_frame_range_snapshot
        managed_ranges[MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES];
    struct micros_frame_owner
        owners[MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES];
    uint8_t
        handoff_targets[MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES];
};

_Static_assert(
    sizeof(((struct micros_frame_ownership *)0)->handoff_targets[0]) == 1,
    "handoff targets must remain one byte per frame"
);

enum micros_frame_ownership_error {
    MICROS_FRAME_OWNERSHIP_OK = 0,
    MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT,
    MICROS_FRAME_OWNERSHIP_ERROR_STORAGE,
    MICROS_FRAME_OWNERSHIP_ERROR_ALREADY_INITIALIZED,
    MICROS_FRAME_OWNERSHIP_ERROR_NOT_INITIALIZED,
    MICROS_FRAME_OWNERSHIP_ERROR_ALLOCATOR_STATE,
    MICROS_FRAME_OWNERSHIP_ERROR_OWNER,
    MICROS_FRAME_OWNERSHIP_ERROR_PHASE,
    MICROS_FRAME_OWNERSHIP_ERROR_EXHAUSTED,
    MICROS_FRAME_OWNERSHIP_ERROR_UNALIGNED,
    MICROS_FRAME_OWNERSHIP_ERROR_UNMANAGED,
    MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED,
    MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER,
    MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION,
    MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT,
};

enum micros_frame_ownership_error micros_frame_owner_make_kernel(
    enum micros_frame_owner_kind kind,
    struct micros_frame_owner *owner
);

enum micros_frame_ownership_error micros_frame_owner_make_process(
    enum micros_frame_owner_kind kind,
    struct micros_process_handle process,
    struct micros_frame_owner *owner
);

enum micros_frame_ownership_error micros_frame_ownership_initialize(
    struct micros_frame_ownership *ownership,
    struct micros_frame_allocator *allocator
);

enum micros_frame_ownership_error micros_frame_ownership_allocate(
    struct micros_frame_ownership *ownership,
    struct micros_frame_owner owner,
    uint64_t *physical_address
);

enum micros_frame_ownership_error micros_frame_ownership_release(
    struct micros_frame_ownership *ownership,
    struct micros_frame_owner expected_owner,
    uint64_t physical_address
);

enum micros_frame_ownership_error
micros_frame_ownership_prepare_handoff(
    struct micros_frame_ownership *ownership,
    uint64_t physical_address,
    struct micros_frame_owner expected_owner,
    enum micros_frame_handoff_target target
);

enum micros_frame_ownership_error micros_frame_ownership_lookup(
    const struct micros_frame_ownership *ownership,
    uint64_t physical_address,
    struct micros_frame_owner *owner
);

enum micros_frame_ownership_error
micros_frame_ownership_count_process(
    const struct micros_frame_ownership *ownership,
    struct micros_process_handle process,
    uint64_t *count
);

enum micros_frame_ownership_error
micros_frame_ownership_complete_handoff(
    struct micros_frame_ownership *ownership
);

enum micros_frame_ownership_error micros_frame_ownership_validate(
    const struct micros_frame_ownership *ownership
);

#endif
