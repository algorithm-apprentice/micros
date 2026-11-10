#include "kernel/scheduler/scheduler_core_private.h"

struct scheduler_return_cursor {
    struct micros_scheduler_return_queue *queue;
    struct micros_thread_handle initial_head;
    struct micros_thread_handle initial_tail;
    struct micros_thread_handle rotated_last;
    bool tail_reached;
    bool wrapped;
};

static void clear_return_candidate(
    struct micros_scheduler_return_plan *candidate
)
{
    unsigned char *bytes = (unsigned char *)candidate;
    size_t index;

    for (index = 0; index < sizeof(*candidate); ++index) {
        bytes[index] = 0;
    }
}

static enum micros_kernel_object_error preflight_return_plan(
    const struct micros_kernel_objects *objects,
    struct micros_hart_handle hart_handle,
    const struct micros_hart **hart_result
)
{
    const struct micros_hart *hart;
    enum micros_kernel_object_error error;

    error = scheduler_core_preflight_hart(objects, hart_handle, &hart);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        hart->accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_THREAD
        || hart->accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_IDLE
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    *hart_result = hart;
    return MICROS_KERNEL_OBJECT_OK;
}

static struct scheduler_return_cursor *return_queue_at_priority(
    const struct micros_scheduler_return_plan *candidate,
    struct scheduler_return_cursor cursors[2],
    uint8_t priority
)
{
    if (candidate->queue_count > 0 && candidate->queues[0].priority == priority) {
        return &cursors[0];
    }
    if (candidate->queue_count > 1 && candidate->queues[1].priority == priority) {
        return &cursors[1];
    }
    return NULL;
}

static enum micros_kernel_object_error prepare_return_queue(
    const struct micros_kernel_objects *objects,
    const struct micros_hart *hart,
    uint8_t priority,
    struct micros_scheduler_return_plan *candidate,
    struct scheduler_return_cursor cursors[2],
    struct scheduler_return_cursor **result
)
{
    struct scheduler_return_cursor *cursor = return_queue_at_priority(candidate, cursors, priority);
    struct micros_scheduler_return_queue *queue;
    enum micros_kernel_object_error error;

    if (cursor != NULL) {
        *result = cursor;
        return MICROS_KERNEL_OBJECT_OK;
    }
    if (candidate->queue_count >= 2) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    error = scheduler_core_preflight_queue_endpoints(objects, hart, priority);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    cursor = &cursors[candidate->queue_count];
    queue = &candidate->queues[candidate->queue_count];
    queue->priority = priority;
    queue->before_head = hart->ready_head[priority];
    queue->before_tail = hart->ready_tail[priority];
    queue->after_head = queue->before_head;
    queue->after_tail = queue->before_tail;
    cursor->queue = queue;
    cursor->initial_head = queue->before_head;
    cursor->initial_tail = queue->before_tail;
    cursor->rotated_last = scheduler_core_null_thread_handle();
    cursor->tail_reached = scheduler_core_thread_handle_is_null(queue->before_head);
    cursor->wrapped = false;
    ++candidate->queue_count;
    *result = cursor;
    return MICROS_KERNEL_OBJECT_OK;
}

static enum micros_kernel_object_error pick_return_queue(
    const struct micros_kernel_objects *objects,
    const struct micros_hart *hart,
    struct micros_scheduler_return_plan *candidate,
    struct scheduler_return_cursor cursors[2],
    struct scheduler_return_cursor **result
)
{
    size_t priority;
    enum micros_kernel_object_error error;

    for (priority = 0; priority < MICROS_SCHEDULER_PRIORITY_COUNT; ++priority) {
        struct scheduler_return_cursor *cursor =
            return_queue_at_priority(candidate, cursors, (uint8_t)priority);
        struct micros_thread_handle head;

        if (cursor != NULL) {
            head = cursor->queue->after_head;
        } else {
            error = scheduler_core_preflight_queue_endpoints(objects, hart, (uint8_t)priority);
            if (error != MICROS_KERNEL_OBJECT_OK) {
                return error;
            }
            head = hart->ready_head[priority];
        }
        if (head.generation != 0) {
            return prepare_return_queue(objects, hart, (uint8_t)priority, candidate, cursors, result);
        }
    }
    *result = NULL;
    return MICROS_KERNEL_OBJECT_OK;
}

