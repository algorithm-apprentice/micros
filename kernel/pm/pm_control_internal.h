#ifndef MICROS_KERNEL_PM_CONTROL_INTERNAL_H
#define MICROS_KERNEL_PM_CONTROL_INTERNAL_H

#include <stddef.h>

#include "kernel/pm/pm_control_core.h"

#define MICROS_PM_CONTROL_MAGIC UINT64_C(0x4d4943524f535043)

bool micros_pm_control_bytes_are_zero(
    const void *storage,
    size_t size
);

void micros_pm_control_clear_bytes(void *storage, size_t size);

void micros_pm_control_copy_bytes(
    void *destination,
    const void *source,
    size_t size
);

bool micros_pm_control_pointer_is_aligned(
    const void *pointer,
    size_t alignment
);

bool micros_pm_control_process_handle_is_valid(
    struct micros_process_handle process
);

bool micros_pm_control_process_handle_is_zero(
    struct micros_process_handle process
);

bool micros_pm_control_process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
);

bool micros_pm_control_hart_handles_equal(
    struct micros_hart_handle left,
    struct micros_hart_handle right
);

bool micros_pm_control_thread_handle_is_valid(
    struct micros_thread_handle thread
);

bool micros_pm_control_thread_handle_is_zero(
    struct micros_thread_handle thread
);

bool micros_pm_control_hart_handle_is_valid(
    struct micros_hart_handle hart
);

bool micros_pm_control_endpoint_matches_process(
    uint32_t endpoint,
    struct micros_process_handle process
);

bool micros_pm_control_process_is_empty(
    const struct micros_process *process
);

bool micros_pm_control_application_profile_is_exact(
    const struct micros_endpoint_registry *registry
);

enum micros_pm_control_error micros_pm_control_validate_prepared_shape(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *registry,
    const struct micros_vm_handoff_state *vm,
    uint64_t reservation_token,
    uint64_t load_token,
    uint64_t mapping_generation,
    uint64_t prepared_token
);

#endif
