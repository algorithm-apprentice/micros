#include "kernel/scheduler/scheduler_core_private.h"

static void initialize_validation_witnesses(
    bool *seen,
    bool *current_seen
)
{
    size_t thread_index;

    for (
        thread_index = 0;
        thread_index < MICROS_THREAD_CAPACITY;
        ++thread_index
    ) {
        seen[thread_index] = false;
        current_seen[thread_index] = false;
    }
}

static enum micros_kernel_object_error validate_hart_accounting(
    const struct micros_kernel_objects *objects,
    const struct micros_hart *hart,
    struct micros_hart_handle hart_handle
)
{
    if (
        hart->accounting_owner
            > MICROS_SCHEDULER_ACCOUNTING_IDLE
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    if (
        hart->accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_NONE
        && (
            hart->accounting_started_at != 0
            || hart->kernel_counter_ticks != 0
            || hart->idle_counter_ticks != 0
        )
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    if (
        hart->accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_THREAD
    ) {
        if (
            scheduler_core_thread_handle_is_null(hart->current_thread)
            || !scheduler_core_thread_handles_equal(
                hart->current_thread,
                hart->accounted_thread
            )
            || hart->current_thread.slot
                >= MICROS_THREAD_CAPACITY
            || objects->threads[
                hart->current_thread.slot
            ].runtime_flags != 0
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
    } else if (!scheduler_core_thread_handle_is_null(hart->accounted_thread)) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    if (
        hart->accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_IDLE
        && !scheduler_core_thread_handle_is_null(hart->current_thread)
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    if (
        hart->reschedule_pending
        && !scheduler_core_hart_has_assigned_thread(objects, hart_handle)
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    return MICROS_KERNEL_OBJECT_OK;
}

static enum micros_kernel_object_error validate_hart_queues(
    const struct micros_kernel_objects *objects,
    const struct micros_hart *hart,
    struct micros_hart_handle hart_handle,
    bool *seen
)
{
    size_t priority;

    for (
        priority = 0;
        priority < MICROS_SCHEDULER_PRIORITY_COUNT;
        ++priority
    ) {
        struct micros_thread_handle cursor =
            hart->ready_head[priority];
        struct micros_thread_handle last =
            scheduler_core_null_thread_handle();
        size_t steps = 0;

        if (
            scheduler_core_thread_handle_is_null(cursor)
            != scheduler_core_thread_handle_is_null(hart->ready_tail[priority])
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        while (!scheduler_core_thread_handle_is_null(cursor)) {
            const struct micros_thread *thread;

            if (
                ++steps > MICROS_THREAD_CAPACITY
                || cursor.slot >= MICROS_THREAD_CAPACITY
                || seen[cursor.slot]
            ) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            thread = &objects->threads[cursor.slot];
            if (
                thread->slot_state
                    != MICROS_KERNEL_OBJECT_SLOT_LIVE
                || thread->generation != cursor.generation
                || !thread->scheduler_assigned
                || !scheduler_core_hart_handles_equal(
                    thread->scheduler_hart,
                    hart_handle
                )
                || thread->scheduler_priority != priority
                || thread->runtime_flags != 0
                || !thread->ready_linked
            ) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            seen[cursor.slot] = true;
            last = cursor;
            cursor = thread->ready_next;
        }
        if (
            !scheduler_core_thread_handles_equal(
                last,
                hart->ready_tail[priority]
            )
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
    }
    return MICROS_KERNEL_OBJECT_OK;
}

static enum micros_kernel_object_error validate_current_binding(
    const struct micros_hart *hart,
    struct micros_hart_handle hart_handle,
    const struct micros_thread *thread,
    const bool *seen
)
{
    if (
        thread->slot_state
            != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || thread->generation
            != hart->current_thread.generation
        || !scheduler_core_hart_handles_equal(
            thread->scheduler_hart,
            hart_handle
        )
        || !thread->context_attached
        || hart->trap.primary_stack_bottom
            != thread->kernel_stack_bottom
        || hart->trap.primary_stack_top
            != thread->kernel_stack_top
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    if (
        thread->runtime_flags == 0
        && (
            !seen[hart->current_thread.slot]
            || !scheduler_core_thread_handles_equal(
                hart->ready_head[
                    thread->scheduler_priority
                ],
                hart->current_thread
            )
        )
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    if (
        thread->runtime_flags != 0
        && seen[hart->current_thread.slot]
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    return MICROS_KERNEL_OBJECT_OK;
}

static enum micros_kernel_object_error validate_harts(
    const struct micros_kernel_objects *objects,
    bool *seen,
    bool *current_seen
)
{
    size_t hart_index;
    enum micros_kernel_object_error error;

    for (hart_index = 0; hart_index < MICROS_HART_CAPACITY; ++hart_index) {
        const struct micros_hart *hart = &objects->harts[hart_index];
        struct micros_hart_handle hart_handle;

        if (hart->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE) {
            continue;
        }
        hart_handle = (struct micros_hart_handle){
            (uint16_t)hart_index,
            hart->generation,
        };
        error = validate_hart_accounting(objects, hart, hart_handle);
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
        error = validate_hart_queues(objects, hart, hart_handle, seen);
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
        if (!scheduler_core_thread_handle_is_null(hart->current_thread)) {
            const struct micros_thread *thread;

            if (
                hart->current_thread.slot
                    >= MICROS_THREAD_CAPACITY
                || current_seen[hart->current_thread.slot]
            ) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            thread = &objects->threads[hart->current_thread.slot];
            if (!thread->scheduler_assigned) {
                if (scheduler_core_hart_has_assigned_thread(objects, hart_handle)) {
                    return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
                }
                continue;
            }
            error = validate_current_binding(hart, hart_handle, thread, seen);
            if (error != MICROS_KERNEL_OBJECT_OK) {
                return error;
            }
            current_seen[hart->current_thread.slot] = true;
        } else if (
            hart->trap_installed
            && (
                hart->trap.primary_stack_bottom
                    != hart->idle_primary_stack_bottom
                || hart->trap.primary_stack_top
                    != hart->idle_primary_stack_top
            )
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
    }
    return MICROS_KERNEL_OBJECT_OK;
}

static enum micros_kernel_object_error validate_threads(
    const struct micros_kernel_objects *objects,
    const bool *seen,
    const bool *current_seen
)
{
    size_t thread_index;

    for (
        thread_index = 0;
        thread_index < MICROS_THREAD_CAPACITY;
        ++thread_index
    ) {
        const struct micros_thread *thread =
            &objects->threads[thread_index];
        const struct micros_hart *assigned_hart;

        if (thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE) {
            continue;
        }
        if (!thread->scheduler_assigned) {
            if (
                thread->runtime_flags
                    != MICROS_THREAD_RTS_INACTIVE
                || thread->scheduler_preemptible
                || thread->scheduler_priority != 0
                || !scheduler_core_hart_handle_is_null(thread->scheduler_hart)
                || thread->quantum_counter_ticks != 0
                || thread->remaining_counter_ticks != 0
                || thread->ready_linked
                || !scheduler_core_thread_handle_is_null(thread->ready_next)
            ) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            continue;
        }
        if (
            (
                thread->runtime_flags
                & ~MICROS_THREAD_RTS_DEFINED_MASK
            ) != 0
            || thread->scheduler_priority
                >= MICROS_SCHEDULER_PRIORITY_COUNT
            || micros_hart_resolve(
                objects,
                thread->scheduler_hart,
                &assigned_hart
            ) != MICROS_KERNEL_OBJECT_OK
            || !thread->context_attached
            || thread->quantum_counter_ticks == 0
            || thread->remaining_counter_ticks
                > thread->quantum_counter_ticks
            || thread->ready_linked != seen[thread_index]
            || (thread->runtime_flags == 0) != seen[thread_index]
            || (
                !seen[thread_index]
                && !scheduler_core_thread_handle_is_null(thread->ready_next)
            )
            || (
                (
                    thread->runtime_flags
                    & MICROS_THREAD_RTS_PREEMPTED
                ) != 0
                && !current_seen[thread_index]
            )
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
    }
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_scheduler_core_validate(
    const struct micros_kernel_objects *objects
)
{
    bool seen[MICROS_THREAD_CAPACITY];
    bool current_seen[MICROS_THREAD_CAPACITY];
    enum micros_kernel_object_error error;

    error = micros_kernel_objects_validate_base(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    initialize_validation_witnesses(seen, current_seen);
    error = validate_harts(objects, seen, current_seen);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    return validate_threads(objects, seen, current_seen);
}
