#include "micros/scheduler_core.h"

#include <stddef.h>

#include "scheduler_core_internal.h"

static bool thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

static bool hart_handles_equal(
    struct micros_hart_handle left,
    struct micros_hart_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

static bool thread_handle_is_null(
    struct micros_thread_handle handle
)
{
    return handle.generation == 0;
}

static bool hart_handle_is_null(struct micros_hart_handle handle)
{
    return handle.generation == 0;
}

static struct micros_thread_handle null_thread_handle(void)
{
    struct micros_thread_handle handle = {0, 0};

    return handle;
}

static enum micros_kernel_object_error resolve_thread_mutable(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    struct micros_thread **thread
)
{
    const struct micros_thread *resolved;
    enum micros_kernel_object_error error;

    if (objects == NULL || thread == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = micros_thread_resolve(objects, handle, &resolved);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    *thread = &objects->threads[handle.slot];
    return MICROS_KERNEL_OBJECT_OK;
}

static enum micros_kernel_object_error resolve_hart_mutable(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle handle,
    struct micros_hart **hart
)
{
    const struct micros_hart *resolved;
    enum micros_kernel_object_error error;

    if (objects == NULL || hart == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = micros_hart_resolve(objects, handle, &resolved);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    *hart = &objects->harts[handle.slot];
    return MICROS_KERNEL_OBJECT_OK;
}

static bool thread_is_current(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle thread
)
{
    size_t index;

    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        const struct micros_hart *hart = &objects->harts[index];

        if (
            hart->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && thread_handles_equal(hart->current_thread, thread)
        ) {
            return true;
        }
    }
    return false;
}

static bool hart_has_assigned_thread(
    const struct micros_kernel_objects *objects,
    struct micros_hart_handle hart_handle
)
{
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];

        if (
            thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && thread->scheduler_assigned
            && hart_handles_equal(
                thread->scheduler_hart,
                hart_handle
            )
        ) {
            return true;
        }
    }
    return false;
}

