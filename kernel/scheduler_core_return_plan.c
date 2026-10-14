#include "scheduler_core_private.h"

struct scheduler_plan_scratch {
    uint16_t slots[
        MICROS_SCHEDULER_PRIORITY_COUNT
    ][MICROS_THREAD_CAPACITY];
    size_t counts[MICROS_SCHEDULER_PRIORITY_COUNT];
};

static struct scheduler_plan_scratch plan_scratch;

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

        while (!scheduler_core_thread_handle_is_null(cursor)) {
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
        !scheduler_core_thread_handle_is_null(hart->current_thread)
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
    scheduler_core_bitmap_clear(candidate.renew_bitmap);
    scheduler_core_bitmap_clear(candidate.ready_bitmap);
    if (!scheduler_core_thread_handle_is_null(hart->current_thread)) {
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
        !scheduler_core_thread_handle_is_null(hart->current_thread)
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
            || scheduler_core_bitmap_test(
                candidate.renew_bitmap,
                selected_slot
            )
        ) {
            break;
        }
        if (++renew_count > MICROS_THREAD_CAPACITY) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        scheduler_core_bitmap_set(candidate.renew_bitmap, selected_slot);
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
        candidate.selected = scheduler_core_null_thread_handle();
    } else {
        candidate.selected = (struct micros_thread_handle){
            (uint16_t)selected_slot,
            objects->threads[selected_slot].generation,
        };
        candidate.action = scheduler_core_thread_handles_equal(
            candidate.selected,
            hart->current_thread
        )
            ? MICROS_SCHEDULER_RETURN_KEEP_CURRENT
            : MICROS_SCHEDULER_RETURN_SELECT_THREAD;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        candidate.ready_next[index] = scheduler_core_null_thread_handle();
    }
    for (
        priority = 0;
        priority < MICROS_SCHEDULER_PRIORITY_COUNT;
        ++priority
    ) {
        size_t count = plan_scratch.counts[priority];

        candidate.ready_head[priority] = scheduler_core_null_thread_handle();
        candidate.ready_tail[priority] = scheduler_core_null_thread_handle();
        for (index = 0; index < count; ++index) {
            uint16_t slot = plan_scratch.slots[priority][index];
            struct micros_thread_handle handle = {
                slot,
                objects->threads[slot].generation,
            };

            scheduler_core_bitmap_set(candidate.ready_bitmap, slot);
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
