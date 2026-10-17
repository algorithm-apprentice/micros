#include "kernel/scheduler/scheduler_core_private.h"

static bool current_ipc_guard_is_zero(
    const struct micros_scheduler_current_ipc_guard *guard
)
{
    return (
        guard != NULL
        && !guard->active
        && guard->hart.slot == 0
        && guard->hart.generation == 0
        && guard->thread.slot == 0
        && guard->thread.generation == 0
        && guard->priority == 0
        && guard->thread_stack_bottom == 0
        && guard->thread_stack_top == 0
    );
}

static void clear_current_ipc_guard(
    struct micros_scheduler_current_ipc_guard *guard
)
{
    guard->active = false;
    guard->hart = (struct micros_hart_handle){0, 0};
    guard->thread = scheduler_core_null_thread_handle();
    guard->priority = 0;
    guard->thread_stack_bottom = 0;
    guard->thread_stack_top = 0;
}

static bool ipc_blocked_flags_are_valid(uint32_t flags)
{
    return (
        flags == MICROS_THREAD_RTS_IPC_SEND
        || flags == MICROS_THREAD_RTS_IPC_RECEIVE
        || flags == MICROS_THREAD_RTS_IPC_REPLY
        || flags
            == (
                MICROS_THREAD_RTS_IPC_SEND
                | MICROS_THREAD_RTS_IPC_REPLY
            )
    );
}

enum micros_kernel_object_error
micros_scheduler_preflight_current_ipc(
    const struct micros_kernel_objects *objects,
    struct micros_hart_handle hart_handle
)
{
    const struct micros_hart *hart;
    const struct micros_thread *thread;
    struct micros_thread_handle current;
    enum micros_kernel_object_error error;

    if (objects == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = micros_scheduler_core_validate(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = micros_hart_resolve(objects, hart_handle, &hart);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    current = hart->current_thread;
    if (
        hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_KERNEL
        || scheduler_core_thread_handle_is_null(current)
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    error = micros_thread_resolve(objects, current, &thread);
    if (
        error != MICROS_KERNEL_OBJECT_OK
        || !thread->scheduler_assigned
        || !thread->context_attached
        || thread->runtime_flags != 0
        || !thread->ready_linked
        || !scheduler_core_thread_handles_equal(
            hart->ready_head[thread->scheduler_priority],
            current
        )
        || hart->trap.primary_stack_bottom
            != thread->kernel_stack_bottom
        || hart->trap.primary_stack_top
            != thread->kernel_stack_top
        || hart->idle_primary_stack_bottom == 0
        || hart->idle_primary_stack_top
            <= hart->idle_primary_stack_bottom
        || !micros_thread_ipc_state_is_clear(thread)
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_scheduler_begin_current_ipc(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart_handle,
    struct micros_scheduler_current_ipc_guard *guard
)
{
    struct micros_hart *hart;
    struct micros_thread *thread;
    struct micros_thread_handle current;
    enum micros_kernel_object_error error;

    if (
        objects == NULL
        || !current_ipc_guard_is_zero(guard)
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = micros_scheduler_preflight_current_ipc(
        objects,
        hart_handle
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = scheduler_core_resolve_hart_mutable(objects, hart_handle, &hart);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    current = hart->current_thread;
    error = scheduler_core_resolve_thread_mutable(objects, current, &thread);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    error = scheduler_core_queue_dequeue(objects, hart, current);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }

    guard->active = true;
    guard->hart = hart_handle;
    guard->thread = current;
    guard->priority = thread->scheduler_priority;
    guard->thread_stack_bottom = thread->kernel_stack_bottom;
    guard->thread_stack_top = thread->kernel_stack_top;
    thread->runtime_flags = MICROS_THREAD_RTS_INACTIVE;
    hart->current_thread = scheduler_core_null_thread_handle();
    hart->trap.primary_stack_bottom =
        hart->idle_primary_stack_bottom;
    hart->trap.primary_stack_top =
        hart->idle_primary_stack_top;
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_scheduler_rollback_current_ipc(
    struct micros_kernel_objects *objects,
    struct micros_scheduler_current_ipc_guard *guard
)
{
    struct micros_hart *hart;
    struct micros_thread *thread;
    enum micros_kernel_object_error error;

    if (objects == NULL || guard == NULL || !guard->active) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    error = micros_scheduler_core_validate(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = scheduler_core_resolve_hart_mutable(objects, guard->hart, &hart);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = scheduler_core_resolve_thread_mutable(objects, guard->thread, &thread);
    if (
        error != MICROS_KERNEL_OBJECT_OK
        || !scheduler_core_thread_handle_is_null(hart->current_thread)
        || hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_KERNEL
        || hart->trap.primary_stack_bottom
            != hart->idle_primary_stack_bottom
        || hart->trap.primary_stack_top
            != hart->idle_primary_stack_top
        || !thread->scheduler_assigned
        || !scheduler_core_hart_handles_equal(thread->scheduler_hart, guard->hart)
        || thread->scheduler_priority != guard->priority
        || thread->kernel_stack_bottom
            != guard->thread_stack_bottom
        || thread->kernel_stack_top != guard->thread_stack_top
        || thread->runtime_flags != MICROS_THREAD_RTS_INACTIVE
        || thread->ready_linked
        || !micros_thread_ipc_state_is_clear(thread)
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }

    thread->runtime_flags = 0;
    scheduler_core_queue_enqueue_head_prevalidated(
        objects,
        hart,
        guard->thread
    );
    hart->current_thread = guard->thread;
    hart->trap.primary_stack_bottom =
        guard->thread_stack_bottom;
    hart->trap.primary_stack_top =
        guard->thread_stack_top;
    clear_current_ipc_guard(guard);
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_scheduler_commit_current_ipc(
    struct micros_kernel_objects *objects,
    struct micros_scheduler_current_ipc_guard *guard
)
{
    struct micros_hart *hart;
    struct micros_thread *thread;
    enum micros_kernel_object_error error;

    if (objects == NULL || guard == NULL || !guard->active) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    error = micros_scheduler_core_validate(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = scheduler_core_resolve_hart_mutable(objects, guard->hart, &hart);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = scheduler_core_resolve_thread_mutable(objects, guard->thread, &thread);
    if (
        error != MICROS_KERNEL_OBJECT_OK
        || !scheduler_core_thread_handle_is_null(hart->current_thread)
        || hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_KERNEL
        || hart->trap.primary_stack_bottom
            != hart->idle_primary_stack_bottom
        || hart->trap.primary_stack_top
            != hart->idle_primary_stack_top
        || !thread->scheduler_assigned
        || !scheduler_core_hart_handles_equal(thread->scheduler_hart, guard->hart)
        || thread->scheduler_priority != guard->priority
        || thread->kernel_stack_bottom
            != guard->thread_stack_bottom
        || thread->kernel_stack_top != guard->thread_stack_top
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    if (thread->runtime_flags == 0) {
        if (!thread->ready_linked) {
            return MICROS_KERNEL_OBJECT_ERROR_STATE;
        }
        error = scheduler_core_queue_dequeue(objects, hart, guard->thread);
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
        scheduler_core_queue_enqueue_head_prevalidated(
            objects,
            hart,
            guard->thread
        );
    } else if (
        !ipc_blocked_flags_are_valid(thread->runtime_flags)
        || thread->ready_linked
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    clear_current_ipc_guard(guard);
    return MICROS_KERNEL_OBJECT_OK;
}
