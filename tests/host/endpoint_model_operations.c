#include "endpoint_model_internal.h"

#include <stdio.h>
#include <string.h>

uint32_t endpoint_model_next_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static bool endpoint_model_authorize_reply_receive(
    struct endpoint_model_fixture *fixture,
    struct endpoint_model_run *run,
    struct endpoint_model_iteration *iteration,
    uint32_t *authorization_operation,
    enum micros_endpoint_error *expected,
    enum micros_endpoint_error *error
)
{
    *authorization_operation =
        MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE;
    if (
        iteration->slot == MODEL_SLOT_COUNT
        || run->model[iteration->slot].endpoint_phase
            != MODEL_ENDPOINT_ACTIVE
    ) {
        *expected = MICROS_ENDPOINT_ERROR_STATE;
    } else if (run->model[iteration->slot].profile == 2) {
        *expected = MICROS_ENDPOINT_OK;
    } else {
        *expected = MICROS_ENDPOINT_ERROR_UNAUTHORIZED;
    }
    if (iteration->slot == MODEL_SLOT_COUNT) {
        return true;
    }
    *error = micros_endpoint_authorize_operation(
        &fixture->registry,
        &fixture->objects,
        run->model[iteration->slot].endpoint,
        *authorization_operation
    );
    iteration->value = *authorization_operation;
    if (*error != *expected) {
        return endpoint_model_model_fail(
            fixture,
            run->step,
            iteration->operation,
            iteration->value,
            "operation authorization diverged"
        );
    }
    if (*error == MICROS_ENDPOINT_OK) {
        ++run->coverage.reply_receive_allowed;
    } else if (
        *error == MICROS_ENDPOINT_ERROR_UNAUTHORIZED
    ) {
        ++run->coverage.reply_receive_denied;
    } else {
        ++run->coverage.authorize_state;
    }
    return true;
}

static bool endpoint_model_authorize_reply(
    struct endpoint_model_fixture *fixture,
    struct endpoint_model_run *run,
    struct endpoint_model_iteration *iteration,
    uint32_t *authorization_operation,
    enum micros_endpoint_error *expected,
    enum micros_endpoint_error *error
)
{
    *authorization_operation =
        MICROS_PRIVILEGE_OPERATION_REPLY;
    if (
        iteration->slot == MODEL_SLOT_COUNT
        || run->model[iteration->slot].endpoint_phase
            != MODEL_ENDPOINT_ACTIVE
    ) {
        *expected = MICROS_ENDPOINT_ERROR_STATE;
    } else if (run->model[iteration->slot].profile == 2) {
        *expected = MICROS_ENDPOINT_OK;
    } else {
        *expected = MICROS_ENDPOINT_ERROR_UNAUTHORIZED;
    }
    if (iteration->slot == MODEL_SLOT_COUNT) {
        return true;
    }
    *error = micros_endpoint_authorize_operation(
        &fixture->registry,
        &fixture->objects,
        run->model[iteration->slot].endpoint,
        *authorization_operation
    );
    iteration->value = *authorization_operation;
    if (*error != *expected) {
        return endpoint_model_model_fail(
            fixture,
            run->step,
            iteration->operation,
            iteration->value,
            "operation authorization diverged"
        );
    }
    if (*error == MICROS_ENDPOINT_OK) {
        ++run->coverage.reply_allowed;
    } else if (
        *error == MICROS_ENDPOINT_ERROR_UNAUTHORIZED
    ) {
        ++run->coverage.reply_denied;
    } else {
        ++run->coverage.authorize_state;
    }
    return true;
}

static void endpoint_model_select_authorization_target(
    const struct endpoint_model_run *run,
    const struct endpoint_model_iteration *iteration,
    uint32_t target_random,
    size_t *target,
    uint32_t *authorization_operation
)
{
    *target = MODEL_SLOT_COUNT;
    if (
        iteration->slot != MODEL_SLOT_COUNT
        && run->model[iteration->slot].endpoint_phase
            == MODEL_ENDPOINT_ACTIVE
    ) {
        if ((iteration->value & 2) != 0) {
            *target = iteration->slot;
            *authorization_operation =
                MICROS_PRIVILEGE_OPERATION_NOTIFY;
        }
    }
    if (
        *target == MODEL_SLOT_COUNT
        && iteration->slot != MODEL_SLOT_COUNT
        && run->model[iteration->slot].endpoint_phase
            == MODEL_ENDPOINT_ACTIVE
    ) {
        uint8_t target_profile =
            run->model[iteration->slot].profile == 1 ? 2 : 1;

        *target = endpoint_model_find_active_profile(
            run->model,
            target_random,
            target_profile
        );
        if (*target != MODEL_SLOT_COUNT) {
            if ((iteration->value & 1) != 0) {
                *authorization_operation =
                    MICROS_PRIVILEGE_OPERATION_NOTIFY;
            } else {
                *authorization_operation =
                    run->model[iteration->slot].profile == 1
                        ? MICROS_PRIVILEGE_OPERATION_CALL
                        : MICROS_PRIVILEGE_OPERATION_SEND;
            }
        }
    }
    if (*target == MODEL_SLOT_COUNT) {
        *target = endpoint_model_find_slot(
            run->model,
            target_random,
            endpoint_model_slot_has_endpoint
        );
    }
}

