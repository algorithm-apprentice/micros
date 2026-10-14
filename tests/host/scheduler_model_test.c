#include "tests/host/scheduler_model_internal.h"

static uint32_t next_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static bool verify_model_coverage(const struct model_state *model)
{
    if (
        model->coverage.preempt_higher == 0
        || model->coverage.enqueue_same == 0
        || model->coverage.enqueue_lower == 0
        || model->coverage.remove_middle == 0
        || model->coverage.flag_first == 0
        || model->coverage.flag_additional == 0
        || model->coverage.flag_partial_clear == 0
        || model->coverage.flag_final_clear == 0
        || model->coverage.return_keep == 0
        || model->coverage.return_switch == 0
        || model->coverage.return_idle == 0
        || model->coverage.quantum_renew == 0
        || model->coverage.thread_to_kernel == 0
        || model->coverage.kernel_to_thread == 0
        || model->coverage.kernel_to_idle == 0
        || model->coverage.idle_to_kernel == 0
        || model->coverage.policy_runnable == 0
        || model->coverage.policy_blocked == 0
    ) {
        fprintf(stderr, "scheduler model coverage incomplete\n");
        return false;
    }
    return true;
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

        if (!scheduler_model_apply_operation(
            fixture, &model, thread, step, value, slot, hart, &operation
        )) {
            return false;
        }
        if (!scheduler_model_model_matches(fixture, &model, step, operation, value)) {
            return false;
        }
    }

    return verify_model_coverage(&model);
}