static enum micros_kernel_object_error repair_preempted_candidate(
    const struct micros_kernel_objects *objects,
    const struct micros_hart *hart,
    struct micros_scheduler_return_plan *candidate,
    struct scheduler_return_cursor cursors[2]
)
{
    const struct micros_thread *current;
    struct scheduler_return_cursor *cursor;
    struct micros_scheduler_return_queue *queue;
    enum micros_kernel_object_error error;
    uint32_t resulting;

    if (scheduler_core_thread_handle_is_null(candidate->outgoing)) {
        return MICROS_KERNEL_OBJECT_OK;
    }
    current = &objects->threads[candidate->outgoing.slot];
    candidate->repair_preempted = (current->runtime_flags & MICROS_THREAD_RTS_PREEMPTED) != 0;
    resulting = current->runtime_flags & ~MICROS_THREAD_RTS_PREEMPTED;
    if (resulting != 0) {
        return MICROS_KERNEL_OBJECT_OK;
    }
    error = prepare_return_queue(objects, hart, current->scheduler_priority, candidate, cursors, &cursor);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (!candidate->repair_preempted) {
        return MICROS_KERNEL_OBJECT_OK;
    }
    candidate->publish_current = true;
    queue = cursor->queue;
    if (scheduler_core_thread_handle_is_null(queue->before_head)) {
        queue->after_head = candidate->outgoing;
        queue->after_tail = candidate->outgoing;
    } else if (current->remaining_counter_ticks == 0) {
        queue->after_tail = candidate->outgoing;
    } else {
        queue->after_head = candidate->outgoing;
    }
    cursor->initial_head = queue->after_head;
    cursor->initial_tail = queue->after_tail;
    return MICROS_KERNEL_OBJECT_OK;
}

static enum micros_kernel_object_error rotate_return_head(
    const struct micros_kernel_objects *objects,
    const struct micros_hart *hart,
    const struct micros_scheduler_return_plan *candidate,
    struct scheduler_return_cursor *cursor,
    struct micros_thread_handle selected,
    const struct micros_thread *thread
)
{
    struct micros_scheduler_return_queue *queue = cursor->queue;
    struct micros_thread_handle next;
    const struct micros_thread *successor;
    enum micros_kernel_object_error error;
    bool inserted = candidate->publish_current
        && scheduler_core_thread_handles_equal(selected, candidate->outgoing);

    if (!scheduler_core_thread_handles_equal(selected, queue->after_head)) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    if (inserted) {
        next = thread->remaining_counter_ticks == 0
            ? scheduler_core_null_thread_handle() : queue->before_head;
    } else {
        next = thread->ready_next;
        if (scheduler_core_thread_handles_equal(selected, queue->before_tail)) {
            if (!scheduler_core_thread_handle_is_null(next)) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            cursor->tail_reached = true;
            if (candidate->publish_current) {
                const struct micros_thread *outgoing = &objects->threads[candidate->outgoing.slot];

                if (outgoing->scheduler_priority == queue->priority
                    && outgoing->remaining_counter_ticks == 0) {
                    next = candidate->outgoing;
                }
            }
        } else {
            if (scheduler_core_thread_handle_is_null(next)
                || scheduler_core_thread_handles_equal(next, selected)
                || scheduler_core_thread_handles_equal(next, candidate->outgoing)) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            error = scheduler_core_preflight_queue_record(objects, hart, queue->priority, next, &successor);
            if (error != MICROS_KERNEL_OBJECT_OK) {
                return error;
            }
        }
    }
    if (scheduler_core_thread_handle_is_null(next)) {
        if (!cursor->tail_reached) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        next = cursor->initial_head;
        cursor->wrapped = true;
    }
    queue->after_head = next;
    queue->after_tail = selected;
    cursor->rotated_last = selected;
    return MICROS_KERNEL_OBJECT_OK;
}

static enum micros_kernel_object_error select_and_renew_candidate(
    const struct micros_kernel_objects *objects,
    const struct micros_hart *hart,
    struct micros_scheduler_return_plan *candidate,
    struct scheduler_return_cursor cursors[2]
)
{
    struct scheduler_return_cursor *cursor = NULL;
    size_t renew_count = 0;
    enum micros_kernel_object_error error;

    if (!scheduler_core_thread_handle_is_null(candidate->outgoing)
        && objects->threads[candidate->outgoing.slot].runtime_flags == 0) {
        cursor = return_queue_at_priority(
            candidate, cursors, objects->threads[candidate->outgoing.slot].scheduler_priority
        );
    } else {
        error = pick_return_queue(objects, hart, candidate, cursors, &cursor);
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
    }
    while (cursor != NULL) {
        struct micros_scheduler_return_queue *queue = cursor->queue;
        struct micros_thread_handle selected = queue->after_head;
        const struct micros_thread *thread;
        bool current = scheduler_core_thread_handles_equal(selected, candidate->outgoing);

        if (current) {
            thread = &objects->threads[selected.slot];
        } else {
            error = scheduler_core_preflight_queue_record(objects, hart, queue->priority, selected, &thread);
            if (error != MICROS_KERNEL_OBJECT_OK) {
                return error;
            }
        }
        if (thread->remaining_counter_ticks != 0) {
            break;
        }
        if ((current && candidate->renew_current)
            || (queue->renew_count != 0 && scheduler_core_thread_handles_equal(selected, queue->renew_first))) {
            if (!cursor->wrapped) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            break;
        }
        if (++renew_count > MICROS_THREAD_CAPACITY) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        if (current) {
            candidate->renew_current = true;
        } else {
            if (queue->renew_count == 0) {
                queue->renew_first = selected;
            }
            ++queue->renew_count;
        }
        candidate->renew_quantum = true;
        if (!thread->scheduler_preemptible) {
            break;
        }
        error = rotate_return_head(objects, hart, candidate, cursor, selected, thread);
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
        error = pick_return_queue(objects, hart, candidate, cursors, &cursor);
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
    }
    candidate->selected = cursor == NULL ? scheduler_core_null_thread_handle() : cursor->queue->after_head;
    return MICROS_KERNEL_OBJECT_OK;
}

