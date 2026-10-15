#include "endpoint_model_internal.h"

#include <stdio.h>
#include <string.h>

static const enum model_operation scripted_operations[] = {
    MODEL_OPERATION_CREATE,
    MODEL_OPERATION_CREATE,
    MODEL_OPERATION_RESERVE,
    MODEL_OPERATION_RESERVE,
    MODEL_OPERATION_INSTALL,
    MODEL_OPERATION_INSTALL,
    MODEL_OPERATION_INSTALL,
    MODEL_OPERATION_ACTIVATE,
    MODEL_OPERATION_ACTIVATE,
    MODEL_OPERATION_AUTHORIZE,
    MODEL_OPERATION_AUTHORIZE,
    MODEL_OPERATION_AUTHORIZE,
    MODEL_OPERATION_AUTHORIZE,
    MODEL_OPERATION_AUTHORIZE,
    MODEL_OPERATION_AUTHORIZE,
    MODEL_OPERATION_AUTHORIZE,
    MODEL_OPERATION_AUTHORIZE,
};
static const uint32_t scripted_values[] = {
    0,
    0,
    UINT32_C(0) << 16,
    UINT32_C(1) << 16,
    3,
    UINT32_C(0) << 16,
    UINT32_C(1) << 16,
    UINT32_C(0) << 16,
    UINT32_C(1) << 16,
    UINT32_C(0) << 16,
    (UINT32_C(0) << 16) | 1,
    (UINT32_C(1) << 16) | 1,
    (UINT32_C(0) << 16) | 2,
    (UINT32_C(1) << 16) | 4,
    (UINT32_C(0) << 16) | 4,
    (UINT32_C(1) << 16) | 8,
    (UINT32_C(0) << 16) | 8,
};

static bool endpoint_model_setup_run(
    struct endpoint_model_fixture *fixture,
    struct endpoint_model_run *run
)
{
    memset(run->model, 0, sizeof(run->model));
    memset(&run->coverage, 0, sizeof(run->coverage));
    if (!endpoint_model_setup_fixture(fixture)) {
        fprintf(stderr, "endpoint model fixture failed\n");
        return false;
    }

    return true;
}

static void endpoint_model_draw_iteration(
    struct endpoint_model_run *run,
    struct endpoint_model_iteration *iteration
)
{
    iteration->random = endpoint_model_next_random(&run->random_state);
    iteration->operation =
        (enum model_operation)(
            (iteration->random >> 24) % MODEL_OPERATION_COUNT
        );
    iteration->slot = MODEL_SLOT_COUNT;
    iteration->value = endpoint_model_next_random(&run->random_state);

    if (
        run->step
            < sizeof(scripted_operations)
                / sizeof(scripted_operations[0])
    ) {
        iteration->operation = scripted_operations[run->step];
        iteration->value = scripted_values[run->step];
    }
}

static bool endpoint_model_apply_operation(
    struct endpoint_model_fixture *fixture,
    struct endpoint_model_run *run,
    struct endpoint_model_iteration *iteration
)
{
    switch (iteration->operation) {
    case MODEL_OPERATION_CREATE:
        return endpoint_model_create(fixture, run, iteration);
    case MODEL_OPERATION_RESERVE:
        return endpoint_model_reserve(fixture, run, iteration);
    case MODEL_OPERATION_INSTALL:
        return endpoint_model_install(fixture, run, iteration);
    case MODEL_OPERATION_ACTIVATE:
        return endpoint_model_activate(fixture, run, iteration);
    case MODEL_OPERATION_AUTHORIZE:
        return endpoint_model_authorize(fixture, run, iteration);
    case MODEL_OPERATION_CLOSE:
        return endpoint_model_close(fixture, run, iteration);
    case MODEL_OPERATION_RELEASE:
        return endpoint_model_release(fixture, run, iteration);
    case MODEL_OPERATION_STALE:
        return endpoint_model_stale(fixture, run, iteration);
    default:
        return endpoint_model_model_fail(
            fixture,
            run->step,
            iteration->operation,
            iteration->value,
            "unknown model operation"
        );
    }
}

