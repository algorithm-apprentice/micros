#ifndef MICROS_SCHEDULER_CORE_H
#define MICROS_SCHEDULER_CORE_H

#include <stdint.h>

#include "micros/kernel_objects.h"

enum micros_scheduler_return_action {
    MICROS_SCHEDULER_RETURN_KEEP_CURRENT = 0,
    MICROS_SCHEDULER_RETURN_SELECT_THREAD,
    MICROS_SCHEDULER_RETURN_ENTER_IDLE,
};

struct micros_scheduler_return_patch {
    struct micros_thread_handle thread;
    struct micros_thread_handle next;
};

struct micros_scheduler_return_queue {
    uint8_t priority;
    uint8_t patch_count;
    uint16_t renew_count;
    struct micros_thread_handle before_head;
    struct micros_thread_handle before_tail;
    struct micros_thread_handle after_head;
    struct micros_thread_handle after_tail;
    struct micros_thread_handle renew_first;
    struct micros_scheduler_return_patch patches[3];
};

struct micros_scheduler_return_plan {
    struct micros_hart_handle hart;
    struct micros_thread_handle outgoing;
    struct micros_thread_handle selected;
    enum micros_scheduler_return_action action;
    bool repair_preempted;
    bool renew_quantum;
    bool renew_current;
    bool publish_current;
    uint8_t queue_count;
    struct micros_scheduler_return_queue queues[2];
};

_Static_assert(sizeof(struct micros_scheduler_return_plan) <= 320, "bounded return plan");

struct micros_scheduler_current_ipc_guard {
    bool active;
    struct micros_hart_handle hart;
    struct micros_thread_handle thread;
    uint8_t priority;
    uintptr_t thread_stack_bottom;
    uintptr_t thread_stack_top;
};

struct micros_scheduler_ipc_wake_plan {
    bool active;
    uint8_t reserved[3];
    uint32_t resulting_flags;
    struct micros_thread_handle thread;
    struct micros_hart_handle hart;
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

enum micros_kernel_object_error micros_scheduler_begin_current_ipc(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    struct micros_scheduler_current_ipc_guard *guard
);

enum micros_kernel_object_error micros_scheduler_rollback_current_ipc(
    struct micros_kernel_objects *objects,
    struct micros_scheduler_current_ipc_guard *guard
);

enum micros_kernel_object_error micros_scheduler_commit_current_ipc(
    struct micros_kernel_objects *objects,
    struct micros_scheduler_current_ipc_guard *guard
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
