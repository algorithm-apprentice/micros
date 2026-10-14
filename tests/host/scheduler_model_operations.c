#include "tests/host/scheduler_model_internal.h"

static bool apply_hold(
    struct scheduler_model_fixture *fixture,
    struct model_state *model,
    struct model_thread *thread,
    size_t step,
    uint32_t value,
    size_t slot,
    size_t hart,
    const char **operation_out
)
{
    const char *operation;
    enum micros_kernel_object_error error;

    operation = "hold";
    *operation_out = operation;
    if (
        thread->assigned
        && model->harts[hart].accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_THREAD
    ) {
        return true;
    }
    error = micros_thread_scheduler_hold(
        &fixture->objects,
        fixture->threads[slot]
    );
    if (
        thread->assigned
        && (thread->flags & MICROS_THREAD_RTS_INACTIVE) == 0
    ) {
        if (error != MICROS_KERNEL_OBJECT_OK) {
            MODEL_FAIL(step, operation, value, "unexpected error");
        }
        if (thread->flags == 0) {
            scheduler_model_queue_remove(
                model,
                hart,
                thread->priority,
                slot
            );
            ++model->coverage.flag_first;
        } else {
            ++model->coverage.flag_additional;
        }
        thread->flags |= MICROS_THREAD_RTS_INACTIVE;
    } else if (error != MICROS_KERNEL_OBJECT_ERROR_STATE) {
        MODEL_FAIL(step, operation, value, "wrong rejection");
    }
    return true;
}

static bool apply_wake(
    struct scheduler_model_fixture *fixture,
    struct model_state *model,
    struct model_thread *thread,
    size_t step,
    uint32_t value,
    size_t slot,
    size_t hart,
    const char **operation_out
)
{
    const char *operation;
    enum micros_kernel_object_error error;

    operation = "wake";
    *operation_out = operation;
    if (
        thread->assigned
        && model->harts[hart].accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_THREAD
    ) {
        return true;
    }
    error = micros_thread_runtime_flags_unset(
        &fixture->objects,
        fixture->threads[slot],
        MICROS_THREAD_RTS_INACTIVE
    );
    if (
        thread->assigned
        && (thread->flags & MICROS_THREAD_RTS_INACTIVE) != 0
        && (
            model->harts[hart].current != slot
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
            scheduler_model_queue_tail(
                model,
                hart,
                thread->priority,
                slot
            );
            ++model->coverage.flag_final_clear;
        } else {
            ++model->coverage.flag_partial_clear;
        }
    } else if (
        error != MICROS_KERNEL_OBJECT_ERROR_STATE
        && error != MICROS_KERNEL_OBJECT_ERROR_ARGUMENT
    ) {
        MODEL_FAIL(step, operation, value, "wrong rejection");
    }
    return true;
}

static bool apply_set_no_quantum(
    struct scheduler_model_fixture *fixture,
    struct model_state *model,
    struct model_thread *thread,
    size_t step,
    uint32_t value,
    size_t slot,
    size_t hart,
    const char **operation_out
)
{
    const char *operation;
    enum micros_kernel_object_error error;

    operation = "set-no-quantum";
    *operation_out = operation;
    if (
        thread->assigned
        && model->harts[hart].accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_THREAD
    ) {
        return true;
    }
    error = micros_thread_runtime_flags_set(
        &fixture->objects,
        fixture->threads[slot],
        MICROS_THREAD_RTS_NO_QUANTUM
    );
    if (thread->assigned) {
        if (error != MICROS_KERNEL_OBJECT_OK) {
            MODEL_FAIL(step, operation, value, "unexpected error");
        }
        if (thread->flags == 0) {
            scheduler_model_queue_remove(
                model,
                hart,
                thread->priority,
                slot
            );
            ++model->coverage.flag_first;
        } else {
            ++model->coverage.flag_additional;
        }
        thread->flags |= MICROS_THREAD_RTS_NO_QUANTUM;
    } else if (error != MICROS_KERNEL_OBJECT_ERROR_STATE) {
        MODEL_FAIL(step, operation, value, "wrong rejection");
    }
    return true;
}

static bool apply_clear_no_quantum(
    struct scheduler_model_fixture *fixture,
    struct model_state *model,
    struct model_thread *thread,
    size_t step,
    uint32_t value,
    size_t slot,
    size_t hart,
    const char **operation_out
)
{
    const char *operation;
    enum micros_kernel_object_error error;

    operation = "clear-no-quantum";
    *operation_out = operation;
    if (
        thread->assigned
        && model->harts[hart].accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_THREAD
    ) {
        return true;
    }
    error = micros_thread_runtime_flags_unset(
        &fixture->objects,
        fixture->threads[slot],
        MICROS_THREAD_RTS_NO_QUANTUM
    );
    if (
        thread->assigned
        && (thread->flags & MICROS_THREAD_RTS_NO_QUANTUM) != 0
        && (
            model->harts[hart].current != slot
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
            scheduler_model_queue_tail(
                model,
                hart,
                thread->priority,
                slot
            );
            ++model->coverage.flag_final_clear;
        } else {
            ++model->coverage.flag_partial_clear;
        }
    } else if (
        error != MICROS_KERNEL_OBJECT_ERROR_STATE
        && error != MICROS_KERNEL_OBJECT_ERROR_ARGUMENT
    ) {
        MODEL_FAIL(step, operation, value, "wrong rejection");
    }
    return true;
}

