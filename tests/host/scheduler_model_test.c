#include "micros/scheduler_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    MODEL_THREAD_COUNT = 8,
    MODEL_HART_COUNT = 2,
    MODEL_STEPS = 4096,
    MODEL_NONE = MODEL_THREAD_COUNT,
};

struct model_thread {
    bool assigned;
    bool preemptible;
    uint32_t flags;
    uint8_t priority;
    uint64_t quantum;
    uint64_t remaining;
    size_t hart;
};

struct model_hart {
    size_t queues[MICROS_SCHEDULER_PRIORITY_COUNT][MODEL_THREAD_COUNT];
    size_t queue_counts[MICROS_SCHEDULER_PRIORITY_COUNT];
    size_t current;
    enum micros_scheduler_accounting_owner accounting_owner;
    uint64_t accounting_started_at;
    size_t accounted_thread;
    uint64_t kernel_ticks;
    uint64_t idle_ticks;
    uint64_t counter;
};

struct model_coverage {
    size_t preempt_higher;
    size_t enqueue_same;
    size_t enqueue_lower;
    size_t remove_middle;
    size_t flag_first;
    size_t flag_additional;
    size_t flag_partial_clear;
    size_t flag_final_clear;
    size_t return_keep;
    size_t return_switch;
    size_t return_idle;
    size_t quantum_renew;
    size_t thread_to_kernel;
    size_t kernel_to_thread;
    size_t kernel_to_idle;
    size_t idle_to_kernel;
    size_t policy_runnable;
    size_t policy_blocked;
};

struct model_state {
    struct model_thread threads[MODEL_THREAD_COUNT];
    struct model_hart harts[MODEL_HART_COUNT];
    struct model_coverage coverage;
};

static struct micros_kernel_objects objects;
static struct micros_process_handle process;
static struct micros_thread_handle threads[MODEL_THREAD_COUNT];
static struct micros_hart_handle harts[MODEL_HART_COUNT];

bool micros_scheduler_model_test_run(void);

#define MODEL_FAIL(step, operation, value, message) \
    do { \
        fprintf( \
            stderr, \
            "scheduler model seed=0x5c4ed026 step=%zu " \
            "operation=%s value=0x%08x: %s\n", \
            (step), \
            (operation), \
            (unsigned)(value), \
            (message) \
        ); \
        return false; \
    } while (false)

static bool handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

static bool handle_is_null(struct micros_thread_handle handle)
{
    return handle.generation == 0;
}

static struct micros_user_context context_pattern(uint64_t base)
{
    struct micros_user_context context;
    uint64_t words[
        sizeof(struct micros_user_context) / sizeof(uint64_t)
    ];
    size_t index;

    for (
        index = 0;
        index < sizeof(words) / sizeof(words[0]);
        ++index
    ) {
        words[index] = base + index;
    }
    memcpy(&context, words, sizeof(context));
    return context;
}

