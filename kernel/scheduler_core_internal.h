#ifndef MICROS_SCHEDULER_CORE_INTERNAL_H
#define MICROS_SCHEDULER_CORE_INTERNAL_H

#include "micros/scheduler_core.h"

enum micros_kernel_object_error micros_kernel_objects_validate_base(
    const struct micros_kernel_objects *objects
);

enum micros_kernel_object_error
micros_thread_scheduler_admit_preflight(
    const struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    struct micros_thread_handle thread,
    uint8_t priority,
    uint64_t quantum_counter_ticks
);

enum micros_kernel_object_error
micros_scheduler_preflight_current_ipc(
    const struct micros_kernel_objects *objects,
    struct micros_hart_handle hart
);

void micros_scheduler_apply_return_plan(
    struct micros_kernel_objects *objects,
    const struct micros_scheduler_return_plan *plan
);

struct micros_scheduler_ipc_transition {
    struct micros_thread_handle handle;
    uint32_t clear_flags;
    uint32_t set_flags;
};

enum micros_kernel_object_error micros_scheduler_commit_ipc_transitions(
    struct micros_kernel_objects *objects,
    const struct micros_scheduler_ipc_transition *requests,
    size_t request_count
);

enum micros_kernel_object_error micros_scheduler_commit_ipc_wake(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread,
    uint32_t clear_flag
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

enum micros_kernel_object_error
micros_scheduler_commit_ipc_reply_receive_wait(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle caller,
    struct micros_thread_handle replier
);

enum micros_kernel_object_error
micros_scheduler_commit_ipc_reply_receive_delivery(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle caller,
    struct micros_thread_handle replier,
    struct micros_thread_handle sender
);

#endif