static bool endpoint_model_authorize_selected_target(
    struct endpoint_model_fixture *fixture,
    struct endpoint_model_run *run,
    struct endpoint_model_iteration *iteration,
    size_t target,
    uint32_t authorization_operation,
    enum micros_endpoint_error *expected,
    enum micros_endpoint_error *error
)
{
    if (
        run->model[iteration->slot].endpoint_phase
            != MODEL_ENDPOINT_ACTIVE
        || run->model[target].endpoint_phase
            != MODEL_ENDPOINT_ACTIVE
    ) {
        *expected = MICROS_ENDPOINT_ERROR_STATE;
    } else if (
        endpoint_model_target_is_allowed(
            run->model[iteration->slot].profile,
            authorization_operation,
            run->model[target].profile
        )
    ) {
        *expected = MICROS_ENDPOINT_OK;
    } else {
        *expected = MICROS_ENDPOINT_ERROR_UNAUTHORIZED;
    }
    *error = micros_endpoint_authorize_target(
        &fixture->registry,
        &fixture->objects,
        run->model[iteration->slot].endpoint,
        authorization_operation,
        run->model[target].endpoint
    );
    iteration->value = authorization_operation;
    if (*error != *expected) {
        return endpoint_model_model_fail(
            fixture,
            run->step,
            iteration->operation,
            iteration->value,
            "authorization result diverged"
        );
    }
    if (*error == MICROS_ENDPOINT_OK) {
        ++run->coverage.authorize_allowed;
        if (
            authorization_operation
                == MICROS_PRIVILEGE_OPERATION_NOTIFY
        ) {
            ++run->coverage.notify_allowed;
        }
    } else if (*error == MICROS_ENDPOINT_ERROR_UNAUTHORIZED) {
        ++run->coverage.authorize_denied;
        if (
            authorization_operation
                == MICROS_PRIVILEGE_OPERATION_NOTIFY
        ) {
            ++run->coverage.notify_denied;
        }
    } else {
        ++run->coverage.authorize_state;
    }

    return true;
}

