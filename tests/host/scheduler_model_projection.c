#include "tests/host/scheduler_model_internal.h"

static bool project_hart_accounting(
    struct scheduler_model_fixture *fixture,
    const struct micros_hart *actual,
    const struct model_hart *expected,
    size_t step,
    const char *operation,
    uint32_t value
)
{
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
        if (!scheduler_model_handle_is_null(actual->accounted_thread)) {
            MODEL_FAIL(
                step,
                operation,
                value,
                "unexpected accounted thread"
            );
        }
    } else if (
        !scheduler_model_handles_equal(
            actual->accounted_thread,
            fixture->threads[expected->accounted_thread]
        )
    ) {
        MODEL_FAIL(step, operation, value, "accounted thread");
    }
    if (expected->current == MODEL_NONE) {
        if (!scheduler_model_handle_is_null(actual->current_thread)) {
            MODEL_FAIL(step, operation, value, "unexpected current");
        }
    } else if (
        !scheduler_model_handles_equal(
            actual->current_thread,
            fixture->threads[expected->current]
        )
    ) {
        MODEL_FAIL(step, operation, value, "current mismatch");
    }
    return true;
}

static bool project_hart_queues(
    struct scheduler_model_fixture *fixture,
    const struct micros_hart *actual,
    const struct model_hart *expected,
    size_t step,
    const char *operation,
    uint32_t value
)
{
    size_t priority;

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
                !scheduler_model_handle_is_null(actual->ready_head[priority])
                || !scheduler_model_handle_is_null(actual->ready_tail[priority])
            ) {
                MODEL_FAIL(step, operation, value, "nonempty queue");
            }
            continue;
        }
        if (
            !scheduler_model_handles_equal(
                actual->ready_tail[priority],
                fixture->threads[expected->queues[priority][count - 1]]
            )
        ) {
            MODEL_FAIL(step, operation, value, "tail mismatch");
        }
        for (queue_index = 0; queue_index < count; ++queue_index) {
            size_t expected_slot =
                expected->queues[priority][queue_index];

            if (!scheduler_model_handles_equal(cursor, fixture->threads[expected_slot])) {
                MODEL_FAIL(step, operation, value, "walk mismatch");
            }
            cursor = fixture->objects.threads[cursor.slot].ready_next;
        }
        if (!scheduler_model_handle_is_null(cursor)) {
            MODEL_FAIL(step, operation, value, "unterminated queue");
        }
    }
    return true;
}

static bool project_thread_state(
    struct scheduler_model_fixture *fixture,
    const struct model_state *model,
    size_t thread_index,
    size_t step,
    const char *operation,
    uint32_t value
)
{
    const struct model_thread *expected =
        &model->threads[thread_index];
    const struct micros_thread *actual =
        &fixture->objects.threads[fixture->threads[thread_index].slot];
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
            && scheduler_model_queue_find(
                model,
                hart_index,
                expected->priority,
                thread_index
            ) != MODEL_NONE
        ) {
            size_t queue_index = scheduler_model_queue_find(
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
        if (!scheduler_model_handle_is_null(actual->ready_next)) {
            MODEL_FAIL(step, operation, value, "stale next link");
        }
    } else if (
        !scheduler_model_handles_equal(
            actual->ready_next,
            fixture->threads[expected_next]
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
                != fixture->harts[expected->hart].slot
            || actual->scheduler_hart.generation
                != fixture->harts[expected->hart].generation
        )
    ) {
        MODEL_FAIL(step, operation, value, "thread policy");
    }
    return true;
}

bool scheduler_model_model_matches(
    struct scheduler_model_fixture *fixture,
    const struct model_state *model,
    size_t step,
    const char *operation,
    uint32_t value
)
{
    size_t hart_index;
    size_t thread_index;

    if (
        micros_scheduler_core_validate(&fixture->objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        MODEL_FAIL(step, operation, value, "production validator");
    }
    for (hart_index = 0; hart_index < MODEL_HART_COUNT; ++hart_index) {
        const struct micros_hart *actual =
            &fixture->objects.harts[fixture->harts[hart_index].slot];
        const struct model_hart *expected = &model->harts[hart_index];

        if (!project_hart_accounting(fixture, actual, expected, step, operation, value)) {
            return false;
        }
        if (!project_hart_queues(fixture, actual, expected, step, operation, value)) {
            return false;
        }
    }
    for (
        thread_index = 0;
        thread_index < MODEL_THREAD_COUNT;
        ++thread_index
    ) {
        if (!project_thread_state(fixture, model, thread_index, step, operation, value)) {
            return false;
        }
    }
    return true;
}
