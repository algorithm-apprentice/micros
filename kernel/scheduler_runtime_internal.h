#ifndef MICROS_KERNEL_SCHEDULER_RUNTIME_INTERNAL_H
#define MICROS_KERNEL_SCHEDULER_RUNTIME_INTERNAL_H

#include "micros/scheduler.h"
#include "kernel/ipc_buffer_internal.h"
#include "scheduler_core_internal.h"

struct scheduler_runtime_context {
    bool scheduler_initialized;
    bool scheduler_timer_enabled;
    bool scheduler_start_timer_prepared;
    uint64_t scheduler_preemption_interval;
    struct micros_user_context selected_context;
    uint64_t selected_root;
    uint64_t scheduler_kernel_root;
};

struct scheduler_accounting_commit {
    uint64_t counter;
    uint64_t kernel_total;
};

enum scheduler_completion_error {
    SCHEDULER_COMPLETION_OK = 0,
    SCHEDULER_COMPLETION_INVALID_BUFFER,
    SCHEDULER_COMPLETION_INVARIANT,
};

struct scheduler_completion_commit {
    bool pending;
    struct micros_thread *thread;
    struct micros_process_handle owner;
    uintptr_t receive_buffer;
    struct micros_ipc_message message;
    struct micros_ipc_buffer_plan buffer_plan;
    uint64_t abi_result;
};

struct scheduler_runtime_context *scheduler_runtime_state(void);

_Noreturn void micros_riscv_enter_user(
    const struct micros_user_context *context
);
_Noreturn void micros_scheduler_idle_pivot(struct micros_hart *hart);
void micros_scheduler_idle_select(struct micros_hart *hart);
_Noreturn void micros_scheduler_idle_accounting_panic(
    struct micros_hart *hart
);

enum micros_scheduler_error scheduler_runtime_map_object_error(
    enum micros_kernel_object_error error
);

struct micros_kernel_objects *scheduler_runtime_authoritative_objects(void);

void scheduler_runtime_copy_user_context(
    struct micros_user_context *destination,
    const struct micros_user_context *source
);

void scheduler_runtime_clear_bytes(void *storage, size_t size);

enum micros_scheduler_error scheduler_runtime_validate_selected_thread(
    struct scheduler_runtime_context *runtime,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread_handle
);

enum micros_scheduler_error scheduler_runtime_load_selected_context(
    struct scheduler_runtime_context *runtime,
    struct micros_thread_handle thread
);

_Noreturn void scheduler_runtime_panic_scheduler_invariant(
    struct micros_hart *hart,
    const char *reason,
    struct micros_trap_frame *frame
);

void scheduler_runtime_preflight_completion_or_panic(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle selected,
    struct scheduler_completion_commit *completion,
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

void scheduler_runtime_commit_completion_or_panic(
    struct scheduler_runtime_context *runtime,
    struct scheduler_completion_commit *completion,
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

enum micros_scheduler_error scheduler_runtime_preflight_kernel_interval(
    const struct micros_hart *hart,
    struct scheduler_accounting_commit *accounting
);

void scheduler_runtime_activate_prevalidated_root(
    struct micros_hart *hart,
    uint64_t root,
    struct micros_trap_frame *frame
);

void scheduler_runtime_enter_selected_thread(
    struct scheduler_runtime_context *runtime,
    struct micros_hart *hart,
    struct micros_thread_handle selected,
    const struct scheduler_accounting_commit *accounting,
    struct micros_trap_frame *frame
);

#endif