static bool setup_fixture(void)
{
    size_t index;

    memset(&objects, 0, sizeof(objects));
    if (
        micros_kernel_objects_initialize(
            &objects,
            MODEL_THREAD_COUNT,
            MODEL_HART_COUNT
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_hart_register(&objects, 0, &harts[0])
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_register(&objects, 1, &harts[1])
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_install_trap_stacks(
            &objects,
            harts[0],
            UINT64_C(0x01000000),
            UINT64_C(0x01004000),
            UINT64_C(0x02000000),
            UINT64_C(0x02001000)
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_hart_install_trap_stacks(
            &objects,
            harts[1],
            UINT64_C(0x03000000),
            UINT64_C(0x03004000),
            UINT64_C(0x04000000),
            UINT64_C(0x04001000)
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(&objects, &process)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    for (index = 0; index < MODEL_THREAD_COUNT; ++index) {
        struct micros_user_context context =
            context_pattern(UINT64_C(0x5000) + index * UINT64_C(0x100));
        uintptr_t bottom =
            UINT64_C(0x10000000)
            + index * UINT64_C(0x00008000);

        if (
            micros_thread_create(
                &objects,
                process,
                &threads[index]
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_attach_execution_context(
                &objects,
                threads[index],
                bottom,
                bottom + MICROS_THREAD_KERNEL_STACK_SIZE,
                &context
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
    }
    return true;
}

static uint32_t next_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static size_t queue_find(
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

static void queue_remove(
    struct model_state *model,
    size_t hart,
    uint8_t priority,
    size_t slot
)
{
    size_t index = queue_find(model, hart, priority, slot);
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

static void queue_tail(
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
            queue_remove(
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

static bool admit(
    struct model_state *model,
    size_t slot,
    size_t hart,
    uint8_t priority,
    uint64_t quantum
)
{
    enum micros_kernel_object_error error =
        micros_thread_scheduler_admit(
            &objects,
            harts[hart],
            threads[slot],
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
    queue_tail(model, hart, priority, slot);
    return true;
}

static bool plan_and_commit(
    struct model_state *model,
    size_t hart
)
{
    struct micros_scheduler_return_plan plan;
    size_t old_current = model->harts[hart].current;
    size_t selected;

    if (
        micros_hart_plan_user_return(
            &objects,
            harts[hart],
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

        queue_remove(model, hart, candidate->priority, selected);
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
        !handles_equal(plan.selected, threads[selected])
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
        micros_hart_commit_user_return(&objects, &plan)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    model->harts[hart].current = selected;
    return true;
}

static bool model_matches(
    const struct model_state *model,
    size_t step,
    const char *operation,
    uint32_t value
)
{
    size_t hart_index;
    size_t thread_index;

    if (
        micros_scheduler_core_validate(&objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        MODEL_FAIL(step, operation, value, "production validator");
    }
    for (hart_index = 0; hart_index < MODEL_HART_COUNT; ++hart_index) {
        const struct micros_hart *actual =
            &objects.harts[harts[hart_index].slot];
        const struct model_hart *expected = &model->harts[hart_index];
        size_t priority;

        if (
            actual->accounting_owner != expected->accounting_owner
            || actual->accounting_started_at
                != expected->accounting_started_at
            || actual->kernel_counter_ticks != expected->kernel_ticks
            || actual->idle_counter_ticks != expected->idle_ticks
        ) {
            MODEL_FAIL(step, operation, value, "accounting mismatch");
        }
        if (expected->accounted_thread == MODEL_NONE) {
            if (!handle_is_null(actual->accounted_thread)) {
                MODEL_FAIL(
                    step,
                    operation,
                    value,
                    "unexpected accounted thread"
                );
            }
        } else if (
            !handles_equal(
                actual->accounted_thread,
                threads[expected->accounted_thread]
            )
        ) {
            MODEL_FAIL(step, operation, value, "accounted thread");
        }
        if (expected->current == MODEL_NONE) {
            if (!handle_is_null(actual->current_thread)) {
                MODEL_FAIL(step, operation, value, "unexpected current");
            }
        } else if (
            !handles_equal(
                actual->current_thread,
                threads[expected->current]
            )
        ) {
            MODEL_FAIL(step, operation, value, "current mismatch");
        }
        for (
            priority = 0;
            priority < MICROS_SCHEDULER_PRIORITY_COUNT;
            ++priority
        ) {
            struct micros_thread_handle cursor =
                actual->ready_head[priority];
            size_t count = expected->queue_counts[priority];
            size_t queue_index;

            if (count == 0) {
                if (
                    !handle_is_null(actual->ready_head[priority])
                    || !handle_is_null(actual->ready_tail[priority])
                ) {
                    MODEL_FAIL(step, operation, value, "nonempty queue");
                }
                continue;
            }
            if (
                !handles_equal(
                    actual->ready_tail[priority],
                    threads[expected->queues[priority][count - 1]]
                )
            ) {
                MODEL_FAIL(step, operation, value, "tail mismatch");
            }
            for (queue_index = 0; queue_index < count; ++queue_index) {
                size_t expected_slot =
                    expected->queues[priority][queue_index];

                if (!handles_equal(cursor, threads[expected_slot])) {
                    MODEL_FAIL(step, operation, value, "walk mismatch");
                }
                cursor = objects.threads[cursor.slot].ready_next;
            }
            if (!handle_is_null(cursor)) {
                MODEL_FAIL(step, operation, value, "unterminated queue");
            }
        }
    }
    for (
        thread_index = 0;
        thread_index < MODEL_THREAD_COUNT;
        ++thread_index
    ) {
        const struct model_thread *expected =
            &model->threads[thread_index];
        const struct micros_thread *actual =
            &objects.threads[threads[thread_index].slot];
        bool expected_linked = false;
        size_t expected_next = MODEL_NONE;
        size_t hart_index;

        for (
            hart_index = 0;
            hart_index < MODEL_HART_COUNT;
            ++hart_index
        ) {
            if (
                expected->assigned
                && queue_find(
                    model,
                    hart_index,
                    expected->priority,
                    thread_index
                ) != MODEL_NONE
            ) {
                size_t queue_index = queue_find(
                    model,
                    hart_index,
                    expected->priority,
                    thread_index
                );
                size_t count = model->harts[hart_index]
                    .queue_counts[expected->priority];

                expected_linked = true;
                if (queue_index + 1 < count) {
                    expected_next = model->harts[hart_index]
                        .queues[expected->priority][queue_index + 1];
                }
            }
        }
        if (
            actual->scheduler_assigned != expected->assigned
            || actual->runtime_flags != expected->flags
            || actual->ready_linked != expected_linked
        ) {
            MODEL_FAIL(step, operation, value, "thread state");
        }
        if (expected_next == MODEL_NONE) {
            if (!handle_is_null(actual->ready_next)) {
                MODEL_FAIL(step, operation, value, "stale next link");
            }
        } else if (
            !handles_equal(
                actual->ready_next,
                threads[expected_next]
            )
        ) {
            MODEL_FAIL(step, operation, value, "next link");
        }
        if (
            expected->assigned
            && (
                actual->scheduler_priority != expected->priority
                || actual->quantum_counter_ticks != expected->quantum
                || actual->remaining_counter_ticks
                    != expected->remaining
                || actual->scheduler_hart.slot
                    != harts[expected->hart].slot
                || actual->scheduler_hart.generation
                    != harts[expected->hart].generation
            )
        ) {
            MODEL_FAIL(step, operation, value, "thread policy");
        }
    }
    return true;
}

static bool initialize_model(struct model_state *model)
{
    size_t hart;

    memset(model, 0, sizeof(*model));
    for (hart = 0; hart < MODEL_HART_COUNT; ++hart) {
        model->harts[hart].current = MODEL_NONE;
        model->harts[hart].accounted_thread = MODEL_NONE;
        model->harts[hart].counter = 100;
        if (
            micros_scheduler_accounting_initialize(
                &objects,
                harts[hart],
                model->harts[hart].counter
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
        model->harts[hart].accounting_owner =
            MICROS_SCHEDULER_ACCOUNTING_KERNEL;
        model->harts[hart].accounting_started_at =
            model->harts[hart].counter;
    }
    return true;
}

static bool run_required_prelude(struct model_state *model)
{
    if (
        !admit(model, 0, 0, 10, 60)
        || !admit(model, 1, 0, 10, 60)
        || !admit(model, 2, 0, 10, 60)
        || !plan_and_commit(model, 0)
    ) {
        return false;
    }
    model->harts[0].counter += 10;
    if (
        micros_scheduler_account_enter_thread(
            &objects,
            harts[0],
            threads[0],
            model->harts[0].counter
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    model->harts[0].kernel_ticks += 10;
    model->harts[0].accounting_owner =
        MICROS_SCHEDULER_ACCOUNTING_THREAD;
    model->harts[0].accounting_started_at =
        model->harts[0].counter;
    model->harts[0].accounted_thread = 0;
    ++model->coverage.kernel_to_thread;

    model->harts[0].counter += 15;
    if (
        micros_scheduler_account_user_trap(
            &objects,
            harts[0],
            model->harts[0].counter
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    model->threads[0].remaining -= 15;
    model->harts[0].accounting_owner =
        MICROS_SCHEDULER_ACCOUNTING_KERNEL;
    model->harts[0].accounting_started_at =
        model->harts[0].counter;
    model->harts[0].accounted_thread = MODEL_NONE;
    ++model->coverage.thread_to_kernel;

    if (
        !admit(model, 3, 0, 3, 60)
        || !plan_and_commit(model, 0)
        || !admit(model, 4, 1, 8, 50)
        || !admit(model, 5, 1, 8, 50)
        || !admit(model, 6, 1, 12, 50)
        || !plan_and_commit(model, 1)
        || !admit(model, 7, 1, 1, 50)
        || !plan_and_commit(model, 1)
    ) {
        return false;
    }
    if (!plan_and_commit(model, 0)) {
        return false;
    }

    if (
        micros_thread_scheduler_hold(&objects, threads[1])
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    queue_remove(model, 0, 10, 1);
    model->threads[1].flags |= MICROS_THREAD_RTS_INACTIVE;
    ++model->coverage.flag_first;
    if (
        micros_thread_runtime_flags_set(
            &objects,
            threads[1],
            MICROS_THREAD_RTS_NO_QUANTUM
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    model->threads[1].flags |= MICROS_THREAD_RTS_NO_QUANTUM;
    ++model->coverage.flag_additional;
    if (
        micros_thread_install_policy(
            &objects,
            threads[1],
            10,
            70
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    model->threads[1].quantum = 70;
    model->threads[1].remaining = 70;
    model->threads[1].flags &= ~MICROS_THREAD_RTS_NO_QUANTUM;
    ++model->coverage.policy_blocked;
    ++model->coverage.flag_partial_clear;
    if (
        micros_thread_runtime_flags_unset(
            &objects,
            threads[1],
            MICROS_THREAD_RTS_INACTIVE
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    model->threads[1].flags &= ~MICROS_THREAD_RTS_INACTIVE;
    queue_tail(model, 0, 10, 1);
    ++model->coverage.flag_final_clear;

    if (
        micros_thread_install_policy(
            &objects,
            threads[2],
            3,
            80
        )
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    queue_remove(model, 0, 10, 2);
    model->threads[2].priority = 3;
    model->threads[2].quantum = 80;
    model->threads[2].remaining = 80;
    queue_tail(model, 0, 3, 2);
    ++model->coverage.policy_runnable;

    model->harts[0].counter += 5;
    if (
        micros_scheduler_account_enter_thread(
            &objects,
            harts[0],
            threads[3],
            model->harts[0].counter
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    model->harts[0].kernel_ticks += 5;
    model->harts[0].accounting_owner =
        MICROS_SCHEDULER_ACCOUNTING_THREAD;
    model->harts[0].accounting_started_at =
        model->harts[0].counter;
    model->harts[0].accounted_thread = 3;
    ++model->coverage.kernel_to_thread;
    model->harts[0].counter += 100;
    if (
        micros_scheduler_account_user_trap(
            &objects,
            harts[0],
            model->harts[0].counter
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    model->threads[3].remaining = 0;
    model->harts[0].accounting_owner =
        MICROS_SCHEDULER_ACCOUNTING_KERNEL;
    model->harts[0].accounting_started_at =
        model->harts[0].counter;
    model->harts[0].accounted_thread = MODEL_NONE;
    ++model->coverage.thread_to_kernel;
    if (!plan_and_commit(model, 0)) {
        return false;
    }

    {
        size_t slots[] = {4, 5, 6, 7};
        size_t index;

        for (index = 0; index < 4; ++index) {
            size_t slot = slots[index];
            struct model_thread *thread = &model->threads[slot];

            if (
                micros_thread_scheduler_hold(
                    &objects,
                    threads[slot]
                ) != MICROS_KERNEL_OBJECT_OK
            ) {
                return false;
            }
            if (thread->flags == 0) {
                queue_remove(
                    model,
                    thread->hart,
                    thread->priority,
                    slot
                );
            }
            thread->flags |= MICROS_THREAD_RTS_INACTIVE;
        }
        if (!plan_and_commit(model, 1)) {
            return false;
        }
        model->harts[1].counter += 5;
        if (
            micros_scheduler_account_enter_idle(
                &objects,
                harts[1],
                model->harts[1].counter
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
        model->harts[1].kernel_ticks += 5;
        model->harts[1].accounting_owner =
            MICROS_SCHEDULER_ACCOUNTING_IDLE;
        model->harts[1].accounting_started_at =
            model->harts[1].counter;
        ++model->coverage.kernel_to_idle;
        model->harts[1].counter += 7;
        if (
            micros_scheduler_account_idle_trap(
                &objects,
                harts[1],
                model->harts[1].counter
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
        model->harts[1].idle_ticks += 7;
        model->harts[1].accounting_owner =
            MICROS_SCHEDULER_ACCOUNTING_KERNEL;
        model->harts[1].accounting_started_at =
            model->harts[1].counter;
        ++model->coverage.idle_to_kernel;
        for (index = 0; index < 4; ++index) {
            size_t slot = slots[index];
            struct model_thread *thread = &model->threads[slot];

            if (
                micros_thread_runtime_flags_unset(
                    &objects,
                    threads[slot],
                    MICROS_THREAD_RTS_INACTIVE
                ) != MICROS_KERNEL_OBJECT_OK
            ) {
                return false;
            }
            thread->flags &= ~MICROS_THREAD_RTS_INACTIVE;
            queue_tail(
                model,
                thread->hart,
                thread->priority,
                slot
            );
        }
    }
    return true;
}

bool micros_scheduler_model_test_run(void)
{
    const uint32_t seed = UINT32_C(0x5c4ed026);
    struct model_state model;
    uint32_t random_state = seed;
    size_t step;

    if (
        !setup_fixture()
        || !initialize_model(&model)
        || !run_required_prelude(&model)
        || !model_matches(&model, 0, "prelude", seed)
    ) {
        return false;
    }

    for (step = 1; step <= MODEL_STEPS; ++step) {
        uint32_t value = next_random(&random_state);
        size_t slot = (value >> 8) % MODEL_THREAD_COUNT;
        struct model_thread *thread = &model.threads[slot];
        size_t hart = thread->assigned
            ? thread->hart
            : ((value >> 16) & 1U);
        const char *operation = "noop";
        enum micros_kernel_object_error error;

        switch (value % 10) {
        case 0:
            operation = "hold";
            if (
                thread->assigned
                && model.harts[hart].accounting_owner
                    == MICROS_SCHEDULER_ACCOUNTING_THREAD
            ) {
                break;
            }
            error = micros_thread_scheduler_hold(
                &objects,
                threads[slot]
            );
            if (
                thread->assigned
                && (thread->flags & MICROS_THREAD_RTS_INACTIVE) == 0
            ) {
                if (error != MICROS_KERNEL_OBJECT_OK) {
                    MODEL_FAIL(step, operation, value, "unexpected error");
                }
                if (thread->flags == 0) {
                    queue_remove(
                        &model,
                        hart,
                        thread->priority,
                        slot
                    );
                    ++model.coverage.flag_first;
                } else {
                    ++model.coverage.flag_additional;
                }
                thread->flags |= MICROS_THREAD_RTS_INACTIVE;
            } else if (error != MICROS_KERNEL_OBJECT_ERROR_STATE) {
                MODEL_FAIL(step, operation, value, "wrong rejection");
            }
            break;
        case 1:
            operation = "wake";
            if (
                thread->assigned
                && model.harts[hart].accounting_owner
                    == MICROS_SCHEDULER_ACCOUNTING_THREAD
            ) {
                break;
            }
            error = micros_thread_runtime_flags_unset(
                &objects,
                threads[slot],
                MICROS_THREAD_RTS_INACTIVE
            );
            if (
                thread->assigned
                && (thread->flags & MICROS_THREAD_RTS_INACTIVE) != 0
                && (
                    model.harts[hart].current != slot
                    || (
                        thread->flags
                        & ~MICROS_THREAD_RTS_INACTIVE
                    ) != 0
                )
            ) {
                if (error != MICROS_KERNEL_OBJECT_OK) {
                    MODEL_FAIL(step, operation, value, "unexpected error");
                }
                thread->flags &= ~MICROS_THREAD_RTS_INACTIVE;
                if (thread->flags == 0) {
                    queue_tail(
                        &model,
                        hart,
                        thread->priority,
                        slot
                    );
                    ++model.coverage.flag_final_clear;
                } else {
                    ++model.coverage.flag_partial_clear;
                }
            } else if (
                error != MICROS_KERNEL_OBJECT_ERROR_STATE
                && error != MICROS_KERNEL_OBJECT_ERROR_ARGUMENT
            ) {
                MODEL_FAIL(step, operation, value, "wrong rejection");
            }
            break;
        case 2:
            operation = "set-no-quantum";
            if (
                thread->assigned
                && model.harts[hart].accounting_owner
                    == MICROS_SCHEDULER_ACCOUNTING_THREAD
            ) {
                break;
            }
            error = micros_thread_runtime_flags_set(
                &objects,
                threads[slot],
                MICROS_THREAD_RTS_NO_QUANTUM
            );
            if (thread->assigned) {
                if (error != MICROS_KERNEL_OBJECT_OK) {
                    MODEL_FAIL(step, operation, value, "unexpected error");
                }
                if (thread->flags == 0) {
                    queue_remove(
                        &model,
                        hart,
                        thread->priority,
                        slot
                    );
                    ++model.coverage.flag_first;
                } else {
                    ++model.coverage.flag_additional;
                }
                thread->flags |= MICROS_THREAD_RTS_NO_QUANTUM;
            } else if (error != MICROS_KERNEL_OBJECT_ERROR_STATE) {
                MODEL_FAIL(step, operation, value, "wrong rejection");
            }
            break;
        case 3:
            operation = "clear-no-quantum";
            if (
                thread->assigned
                && model.harts[hart].accounting_owner
                    == MICROS_SCHEDULER_ACCOUNTING_THREAD
            ) {
                break;
            }
            error = micros_thread_runtime_flags_unset(
                &objects,
                threads[slot],
                MICROS_THREAD_RTS_NO_QUANTUM
            );
            if (
                thread->assigned
                && (thread->flags & MICROS_THREAD_RTS_NO_QUANTUM) != 0
                && (
                    model.harts[hart].current != slot
                    || (
                        thread->flags
                        & ~MICROS_THREAD_RTS_NO_QUANTUM
                    ) != 0
                )
            ) {
                if (error != MICROS_KERNEL_OBJECT_OK) {
                    MODEL_FAIL(step, operation, value, "unexpected error");
                }
                thread->flags &= ~MICROS_THREAD_RTS_NO_QUANTUM;
                if (thread->flags == 0) {
                    queue_tail(
                        &model,
                        hart,
                        thread->priority,
                        slot
                    );
                    ++model.coverage.flag_final_clear;
                } else {
                    ++model.coverage.flag_partial_clear;
                }
            } else if (
                error != MICROS_KERNEL_OBJECT_ERROR_STATE
                && error != MICROS_KERNEL_OBJECT_ERROR_ARGUMENT
            ) {
                MODEL_FAIL(step, operation, value, "wrong rejection");
            }
            break;
        case 4:
            operation = "return";
            if (
                model.harts[hart].accounting_owner
                    != MICROS_SCHEDULER_ACCOUNTING_KERNEL
            ) {
                break;
            }
            if (!plan_and_commit(&model, hart)) {
                MODEL_FAIL(step, operation, value, "plan or commit");
            }
            break;
        case 5:
            operation = "enter-thread";
            model.harts[hart].counter += 1 + (value & 15U);
            error = micros_scheduler_account_enter_thread(
                &objects,
                harts[hart],
                model.harts[hart].current == MODEL_NONE
                    ? threads[0]
                    : threads[model.harts[hart].current],
                model.harts[hart].counter
            );
            if (
                model.harts[hart].accounting_owner
                    == MICROS_SCHEDULER_ACCOUNTING_KERNEL
                && model.harts[hart].current != MODEL_NONE
                && model.threads[
                    model.harts[hart].current
                ].flags == 0
            ) {
                uint64_t delta =
                    model.harts[hart].counter
                    - model.harts[hart].accounting_started_at;

                if (error != MICROS_KERNEL_OBJECT_OK) {
                    MODEL_FAIL(step, operation, value, "unexpected error");
                }
                model.harts[hart].kernel_ticks += delta;
                model.harts[hart].accounting_owner =
                    MICROS_SCHEDULER_ACCOUNTING_THREAD;
                model.harts[hart].accounting_started_at =
                    model.harts[hart].counter;
                model.harts[hart].accounted_thread =
                    model.harts[hart].current;
                ++model.coverage.kernel_to_thread;
            } else if (error != MICROS_KERNEL_OBJECT_ERROR_STATE) {
                MODEL_FAIL(step, operation, value, "wrong rejection");
            }
            break;
        case 6:
            operation = "user-trap";
            model.harts[hart].counter += 1 + (value & 31U);
            error = micros_scheduler_account_user_trap(
                &objects,
                harts[hart],
                model.harts[hart].counter
            );
            if (
                model.harts[hart].accounting_owner
                    == MICROS_SCHEDULER_ACCOUNTING_THREAD
            ) {
                size_t accounted =
                    model.harts[hart].accounted_thread;
                uint64_t delta =
                    model.harts[hart].counter
                    - model.harts[hart].accounting_started_at;

                if (error != MICROS_KERNEL_OBJECT_OK) {
                    MODEL_FAIL(step, operation, value, "unexpected error");
                }
                model.threads[accounted].remaining =
                    delta >= model.threads[accounted].remaining
                        ? 0
                        : model.threads[accounted].remaining - delta;
                model.harts[hart].accounting_owner =
                    MICROS_SCHEDULER_ACCOUNTING_KERNEL;
                model.harts[hart].accounting_started_at =
                    model.harts[hart].counter;
                model.harts[hart].accounted_thread = MODEL_NONE;
                ++model.coverage.thread_to_kernel;
            } else if (error != MICROS_KERNEL_OBJECT_ERROR_STATE) {
                MODEL_FAIL(step, operation, value, "wrong rejection");
            }
            break;
        case 7:
            operation = "enter-idle";
            model.harts[hart].counter += 1 + (value & 7U);
            error = micros_scheduler_account_enter_idle(
                &objects,
                harts[hart],
                model.harts[hart].counter
            );
            if (
                model.harts[hart].accounting_owner
                    == MICROS_SCHEDULER_ACCOUNTING_KERNEL
                && model.harts[hart].current == MODEL_NONE
            ) {
                uint64_t delta =
                    model.harts[hart].counter
                    - model.harts[hart].accounting_started_at;

                if (error != MICROS_KERNEL_OBJECT_OK) {
                    MODEL_FAIL(step, operation, value, "unexpected error");
                }
                model.harts[hart].kernel_ticks += delta;
                model.harts[hart].accounting_owner =
                    MICROS_SCHEDULER_ACCOUNTING_IDLE;
                model.harts[hart].accounting_started_at =
                    model.harts[hart].counter;
                ++model.coverage.kernel_to_idle;
            } else if (error != MICROS_KERNEL_OBJECT_ERROR_STATE) {
                MODEL_FAIL(step, operation, value, "wrong rejection");
            }
            break;
        case 8:
            operation = "idle-trap";
            model.harts[hart].counter += 1 + (value & 7U);
            error = micros_scheduler_account_idle_trap(
                &objects,
                harts[hart],
                model.harts[hart].counter
            );
            if (
                model.harts[hart].accounting_owner
                    == MICROS_SCHEDULER_ACCOUNTING_IDLE
            ) {
                uint64_t delta =
                    model.harts[hart].counter
                    - model.harts[hart].accounting_started_at;

                if (error != MICROS_KERNEL_OBJECT_OK) {
                    MODEL_FAIL(step, operation, value, "unexpected error");
                }
                model.harts[hart].idle_ticks += delta;
                model.harts[hart].accounting_owner =
                    MICROS_SCHEDULER_ACCOUNTING_KERNEL;
                model.harts[hart].accounting_started_at =
                    model.harts[hart].counter;
                ++model.coverage.idle_to_kernel;
            } else if (error != MICROS_KERNEL_OBJECT_ERROR_STATE) {
                MODEL_FAIL(step, operation, value, "wrong rejection");
            }
            break;
        default: {
            uint8_t priority = (uint8_t)((value >> 20) & 15U);
            uint64_t quantum = 32 + (value & 63U);

            operation = "policy";
            if (
                thread->assigned
                && model.harts[hart].accounting_owner
                    == MICROS_SCHEDULER_ACCOUNTING_THREAD
            ) {
                break;
            }
            error = micros_thread_install_policy(
                &objects,
                threads[slot],
                priority,
                quantum
            );
            if (
                thread->assigned
                && model.harts[hart].current != slot
            ) {
                bool was_runnable = thread->flags == 0;

                if (error != MICROS_KERNEL_OBJECT_OK) {
                    MODEL_FAIL(step, operation, value, "unexpected error");
                }
                if (was_runnable) {
                    queue_remove(
                        &model,
                        hart,
                        thread->priority,
                        slot
                    );
                    ++model.coverage.policy_runnable;
                } else {
                    ++model.coverage.policy_blocked;
                }
                thread->priority = priority;
                thread->quantum = quantum;
                thread->remaining = quantum;
                thread->flags &= ~MICROS_THREAD_RTS_NO_QUANTUM;
                if (thread->flags == 0) {
                    queue_tail(&model, hart, priority, slot);
                }
            } else if (error != MICROS_KERNEL_OBJECT_ERROR_STATE) {
                MODEL_FAIL(step, operation, value, "wrong rejection");
            }
            break;
        }
        }
        if (!model_matches(&model, step, operation, value)) {
            return false;
        }
    }

    if (
        model.coverage.preempt_higher == 0
        || model.coverage.enqueue_same == 0
        || model.coverage.enqueue_lower == 0
        || model.coverage.remove_middle == 0
        || model.coverage.flag_first == 0
        || model.coverage.flag_additional == 0
        || model.coverage.flag_partial_clear == 0
        || model.coverage.flag_final_clear == 0
        || model.coverage.return_keep == 0
        || model.coverage.return_switch == 0
        || model.coverage.return_idle == 0
        || model.coverage.quantum_renew == 0
        || model.coverage.thread_to_kernel == 0
        || model.coverage.kernel_to_thread == 0
        || model.coverage.kernel_to_idle == 0
        || model.coverage.idle_to_kernel == 0
        || model.coverage.policy_runnable == 0
        || model.coverage.policy_blocked == 0
    ) {
        fprintf(stderr, "scheduler model coverage incomplete\n");
        return false;
    }
    return true;
}
