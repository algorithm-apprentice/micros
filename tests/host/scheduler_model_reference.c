#include "tests/host/scheduler_model_internal.h"

bool scheduler_model_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

bool scheduler_model_handle_is_null(struct micros_thread_handle handle)
{
    return handle.generation == 0;
}

size_t scheduler_model_queue_find(
    const struct model_state *model,
    size_t hart,
    uint8_t priority,
    size_t slot
)
{
    size_t index;

    for (
        index = 0;
        index < model->harts[hart].queue_counts[priority];
        ++index
    ) {
        if (model->harts[hart].queues[priority][index] == slot) {
            return index;
        }
    }
    return MODEL_NONE;
}

void scheduler_model_queue_remove(
    struct model_state *model,
    size_t hart,
    uint8_t priority,
    size_t slot
)
{
    size_t index = scheduler_model_queue_find(model, hart, priority, slot);
    size_t count = model->harts[hart].queue_counts[priority];

    if (index == MODEL_NONE) {
        return;
    }
    if (index != 0 && index + 1 != count) {
        ++model->coverage.remove_middle;
    }
    for (; index + 1 < count; ++index) {
        model->harts[hart].queues[priority][index] =
            model->harts[hart].queues[priority][index + 1];
    }
    --model->harts[hart].queue_counts[priority];
}

static void queue_head(
    struct model_state *model,
    size_t hart,
    uint8_t priority,
    size_t slot
)
{
    size_t count = model->harts[hart].queue_counts[priority];
    size_t index;

    for (index = count; index != 0; --index) {
        model->harts[hart].queues[priority][index] =
            model->harts[hart].queues[priority][index - 1];
    }
    model->harts[hart].queues[priority][0] = slot;
    ++model->harts[hart].queue_counts[priority];
}

static void queue_tail_without_preemption(
    struct model_state *model,
    size_t hart,
    uint8_t priority,
    size_t slot
)
{
    size_t *count = &model->harts[hart].queue_counts[priority];

    model->harts[hart].queues[priority][*count] = slot;
    ++*count;
}

void scheduler_model_queue_tail(
    struct model_state *model,
    size_t hart,
    uint8_t priority,
    size_t slot
)
{
    size_t current = model->harts[hart].current;

    queue_tail_without_preemption(model, hart, priority, slot);
    if (
        current != MODEL_NONE
        && current != slot
        && model->threads[current].flags == 0
        && model->threads[current].preemptible
    ) {
        if (model->threads[current].priority > priority) {
            model->threads[current].flags |=
                MICROS_THREAD_RTS_PREEMPTED;
            scheduler_model_queue_remove(
                model,
                hart,
                model->threads[current].priority,
                current
            );
            ++model->coverage.preempt_higher;
        } else if (model->threads[current].priority == priority) {
            ++model->coverage.enqueue_same;
        } else {
            ++model->coverage.enqueue_lower;
        }
    }
}

static size_t pick_ready(
    const struct model_state *model,
    size_t hart
)
{
    size_t priority;

    for (
        priority = 0;
        priority < MICROS_SCHEDULER_PRIORITY_COUNT;
        ++priority
    ) {
        if (model->harts[hart].queue_counts[priority] != 0) {
            return model->harts[hart].queues[priority][0];
        }
    }
    return MODEL_NONE;
}

bool scheduler_model_admit(
    struct scheduler_model_fixture *fixture,
    struct model_state *model,
    size_t slot,
    size_t hart,
    uint8_t priority,
    uint64_t quantum
)
{
    enum micros_kernel_object_error error =
        micros_thread_scheduler_admit(
            &fixture->objects,
            fixture->harts[hart],
            fixture->threads[slot],
            priority,
            quantum,
            true
        );

    if (error != MICROS_KERNEL_OBJECT_OK) {
        return false;
    }
    model->threads[slot].assigned = true;
    model->threads[slot].preemptible = true;
    model->threads[slot].flags = 0;
    model->threads[slot].priority = priority;
    model->threads[slot].quantum = quantum;
    model->threads[slot].remaining = quantum;
    model->threads[slot].hart = hart;
    scheduler_model_queue_tail(model, hart, priority, slot);
    return true;
}

bool scheduler_model_plan_and_commit(
    struct scheduler_model_fixture *fixture,
    struct model_state *model,
    size_t hart
)
{
    struct micros_scheduler_return_plan plan;
    size_t old_current = model->harts[hart].current;
    size_t selected;

    if (
        micros_hart_plan_user_return(
            &fixture->objects,
            fixture->harts[hart],
            &plan
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }

    if (
        old_current != MODEL_NONE
        && (
            model->threads[old_current].flags
            & MICROS_THREAD_RTS_PREEMPTED
        ) != 0
    ) {
        struct model_thread *current =
            &model->threads[old_current];

        current->flags &= ~MICROS_THREAD_RTS_PREEMPTED;
        if (current->flags == 0) {
            if (current->remaining == 0) {
                queue_tail_without_preemption(
                    model,
                    hart,
                    current->priority,
                    old_current
                );
            } else {
                queue_head(
                    model,
                    hart,
                    current->priority,
                    old_current
                );
            }
        }
    }
    selected = pick_ready(model, hart);
    if (
        selected != MODEL_NONE
        && model->threads[selected].flags == 0
        && model->threads[selected].remaining == 0
    ) {
        struct model_thread *candidate =
            &model->threads[selected];

        scheduler_model_queue_remove(model, hart, candidate->priority, selected);
        candidate->flags |= MICROS_THREAD_RTS_NO_QUANTUM;
        candidate->remaining = candidate->quantum;
        candidate->flags &= ~MICROS_THREAD_RTS_NO_QUANTUM;
        queue_tail_without_preemption(
            model,
            hart,
            candidate->priority,
            selected
        );
        ++model->coverage.quantum_renew;
        selected = pick_ready(model, hart);
        if (!plan.renew_quantum) {
            return false;
        }
    } else if (plan.renew_quantum) {
        return false;
    }
    if (selected == MODEL_NONE) {
        if (plan.action != MICROS_SCHEDULER_RETURN_ENTER_IDLE) {
            return false;
        }
        ++model->coverage.return_idle;
    } else if (
        !scheduler_model_handles_equal(plan.selected, fixture->threads[selected])
    ) {
        return false;
    } else if (selected == old_current) {
        if (
            plan.action
                != MICROS_SCHEDULER_RETURN_KEEP_CURRENT
        ) {
            return false;
        }
        ++model->coverage.return_keep;
    } else {
        if (
            plan.action
                != MICROS_SCHEDULER_RETURN_SELECT_THREAD
        ) {
            return false;
        }
        ++model->coverage.return_switch;
    }
    if (
        micros_hart_commit_user_return(&fixture->objects, &plan)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    model->harts[hart].current = selected;
    return true;
}
