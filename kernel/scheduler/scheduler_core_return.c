#include "kernel/scheduler/scheduler_core_private.h"

static bool plans_equal(
    const struct micros_scheduler_return_plan *left,
    const struct micros_scheduler_return_plan *right
)
{
    size_t index;

    if (
        !scheduler_core_hart_handles_equal(left->hart, right->hart)
        || !scheduler_core_thread_handles_equal(left->outgoing, right->outgoing)
        || !scheduler_core_thread_handles_equal(left->selected, right->selected)
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
            !scheduler_core_thread_handles_equal(
                left->ready_head[index],
                right->ready_head[index]
            )
            || !scheduler_core_thread_handles_equal(
                left->ready_tail[index],
                right->ready_tail[index]
            )
        ) {
            return false;
        }
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        if (
            !scheduler_core_thread_handles_equal(
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

        if (scheduler_core_bitmap_test(plan->renew_bitmap, index)) {
            thread->runtime_flags |= MICROS_THREAD_RTS_NO_QUANTUM;
            thread->remaining_counter_ticks =
                thread->quantum_counter_ticks;
            thread->runtime_flags &= ~MICROS_THREAD_RTS_NO_QUANTUM;
        }
        if (
            thread->scheduler_assigned
            && scheduler_core_hart_handles_equal(
                thread->scheduler_hart,
                plan->hart
            )
        ) {
            thread->ready_linked =
                scheduler_core_bitmap_test(plan->ready_bitmap, index);
            thread->ready_next = plan->ready_next[index];
        }
    }
    for (index = 0; index < MICROS_SCHEDULER_PRIORITY_COUNT; ++index) {
        hart->ready_head[index] = plan->ready_head[index];
        hart->ready_tail[index] = plan->ready_tail[index];
    }

    hart->current_thread = plan->selected;
    hart->reschedule_pending = false;
    if (scheduler_core_thread_handle_is_null(plan->selected)) {
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