static enum micros_kernel_object_error set_return_patch(
    struct micros_scheduler_return_queue *queue,
    struct micros_thread_handle thread,
    struct micros_thread_handle next
)
{
    size_t index;

    for (index = 0; index < queue->patch_count; ++index) {
        if (scheduler_core_thread_handles_equal(queue->patches[index].thread, thread)) {
            queue->patches[index].next = next;
            return MICROS_KERNEL_OBJECT_OK;
        }
    }
    if (queue->patch_count >= 3) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    queue->patches[queue->patch_count].thread = thread;
    queue->patches[queue->patch_count].next = next;
    ++queue->patch_count;
    return MICROS_KERNEL_OBJECT_OK;
}

static enum micros_kernel_object_error finish_return_queue(
    const struct micros_kernel_objects *objects,
    const struct micros_scheduler_return_plan *candidate,
    struct scheduler_return_cursor *cursor
)
{
    struct micros_scheduler_return_queue *queue = cursor->queue;
    enum micros_kernel_object_error error;

    if (candidate->publish_current) {
        const struct micros_thread *current = &objects->threads[candidate->outgoing.slot];

        if (current->scheduler_priority == queue->priority) {
            if (current->remaining_counter_ticks == 0
                && !scheduler_core_thread_handle_is_null(queue->before_head)) {
                error = set_return_patch(queue, queue->before_tail, candidate->outgoing);
                if (error != MICROS_KERNEL_OBJECT_OK) {
                    return error;
                }
            }
            error = set_return_patch(
                queue, candidate->outgoing, current->remaining_counter_ticks == 0
                    ? scheduler_core_null_thread_handle() : queue->before_head
            );
            if (error != MICROS_KERNEL_OBJECT_OK) {
                return error;
            }
        }
    }
    if (!scheduler_core_thread_handle_is_null(cursor->rotated_last)) {
        error = set_return_patch(queue, cursor->initial_tail, cursor->initial_head);
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
        error = set_return_patch(queue, cursor->rotated_last, scheduler_core_null_thread_handle());
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
    }
    return MICROS_KERNEL_OBJECT_OK;
}

static void publish_return_candidate(
    struct micros_scheduler_return_plan *plan,
    const struct micros_scheduler_return_plan *candidate
)
{
    unsigned char *destination = (unsigned char *)plan;
    const unsigned char *source =
        (const unsigned char *)candidate;
    size_t index;

    for (index = 0; index < sizeof(*candidate); ++index) {
        destination[index] = source[index];
    }
}

enum micros_kernel_object_error micros_hart_plan_user_return(
    const struct micros_kernel_objects *objects,
    struct micros_hart_handle hart_handle,
    struct micros_scheduler_return_plan *plan
)
{
    const struct micros_hart *hart;
    struct micros_scheduler_return_plan candidate;
    struct scheduler_return_cursor cursors[2];
    size_t index;
    enum micros_kernel_object_error error;

    if (plan == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    clear_return_candidate(&candidate);
    error = preflight_return_plan(objects, hart_handle, &hart);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    candidate.hart = hart_handle;
    candidate.outgoing = hart->current_thread;
    error = repair_preempted_candidate(objects, hart, &candidate, cursors);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = select_and_renew_candidate(objects, hart, &candidate, cursors);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    for (index = 0; index < candidate.queue_count; ++index) {
        error = finish_return_queue(objects, &candidate, &cursors[index]);
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
    }
    candidate.action = scheduler_core_thread_handle_is_null(candidate.selected)
        ? MICROS_SCHEDULER_RETURN_ENTER_IDLE
        : scheduler_core_thread_handles_equal(candidate.selected, candidate.outgoing)
            ? MICROS_SCHEDULER_RETURN_KEEP_CURRENT : MICROS_SCHEDULER_RETURN_SELECT_THREAD;
    publish_return_candidate(plan, &candidate);
    return MICROS_KERNEL_OBJECT_OK;
}
