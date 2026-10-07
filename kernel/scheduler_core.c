#include "micros/scheduler_core.h"

#include <stddef.h>

#include "scheduler_core_internal.h"

struct scheduler_plan_scratch {
    uint16_t slots[
        MICROS_SCHEDULER_PRIORITY_COUNT
    ][MICROS_THREAD_CAPACITY];
    size_t counts[MICROS_SCHEDULER_PRIORITY_COUNT];
};

static struct scheduler_plan_scratch plan_scratch;

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

static enum micros_kernel_object_error queue_enqueue_tail(
    struct micros_kernel_objects *objects,
    struct micros_hart *hart,
    struct micros_thread_handle thread_handle
)
{
    struct micros_thread *thread =
        &objects->threads[thread_handle.slot];

    queue_enqueue_tail_raw(objects, hart, thread_handle);
    if (!thread_handle_is_null(hart->current_thread)) {
        struct micros_thread *current =
            &objects->threads[hart->current_thread.slot];

        if (
            !thread_handles_equal(
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
                queue_dequeue(
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

static enum micros_kernel_object_error preflight_tail_enqueue(
    const struct micros_kernel_objects *objects,
    const struct micros_hart *hart,
    struct micros_thread_handle thread_handle,
    uint8_t priority
)
{
    const struct micros_thread *current;

    if (thread_handle_is_null(hart->current_thread)) {
        return MICROS_KERNEL_OBJECT_OK;
    }
    current = &objects->threads[hart->current_thread.slot];
    if (
        !thread_handles_equal(
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

static bool add_overflows(uint64_t left, uint64_t right)
{
    return UINT64_MAX - left < right;
}

static void bitmap_clear(uint64_t *bitmap)
{
    size_t index;

    for (
        index = 0;
        index < (MICROS_THREAD_CAPACITY + 63) / 64;
        ++index
    ) {
        bitmap[index] = 0;
    }
}

static void bitmap_set(uint64_t *bitmap, size_t slot)
{
    bitmap[slot / 64] |= UINT64_C(1) << (slot % 64);
}

static bool bitmap_test(const uint64_t *bitmap, size_t slot)
{
    return (
        bitmap[slot / 64] & (UINT64_C(1) << (slot % 64))
    ) != 0;
}

static enum micros_kernel_object_error scratch_load(
    const struct micros_kernel_objects *objects,
    const struct micros_hart *hart
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
        size_t count = 0;

        while (!thread_handle_is_null(cursor)) {
            const struct micros_thread *thread;

            if (
                count >= MICROS_THREAD_CAPACITY
                || cursor.slot >= MICROS_THREAD_CAPACITY
            ) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            thread = &objects->threads[cursor.slot];
            if (
                thread->slot_state
                    != MICROS_KERNEL_OBJECT_SLOT_LIVE
                || thread->generation != cursor.generation
            ) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            plan_scratch.slots[priority][count++] =
                cursor.slot;
            cursor = thread->ready_next;
        }
        plan_scratch.counts[priority] = count;
    }
    return MICROS_KERNEL_OBJECT_OK;
}

static void scratch_remove(uint8_t priority, uint16_t slot)
{
    size_t index;
    size_t count = plan_scratch.counts[priority];

    for (index = 0; index < count; ++index) {
        if (plan_scratch.slots[priority][index] == slot) {
            for (; index + 1 < count; ++index) {
                plan_scratch.slots[priority][index] =
                    plan_scratch.slots[priority][index + 1];
            }
            --plan_scratch.counts[priority];
            return;
        }
    }
}

static void scratch_head(uint8_t priority, uint16_t slot)
{
    size_t count = plan_scratch.counts[priority];
    size_t index;

    for (index = count; index != 0; --index) {
        plan_scratch.slots[priority][index] =
            plan_scratch.slots[priority][index - 1];
    }
    plan_scratch.slots[priority][0] = slot;
    ++plan_scratch.counts[priority];
}

static void scratch_tail(uint8_t priority, uint16_t slot)
{
    plan_scratch.slots[priority][
        plan_scratch.counts[priority]++
    ] = slot;
}

static size_t scratch_pick(void)
{
    size_t priority;

    for (
        priority = 0;
        priority < MICROS_SCHEDULER_PRIORITY_COUNT;
        ++priority
    ) {
        if (plan_scratch.counts[priority] != 0) {
            return plan_scratch.slots[priority][0];
        }
    }
    return MICROS_THREAD_CAPACITY;
}

enum micros_kernel_object_error micros_scheduler_core_validate(
    const struct micros_kernel_objects *objects
)
{
    bool seen[MICROS_THREAD_CAPACITY];
    bool current_seen[MICROS_THREAD_CAPACITY];
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
        current_seen[thread_index] = false;
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
                thread_handle_is_null(hart->current_thread)
                || !thread_handles_equal(
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
        } else if (!thread_handle_is_null(hart->accounted_thread)) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        if (
            hart->accounting_owner
                == MICROS_SCHEDULER_ACCOUNTING_IDLE
            && !thread_handle_is_null(hart->current_thread)
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        if (
            hart->reschedule_pending
            && !hart_has_assigned_thread(objects, hart_handle)
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
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
        if (!thread_handle_is_null(hart->current_thread)) {
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
                if (hart_has_assigned_thread(objects, hart_handle)) {
                    return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
                }
                continue;
            }
            if (
                thread->slot_state
                    != MICROS_KERNEL_OBJECT_SLOT_LIVE
                || thread->generation
                    != hart->current_thread.generation
                || !hart_handles_equal(
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
                    || !thread_handles_equal(
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
                && !thread_handle_is_null(thread->ready_next)
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
        (
            !thread_handle_is_null(hart->current_thread)
            && !objects->threads[
                hart->current_thread.slot
            ].scheduler_assigned
        )
        || thread->scheduler_assigned
        || !thread->context_attached
        || thread->runtime_flags != MICROS_THREAD_RTS_INACTIVE
        || thread_is_current(objects, thread_handle)
        || !micros_thread_ipc_state_is_clear(thread)
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    error = preflight_tail_enqueue(
        objects,
        hart,
        thread_handle,
        priority
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }

    thread->scheduler_assigned = true;
    thread->scheduler_preemptible = preemptible;
    thread->scheduler_priority = priority;
    thread->scheduler_hart = hart_handle;
    thread->quantum_counter_ticks = quantum_counter_ticks;
    thread->remaining_counter_ticks = quantum_counter_ticks;
    thread->runtime_flags = 0;
    return queue_enqueue_tail(objects, hart, thread_handle);
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
    if (
        thread_is_current(objects, thread_handle)
        && hart->accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_THREAD
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
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
        || (flags & MICROS_THREAD_RTS_IPC_MASK) != 0
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
    if (
        thread_is_current(objects, thread_handle)
        && hart->accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_THREAD
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
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
        || (flags & MICROS_THREAD_RTS_IPC_MASK) != 0
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
    if (
        resulting_flags == 0
        && thread_is_current(objects, thread_handle)
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
    if (resulting_flags == 0) {
        error = preflight_tail_enqueue(
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
        return queue_enqueue_tail(objects, hart, thread_handle);
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
    if (
        !thread->scheduler_assigned
        || thread_is_current(objects, thread_handle)
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
    resulting_flags =
        thread->runtime_flags & ~MICROS_THREAD_RTS_NO_QUANTUM;
    if (resulting_flags == 0) {
        error = preflight_tail_enqueue(
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
        error = queue_dequeue(objects, hart, thread_handle);
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
    }
    thread->scheduler_priority = priority;
    thread->quantum_counter_ticks = quantum_counter_ticks;
    thread->remaining_counter_ticks = quantum_counter_ticks;
    thread->runtime_flags = resulting_flags;
    if (resulting_flags == 0) {
        return queue_enqueue_tail(objects, hart, thread_handle);
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

enum micros_kernel_object_error micros_hart_plan_user_return(
    const struct micros_kernel_objects *objects,
    struct micros_hart_handle hart_handle,
    struct micros_scheduler_return_plan *plan
)
{
    const struct micros_hart *hart;
    struct micros_scheduler_return_plan candidate;
    size_t selected_slot;
    size_t renew_count = 0;
    size_t index;
    size_t priority;
    enum micros_kernel_object_error error;

    if (plan == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    {
        unsigned char *bytes = (unsigned char *)&candidate;

        for (index = 0; index < sizeof(candidate); ++index) {
            bytes[index] = 0;
        }
    }
    error = micros_scheduler_core_validate(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = micros_hart_resolve(objects, hart_handle, &hart);
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
    if (
        !thread_handle_is_null(hart->current_thread)
        && !objects->threads[
            hart->current_thread.slot
        ].scheduler_assigned
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    error = scratch_load(objects, hart);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }

    candidate.hart = hart_handle;
    candidate.outgoing = hart->current_thread;
    bitmap_clear(candidate.renew_bitmap);
    bitmap_clear(candidate.ready_bitmap);
    if (!thread_handle_is_null(hart->current_thread)) {
        const struct micros_thread *current =
            &objects->threads[hart->current_thread.slot];

        if (
            (current->runtime_flags
                & MICROS_THREAD_RTS_PREEMPTED) != 0
        ) {
            uint32_t resulting =
                current->runtime_flags
                & ~MICROS_THREAD_RTS_PREEMPTED;

            candidate.repair_preempted = true;
            if (resulting == 0) {
                if (current->remaining_counter_ticks == 0) {
                    scratch_tail(
                        current->scheduler_priority,
                        hart->current_thread.slot
                    );
                } else {
                    scratch_head(
                        current->scheduler_priority,
                        hart->current_thread.slot
                    );
                }
            }
        }
    }

    selected_slot = (
        !thread_handle_is_null(hart->current_thread)
        && objects->threads[
            hart->current_thread.slot
        ].runtime_flags == 0
    ) ? hart->current_thread.slot : scratch_pick();

    for (;;) {
        const struct micros_thread *selected;

        if (selected_slot == MICROS_THREAD_CAPACITY) {
            break;
        }
        selected = &objects->threads[selected_slot];
        if (
            selected->remaining_counter_ticks != 0
            || bitmap_test(
                candidate.renew_bitmap,
                selected_slot
            )
        ) {
            break;
        }
        if (++renew_count > MICROS_THREAD_CAPACITY) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        bitmap_set(candidate.renew_bitmap, selected_slot);
        candidate.renew_quantum = true;
        if (!selected->scheduler_preemptible) {
            break;
        }
        scratch_remove(
            selected->scheduler_priority,
            (uint16_t)selected_slot
        );
        scratch_tail(
            selected->scheduler_priority,
            (uint16_t)selected_slot
        );
        selected_slot = scratch_pick();
    }

    if (selected_slot == MICROS_THREAD_CAPACITY) {
        candidate.action = MICROS_SCHEDULER_RETURN_ENTER_IDLE;
        candidate.selected = null_thread_handle();
    } else {
        candidate.selected = (struct micros_thread_handle){
            (uint16_t)selected_slot,
            objects->threads[selected_slot].generation,
        };
        candidate.action = thread_handles_equal(
            candidate.selected,
            hart->current_thread
        )
            ? MICROS_SCHEDULER_RETURN_KEEP_CURRENT
            : MICROS_SCHEDULER_RETURN_SELECT_THREAD;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        candidate.ready_next[index] = null_thread_handle();
    }
    for (
        priority = 0;
        priority < MICROS_SCHEDULER_PRIORITY_COUNT;
        ++priority
    ) {
        size_t count = plan_scratch.counts[priority];

        candidate.ready_head[priority] = null_thread_handle();
        candidate.ready_tail[priority] = null_thread_handle();
        for (index = 0; index < count; ++index) {
            uint16_t slot = plan_scratch.slots[priority][index];
            struct micros_thread_handle handle = {
                slot,
                objects->threads[slot].generation,
            };

            bitmap_set(candidate.ready_bitmap, slot);
            if (index == 0) {
                candidate.ready_head[priority] = handle;
            } else {
                uint16_t previous_slot =
                    plan_scratch.slots[priority][index - 1];

                candidate.ready_next[previous_slot] = handle;
            }
            candidate.ready_tail[priority] = handle;
        }
    }
    {
        unsigned char *destination = (unsigned char *)plan;
        const unsigned char *source =
            (const unsigned char *)&candidate;

        for (index = 0; index < sizeof(candidate); ++index) {
            destination[index] = source[index];
        }
    }
    return MICROS_KERNEL_OBJECT_OK;
}

static bool plans_equal(
    const struct micros_scheduler_return_plan *left,
    const struct micros_scheduler_return_plan *right
)
{
    size_t index;

    if (
        !hart_handles_equal(left->hart, right->hart)
        || !thread_handles_equal(left->outgoing, right->outgoing)
        || !thread_handles_equal(left->selected, right->selected)
        || left->action != right->action
        || left->repair_preempted != right->repair_preempted
        || left->renew_quantum != right->renew_quantum
    ) {
        return false;
    }
    for (
        index = 0;
        index < (MICROS_THREAD_CAPACITY + 63) / 64;
        ++index
    ) {
        if (
            left->renew_bitmap[index] != right->renew_bitmap[index]
            || left->ready_bitmap[index] != right->ready_bitmap[index]
        ) {
            return false;
        }
    }
    for (index = 0; index < MICROS_SCHEDULER_PRIORITY_COUNT; ++index) {
        if (
            !thread_handles_equal(
                left->ready_head[index],
                right->ready_head[index]
            )
            || !thread_handles_equal(
                left->ready_tail[index],
                right->ready_tail[index]
            )
        ) {
            return false;
        }
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        if (
            !thread_handles_equal(
                left->ready_next[index],
                right->ready_next[index]
            )
        ) {
            return false;
        }
    }
    return true;
}

enum micros_kernel_object_error micros_hart_commit_user_return(
    struct micros_kernel_objects *objects,
    const struct micros_scheduler_return_plan *plan
)
{
    struct micros_scheduler_return_plan expected;
    enum micros_kernel_object_error error;

    if (objects == NULL || plan == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = micros_hart_plan_user_return(
        objects,
        plan->hart,
        &expected
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (!plans_equal(plan, &expected)) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    micros_scheduler_apply_return_plan(objects, plan);
    return MICROS_KERNEL_OBJECT_OK;
}

void micros_scheduler_apply_return_plan(
    struct micros_kernel_objects *objects,
    const struct micros_scheduler_return_plan *plan
)
{
    struct micros_hart *hart = &objects->harts[plan->hart.slot];
    size_t index;

    if (plan->repair_preempted) {
        struct micros_thread *outgoing =
            &objects->threads[plan->outgoing.slot];

        outgoing->runtime_flags &=
            ~MICROS_THREAD_RTS_PREEMPTED;
    }

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        struct micros_thread *thread = &objects->threads[index];

        if (bitmap_test(plan->renew_bitmap, index)) {
            thread->runtime_flags |= MICROS_THREAD_RTS_NO_QUANTUM;
            thread->remaining_counter_ticks =
                thread->quantum_counter_ticks;
            thread->runtime_flags &= ~MICROS_THREAD_RTS_NO_QUANTUM;
        }
        if (
            thread->scheduler_assigned
            && hart_handles_equal(
                thread->scheduler_hart,
                plan->hart
            )
        ) {
            thread->ready_linked =
                bitmap_test(plan->ready_bitmap, index);
            thread->ready_next = plan->ready_next[index];
        }
    }
    for (index = 0; index < MICROS_SCHEDULER_PRIORITY_COUNT; ++index) {
        hart->ready_head[index] = plan->ready_head[index];
        hart->ready_tail[index] = plan->ready_tail[index];
    }

    hart->current_thread = plan->selected;
    hart->reschedule_pending = false;
    if (thread_handle_is_null(plan->selected)) {
        hart->trap.primary_stack_bottom =
            hart->idle_primary_stack_bottom;
        hart->trap.primary_stack_top =
            hart->idle_primary_stack_top;
    } else {
        const struct micros_thread *selected =
            &objects->threads[plan->selected.slot];

        hart->trap.primary_stack_bottom =
            selected->kernel_stack_bottom;
        hart->trap.primary_stack_top =
            selected->kernel_stack_top;
    }
}

enum micros_kernel_object_error micros_scheduler_accounting_initialize(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart_handle,
    uint64_t counter
)
{
    struct micros_hart *hart;
    enum micros_kernel_object_error error;

    error = micros_scheduler_core_validate(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = resolve_hart_mutable(objects, hart_handle, &hart);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_NONE
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    hart->accounting_owner =
        MICROS_SCHEDULER_ACCOUNTING_KERNEL;
    hart->accounting_started_at = counter;
    hart->accounted_thread = null_thread_handle();
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_scheduler_account_user_trap(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart_handle,
    uint64_t counter
)
{
    struct micros_hart *hart;
    struct micros_thread *thread;
    uint64_t delta;
    enum micros_kernel_object_error error;

    error = resolve_hart_mutable(objects, hart_handle, &hart);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_THREAD
        || thread_handle_is_null(hart->current_thread)
        || !thread_handles_equal(
            hart->accounted_thread,
            hart->current_thread
        )
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    error = resolve_thread_mutable(
        objects,
        hart->current_thread,
        &thread
    );
    if (
        error != MICROS_KERNEL_OBJECT_OK
        || !thread->scheduler_assigned
        || !thread->context_attached
        || !hart_handles_equal(
            thread->scheduler_hart,
            hart_handle
        )
        || thread->runtime_flags != 0
        || thread->scheduler_priority
            >= MICROS_SCHEDULER_PRIORITY_COUNT
        || !thread->ready_linked
        || !thread_handles_equal(
            hart->ready_head[thread->scheduler_priority],
            hart->current_thread
        )
        || thread->quantum_counter_ticks == 0
        || thread->remaining_counter_ticks
            > thread->quantum_counter_ticks
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    delta = counter - hart->accounting_started_at;
    thread->remaining_counter_ticks =
        delta >= thread->remaining_counter_ticks
            ? 0
            : thread->remaining_counter_ticks - delta;
    hart->accounting_owner =
        MICROS_SCHEDULER_ACCOUNTING_KERNEL;
    hart->accounting_started_at = counter;
    hart->accounted_thread = null_thread_handle();
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_scheduler_account_idle_trap(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart_handle,
    uint64_t counter
)
{
    struct micros_hart *hart;
    uint64_t delta;
    enum micros_kernel_object_error error;

    error = resolve_hart_mutable(objects, hart_handle, &hart);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_IDLE
        || !thread_handle_is_null(hart->current_thread)
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    if (
        !thread_handle_is_null(hart->accounted_thread)
        || hart->trap.primary_stack_bottom
            != hart->idle_primary_stack_bottom
        || hart->trap.primary_stack_top
            != hart->idle_primary_stack_top
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    delta = counter - hart->accounting_started_at;
    if (add_overflows(hart->idle_counter_ticks, delta)) {
        return MICROS_KERNEL_OBJECT_ERROR_OVERFLOW;
    }
    hart->idle_counter_ticks += delta;
    hart->accounting_owner =
        MICROS_SCHEDULER_ACCOUNTING_KERNEL;
    hart->accounting_started_at = counter;
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_scheduler_account_enter_thread(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart_handle,
    struct micros_thread_handle thread_handle,
    uint64_t counter
)
{
    struct micros_hart *hart;
    const struct micros_thread *thread;
    uint64_t delta;
    enum micros_kernel_object_error error;

    error = micros_scheduler_core_validate(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = resolve_hart_mutable(objects, hart_handle, &hart);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_KERNEL
        || !thread_handles_equal(
            hart->current_thread,
            thread_handle
        )
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    error = micros_thread_resolve(
        objects,
        thread_handle,
        &thread
    );
    if (
        error != MICROS_KERNEL_OBJECT_OK
        || thread->runtime_flags != 0
        || thread->scheduler_priority
            >= MICROS_SCHEDULER_PRIORITY_COUNT
        || !thread->ready_linked
        || !thread_handles_equal(
            hart->ready_head[thread->scheduler_priority],
            thread_handle
        )
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    delta = counter - hart->accounting_started_at;
    if (add_overflows(hart->kernel_counter_ticks, delta)) {
        return MICROS_KERNEL_OBJECT_ERROR_OVERFLOW;
    }
    hart->kernel_counter_ticks += delta;
    hart->accounting_owner =
        MICROS_SCHEDULER_ACCOUNTING_THREAD;
    hart->accounting_started_at = counter;
    hart->accounted_thread = thread_handle;
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_scheduler_account_enter_idle(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart_handle,
    uint64_t counter
)
{
    struct micros_hart *hart;
    uint64_t delta;
    enum micros_kernel_object_error error;

    error = micros_scheduler_core_validate(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = resolve_hart_mutable(objects, hart_handle, &hart);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_KERNEL
        || !thread_handle_is_null(hart->current_thread)
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    delta = counter - hart->accounting_started_at;
    if (add_overflows(hart->kernel_counter_ticks, delta)) {
        return MICROS_KERNEL_OBJECT_ERROR_OVERFLOW;
    }
    hart->kernel_counter_ticks += delta;
    hart->accounting_owner =
        MICROS_SCHEDULER_ACCOUNTING_IDLE;
    hart->accounting_started_at = counter;
    return MICROS_KERNEL_OBJECT_OK;
}
