#include "tests/host/scheduler_model_internal.h"

bool scheduler_model_run_required_prelude(
    struct scheduler_model_fixture *fixture,struct model_state *model)
{
    if (
        !scheduler_model_admit(fixture, model, 0, 0, 10, 60)
        || !scheduler_model_admit(fixture, model, 1, 0, 10, 60)
        || !scheduler_model_admit(fixture, model, 2, 0, 10, 60)
        || !scheduler_model_plan_and_commit(fixture, model, 0)
    ) {
        return false;
    }
    model->harts[0].counter += 10;
    if (
        micros_scheduler_account_enter_thread(
            &fixture->objects,
            fixture->harts[0],
            fixture->threads[0],
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
            &fixture->objects,
            fixture->harts[0],
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
        !scheduler_model_admit(fixture, model, 3, 0, 3, 60)
        || !scheduler_model_plan_and_commit(fixture, model, 0)
        || !scheduler_model_admit(fixture, model, 4, 1, 8, 50)
        || !scheduler_model_admit(fixture, model, 5, 1, 8, 50)
        || !scheduler_model_admit(fixture, model, 6, 1, 12, 50)
        || !scheduler_model_plan_and_commit(fixture, model, 1)
        || !scheduler_model_admit(fixture, model, 7, 1, 1, 50)
        || !scheduler_model_plan_and_commit(fixture, model, 1)
    ) {
        return false;
    }
    if (!scheduler_model_plan_and_commit(fixture, model, 0)) {
        return false;
    }

    if (
        micros_thread_scheduler_hold(&fixture->objects, fixture->threads[1])
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    scheduler_model_queue_remove(model, 0, 10, 1);
    model->threads[1].flags |= MICROS_THREAD_RTS_INACTIVE;
    ++model->coverage.flag_first;
    if (
        micros_thread_runtime_flags_set(
            &fixture->objects,
            fixture->threads[1],
            MICROS_THREAD_RTS_NO_QUANTUM
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    model->threads[1].flags |= MICROS_THREAD_RTS_NO_QUANTUM;
    ++model->coverage.flag_additional;
    if (
        micros_thread_install_policy(
            &fixture->objects,
            fixture->threads[1],
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
            &fixture->objects,
            fixture->threads[1],
            MICROS_THREAD_RTS_INACTIVE
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    model->threads[1].flags &= ~MICROS_THREAD_RTS_INACTIVE;
    scheduler_model_queue_tail(model, 0, 10, 1);
    ++model->coverage.flag_final_clear;

    if (
        micros_thread_install_policy(
            &fixture->objects,
            fixture->threads[2],
            3,
            80
        )
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    scheduler_model_queue_remove(model, 0, 10, 2);
    model->threads[2].priority = 3;
    model->threads[2].quantum = 80;
    model->threads[2].remaining = 80;
    scheduler_model_queue_tail(model, 0, 3, 2);
    ++model->coverage.policy_runnable;

    model->harts[0].counter += 5;
    if (
        micros_scheduler_account_enter_thread(
            &fixture->objects,
            fixture->harts[0],
            fixture->threads[3],
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
            &fixture->objects,
            fixture->harts[0],
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
    if (!scheduler_model_plan_and_commit(fixture, model, 0)) {
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
                    &fixture->objects,
                    fixture->threads[slot]
                ) != MICROS_KERNEL_OBJECT_OK
            ) {
                return false;
            }
            if (thread->flags == 0) {
                scheduler_model_queue_remove(
                    model,
                    thread->hart,
                    thread->priority,
                    slot
                );
            }
            thread->flags |= MICROS_THREAD_RTS_INACTIVE;
        }
        if (!scheduler_model_plan_and_commit(fixture, model, 1)) {
            return false;
        }
        model->harts[1].counter += 5;
        if (
            micros_scheduler_account_enter_idle(
                &fixture->objects,
                fixture->harts[1],
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
                &fixture->objects,
                fixture->harts[1],
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
                    &fixture->objects,
                    fixture->threads[slot],
                    MICROS_THREAD_RTS_INACTIVE
                ) != MICROS_KERNEL_OBJECT_OK
            ) {
                return false;
            }
            thread->flags &= ~MICROS_THREAD_RTS_INACTIVE;
            scheduler_model_queue_tail(
                model,
                thread->hart,
                thread->priority,
                slot
            );
        }
    }
    return true;
}