bool endpoint_model_create(
    struct endpoint_model_fixture *fixture,
    struct endpoint_model_run *run,
    struct endpoint_model_iteration *iteration
)
{
    for (iteration->slot = 0; iteration->slot < MODEL_SLOT_COUNT; ++iteration->slot) {
        if (!run->model[iteration->slot].process_live) {
            break;
        }
    }
    if (iteration->slot != MODEL_SLOT_COUNT) {
        struct micros_process_handle process;
        struct micros_thread_handle thread;

        if (
            micros_process_create(&fixture->objects, &process)
                != MICROS_KERNEL_OBJECT_OK
            || process.slot != iteration->slot
            || process.generation
                != run->model[iteration->slot].generation + 1
            || micros_thread_create(
                &fixture->objects,
                process,
                &thread
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return endpoint_model_model_fail(
                fixture,
                run->step,
                iteration->operation,
                iteration->value,
                "create transition failed"
            );
        }
        run->model[iteration->slot].process_live = true;
        run->model[iteration->slot].thread_live = true;
        run->model[iteration->slot].generation = process.generation;
        run->model[iteration->slot].process = process;
        run->model[iteration->slot].thread = thread;
        run->model[iteration->slot].endpoint_phase =
            MODEL_ENDPOINT_UNUSED;
        run->model[iteration->slot].endpoint = 0;
        run->model[iteration->slot].profile = 0;
        ++run->coverage.create;
    }

    return true;
}

bool endpoint_model_reserve(
    struct endpoint_model_fixture *fixture,
    struct endpoint_model_run *run,
    struct endpoint_model_iteration *iteration
)
{
    iteration->slot = endpoint_model_find_slot(run->model, iteration->value, endpoint_model_slot_is_live);
    if (iteration->slot != MODEL_SLOT_COUNT) {
        micros_endpoint_t endpoint = MICROS_ENDPOINT_ANY;
        bool allowed =
            run->model[iteration->slot].endpoint_phase
                == MODEL_ENDPOINT_UNUSED;
        enum micros_endpoint_error error =
            micros_endpoint_reserve(
                &fixture->registry,
                &fixture->objects,
                run->model[iteration->slot].process,
                &endpoint
            );

        if (
            error
                != (
                    allowed
                        ? MICROS_ENDPOINT_OK
                        : MICROS_ENDPOINT_ERROR_STATE
                )
            || (!allowed && endpoint != MICROS_ENDPOINT_ANY)
        ) {
            return endpoint_model_model_fail(
                fixture,
                run->step,
                iteration->operation,
                iteration->value,
                "reserve result diverged"
            );
        }
        if (allowed) {
            run->model[iteration->slot].endpoint_phase =
                MODEL_ENDPOINT_RESERVED;
            run->model[iteration->slot].endpoint = endpoint;
            run->model[iteration->slot].profile = 0;
            ++run->coverage.reserve;
        } else {
            ++run->coverage.reserve_rejected;
        }
    }

    return true;
}

bool endpoint_model_install(
    struct endpoint_model_fixture *fixture,
    struct endpoint_model_run *run,
    struct endpoint_model_iteration *iteration
)
{
    iteration->slot = endpoint_model_find_slot(run->model, iteration->value, endpoint_model_slot_has_endpoint);
    if (iteration->slot != MODEL_SLOT_COUNT) {
        uint8_t profile = (iteration->value & 3) == 3
            ? 3
            : (uint8_t)(iteration->slot % 2 + 1);
        bool installable =
            run->model[iteration->slot].endpoint_phase
                == MODEL_ENDPOINT_RESERVED;
        enum micros_endpoint_error expected =
            installable
                ? (
                    profile <= 2
                        ? MICROS_ENDPOINT_OK
                        : MICROS_ENDPOINT_ERROR_PROFILE
                )
                : MICROS_ENDPOINT_ERROR_STATE;
        enum micros_endpoint_error error =
            micros_endpoint_install_profile(
                &fixture->registry,
                &fixture->objects,
                run->model[iteration->slot].process,
                profile
            );

        iteration->value = profile;
        if (error != expected) {
            return endpoint_model_model_fail(
                fixture,
                run->step,
                iteration->operation,
                iteration->value,
                "profile installation diverged"
            );
        }
        if (error == MICROS_ENDPOINT_OK) {
            run->model[iteration->slot].endpoint_phase =
                MODEL_ENDPOINT_PROFILED;
            run->model[iteration->slot].profile = profile;
            if (profile == 1) {
                ++run->coverage.install_client;
            } else {
                ++run->coverage.install_server;
            }
        } else if (
            error == MICROS_ENDPOINT_ERROR_PROFILE
        ) {
            ++run->coverage.install_invalid;
        }
    }

    return true;
}

bool endpoint_model_activate(
    struct endpoint_model_fixture *fixture,
    struct endpoint_model_run *run,
    struct endpoint_model_iteration *iteration
)
{
    iteration->slot = endpoint_model_find_slot(run->model, iteration->value, endpoint_model_slot_has_endpoint);
    if (iteration->slot != MODEL_SLOT_COUNT) {
        bool allowed =
            run->model[iteration->slot].endpoint_phase
                == MODEL_ENDPOINT_PROFILED;
        enum micros_endpoint_error error =
            micros_endpoint_activate(
                &fixture->registry,
                &fixture->objects,
                run->model[iteration->slot].endpoint
            );

        if (
            error
                != (
                    allowed
                        ? MICROS_ENDPOINT_OK
                        : MICROS_ENDPOINT_ERROR_STATE
                )
        ) {
            return endpoint_model_model_fail(
                fixture,
                run->step,
                iteration->operation,
                iteration->value,
                "activation result diverged"
            );
        }
        if (allowed) {
            run->model[iteration->slot].endpoint_phase =
                MODEL_ENDPOINT_ACTIVE;
            ++run->coverage.activate;
        }
    }

    return true;
}

bool endpoint_model_authorize(
    struct endpoint_model_fixture *fixture,
    struct endpoint_model_run *run,
    struct endpoint_model_iteration *iteration
)
{
    static const uint32_t operations[3] = {
        MICROS_PRIVILEGE_OPERATION_CALL,
        MICROS_PRIVILEGE_OPERATION_SEND,
        MICROS_PRIVILEGE_OPERATION_NOTIFY,
    };
    size_t target;
    uint32_t target_random = endpoint_model_next_random(&run->random_state);
    uint32_t authorization_operation =
        operations[
            (endpoint_model_next_random(&run->random_state) >> 16) % 3
        ];
    enum micros_endpoint_error expected;
    enum micros_endpoint_error error;

    iteration->slot = endpoint_model_find_slot(run->model, iteration->value, endpoint_model_slot_has_endpoint);
    if ((iteration->value & 8) != 0) {
        return endpoint_model_authorize_reply_receive(
            fixture, run, iteration,
            &authorization_operation, &expected, &error
        );
    }
    if ((iteration->value & 4) != 0) {
        return endpoint_model_authorize_reply(
            fixture, run, iteration,
            &authorization_operation, &expected, &error
        );
    }
    endpoint_model_select_authorization_target(
        run, iteration, target_random, &target,
        &authorization_operation
    );
    if (
        iteration->slot == MODEL_SLOT_COUNT
        || target == MODEL_SLOT_COUNT
    ) {
        return true;
    }
    if (!endpoint_model_authorize_selected_target(
        fixture, run, iteration, target, authorization_operation,
        &expected, &error
    )) {
        return false;
    }

    return true;
}

bool endpoint_model_close(
    struct endpoint_model_fixture *fixture,
    struct endpoint_model_run *run,
    struct endpoint_model_iteration *iteration
)
{
    iteration->slot = endpoint_model_find_slot(
        run->model,
        iteration->value,
        endpoint_model_slot_has_nonactive_endpoint
    );
    if (iteration->slot == MODEL_SLOT_COUNT) {
        iteration->slot = endpoint_model_find_slot(run->model, iteration->value, endpoint_model_slot_has_endpoint);
    }
    if (iteration->slot != MODEL_SLOT_COUNT) {
        if (
            micros_endpoint_close(
                &fixture->registry,
                &fixture->objects,
                run->model[iteration->slot].endpoint
            ) != MICROS_ENDPOINT_OK
        ) {
            return endpoint_model_model_fail(
                fixture,
                run->step,
                iteration->operation,
                iteration->value,
                "close transition failed"
            );
        }
        run->model[iteration->slot].endpoint_phase = MODEL_ENDPOINT_CLOSED;
        run->model[iteration->slot].stale_endpoint = run->model[iteration->slot].endpoint;
        run->model[iteration->slot].endpoint = 0;
        run->model[iteration->slot].profile = 0;
        ++run->coverage.close;
    }

    return true;
}

bool endpoint_model_release(
    struct endpoint_model_fixture *fixture,
    struct endpoint_model_run *run,
    struct endpoint_model_iteration *iteration
)
{
    iteration->slot = endpoint_model_find_slot(run->model, iteration->value, endpoint_model_slot_is_releasable);
    if (iteration->slot != MODEL_SLOT_COUNT) {
        if (
            micros_thread_release(
                &fixture->objects,
                run->model[iteration->slot].thread
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_process_release(
                &fixture->objects,
                run->model[iteration->slot].process
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return endpoint_model_model_fail(
                fixture,
                run->step,
                iteration->operation,
                iteration->value,
                "release transition failed"
            );
        }
        run->model[iteration->slot].process_live = false;
        run->model[iteration->slot].thread_live = false;
        run->model[iteration->slot].endpoint_phase =
            MODEL_ENDPOINT_UNUSED;
        run->model[iteration->slot].endpoint = 0;
        run->model[iteration->slot].profile = 0;
        ++run->coverage.release;
    }

    return true;
}

bool endpoint_model_stale(
    struct endpoint_model_fixture *fixture,
    struct endpoint_model_run *run,
    struct endpoint_model_iteration *iteration
)
{
    iteration->slot = endpoint_model_find_slot(
        run->model,
        iteration->value,
        endpoint_model_slot_has_stale_endpoint
    );
    if (iteration->slot != MODEL_SLOT_COUNT) {
        const struct micros_endpoint_record *record =
            (const struct micros_endpoint_record *)(uintptr_t)1;

        if (
            micros_endpoint_resolve_internal(
                &fixture->registry,
                &fixture->objects,
                run->model[iteration->slot].stale_endpoint,
                &record
            ) != MICROS_ENDPOINT_ERROR_STALE
            || record
                != (
                    const struct micros_endpoint_record *
                )(uintptr_t)1
        ) {
            return endpoint_model_model_fail(
                fixture,
                run->step,
                iteration->operation,
                iteration->value,
                "stale resolution diverged"
            );
        }
        ++run->coverage.stale;
    }

    return true;
}
