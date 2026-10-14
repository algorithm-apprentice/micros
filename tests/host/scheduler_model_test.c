#include "tests/host/scheduler_model_internal.h"

static uint32_t next_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

bool micros_scheduler_model_test_run(void)
{
    struct scheduler_model_fixture *fixture =
        scheduler_model_fixture();
    const uint32_t seed = UINT32_C(0x5c4ed026);
    struct model_state model;
    uint32_t random_state = seed;
    size_t step;

    if (
        !scheduler_model_setup_fixture(fixture)
        || !scheduler_model_initialize_model(fixture, &model)
        || !scheduler_model_run_required_prelude(fixture, &model)
        || !scheduler_model_model_matches(fixture, &model, 0, "prelude", seed)
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
                &fixture->objects,
                fixture->threads[slot],
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
                    scheduler_model_queue_tail(
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
                &fixture->objects,
                fixture->threads[slot],
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
                    scheduler_model_queue_tail(
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
            if (!scheduler_model_plan_and_commit(fixture, &model, hart)) {
                MODEL_FAIL(step, operation, value, "plan or commit");
            }
            break;
        case 5:
            operation = "enter-thread";
            model.harts[hart].counter += 1 + (value & 15U);
            error = micros_scheduler_account_enter_thread(
                &fixture->objects,
                fixture->harts[hart],
                model.harts[hart].current == MODEL_NONE
                    ? fixture->threads[0]
                    : fixture->threads[model.harts[hart].current],
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
                &fixture->objects,
                fixture->harts[hart],
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
                &fixture->objects,
                fixture->harts[hart],
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
                &fixture->objects,
                fixture->harts[hart],
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
                &fixture->objects,
                fixture->threads[slot],
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
                    scheduler_model_queue_remove(
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
                    scheduler_model_queue_tail(&model, hart, priority, slot);
                }
            } else if (error != MICROS_KERNEL_OBJECT_ERROR_STATE) {
                MODEL_FAIL(step, operation, value, "wrong rejection");
            }
            break;
        }
        }
        if (!scheduler_model_model_matches(fixture, &model, step, operation, value)) {
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
