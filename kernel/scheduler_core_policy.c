#include "scheduler_core_private.h"

enum micros_kernel_object_error
micros_thread_scheduler_admit_preflight(
    const struct micros_kernel_objects *objects,
    struct micros_hart_handle hart_handle,
    struct micros_thread_handle thread_handle,
    uint8_t priority,
    uint64_t quantum_counter_ticks
)
{
    const struct micros_thread *thread;
    const struct micros_hart *hart;
    enum micros_kernel_object_error error;

    if (objects == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    if (
        priority >= MICROS_SCHEDULER_PRIORITY_COUNT
        || quantum_counter_ticks == 0
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_POLICY;
    }
    error = micros_scheduler_core_validate(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = micros_thread_resolve(objects, thread_handle, &thread);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = micros_hart_resolve(objects, hart_handle, &hart);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        (
            !scheduler_core_thread_handle_is_null(hart->current_thread)
            && !objects->threads[
                hart->current_thread.slot
            ].scheduler_assigned
        )
        || thread->scheduler_assigned
        || !thread->context_attached
        || thread->runtime_flags != MICROS_THREAD_RTS_INACTIVE
        || scheduler_core_thread_is_current(objects, thread_handle)
        || !micros_thread_ipc_state_is_clear(thread)
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    error = scheduler_core_preflight_tail_enqueue(
        objects,
        hart,
        thread_handle,
        priority
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_thread_scheduler_admit(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart_handle,
    struct micros_thread_handle thread_handle,
    uint8_t priority,
    uint64_t quantum_counter_ticks,
    bool preemptible
)
{
    struct micros_thread *thread;
    struct micros_hart *hart;
    enum micros_kernel_object_error error;

    error = micros_thread_scheduler_admit_preflight(
        objects,
        hart_handle,
        thread_handle,
        priority,
        quantum_counter_ticks
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = scheduler_core_resolve_thread_mutable(objects, thread_handle, &thread);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    error = scheduler_core_resolve_hart_mutable(objects, hart_handle, &hart);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }

    thread->scheduler_assigned = true;
    thread->scheduler_preemptible = preemptible;
    thread->scheduler_priority = priority;
    thread->scheduler_hart = hart_handle;
    thread->quantum_counter_ticks = quantum_counter_ticks;
    thread->remaining_counter_ticks = quantum_counter_ticks;
    thread->runtime_flags = 0;
    return scheduler_core_queue_enqueue_tail(objects, hart, thread_handle);
}

enum micros_kernel_object_error micros_thread_scheduler_hold(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread_handle
)
{
    struct micros_thread *thread;
    struct micros_hart *hart;
    enum micros_kernel_object_error error;

    error = micros_scheduler_core_validate(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = scheduler_core_resolve_thread_mutable(objects, thread_handle, &thread);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        !thread->scheduler_assigned
        || (
            thread->runtime_flags
            & MICROS_THREAD_RTS_INACTIVE
        ) != 0
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    error = scheduler_core_resolve_hart_mutable(
        objects,
        thread->scheduler_hart,
        &hart
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    if (
        scheduler_core_thread_is_current(objects, thread_handle)
        && hart->accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_THREAD
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    if (thread->runtime_flags == 0) {
        error = scheduler_core_queue_dequeue(objects, hart, thread_handle);
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
    }
    thread->runtime_flags |= MICROS_THREAD_RTS_INACTIVE;
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_thread_scheduler_remove(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread_handle
)
{
    struct micros_thread *thread;
    enum micros_kernel_object_error error;

    error = micros_scheduler_core_validate(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = scheduler_core_resolve_thread_mutable(objects, thread_handle, &thread);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        !thread->scheduler_assigned
        || thread->runtime_flags != MICROS_THREAD_RTS_INACTIVE
        || thread->ready_linked
        || scheduler_core_thread_is_current(objects, thread_handle)
        || !micros_thread_ipc_state_is_clear(thread)
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    thread->scheduler_assigned = false;
    thread->scheduler_preemptible = false;
    thread->scheduler_priority = 0;
    thread->scheduler_hart.slot = 0;
    thread->scheduler_hart.generation = 0;
    thread->quantum_counter_ticks = 0;
    thread->remaining_counter_ticks = 0;
    thread->ready_next = scheduler_core_null_thread_handle();
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_thread_runtime_flags_set(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread_handle,
    uint32_t flags
)
{
    struct micros_thread *thread;
    struct micros_hart *hart;
    enum micros_kernel_object_error error;

    if (
        flags == 0
        || (flags & ~MICROS_THREAD_RTS_DEFINED_MASK) != 0
        || (flags & MICROS_THREAD_RTS_PREEMPTED) != 0
        || (flags & MICROS_THREAD_RTS_IPC_MASK) != 0
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = micros_scheduler_core_validate(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = scheduler_core_resolve_thread_mutable(objects, thread_handle, &thread);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (!thread->scheduler_assigned) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    if ((thread->runtime_flags & flags) == flags) {
        return MICROS_KERNEL_OBJECT_OK;
    }
    error = scheduler_core_resolve_hart_mutable(
        objects,
        thread->scheduler_hart,
        &hart
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    if (
        scheduler_core_thread_is_current(objects, thread_handle)
        && hart->accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_THREAD
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    if (thread->runtime_flags == 0) {
        error = scheduler_core_queue_dequeue(objects, hart, thread_handle);
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
    }
    thread->runtime_flags |= flags;
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_thread_runtime_flags_unset(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread_handle,
    uint32_t flags
)
{
    struct micros_thread *thread;
    struct micros_hart *hart;
    uint32_t resulting_flags;
    enum micros_kernel_object_error error;

    if (
        flags == 0
        || (flags & ~MICROS_THREAD_RTS_DEFINED_MASK) != 0
        || (flags & MICROS_THREAD_RTS_PREEMPTED) != 0
        || (flags & MICROS_THREAD_RTS_IPC_MASK) != 0
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = micros_scheduler_core_validate(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = scheduler_core_resolve_thread_mutable(objects, thread_handle, &thread);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        !thread->scheduler_assigned
        || (thread->runtime_flags & flags) != flags
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    resulting_flags = thread->runtime_flags & ~flags;
    if (
        resulting_flags == 0
        && scheduler_core_thread_is_current(objects, thread_handle)
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    error = scheduler_core_resolve_hart_mutable(
        objects,
        thread->scheduler_hart,
        &hart
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    if (resulting_flags == 0) {
        error = scheduler_core_preflight_tail_enqueue(
            objects,
            hart,
            thread_handle,
            thread->scheduler_priority
        );
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
    }
    thread->runtime_flags = resulting_flags;
    if (resulting_flags == 0) {
        return scheduler_core_queue_enqueue_tail(objects, hart, thread_handle);
    }
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_thread_install_policy(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread_handle,
    uint8_t priority,
    uint64_t quantum_counter_ticks
)
{
    struct micros_thread *thread;
    struct micros_hart *hart;
    uint32_t resulting_flags;
    enum micros_kernel_object_error error;

    if (
        priority >= MICROS_SCHEDULER_PRIORITY_COUNT
        || quantum_counter_ticks == 0
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_POLICY;
    }
    error = micros_scheduler_core_validate(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = scheduler_core_resolve_thread_mutable(objects, thread_handle, &thread);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        !thread->scheduler_assigned
        || scheduler_core_thread_is_current(objects, thread_handle)
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    error = scheduler_core_resolve_hart_mutable(
        objects,
        thread->scheduler_hart,
        &hart
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    resulting_flags =
        thread->runtime_flags & ~MICROS_THREAD_RTS_NO_QUANTUM;
    if (resulting_flags == 0) {
        error = scheduler_core_preflight_tail_enqueue(
            objects,
            hart,
            thread_handle,
            priority
        );
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
    }
    if (thread->runtime_flags == 0) {
        error = scheduler_core_queue_dequeue(objects, hart, thread_handle);
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
    }
    thread->scheduler_priority = priority;
    thread->quantum_counter_ticks = quantum_counter_ticks;
    thread->remaining_counter_ticks = quantum_counter_ticks;
    thread->runtime_flags = resulting_flags;
    if (resulting_flags == 0) {
        return scheduler_core_queue_enqueue_tail(objects, hart, thread_handle);
    }
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_hart_pick_ready(
    const struct micros_kernel_objects *objects,
    struct micros_hart_handle hart_handle,
    struct micros_thread_handle *thread
)
{
    const struct micros_hart *hart;
    struct micros_thread_handle selected;
    enum micros_kernel_object_error error;

    if (thread == NULL) {
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
    selected = scheduler_core_queue_pick(hart);
    if (scheduler_core_thread_handle_is_null(selected)) {
        return MICROS_KERNEL_OBJECT_ERROR_EMPTY;
    }
    *thread = selected;
    return MICROS_KERNEL_OBJECT_OK;
}
