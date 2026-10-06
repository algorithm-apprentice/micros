#ifndef MICROS_SCHEDULER_CORE_H
#define MICROS_SCHEDULER_CORE_H

#include <stdint.h>

#include "micros/kernel_objects.h"

enum micros_kernel_object_error micros_scheduler_core_validate(
    const struct micros_kernel_objects *objects
);

enum micros_kernel_object_error micros_thread_scheduler_admit(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    struct micros_thread_handle thread,
    uint8_t priority,
    uint64_t quantum_counter_ticks,
    bool preemptible
);

enum micros_kernel_object_error micros_thread_scheduler_hold(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread
);

enum micros_kernel_object_error micros_thread_scheduler_remove(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread
);

enum micros_kernel_object_error micros_thread_runtime_flags_set(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread,
    uint32_t flags
);

enum micros_kernel_object_error micros_thread_runtime_flags_unset(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread,
    uint32_t flags
);

enum micros_kernel_object_error micros_thread_install_policy(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread,
    uint8_t priority,
    uint64_t quantum_counter_ticks
);

enum micros_kernel_object_error micros_hart_pick_ready(
    const struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    struct micros_thread_handle *thread
);

#endif