static bool endpoint_model_cleanup_run(
    struct endpoint_model_fixture *fixture,
    struct endpoint_model_run *run
)
{
    for (run->step = 0; run->step < MODEL_SLOT_COUNT; ++run->step) {
        if (!run->model[run->step].process_live) {
            continue;
        }
        if (endpoint_model_phase_has_endpoint(run->model[run->step].endpoint_phase)) {
            if (
                micros_endpoint_close(
                    &fixture->registry,
                    &fixture->objects,
                    run->model[run->step].endpoint
                ) != MICROS_ENDPOINT_OK
            ) {
                return endpoint_model_model_fail(
                    fixture,
                    MODEL_STEPS,
                    MODEL_OPERATION_CLOSE,
                    (uint32_t)run->step,
                    "cleanup close failed"
                );
            }
        }
        if (
            micros_thread_release(&fixture->objects, run->model[run->step].thread)
                != MICROS_KERNEL_OBJECT_OK
            || micros_process_release(
                &fixture->objects,
                run->model[run->step].process
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return endpoint_model_model_fail(
                fixture,
                MODEL_STEPS,
                MODEL_OPERATION_RELEASE,
                (uint32_t)run->step,
                "cleanup release failed"
            );
        }
    }
    if (
        micros_endpoint_registry_validate_objects(&fixture->registry, &fixture->objects)
            != MICROS_ENDPOINT_OK
        || fixture->objects.live_process_count != 0
        || fixture->objects.live_thread_count != 0
        || fixture->objects.registered_hart_count != 1
    ) {
        return endpoint_model_model_fail(
            fixture,
            MODEL_STEPS,
            MODEL_OPERATION_RELEASE,
            0,
            "cleanup baseline validation failed"
        );
    }

    return true;
}

static bool endpoint_model_coverage_complete(
    const struct endpoint_model_run *run
)
{
    if (
        run->coverage.create == 0
        || run->coverage.release == 0
        || run->coverage.reserve == 0
        || run->coverage.reserve_rejected == 0
        || run->coverage.install_client == 0
        || run->coverage.install_server == 0
        || run->coverage.install_invalid == 0
        || run->coverage.activate == 0
        || run->coverage.close == 0
        || run->coverage.authorize_allowed == 0
        || run->coverage.authorize_denied == 0
        || run->coverage.authorize_state == 0
        || run->coverage.notify_allowed == 0
        || run->coverage.notify_denied == 0
        || run->coverage.reply_allowed == 0
        || run->coverage.reply_denied == 0
        || run->coverage.reply_receive_allowed == 0
        || run->coverage.reply_receive_denied == 0
        || run->coverage.stale == 0
    ) {
        fprintf(
            stderr,
            "endpoint model seed=0xe029cafe coverage incomplete "
            "create=%zu release=%zu reserve=%zu reject=%zu "
            "client=%zu server=%zu invalid=%zu activate=%zu "
            "close=%zu allow=%zu deny=%zu state=%zu "
            "notify-allow=%zu notify-deny=%zu "
            "reply-allow=%zu reply-deny=%zu "
            "reply-receive-allow=%zu "
            "reply-receive-deny=%zu stale=%zu\n",
            run->coverage.create,
            run->coverage.release,
            run->coverage.reserve,
            run->coverage.reserve_rejected,
            run->coverage.install_client,
            run->coverage.install_server,
            run->coverage.install_invalid,
            run->coverage.activate,
            run->coverage.close,
            run->coverage.authorize_allowed,
            run->coverage.authorize_denied,
            run->coverage.authorize_state,
            run->coverage.notify_allowed,
            run->coverage.notify_denied,
            run->coverage.reply_allowed,
            run->coverage.reply_denied,
            run->coverage.reply_receive_allowed,
            run->coverage.reply_receive_denied,
            run->coverage.stale
        );
        return false;
    }
    return true;
}

bool micros_endpoint_model_test_run(void)
{
    struct endpoint_model_fixture *fixture = endpoint_model_context();
    const uint32_t seed = UINT32_C(0xe029cafe);
    struct endpoint_model_run run;

    run.random_state = seed;
    if (!endpoint_model_setup_run(fixture, &run)) {
        return false;
    }
    for (run.step = 0; run.step < MODEL_STEPS; ++run.step) {
        struct endpoint_model_iteration iteration;

        endpoint_model_draw_iteration(&run, &iteration);
        if (!endpoint_model_apply_operation(fixture, &run, &iteration)) {
            return false;
        }
        endpoint_model_record_transition(
            fixture, run.step, iteration.operation, iteration.slot,
            iteration.value
        );
        if (!endpoint_model_validate_model(
            fixture, run.model, run.step, iteration.operation,
            iteration.value
        )) {
            return false;
        }
    }
    if (!endpoint_model_cleanup_run(fixture, &run)) {
        return false;
    }
    return endpoint_model_coverage_complete(&run);
}
