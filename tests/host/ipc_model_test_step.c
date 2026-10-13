#include "ipc_model_test_internal.h"

static void update_coverage(
    struct model_coverage *coverage,
    enum model_operation operation,
    enum micros_ipc_error result
)
{
    if ((size_t)operation < MODEL_OPERATION_COUNT) {
        ++coverage->operation_count[operation];
        if (result == MICROS_IPC_OK) {
            ++coverage->operation_success[operation];
        }
    }
    if (result == MICROS_IPC_ERROR_DEADLOCK) {
        ++coverage->deadlock;
    } else if (result == MICROS_IPC_ERROR_UNAUTHORIZED) {
        ++coverage->unauthorized;
    } else if (result == MICROS_IPC_ERROR_ARGUMENT) {
        ++coverage->argument;
    }
}

bool ipc_model_execute_step(
    struct ipc_model_test_context *fixture,
    struct model_state *model,
    const struct model_action *action,
    size_t step,
    uint32_t seed,
    struct model_coverage *coverage,
    enum micros_ipc_error *result
)
{
    enum micros_ipc_error expected =
        ipc_model_reference_execute_action(model, action);
    enum micros_ipc_error actual =
        ipc_model_production_execute_action(fixture, action);

    ipc_model_record_trace(fixture, step, action, expected);
    update_coverage(coverage, action->operation, expected);
    *result = expected;
    if (expected != actual) {
        ipc_model_dump_trace(fixture, seed, step, "operation result mismatch");
        fprintf(
            stderr,
            "  expected=%d actual=%d\n",
            (int)expected,
            (int)actual
        );
        ipc_model_dump_state(fixture, model);
        return false;
    }
    if (!ipc_model_matches_production(fixture, model, step)) {
        ipc_model_dump_trace(fixture, seed, step, "complete state mismatch");
        ipc_model_dump_state(fixture, model);
        return false;
    }
    return true;
}

bool ipc_model_run_checked_action(
    struct ipc_model_test_context *fixture,
    struct model_state *model,
    struct model_action action,
    enum micros_ipc_error expected,
    size_t *step,
    uint32_t seed,
    struct model_coverage *coverage
)
{
    enum micros_ipc_error result;

    if (
        !ipc_model_execute_step(
            fixture,
            model,
            &action,
            *step,
            seed,
            coverage,
            &result
        )
    ) {
        return false;
    }
    if (result != expected) {
        ipc_model_dump_trace(fixture, seed, *step, "prelude expectation mismatch");
        fprintf(
            stderr,
            "  expected=%d actual=%d\n",
            (int)expected,
            (int)result
        );
        return false;
    }
    ++*step;
    return true;
}
