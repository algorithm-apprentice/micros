#include "kernel/scheduler/scheduler_core_private.h"

static bool queue_deltas_equal(
    const struct micros_scheduler_return_queue *left,
    const struct micros_scheduler_return_queue *right
)
{
    size_t index;

    if (left->priority != right->priority || left->patch_count != right->patch_count
        || left->renew_count != right->renew_count
        || !scheduler_core_thread_handles_equal(left->before_head, right->before_head)
        || !scheduler_core_thread_handles_equal(left->before_tail, right->before_tail)
        || !scheduler_core_thread_handles_equal(left->after_head, right->after_head)
        || !scheduler_core_thread_handles_equal(left->after_tail, right->after_tail)
        || !scheduler_core_thread_handles_equal(left->renew_first, right->renew_first)) {
        return false;
    }
    for (index = 0; index < 3; ++index) {
        if (!scheduler_core_thread_handles_equal(left->patches[index].thread, right->patches[index].thread)
            || !scheduler_core_thread_handles_equal(left->patches[index].next, right->patches[index].next)) {
            return false;
        }
    }
    return true;
}

static bool plans_equal(
    const struct micros_scheduler_return_plan *left,
    const struct micros_scheduler_return_plan *right
)
{
    size_t index;

    if (!scheduler_core_hart_handles_equal(left->hart, right->hart)
        || !scheduler_core_thread_handles_equal(left->outgoing, right->outgoing)
        || !scheduler_core_thread_handles_equal(left->selected, right->selected)
        || left->action != right->action || left->repair_preempted != right->repair_preempted
        || left->renew_quantum != right->renew_quantum || left->renew_current != right->renew_current
        || left->publish_current != right->publish_current
        || left->queue_count != right->queue_count || left->queue_count > 2) {
        return false;
    }
    for (index = 0; index < 2; ++index) {
        if (!queue_deltas_equal(&left->queues[index], &right->queues[index])) {
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

static void renew_return_thread(struct micros_thread *thread)
{
    thread->runtime_flags |= MICROS_THREAD_RTS_NO_QUANTUM;
    thread->remaining_counter_ticks = thread->quantum_counter_ticks;
    thread->runtime_flags &= ~MICROS_THREAD_RTS_NO_QUANTUM;
}

void micros_scheduler_apply_return_plan(
    struct micros_kernel_objects *objects,
    const struct micros_scheduler_return_plan *plan
)
{
    struct micros_hart *hart = &objects->harts[plan->hart.slot];
    size_t index;

    if (plan->repair_preempted) {
        struct micros_thread *outgoing = &objects->threads[plan->outgoing.slot];

        outgoing->runtime_flags &= ~MICROS_THREAD_RTS_PREEMPTED;
    }
    if (plan->renew_current) {
        renew_return_thread(&objects->threads[plan->outgoing.slot]);
    }
    for (index = 0; index < plan->queue_count; ++index) {
        const struct micros_scheduler_return_queue *queue = &plan->queues[index];
        struct micros_thread_handle cursor = queue->renew_first;
        size_t renewed;

        for (renewed = 0; renewed < queue->renew_count; ++renewed) {
            struct micros_thread *thread = &objects->threads[cursor.slot];

            cursor = thread->ready_next;
            renew_return_thread(thread);
        }
    }
    if (plan->publish_current) {
        objects->threads[plan->outgoing.slot].ready_linked = true;
    }
    for (index = 0; index < plan->queue_count; ++index) {
        const struct micros_scheduler_return_queue *queue = &plan->queues[index];
        size_t patch;

        for (patch = 0; patch < queue->patch_count; ++patch) {
            objects->threads[queue->patches[patch].thread.slot].ready_next = queue->patches[patch].next;
        }
        hart->ready_head[queue->priority] = queue->after_head;
        hart->ready_tail[queue->priority] = queue->after_tail;
    }
    hart->current_thread = plan->selected;
    hart->reschedule_pending = false;
    if (scheduler_core_thread_handle_is_null(plan->selected)) {
        hart->trap.primary_stack_bottom = hart->idle_primary_stack_bottom;
        hart->trap.primary_stack_top = hart->idle_primary_stack_top;
    } else {
        const struct micros_thread *selected = &objects->threads[plan->selected.slot];

        hart->trap.primary_stack_bottom = selected->kernel_stack_bottom;
        hart->trap.primary_stack_top = selected->kernel_stack_top;
    }
}
