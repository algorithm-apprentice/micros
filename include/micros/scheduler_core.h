#ifndef MICROS_SCHEDULER_CORE_H
#define MICROS_SCHEDULER_CORE_H

#include <stdint.h>

#include "micros/kernel_objects.h"

enum micros_scheduler_return_action {
    MICROS_SCHEDULER_RETURN_KEEP_CURRENT = 0,
    MICROS_SCHEDULER_RETURN_SELECT_THREAD,
    MICROS_SCHEDULER_RETURN_ENTER_IDLE,
};

struct micros_scheduler_return_plan {
    struct micros_hart_handle hart;
    struct micros_thread_handle outgoing;
    struct micros_thread_handle selected;
    enum micros_scheduler_return_action action;
    bool repair_preempted;
    bool renew_quantum;
    uint64_t renew_bitmap[
        (MICROS_THREAD_CAPACITY + 63) / 64
    ];
    uint64_t ready_bitmap[
        (MICROS_THREAD_CAPACITY + 63) / 64
    ];
    struct micros_thread_handle
        ready_head[MICROS_SCHEDULER_PRIORITY_COUNT];
    struct micros_thread_handle
        ready_tail[MICROS_SCHEDULER_PRIORITY_COUNT];
    struct micros_thread_handle ready_next[MICROS_THREAD_CAPACITY];
};

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

enum micros_kernel_object_error micros_hart_plan_user_return(
    const struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    struct micros_scheduler_return_plan *plan
);

enum micros_kernel_object_error micros_hart_commit_user_return(
    struct micros_kernel_objects *objects,
    const struct micros_scheduler_return_plan *plan
);

enum micros_kernel_object_error micros_scheduler_accounting_initialize(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    uint64_t counter
);

enum micros_kernel_object_error micros_scheduler_account_user_trap(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    uint64_t counter
);

enum micros_kernel_object_error micros_scheduler_account_idle_trap(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    uint64_t counter
);

enum micros_kernel_object_error micros_scheduler_account_enter_thread(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    struct micros_thread_handle thread,
    uint64_t counter
);

enum micros_kernel_object_error micros_scheduler_account_enter_idle(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    uint64_t counter
);

#endif
