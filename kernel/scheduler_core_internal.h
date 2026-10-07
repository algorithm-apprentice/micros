#ifndef MICROS_SCHEDULER_CORE_INTERNAL_H
#define MICROS_SCHEDULER_CORE_INTERNAL_H

#include "micros/scheduler_core.h"

enum micros_kernel_object_error micros_kernel_objects_validate_base(
    const struct micros_kernel_objects *objects
);

void micros_scheduler_apply_return_plan(
    struct micros_kernel_objects *objects,
    const struct micros_scheduler_return_plan *plan
);

enum micros_kernel_object_error micros_scheduler_commit_ipc_wake_pair(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle first_thread,
    uint32_t first_clear_flag,
    struct micros_thread_handle second_thread,
    uint32_t second_clear_flag
);

enum micros_kernel_object_error micros_scheduler_commit_ipc_call_delivery(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle caller,
    struct micros_thread_handle receiver
);

#endif