static bool apply_return(
    struct scheduler_model_fixture *fixture,
    struct model_state *model,
    size_t step,
    uint32_t value,
    size_t hart,
    const char **operation_out
)
{
    const char *operation;

    operation = "return";
    *operation_out = operation;
    if (
        model->harts[hart].accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_KERNEL
    ) {
        return true;
    }
    if (!scheduler_model_plan_and_commit(fixture, model, hart)) {
        MODEL_FAIL(step, operation, value, "plan or commit");
    }
    return true;
}

static bool apply_enter_thread(
    struct scheduler_model_fixture *fixture,
    struct model_state *model,
    size_t step,
    uint32_t value,
    size_t hart,
    const char **operation_out
)
{
    const char *operation;
    enum micros_kernel_object_error error;

    operation = "enter-thread";
    *operation_out = operation;
    model->harts[hart].counter += 1 + (value & 15U);
    error = micros_scheduler_account_enter_thread(
        &fixture->objects,
        fixture->harts[hart],
        model->harts[hart].current == MODEL_NONE
            ? fixture->threads[0]
            : fixture->threads[model->harts[hart].current],
        model->harts[hart].counter
    );
    if (
        model->harts[hart].accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_KERNEL
        && model->harts[hart].current != MODEL_NONE
        && model->threads[
            model->harts[hart].current
        ].flags == 0
    ) {
        uint64_t delta =
            model->harts[hart].counter
            - model->harts[hart].accounting_started_at;

        if (error != MICROS_KERNEL_OBJECT_OK) {
            MODEL_FAIL(step, operation, value, "unexpected error");
        }
        model->harts[hart].kernel_ticks += delta;
        model->harts[hart].accounting_owner =
            MICROS_SCHEDULER_ACCOUNTING_THREAD;
        model->harts[hart].accounting_started_at =
            model->harts[hart].counter;
        model->harts[hart].accounted_thread =
            model->harts[hart].current;
        ++model->coverage.kernel_to_thread;
    } else if (error != MICROS_KERNEL_OBJECT_ERROR_STATE) {
        MODEL_FAIL(step, operation, value, "wrong rejection");
    }
    return true;
}

static bool apply_user_trap(
    struct scheduler_model_fixture *fixture,
    struct model_state *model,
    size_t step,
    uint32_t value,
    size_t hart,
    const char **operation_out
)
{
    const char *operation;
    enum micros_kernel_object_error error;

    operation = "user-trap";
    *operation_out = operation;
    model->harts[hart].counter += 1 + (value & 31U);
    error = micros_scheduler_account_user_trap(
        &fixture->objects,
        fixture->harts[hart],
        model->harts[hart].counter
    );
    if (
        model->harts[hart].accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_THREAD
    ) {
        size_t accounted =
            model->harts[hart].accounted_thread;
        uint64_t delta =
            model->harts[hart].counter
            - model->harts[hart].accounting_started_at;

        if (error != MICROS_KERNEL_OBJECT_OK) {
            MODEL_FAIL(step, operation, value, "unexpected error");
        }
        model->threads[accounted].remaining =
            delta >= model->threads[accounted].remaining
                ? 0
                : model->threads[accounted].remaining - delta;
        model->harts[hart].accounting_owner =
            MICROS_SCHEDULER_ACCOUNTING_KERNEL;
        model->harts[hart].accounting_started_at =
            model->harts[hart].counter;
        model->harts[hart].accounted_thread = MODEL_NONE;
        ++model->coverage.thread_to_kernel;
    } else if (error != MICROS_KERNEL_OBJECT_ERROR_STATE) {
        MODEL_FAIL(step, operation, value, "wrong rejection");
    }
    return true;
}

static bool apply_enter_idle(
    struct scheduler_model_fixture *fixture,
    struct model_state *model,
    size_t step,
    uint32_t value,
    size_t hart,
    const char **operation_out
)
{
    const char *operation;
    enum micros_kernel_object_error error;

    operation = "enter-idle";
    *operation_out = operation;
    model->harts[hart].counter += 1 + (value & 7U);
    error = micros_scheduler_account_enter_idle(
        &fixture->objects,
        fixture->harts[hart],
        model->harts[hart].counter
    );
    if (
        model->harts[hart].accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_KERNEL
        && model->harts[hart].current == MODEL_NONE
    ) {
        uint64_t delta =
            model->harts[hart].counter
            - model->harts[hart].accounting_started_at;

        if (error != MICROS_KERNEL_OBJECT_OK) {
            MODEL_FAIL(step, operation, value, "unexpected error");
        }
        model->harts[hart].kernel_ticks += delta;
        model->harts[hart].accounting_owner =
            MICROS_SCHEDULER_ACCOUNTING_IDLE;
        model->harts[hart].accounting_started_at =
            model->harts[hart].counter;
        ++model->coverage.kernel_to_idle;
    } else if (error != MICROS_KERNEL_OBJECT_ERROR_STATE) {
        MODEL_FAIL(step, operation, value, "wrong rejection");
    }
    return true;
}

