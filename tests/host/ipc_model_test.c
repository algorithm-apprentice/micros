#include "ipc_model_test_internal.h"

static uint32_t model_random_next(uint32_t *state)
{
    uint32_t value = *state;

    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static bool coverage_is_complete(const struct model_coverage *coverage)
{
    size_t index;

    for (index = 0; index < MODEL_OPERATION_COUNT; ++index) {
        if (
            coverage->operation_count[index] == 0
            || coverage->operation_success[index] == 0
        ) {
            fprintf(
                stderr,
                "IPC model coverage missing op=%s count=%zu success=%zu\n",
                ipc_model_operation_name((enum model_operation)index),
                coverage->operation_count[index],
                coverage->operation_success[index]
            );
            return false;
        }
    }
    if (
        coverage->deadlock == 0
        || coverage->unauthorized == 0
        || coverage->argument == 0
    ) {
        fprintf(
            stderr,
            "IPC model error coverage incomplete deadlock=%zu "
            "unauthorized=%zu argument=%zu\n",
            coverage->deadlock,
            coverage->unauthorized,
            coverage->argument
        );
        return false;
    }
    return true;
}

bool micros_ipc_model_test_run(void)
{
    struct ipc_model_test_context *fixture = ipc_model_test_context_get();
    const uint32_t seed = UINT32_C(0x30accee5);
    struct model_state model;
    struct model_coverage coverage;
    uint32_t random_state = seed;
    size_t step = 0;
    size_t index;

    memset(&coverage, 0, sizeof(coverage));
    if (!ipc_model_setup_model(fixture, &model)) {
        fprintf(stderr, "IPC model setup failed seed=0x%08x\n", seed);
        return false;
    }
    for (index = 0; index < 3; ++index) {
        if (model.processes[index].live_thread_count < 2) {
            fprintf(
                stderr,
                "IPC model requires multiple threads for process %zu\n",
                index
            );
            return false;
        }
    }
    if (!ipc_model_run_model_prelude(
        fixture,
        &model,
        &step,
        seed,
        &coverage
    )) {
        return false;
    }
    while (step < MODEL_STEPS) {
        uint32_t random = model_random_next(&random_state);
        struct model_action action =
            ipc_model_build_random_action(fixture, &model, random, step);
        enum micros_ipc_error result;

        if (
            !ipc_model_execute_step(
                fixture,
                &model,
                &action,
                step,
                seed,
                &coverage,
                &result
            )
        ) {
            return false;
        }
        (void)result;
        ++step;
    }
    if (!coverage_is_complete(&coverage)) {
        ipc_model_dump_trace(fixture, seed, step, "coverage incomplete");
        return false;
    }
    printf(
        "# IPC acceptance model seed=0x%08x transitions=%zu "
        "trace=0x%016llx\n",
        seed,
        step,
        (unsigned long long)fixture->model_trace_hash
    );
    return true;
}