static enum micros_kernel_object_error queue_dequeue(
    struct micros_kernel_objects *objects,
    struct micros_hart *hart,
    struct micros_thread_handle thread_handle
)
{
    struct micros_thread *thread =
        &objects->threads[thread_handle.slot];
    struct micros_thread_handle cursor;
    struct micros_thread_handle previous =
        null_thread_handle();
    uint8_t priority = thread->scheduler_priority;
    size_t steps = 0;

    if (
        priority >= MICROS_SCHEDULER_PRIORITY_COUNT
        || !thread->ready_linked
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    cursor = hart->ready_head[priority];
    while (!thread_handle_is_null(cursor)) {
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
        if (thread_handles_equal(cursor, thread_handle)) {
            if (thread_handle_is_null(previous)) {
                hart->ready_head[priority] =
                    candidate->ready_next;
            } else {
                objects->threads[previous.slot].ready_next =
                    candidate->ready_next;
            }
            if (
                thread_handles_equal(
                    hart->ready_tail[priority],
                    thread_handle
                )
            ) {
                hart->ready_tail[priority] = previous;
            }
            if (thread_handle_is_null(hart->ready_head[priority])) {
                hart->ready_tail[priority] =
                    null_thread_handle();
            }
            thread->ready_linked = false;
            thread->ready_next = null_thread_handle();
            return MICROS_KERNEL_OBJECT_OK;
        }
        previous = cursor;
        cursor = candidate->ready_next;
    }
    return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
}

static void queue_enqueue_tail(
    struct micros_kernel_objects *objects,
    struct micros_hart *hart,
    struct micros_thread_handle thread_handle
)
{
    struct micros_thread *thread =
        &objects->threads[thread_handle.slot];
    uint8_t priority = thread->scheduler_priority;

    thread->ready_linked = true;
    thread->ready_next = null_thread_handle();
    if (thread_handle_is_null(hart->ready_head[priority])) {
        hart->ready_head[priority] = thread_handle;
        hart->ready_tail[priority] = thread_handle;
        return;
    }
    objects->threads[
        hart->ready_tail[priority].slot
    ].ready_next = thread_handle;
    hart->ready_tail[priority] = thread_handle;
}

static struct micros_thread_handle queue_pick(
    const struct micros_hart *hart
)
{
    size_t priority;

    for (
        priority = 0;
        priority < MICROS_SCHEDULER_PRIORITY_COUNT;
        ++priority
    ) {
        if (!thread_handle_is_null(hart->ready_head[priority])) {
            return hart->ready_head[priority];
        }
    }
    return null_thread_handle();
}

enum micros_kernel_object_error micros_scheduler_core_validate(
    const struct micros_kernel_objects *objects
)
{
    bool seen[MICROS_THREAD_CAPACITY];
    size_t hart_index;
    size_t thread_index;
    enum micros_kernel_object_error error;

    error = micros_kernel_objects_validate_base(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    for (
        thread_index = 0;
        thread_index < MICROS_THREAD_CAPACITY;
        ++thread_index
    ) {
        seen[thread_index] = false;
    }
    for (hart_index = 0; hart_index < MICROS_HART_CAPACITY; ++hart_index) {
        const struct micros_hart *hart = &objects->harts[hart_index];
        struct micros_hart_handle hart_handle;
        size_t priority;

        if (hart->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE) {
            continue;
        }
        hart_handle = (struct micros_hart_handle){
            (uint16_t)hart_index,
            hart->generation,
        };
        if (
            hart->accounting_owner
                != MICROS_SCHEDULER_ACCOUNTING_NONE
            || hart->accounting_started_at != 0
            || !thread_handle_is_null(hart->accounted_thread)
            || hart->kernel_counter_ticks != 0
            || hart->idle_counter_ticks != 0
            || hart->reschedule_pending
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        if (!thread_handle_is_null(hart->current_thread)) {
            const struct micros_thread *current =
                &objects->threads[hart->current_thread.slot];

            if (
                current->scheduler_assigned
                || hart_has_assigned_thread(objects, hart_handle)
            ) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
        }
        for (
            priority = 0;
            priority < MICROS_SCHEDULER_PRIORITY_COUNT;
            ++priority
        ) {
            struct micros_thread_handle cursor =
                hart->ready_head[priority];
            struct micros_thread_handle last =
                null_thread_handle();
            size_t steps = 0;

            if (
                thread_handle_is_null(cursor)
                != thread_handle_is_null(hart->ready_tail[priority])
            ) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            while (!thread_handle_is_null(cursor)) {
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
                    || thread->state
                        != MICROS_THREAD_STATE_INACTIVE
                    || !hart_handles_equal(
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
                !thread_handles_equal(
                    last,
                    hart->ready_tail[priority]
                )
            ) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
        }
    }

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
                || !hart_handle_is_null(thread->scheduler_hart)
                || thread->quantum_counter_ticks != 0
                || thread->remaining_counter_ticks != 0
                || thread->ready_linked
                || !thread_handle_is_null(thread->ready_next)
            ) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            continue;
        }
        if (
            thread->state != MICROS_THREAD_STATE_INACTIVE
            || (
                thread->runtime_flags
                & ~MICROS_THREAD_RTS_DEFINED_MASK
            ) != 0
            || (
                thread->runtime_flags
                & MICROS_THREAD_RTS_PREEMPTED
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
                && !thread_handle_is_null(thread->ready_next)
            )
            || thread_is_current(
                objects,
                (struct micros_thread_handle){
                    (uint16_t)thread_index,
                    thread->generation,
                }
            )
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
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
    error = resolve_thread_mutable(objects, thread_handle, &thread);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = resolve_hart_mutable(objects, hart_handle, &hart);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        !thread_handle_is_null(hart->current_thread)
        || thread->scheduler_assigned
        || !thread->context_attached
        || thread->state != MICROS_THREAD_STATE_INACTIVE
        || thread->runtime_flags != MICROS_THREAD_RTS_INACTIVE
        || thread_is_current(objects, thread_handle)
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }

    thread->scheduler_assigned = true;
    thread->scheduler_preemptible = preemptible;
    thread->scheduler_priority = priority;
    thread->scheduler_hart = hart_handle;
    thread->quantum_counter_ticks = quantum_counter_ticks;
    thread->remaining_counter_ticks = quantum_counter_ticks;
    thread->runtime_flags = 0;
    queue_enqueue_tail(objects, hart, thread_handle);
    return MICROS_KERNEL_OBJECT_OK;
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
    error = resolve_thread_mutable(objects, thread_handle, &thread);
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
    error = resolve_hart_mutable(
        objects,
        thread->scheduler_hart,
        &hart
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    if (thread->runtime_flags == 0) {
        error = queue_dequeue(objects, hart, thread_handle);
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
    error = resolve_thread_mutable(objects, thread_handle, &thread);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        !thread->scheduler_assigned
        || thread->runtime_flags != MICROS_THREAD_RTS_INACTIVE
        || thread->ready_linked
        || thread_is_current(objects, thread_handle)
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
    thread->ready_next = null_thread_handle();
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
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = micros_scheduler_core_validate(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = resolve_thread_mutable(objects, thread_handle, &thread);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (!thread->scheduler_assigned) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    if ((thread->runtime_flags & flags) == flags) {
        return MICROS_KERNEL_OBJECT_OK;
    }
    error = resolve_hart_mutable(
        objects,
        thread->scheduler_hart,
        &hart
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    if (thread->runtime_flags == 0) {
        error = queue_dequeue(objects, hart, thread_handle);
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
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = micros_scheduler_core_validate(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = resolve_thread_mutable(objects, thread_handle, &thread);
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
    error = resolve_hart_mutable(
        objects,
        thread->scheduler_hart,
        &hart
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    thread->runtime_flags = resulting_flags;
    if (resulting_flags == 0) {
        queue_enqueue_tail(objects, hart, thread_handle);
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
    error = resolve_thread_mutable(objects, thread_handle, &thread);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (!thread->scheduler_assigned) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    error = resolve_hart_mutable(
        objects,
        thread->scheduler_hart,
        &hart
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    if (thread->runtime_flags == 0) {
        error = queue_dequeue(objects, hart, thread_handle);
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
    }
    resulting_flags =
        thread->runtime_flags & ~MICROS_THREAD_RTS_NO_QUANTUM;
    thread->scheduler_priority = priority;
    thread->quantum_counter_ticks = quantum_counter_ticks;
    thread->remaining_counter_ticks = quantum_counter_ticks;
    thread->runtime_flags = resulting_flags;
    if (resulting_flags == 0) {
        queue_enqueue_tail(objects, hart, thread_handle);
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
    selected = queue_pick(hart);
    if (thread_handle_is_null(selected)) {
        return MICROS_KERNEL_OBJECT_ERROR_EMPTY;
    }
    *thread = selected;
    return MICROS_KERNEL_OBJECT_OK;
}
