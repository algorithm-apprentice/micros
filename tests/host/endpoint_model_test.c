#include "endpoint_model_internal.h"

#include <stdio.h>
#include <string.h>

bool micros_endpoint_model_test_run(void)
{
    struct endpoint_model_fixture *fixture = endpoint_model_context();
    const uint32_t seed = UINT32_C(0xe029cafe);
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
    struct model_slot model[MODEL_SLOT_COUNT];
    struct model_coverage coverage;
    uint32_t random_state = seed;
    size_t step;

    memset(model, 0, sizeof(model));
    memset(&coverage, 0, sizeof(coverage));
    if (!endpoint_model_setup_fixture(fixture)) {
        fprintf(stderr, "endpoint model fixture failed\n");
        return false;
    }

    for (step = 0; step < MODEL_STEPS; ++step) {
        uint32_t random = endpoint_model_next_random(&random_state);
        enum model_operation operation =
            (enum model_operation)(
                (random >> 24) % MODEL_OPERATION_COUNT
            );
        size_t slot = MODEL_SLOT_COUNT;
        uint32_t value = endpoint_model_next_random(&random_state);

        if (
            step
                < sizeof(scripted_operations)
                    / sizeof(scripted_operations[0])
        ) {
            operation = scripted_operations[step];
            value = scripted_values[step];
        }
        switch (operation) {
        case MODEL_OPERATION_CREATE:
            for (slot = 0; slot < MODEL_SLOT_COUNT; ++slot) {
                if (!model[slot].process_live) {
                    break;
                }
            }
            if (slot != MODEL_SLOT_COUNT) {
                struct micros_process_handle process;
                struct micros_thread_handle thread;

                if (
                    micros_process_create(&fixture->objects, &process)
                        != MICROS_KERNEL_OBJECT_OK
                    || process.slot != slot
                    || process.generation
                        != model[slot].generation + 1
                    || micros_thread_create(
                        &fixture->objects,
                        process,
                        &thread
                    ) != MICROS_KERNEL_OBJECT_OK
                ) {
                    return endpoint_model_model_fail(
                        fixture,
                        step,
                        operation,
                        value,
                        "create transition failed"
                    );
                }
                model[slot].process_live = true;
                model[slot].thread_live = true;
                model[slot].generation = process.generation;
                model[slot].process = process;
                model[slot].thread = thread;
                model[slot].endpoint_phase =
                    MODEL_ENDPOINT_UNUSED;
                model[slot].endpoint = 0;
                model[slot].profile = 0;
                ++coverage.create;
            }
            break;
        case MODEL_OPERATION_RESERVE:
            slot = endpoint_model_find_slot(model, value, endpoint_model_slot_is_live);
            if (slot != MODEL_SLOT_COUNT) {
                micros_endpoint_t endpoint = MICROS_ENDPOINT_ANY;
                bool allowed =
                    model[slot].endpoint_phase
                        == MODEL_ENDPOINT_UNUSED;
                enum micros_endpoint_error error =
                    micros_endpoint_reserve(
                        &fixture->registry,
                        &fixture->objects,
                        model[slot].process,
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
                        step,
                        operation,
                        value,
                        "reserve result diverged"
                    );
                }
                if (allowed) {
                    model[slot].endpoint_phase =
                        MODEL_ENDPOINT_RESERVED;
                    model[slot].endpoint = endpoint;
                    model[slot].profile = 0;
                    ++coverage.reserve;
                } else {
                    ++coverage.reserve_rejected;
                }
            }
            break;
        case MODEL_OPERATION_INSTALL:
            slot = endpoint_model_find_slot(model, value, endpoint_model_slot_has_endpoint);
            if (slot != MODEL_SLOT_COUNT) {
                uint8_t profile = (value & 3) == 3
                    ? 3
                    : (uint8_t)(slot % 2 + 1);
                bool installable =
                    model[slot].endpoint_phase
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
                        model[slot].process,
                        profile
                    );

                value = profile;
                if (error != expected) {
                    return endpoint_model_model_fail(
                        fixture,
                        step,
                        operation,
                        value,
                        "profile installation diverged"
                    );
                }
                if (error == MICROS_ENDPOINT_OK) {
                    model[slot].endpoint_phase =
                        MODEL_ENDPOINT_PROFILED;
                    model[slot].profile = profile;
                    if (profile == 1) {
                        ++coverage.install_client;
                    } else {
                        ++coverage.install_server;
                    }
                } else if (
                    error == MICROS_ENDPOINT_ERROR_PROFILE
                ) {
                    ++coverage.install_invalid;
                }
            }
            break;
        case MODEL_OPERATION_ACTIVATE:
            slot = endpoint_model_find_slot(model, value, endpoint_model_slot_has_endpoint);
            if (slot != MODEL_SLOT_COUNT) {
                bool allowed =
                    model[slot].endpoint_phase
                        == MODEL_ENDPOINT_PROFILED;
                enum micros_endpoint_error error =
                    micros_endpoint_activate(
                        &fixture->registry,
                        &fixture->objects,
                        model[slot].endpoint
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
                        step,
                        operation,
                        value,
                        "activation result diverged"
                    );
                }
                if (allowed) {
                    model[slot].endpoint_phase =
                        MODEL_ENDPOINT_ACTIVE;
                    ++coverage.activate;
                }
            }
            break;
        case MODEL_OPERATION_AUTHORIZE:
        {
            static const uint32_t operations[3] = {
                MICROS_PRIVILEGE_OPERATION_CALL,
                MICROS_PRIVILEGE_OPERATION_SEND,
                MICROS_PRIVILEGE_OPERATION_NOTIFY,
            };
            size_t target;
            uint32_t target_random = endpoint_model_next_random(&random_state);
            uint32_t authorization_operation =
                operations[
                    (endpoint_model_next_random(&random_state) >> 16) % 3
                ];
            enum micros_endpoint_error expected;
            enum micros_endpoint_error error;

            slot = endpoint_model_find_slot(model, value, endpoint_model_slot_has_endpoint);
            if ((value & 8) != 0) {
                authorization_operation =
                    MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE;
                if (
                    slot == MODEL_SLOT_COUNT
                    || model[slot].endpoint_phase
                        != MODEL_ENDPOINT_ACTIVE
                ) {
                    expected = MICROS_ENDPOINT_ERROR_STATE;
                } else if (model[slot].profile == 2) {
                    expected = MICROS_ENDPOINT_OK;
                } else {
                    expected = MICROS_ENDPOINT_ERROR_UNAUTHORIZED;
                }
                if (slot == MODEL_SLOT_COUNT) {
                    break;
                }
                error = micros_endpoint_authorize_operation(
                    &fixture->registry,
                    &fixture->objects,
                    model[slot].endpoint,
                    authorization_operation
                );
                value = authorization_operation;
                if (error != expected) {
                    return endpoint_model_model_fail(
                        fixture,
                        step,
                        operation,
                        value,
                        "operation authorization diverged"
                    );
                }
                if (error == MICROS_ENDPOINT_OK) {
                    ++coverage.reply_receive_allowed;
                } else if (
                    error == MICROS_ENDPOINT_ERROR_UNAUTHORIZED
                ) {
                    ++coverage.reply_receive_denied;
                } else {
                    ++coverage.authorize_state;
                }
                break;
            }
            if ((value & 4) != 0) {
                authorization_operation =
                    MICROS_PRIVILEGE_OPERATION_REPLY;
                if (
                    slot == MODEL_SLOT_COUNT
                    || model[slot].endpoint_phase
                        != MODEL_ENDPOINT_ACTIVE
                ) {
                    expected = MICROS_ENDPOINT_ERROR_STATE;
                } else if (model[slot].profile == 2) {
                    expected = MICROS_ENDPOINT_OK;
                } else {
                    expected = MICROS_ENDPOINT_ERROR_UNAUTHORIZED;
                }
                if (slot == MODEL_SLOT_COUNT) {
                    break;
                }
                error = micros_endpoint_authorize_operation(
                    &fixture->registry,
                    &fixture->objects,
                    model[slot].endpoint,
                    authorization_operation
                );
                value = authorization_operation;
                if (error != expected) {
                    return endpoint_model_model_fail(
                        fixture,
                        step,
                        operation,
                        value,
                        "operation authorization diverged"
                    );
                }
                if (error == MICROS_ENDPOINT_OK) {
                    ++coverage.reply_allowed;
                } else if (
                    error == MICROS_ENDPOINT_ERROR_UNAUTHORIZED
                ) {
                    ++coverage.reply_denied;
                } else {
                    ++coverage.authorize_state;
                }
                break;
            }
            target = MODEL_SLOT_COUNT;
            if (
                slot != MODEL_SLOT_COUNT
                && model[slot].endpoint_phase
                    == MODEL_ENDPOINT_ACTIVE
            ) {
                if ((value & 2) != 0) {
                    target = slot;
                    authorization_operation =
                        MICROS_PRIVILEGE_OPERATION_NOTIFY;
                }
            }
            if (
                target == MODEL_SLOT_COUNT
                && slot != MODEL_SLOT_COUNT
                && model[slot].endpoint_phase
                    == MODEL_ENDPOINT_ACTIVE
            ) {
                uint8_t target_profile =
                    model[slot].profile == 1 ? 2 : 1;

                target = endpoint_model_find_active_profile(
                    model,
                    target_random,
                    target_profile
                );
                if (target != MODEL_SLOT_COUNT) {
                    if ((value & 1) != 0) {
                        authorization_operation =
                            MICROS_PRIVILEGE_OPERATION_NOTIFY;
                    } else {
                        authorization_operation =
                            model[slot].profile == 1
                                ? MICROS_PRIVILEGE_OPERATION_CALL
                                : MICROS_PRIVILEGE_OPERATION_SEND;
                    }
                }
            }
            if (target == MODEL_SLOT_COUNT) {
                target = endpoint_model_find_slot(
                    model,
                    target_random,
                    endpoint_model_slot_has_endpoint
                );
            }
            if (
                slot == MODEL_SLOT_COUNT
                || target == MODEL_SLOT_COUNT
            ) {
                break;
            }
            if (
                model[slot].endpoint_phase
                    != MODEL_ENDPOINT_ACTIVE
                || model[target].endpoint_phase
                    != MODEL_ENDPOINT_ACTIVE
            ) {
                expected = MICROS_ENDPOINT_ERROR_STATE;
            } else if (
                endpoint_model_target_is_allowed(
                    model[slot].profile,
                    authorization_operation,
                    model[target].profile
                )
            ) {
                expected = MICROS_ENDPOINT_OK;
            } else {
                expected = MICROS_ENDPOINT_ERROR_UNAUTHORIZED;
            }
            error = micros_endpoint_authorize_target(
                &fixture->registry,
                &fixture->objects,
                model[slot].endpoint,
                authorization_operation,
                model[target].endpoint
            );
            value = authorization_operation;
            if (error != expected) {
                return endpoint_model_model_fail(
                    fixture,
                    step,
                    operation,
                    value,
                    "authorization result diverged"
                );
            }
            if (error == MICROS_ENDPOINT_OK) {
                ++coverage.authorize_allowed;
                if (
                    authorization_operation
                        == MICROS_PRIVILEGE_OPERATION_NOTIFY
                ) {
                    ++coverage.notify_allowed;
                }
            } else if (error == MICROS_ENDPOINT_ERROR_UNAUTHORIZED) {
                ++coverage.authorize_denied;
                if (
                    authorization_operation
                        == MICROS_PRIVILEGE_OPERATION_NOTIFY
                ) {
                    ++coverage.notify_denied;
                }
            } else {
                ++coverage.authorize_state;
            }
            break;
        }
        case MODEL_OPERATION_CLOSE:
            slot = endpoint_model_find_slot(
                model,
                value,
                endpoint_model_slot_has_nonactive_endpoint
            );
            if (slot == MODEL_SLOT_COUNT) {
                slot = endpoint_model_find_slot(model, value, endpoint_model_slot_has_endpoint);
            }
            if (slot != MODEL_SLOT_COUNT) {
                if (
                    micros_endpoint_close(
                        &fixture->registry,
                        &fixture->objects,
                        model[slot].endpoint
                    ) != MICROS_ENDPOINT_OK
                ) {
                    return endpoint_model_model_fail(
                        fixture,
                        step,
                        operation,
                        value,
                        "close transition failed"
                    );
                }
                model[slot].endpoint_phase = MODEL_ENDPOINT_CLOSED;
                model[slot].stale_endpoint = model[slot].endpoint;
                model[slot].endpoint = 0;
                model[slot].profile = 0;
                ++coverage.close;
            }
            break;
        case MODEL_OPERATION_RELEASE:
            slot = endpoint_model_find_slot(model, value, endpoint_model_slot_is_releasable);
            if (slot != MODEL_SLOT_COUNT) {
                if (
                    micros_thread_release(
                        &fixture->objects,
                        model[slot].thread
                    ) != MICROS_KERNEL_OBJECT_OK
                    || micros_process_release(
                        &fixture->objects,
                        model[slot].process
                    ) != MICROS_KERNEL_OBJECT_OK
                ) {
                    return endpoint_model_model_fail(
                        fixture,
                        step,
                        operation,
                        value,
                        "release transition failed"
                    );
                }
                model[slot].process_live = false;
                model[slot].thread_live = false;
                model[slot].endpoint_phase =
                    MODEL_ENDPOINT_UNUSED;
                model[slot].endpoint = 0;
                model[slot].profile = 0;
                ++coverage.release;
            }
            break;
        case MODEL_OPERATION_STALE:
            slot = endpoint_model_find_slot(
                model,
                value,
                endpoint_model_slot_has_stale_endpoint
            );
            if (slot != MODEL_SLOT_COUNT) {
                const struct micros_endpoint_record *record =
                    (const struct micros_endpoint_record *)(uintptr_t)1;

                if (
                    micros_endpoint_resolve_internal(
                        &fixture->registry,
                        &fixture->objects,
                        model[slot].stale_endpoint,
                        &record
                    ) != MICROS_ENDPOINT_ERROR_STALE
                    || record
                        != (
                            const struct micros_endpoint_record *
                        )(uintptr_t)1
                ) {
                    return endpoint_model_model_fail(
                        fixture,
                        step,
                        operation,
                        value,
                        "stale resolution diverged"
                    );
                }
                ++coverage.stale;
            }
            break;
        default:
            return endpoint_model_model_fail(
                fixture,
                step,
                operation,
                value,
                "unknown model operation"
            );
        }
        endpoint_model_record_transition(fixture, step, operation, slot, value);
        if (!endpoint_model_validate_model(fixture, model, step, operation, value)) {
            return false;
        }
    }

    for (step = 0; step < MODEL_SLOT_COUNT; ++step) {
        if (!model[step].process_live) {
            continue;
        }
        if (endpoint_model_phase_has_endpoint(model[step].endpoint_phase)) {
            if (
                micros_endpoint_close(
                    &fixture->registry,
                    &fixture->objects,
                    model[step].endpoint
                ) != MICROS_ENDPOINT_OK
            ) {
                return endpoint_model_model_fail(
                    fixture,
                    MODEL_STEPS,
                    MODEL_OPERATION_CLOSE,
                    (uint32_t)step,
                    "cleanup close failed"
                );
            }
        }
        if (
            micros_thread_release(&fixture->objects, model[step].thread)
                != MICROS_KERNEL_OBJECT_OK
            || micros_process_release(
                &fixture->objects,
                model[step].process
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return endpoint_model_model_fail(
                fixture,
                MODEL_STEPS,
                MODEL_OPERATION_RELEASE,
                (uint32_t)step,
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
    if (
        coverage.create == 0
        || coverage.release == 0
        || coverage.reserve == 0
        || coverage.reserve_rejected == 0
        || coverage.install_client == 0
        || coverage.install_server == 0
        || coverage.install_invalid == 0
        || coverage.activate == 0
        || coverage.close == 0
        || coverage.authorize_allowed == 0
        || coverage.authorize_denied == 0
        || coverage.authorize_state == 0
        || coverage.notify_allowed == 0
        || coverage.notify_denied == 0
        || coverage.reply_allowed == 0
        || coverage.reply_denied == 0
        || coverage.reply_receive_allowed == 0
        || coverage.reply_receive_denied == 0
        || coverage.stale == 0
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
            coverage.create,
            coverage.release,
            coverage.reserve,
            coverage.reserve_rejected,
            coverage.install_client,
            coverage.install_server,
            coverage.install_invalid,
            coverage.activate,
            coverage.close,
            coverage.authorize_allowed,
            coverage.authorize_denied,
            coverage.authorize_state,
            coverage.notify_allowed,
            coverage.notify_denied,
            coverage.reply_allowed,
            coverage.reply_denied,
            coverage.reply_receive_allowed,
            coverage.reply_receive_denied,
            coverage.stale
        );
        return false;
    }
    return true;
}
