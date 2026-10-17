#include "kernel/scheduler/scheduler_core_private.h"

enum micros_kernel_object_error scheduler_core_queue_dequeue(
    struct micros_kernel_objects *objects,
    struct micros_hart *hart,
    struct micros_thread_handle thread_handle
)
{
    struct micros_thread *thread =
        &objects->threads[thread_handle.slot];
    struct micros_thread_handle cursor;
    struct micros_thread_handle previous =
        scheduler_core_null_thread_handle();
    uint8_t priority = thread->scheduler_priority;
    size_t steps = 0;

    if (
        priority >= MICROS_SCHEDULER_PRIORITY_COUNT
        || !thread->ready_linked
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    cursor = hart->ready_head[priority];
    while (!scheduler_core_thread_handle_is_null(cursor)) {
        struct micros_thread *candidate;

        if (
            ++steps > MICROS_THREAD_CAPACITY
            || cursor.slot >= MICROS_THREAD_CAPACITY
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        candidate = &objects->threads[cursor.slot];
        if (
            candidate->slot_state
                != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || candidate->generation != cursor.generation
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        if (scheduler_core_thread_handles_equal(cursor, thread_handle)) {
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

enum micros_kernel_object_error scheduler_core_queue_enqueue_tail(
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
            enum micros_kernel_object_error error =
                scheduler_core_queue_dequeue(
                    objects,
                    hart,
                    hart->current_thread
                );

            if (error != MICROS_KERNEL_OBJECT_OK) {
                return error;
            }
            current->runtime_flags |=
                MICROS_THREAD_RTS_PREEMPTED;
        }
    }
    return MICROS_KERNEL_OBJECT_OK;
}

struct micros_thread_handle scheduler_core_queue_pick(
    const struct micros_hart *hart
)
{
    size_t priority;

    for (
        priority = 0;
        priority < MICROS_SCHEDULER_PRIORITY_COUNT;
        ++priority
    ) {
        if (!scheduler_core_thread_handle_is_null(hart->ready_head[priority])) {
            return hart->ready_head[priority];
        }
    }
    return scheduler_core_null_thread_handle();
}

enum micros_kernel_object_error scheduler_core_preflight_tail_enqueue(
    const struct micros_kernel_objects *objects,
    const struct micros_hart *hart,
    struct micros_thread_handle thread_handle,
    uint8_t priority
)
{
    const struct micros_thread *current;

    if (scheduler_core_thread_handle_is_null(hart->current_thread)) {
        return MICROS_KERNEL_OBJECT_OK;
    }
    current = &objects->threads[hart->current_thread.slot];
    if (
        !scheduler_core_thread_handles_equal(
            hart->current_thread,
            thread_handle
        )
        && current->scheduler_assigned
        && current->runtime_flags == 0
        && current->scheduler_preemptible
        && current->scheduler_priority > priority
        && hart->accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_THREAD
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
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
