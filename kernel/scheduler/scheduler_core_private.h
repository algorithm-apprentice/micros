#ifndef MICROS_KERNEL_SCHEDULER_CORE_PRIVATE_H
#define MICROS_KERNEL_SCHEDULER_CORE_PRIVATE_H

#include "micros/scheduler_core.h"
#include "kernel/scheduler/scheduler_core_internal.h"

bool scheduler_core_thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
);

bool scheduler_core_hart_handles_equal(
    struct micros_hart_handle left,
    struct micros_hart_handle right
);

bool scheduler_core_thread_handle_is_null(
    struct micros_thread_handle handle
);

bool scheduler_core_hart_handle_is_null(struct micros_hart_handle handle);

struct micros_thread_handle scheduler_core_null_thread_handle(void);

enum micros_kernel_object_error scheduler_core_resolve_thread_mutable(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    struct micros_thread **thread
);

enum micros_kernel_object_error scheduler_core_resolve_hart_mutable(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle handle,
    struct micros_hart **hart
);

bool scheduler_core_thread_is_current(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle thread
);

bool scheduler_core_hart_has_assigned_thread(
    const struct micros_kernel_objects *objects,
    struct micros_hart_handle hart_handle
);

bool scheduler_core_add_overflows(uint64_t left, uint64_t right);

void scheduler_core_bitmap_clear(uint64_t *bitmap);

void scheduler_core_bitmap_set(uint64_t *bitmap, size_t slot);

bool scheduler_core_bitmap_test(const uint64_t *bitmap, size_t slot);

enum micros_kernel_object_error scheduler_core_queue_dequeue(
    struct micros_kernel_objects *objects,
    struct micros_hart *hart,
    struct micros_thread_handle thread_handle
);

enum micros_kernel_object_error scheduler_core_queue_enqueue_tail(
    struct micros_kernel_objects *objects,
    struct micros_hart *hart,
    struct micros_thread_handle thread_handle
);

struct micros_thread_handle scheduler_core_queue_pick(
    const struct micros_hart *hart
);

enum micros_kernel_object_error scheduler_core_preflight_tail_enqueue(
    const struct micros_kernel_objects *objects,
    const struct micros_hart *hart,
    struct micros_thread_handle thread_handle,
    uint8_t priority
);

void scheduler_core_queue_enqueue_tail_prevalidated(
    struct micros_kernel_objects *objects,
    struct micros_hart *hart,
    struct micros_thread_handle thread_handle
);

void scheduler_core_queue_enqueue_head_prevalidated(
    struct micros_kernel_objects *objects,
    struct micros_hart *hart,
    struct micros_thread_handle thread_handle
);

#endif