static bool apply_idle_trap(
    struct scheduler_model_fixture *fixture,
    struct model_state *model,
    size_t step,
    uint32_t value,
    size_t hart,
    const char **operation_out
)
{
    const char *operation;
    enum micros_kernel_object_error error;

    operation = "idle-trap";
    *operation_out = operation;
    model->harts[hart].counter += 1 + (value & 7U);
    error = micros_scheduler_account_idle_trap(
        &fixture->objects,
        fixture->harts[hart],
        model->harts[hart].counter
    );
    if (
        model->harts[hart].accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_IDLE
    ) {
        uint64_t delta =
            model->harts[hart].counter
            - model->harts[hart].accounting_started_at;

        if (error != MICROS_KERNEL_OBJECT_OK) {
            MODEL_FAIL(step, operation, value, "unexpected error");
        }
        model->harts[hart].idle_ticks += delta;
        model->harts[hart].accounting_owner =
            MICROS_SCHEDULER_ACCOUNTING_KERNEL;
        model->harts[hart].accounting_started_at =
            model->harts[hart].counter;
        ++model->coverage.idle_to_kernel;
    } else if (error != MICROS_KERNEL_OBJECT_ERROR_STATE) {
        MODEL_FAIL(step, operation, value, "wrong rejection");
    }
    return true;
}

static bool apply_policy(
    struct scheduler_model_fixture *fixture,
    struct model_state *model,
    struct model_thread *thread,
    size_t step,
    uint32_t value,
    size_t slot,
    size_t hart,
    const char **operation_out
)
{
    const char *operation;
    enum micros_kernel_object_error error;

    uint8_t priority = (uint8_t)((value >> 20) & 15U);
    uint64_t quantum = 32 + (value & 63U);

    operation = "policy";
    *operation_out = operation;
    if (
        thread->assigned
        && model->harts[hart].accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_THREAD
    ) {
        return true;
    }
    error = micros_thread_install_policy(
        &fixture->objects,
        fixture->threads[slot],
        priority,
        quantum
    );
    if (
        thread->assigned
        && model->harts[hart].current != slot
    ) {
        bool was_runnable = thread->flags == 0;

        if (error != MICROS_KERNEL_OBJECT_OK) {
            MODEL_FAIL(step, operation, value, "unexpected error");
        }
        if (was_runnable) {
            scheduler_model_queue_remove(
                model,
                hart,
                thread->priority,
                slot
            );
            ++model->coverage.policy_runnable;
        } else {
            ++model->coverage.policy_blocked;
        }
        thread->priority = priority;
        thread->quantum = quantum;
        thread->remaining = quantum;
        thread->flags &= ~MICROS_THREAD_RTS_NO_QUANTUM;
        if (thread->flags == 0) {
            scheduler_model_queue_tail(model, hart, priority, slot);
        }
    } else if (error != MICROS_KERNEL_OBJECT_ERROR_STATE) {
        MODEL_FAIL(step, operation, value, "wrong rejection");
    }
    return true;
}

bool scheduler_model_apply_operation(
    struct scheduler_model_fixture *fixture,
    struct model_state *model,
    struct model_thread *thread,
    size_t step,
    uint32_t value,
    size_t slot,
    size_t hart,
    const char **operation_out
)
{
    switch (value % 10) {
    case 0:
        if (!apply_hold(
            fixture, model, thread, step, value, slot, hart, operation_out
        )) {
            return false;
        }
        break;
    case 1:
        if (!apply_wake(
            fixture, model, thread, step, value, slot, hart, operation_out
        )) {
            return false;
        }
        break;
    case 2:
        if (!apply_set_no_quantum(
            fixture, model, thread, step, value, slot, hart, operation_out
        )) {
            return false;
        }
        break;
    case 3:
        if (!apply_clear_no_quantum(
            fixture, model, thread, step, value, slot, hart, operation_out
        )) {
            return false;
        }
        break;
    case 4:
        if (!apply_return(
            fixture, model, step, value, hart, operation_out
        )) {
            return false;
        }
        break;
    case 5:
        if (!apply_enter_thread(
            fixture, model, step, value, hart, operation_out
        )) {
            return false;
        }
        break;
    case 6:
        if (!apply_user_trap(
            fixture, model, step, value, hart, operation_out
        )) {
            return false;
        }
        break;
    case 7:
        if (!apply_enter_idle(
            fixture, model, step, value, hart, operation_out
        )) {
            return false;
        }
        break;
    case 8:
        if (!apply_idle_trap(
            fixture, model, step, value, hart, operation_out
        )) {
            return false;
        }
        break;
    default:
        if (!apply_policy(
            fixture, model, thread, step, value, slot, hart, operation_out
        )) {
            return false;
        }
        break;
    }
    return true;
}
