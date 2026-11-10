#include "kernel/scheduler/scheduler_core_private.h"

static enum micros_kernel_object_error preflight_dequeue_successor(
    const struct micros_kernel_objects *objects,
    const struct micros_hart *hart,
    struct micros_thread_handle handle,
    const struct micros_thread *thread,
    size_t steps
)
{
    struct micros_thread_handle next = thread->ready_next;
    struct micros_thread_handle cursor;
    const struct micros_thread *successor;
    enum micros_kernel_object_error error;
    size_t index;

    if (scheduler_core_thread_handles_equal(hart->ready_tail[thread->scheduler_priority], handle)) {
        return scheduler_core_thread_handle_is_null(next)
            ? MICROS_KERNEL_OBJECT_OK : MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    if (
        scheduler_core_thread_handle_is_null(next)
        || scheduler_core_thread_handles_equal(next, handle)
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    error = scheduler_core_preflight_queue_record(objects, hart, thread->scheduler_priority, next, &successor);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    cursor = hart->ready_head[thread->scheduler_priority];
    for (index = 1; index < steps; ++index) {
        if (scheduler_core_thread_handles_equal(cursor, next)) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        cursor = objects->threads[cursor.slot].ready_next;
    }
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error scheduler_core_queue_dequeue(
    struct micros_kernel_objects *objects,
    struct micros_hart *hart,
    struct micros_thread_handle thread_handle
)
{
    struct micros_thread *thread;
    const struct micros_thread *checked;
    struct micros_thread_handle cursor;
    struct micros_thread_handle previous =
        scheduler_core_null_thread_handle();
    uint8_t priority;
    size_t steps = 0;
    enum micros_kernel_object_error error;

    error = scheduler_core_preflight_thread(objects, thread_handle, &checked);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    thread = &objects->threads[thread_handle.slot];
    priority = thread->scheduler_priority;
    if (
        priority >= MICROS_SCHEDULER_PRIORITY_COUNT
        || !thread->ready_linked
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    error = scheduler_core_preflight_queue_endpoints(objects, hart, priority);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    cursor = hart->ready_head[priority];
    while (!scheduler_core_thread_handle_is_null(cursor)) {
        const struct micros_thread *candidate;

        if (++steps > MICROS_THREAD_CAPACITY) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        error = scheduler_core_preflight_queue_record(objects, hart, priority, cursor, &candidate);
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
        if (scheduler_core_thread_handles_equal(cursor, thread_handle)) {
            error = preflight_dequeue_successor(objects, hart, thread_handle, candidate, steps);
            if (error != MICROS_KERNEL_OBJECT_OK) {
                return error;
            }
            if (scheduler_core_thread_handle_is_null(previous)) {
                hart->ready_head[priority] =
                    candidate->ready_next;
            } else {
                objects->threads[previous.slot].ready_next =
                    candidate->ready_next;
            }
            if (
                scheduler_core_thread_handles_equal(
                    hart->ready_tail[priority],
                    thread_handle
                )
            ) {
                hart->ready_tail[priority] = previous;
            }
            if (scheduler_core_thread_handle_is_null(hart->ready_head[priority])) {
                hart->ready_tail[priority] =
                    scheduler_core_null_thread_handle();
            }
            thread->ready_linked = false;
            thread->ready_next = scheduler_core_null_thread_handle();
            return MICROS_KERNEL_OBJECT_OK;
        }
        previous = cursor;
        cursor = candidate->ready_next;
    }
    return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
}

static void queue_enqueue_tail_raw(
    struct micros_kernel_objects *objects,
    struct micros_hart *hart,
    struct micros_thread_handle thread_handle
)
{
    struct micros_thread *thread =
        &objects->threads[thread_handle.slot];
    uint8_t priority = thread->scheduler_priority;

    thread->ready_linked = true;
    thread->ready_next = scheduler_core_null_thread_handle();
    if (scheduler_core_thread_handle_is_null(hart->ready_head[priority])) {
        hart->ready_head[priority] = thread_handle;
        hart->ready_tail[priority] = thread_handle;
        return;
    }
    objects->threads[
        hart->ready_tail[priority].slot
    ].ready_next = thread_handle;
    hart->ready_tail[priority] = thread_handle;
}

enum micros_kernel_object_error scheduler_core_queue_pick(
    const struct micros_kernel_objects *objects,
    const struct micros_hart *hart,
    struct micros_thread_handle *result
)
{
    size_t priority;
    enum micros_kernel_object_error error;

    for (
        priority = 0;
        priority < MICROS_SCHEDULER_PRIORITY_COUNT;
        ++priority
    ) {
        error = scheduler_core_preflight_queue_endpoints(objects, hart, (uint8_t)priority);
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
        if (!scheduler_core_thread_handle_is_null(hart->ready_head[priority])) {
            *result = hart->ready_head[priority];
            return MICROS_KERNEL_OBJECT_OK;
        }
    }
    return MICROS_KERNEL_OBJECT_ERROR_EMPTY;
}

enum micros_kernel_object_error scheduler_core_preflight_tail_enqueue(
    const struct micros_kernel_objects *objects,
    const struct micros_hart *hart,
    struct micros_thread_handle thread_handle,
    uint8_t priority
)
{
    const struct micros_thread *current;
    enum micros_kernel_object_error error = scheduler_core_preflight_queue_endpoints(objects, hart, priority);

    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (!scheduler_core_thread_handle_is_null(hart->current_thread)) {
        current = &objects->threads[hart->current_thread.slot];
        if (
            !scheduler_core_thread_handles_equal(hart->current_thread, thread_handle)
            && current->scheduler_assigned
            && current->runtime_flags == 0
            && current->scheduler_preemptible
            && current->scheduler_priority > priority
        ) {
            if (hart->accounting_owner == MICROS_SCHEDULER_ACCOUNTING_THREAD) {
                return MICROS_KERNEL_OBJECT_ERROR_STATE;
            }
            return scheduler_core_preflight_queue_endpoints(objects, hart, current->scheduler_priority);
        }
    }
    return MICROS_KERNEL_OBJECT_OK;
}

void scheduler_core_queue_enqueue_tail_prevalidated(
    struct micros_kernel_objects *objects,
    struct micros_hart *hart,
    struct micros_thread_handle thread_handle
)
{
    struct micros_thread *thread =
        &objects->threads[thread_handle.slot];

    queue_enqueue_tail_raw(objects, hart, thread_handle);
    if (!scheduler_core_thread_handle_is_null(hart->current_thread)) {
        struct micros_thread *current =
            &objects->threads[hart->current_thread.slot];

        if (
            !scheduler_core_thread_handles_equal(
                hart->current_thread,
                thread_handle
            )
            && current->scheduler_assigned
            && current->runtime_flags == 0
            && current->scheduler_preemptible
            && current->scheduler_priority
                > thread->scheduler_priority
        ) {
            uint8_t priority = current->scheduler_priority;

            hart->ready_head[priority] = current->ready_next;
            if (
                scheduler_core_thread_handles_equal(
                    hart->ready_tail[priority],
                    hart->current_thread
                )
            ) {
                hart->ready_tail[priority] =
                    scheduler_core_null_thread_handle();
            }
            current->ready_linked = false;
            current->ready_next = scheduler_core_null_thread_handle();
            current->runtime_flags |=
                MICROS_THREAD_RTS_PREEMPTED;
        }
    }
}

void scheduler_core_queue_enqueue_head_prevalidated(
    struct micros_kernel_objects *objects,
    struct micros_hart *hart,
    struct micros_thread_handle thread_handle
)
{
    struct micros_thread *thread =
        &objects->threads[thread_handle.slot];
    uint8_t priority = thread->scheduler_priority;
    struct micros_thread_handle old_head =
        hart->ready_head[priority];

    thread->ready_linked = true;
    thread->ready_next = old_head;
    hart->ready_head[priority] = thread_handle;
    if (scheduler_core_thread_handle_is_null(hart->ready_tail[priority])) {
        hart->ready_tail[priority] = thread_handle;
    }
}
