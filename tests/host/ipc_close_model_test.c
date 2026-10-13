#include "ipc_close_test_internal.h"

static const char *const close_scenario_operation_names[
    CLOSE_SCENARIO_OPERATION_COUNT
] = {
    "foreign-send",
    "foreign-call",
    "specific-receive",
    "any-receive",
    "closing-send",
    "staged-send",
    "notification-from-closing",
    "notification-to-closing",
};

static uint32_t close_scenario_next_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void close_scenario_record(
    struct ipc_close_test_context *fixture,
    size_t case_index,
    enum close_scenario_operation operation
)
{
    size_t index =
        fixture->close_scenario_trace_count % CLOSE_SCENARIO_TRACE_COUNT;

    fixture->close_scenario_trace[index].case_index = case_index;
    fixture->close_scenario_trace[index].operation = operation;
    ++fixture->close_scenario_trace_count;
}

static bool close_scenario_fail(
    struct ipc_close_test_context *fixture,
    uint32_t seed,
    size_t case_index,
    enum close_scenario_operation operation,
    const char *message
)
{
    size_t available =
        fixture->close_scenario_trace_count < CLOSE_SCENARIO_TRACE_COUNT
            ? fixture->close_scenario_trace_count
            : CLOSE_SCENARIO_TRACE_COUNT;
    size_t first = fixture->close_scenario_trace_count - available;
    size_t index;

    fprintf(
        stderr,
        "IPC close scenarios seed=0x%08x case=%zu op=%s: %s\n",
        seed,
        case_index,
        close_scenario_operation_names[operation],
        message
    );
    for (index = first; index < fixture->close_scenario_trace_count; ++index) {
        const struct close_scenario_trace_entry *entry =
            &fixture->close_scenario_trace[
                index % CLOSE_SCENARIO_TRACE_COUNT
            ];

        fprintf(
            stderr,
            "  case=%zu op=%s\n",
            entry->case_index,
            close_scenario_operation_names[entry->operation]
        );
    }
    return false;
}

struct close_model_scenario {
    uint32_t seed;
    size_t case_index;
    enum close_scenario_operation operation;
    struct micros_ipc_message message;
    struct micros_thread actor_snapshot;
    struct micros_thread peer_snapshot;
    uint64_t event_mask;
    bool actor_should_fail;
    bool staged_close_blocked;
};

static void select_close_model_scenario(
    uint32_t *random_state,
    struct close_model_scenario *scenario
)
{
    scenario->operation =
        (enum close_scenario_operation)(
            close_scenario_next_random(random_state)
            % CLOSE_SCENARIO_OPERATION_COUNT
        );
    scenario->message = ipc_close_test_message_pattern(
        UINT32_C(0x3000) + (uint32_t)scenario->case_index,
        (uint8_t)*random_state
    );
    scenario->event_mask =
        (
            (uint64_t)close_scenario_next_random(
                random_state
            )
            << 32
        )
        | close_scenario_next_random(random_state)
        | UINT64_C(1);
    scenario->actor_should_fail = false;
    scenario->staged_close_blocked = false;

}

static bool prepare_foreign_send_case(
    struct ipc_close_test_context *fixture,
    struct close_model_scenario *scenario
)
{
    scenario->actor_should_fail = true;
    if (
        micros_ipc_send(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[1],
            fixture->endpoints[0],
            &scenario->message
        ) != MICROS_IPC_OK
    ) {
        return close_scenario_fail(
            fixture,
            scenario->seed,
            scenario->case_index,
            scenario->operation,
            "foreign send setup failed"
        );
    }
    return true;
}

static bool prepare_foreign_call_case(
    struct ipc_close_test_context *fixture,
    struct close_model_scenario *scenario
)
{
                scenario->actor_should_fail = true;
                if (
                    micros_ipc_call(
                        &fixture->registry,
                        &fixture->objects,
                        fixture->primary_threads[1],
                        fixture->endpoints[0],
                        &scenario->message,
                        (uintptr_t)&scenario->message
    ) != MICROS_IPC_OK
                ) {
                    return close_scenario_fail(
                        fixture,
                        scenario->seed,
                        scenario->case_index,
                        scenario->operation,
                        "foreign call setup failed"
                    );
                }
                return true;
}

static bool prepare_specific_receive_case(
    struct ipc_close_test_context *fixture,
    struct close_model_scenario *scenario
)
{
    scenario->actor_should_fail = true;
    if (
        micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[1],
            fixture->endpoints[0],
            UINT64_C(0x61000000)
        ) != MICROS_IPC_OK
    ) {
        return close_scenario_fail(
            fixture,
            scenario->seed,
            scenario->case_index,
            scenario->operation,
            "specific receive setup failed"
        );
    }
    return true;
}

static bool prepare_any_receive_case(
    struct ipc_close_test_context *fixture,
    struct close_model_scenario *scenario
)
{
    if (
        micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[1],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x61000000)
        ) != MICROS_IPC_OK
    ) {
        return close_scenario_fail(
            fixture,
            scenario->seed,
            scenario->case_index,
            scenario->operation,
            "ANY receive setup failed"
        );
    }
    return true;
}

static bool prepare_closing_send_case(
    struct ipc_close_test_context *fixture,
    struct close_model_scenario *scenario
)
{
    if (
        micros_ipc_send(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[0],
            fixture->endpoints[1],
            &scenario->message
        ) != MICROS_IPC_OK
        || !ipc_close_test_hold_thread_for_close(fixture, fixture->primary_threads[0])
    ) {
        return close_scenario_fail(
            fixture,
            scenario->seed,
            scenario->case_index,
            scenario->operation,
            "closing send setup failed"
        );
    }
    return true;
}

static bool prepare_staged_send_case(
    struct ipc_close_test_context *fixture,
    struct close_model_scenario *scenario
)
{
    scenario->staged_close_blocked = true;
    if (
        micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[1],
            fixture->endpoints[0],
            UINT64_C(0x61000000)
        ) != MICROS_IPC_OK
        || micros_ipc_send(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[0],
            fixture->endpoints[1],
            &scenario->message
        ) != MICROS_IPC_OK
        || !ipc_close_test_hold_thread_for_close(fixture, fixture->primary_threads[0])
    ) {
        return close_scenario_fail(
            fixture,
            scenario->seed,
            scenario->case_index,
            scenario->operation,
            "staged send setup failed"
        );
    }
    return true;
}

static bool prepare_notification_from_closing_case(
    struct ipc_close_test_context *fixture,
    struct close_model_scenario *scenario
)
{
    if (
        micros_ipc_notify(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[0],
            fixture->endpoints[1],
            scenario->event_mask
        ) != MICROS_IPC_OK
        || !ipc_close_test_hold_thread_for_close(fixture, fixture->primary_threads[0])
    ) {
        return close_scenario_fail(
            fixture,
            scenario->seed,
            scenario->case_index,
            scenario->operation,
            "closing notification setup failed"
        );
    }
    return true;
}

static bool prepare_notification_to_closing_case(
    struct ipc_close_test_context *fixture,
    struct close_model_scenario *scenario
)
{
    if (
        micros_ipc_notify(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[1],
            fixture->endpoints[0],
            scenario->event_mask
        ) != MICROS_IPC_OK
    ) {
        return close_scenario_fail(
            fixture,
            scenario->seed,
            scenario->case_index,
            scenario->operation,
            "foreign notification setup failed"
        );
    }
    return true;
}

static bool prepare_close_model_operation(
    struct ipc_close_test_context *fixture,
    struct close_model_scenario *scenario
)
{
    switch (scenario->operation) {
    case CLOSE_SCENARIO_FOREIGN_SEND:
        return prepare_foreign_send_case(fixture, scenario);
    case CLOSE_SCENARIO_FOREIGN_CALL:
        return prepare_foreign_call_case(fixture, scenario);
    case CLOSE_SCENARIO_SPECIFIC_RECEIVE:
        return prepare_specific_receive_case(fixture, scenario);
    case CLOSE_SCENARIO_ANY_RECEIVE:
        return prepare_any_receive_case(fixture, scenario);
    case CLOSE_SCENARIO_CLOSING_SEND:
        return prepare_closing_send_case(fixture, scenario);
    case CLOSE_SCENARIO_STAGED_SEND:
        return prepare_staged_send_case(fixture, scenario);
    case CLOSE_SCENARIO_NOTIFICATION_FROM_CLOSING:
        return prepare_notification_from_closing_case(fixture, scenario);
    case CLOSE_SCENARIO_NOTIFICATION_TO_CLOSING:
        return prepare_notification_to_closing_case(fixture, scenario);
    default:
        return close_scenario_fail(
            fixture, scenario->seed, scenario->case_index,
            scenario->operation, "unknown operation"
        );
    }
}

static bool close_and_validate_model_endpoint(
    struct ipc_close_test_context *fixture,
    struct close_model_scenario *scenario
)
{
    scenario->actor_snapshot = fixture->objects.threads[fixture->primary_threads[1].slot];
    scenario->peer_snapshot = fixture->objects.threads[fixture->primary_threads[2].slot];
    if (scenario->staged_close_blocked) {
        if (
            !ipc_close_test_expect_close_failure_unchanged(
                fixture,
                MICROS_IPC_ERROR_STATE,
                fixture->endpoints[0]
            )
            || !ipc_close_test_clear_staged_delivery(fixture, fixture->primary_threads[1])
        ) {
            return close_scenario_fail(
                fixture,
                scenario->seed,
                scenario->case_index,
                scenario->operation,
                "staged close preflight mismatch"
            );
        }
        scenario->actor_snapshot =
            fixture->objects.threads[fixture->primary_threads[1].slot];
    }
    if (
        micros_ipc_endpoint_close(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[0]
        ) != MICROS_IPC_OK
    ) {
        return close_scenario_fail(
            fixture,
            scenario->seed,
            scenario->case_index,
            scenario->operation,
            "close failed"
        );
    }
    if (
        !ipc_close_test_closing_thread_is_detached(fixture, fixture->primary_threads[0])
        || fixture->registry.endpoints[fixture->processes[0].slot].state
            != MICROS_ENDPOINT_STATE_FREE
        || micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) != MICROS_ENDPOINT_OK
    ) {
        return close_scenario_fail(
            fixture,
            scenario->seed,
            scenario->case_index,
            scenario->operation,
            "closed state is invalid"
        );
    }
    return true;
}

static bool compare_close_actor_peer_and_pending_state(
    struct ipc_close_test_context *fixture,
    struct close_model_scenario *scenario
)
{
    if (
        scenario->actor_should_fail
            ? !ipc_close_test_thread_has_dead_endpoint_result(
                fixture,
                fixture->primary_threads[1]
            )
            : memcmp(
                &fixture->objects.threads[fixture->primary_threads[1].slot],
                &scenario->actor_snapshot,
                sizeof(scenario->actor_snapshot)
            ) != 0
    ) {
        return close_scenario_fail(
            fixture,
            scenario->seed,
            scenario->case_index,
            scenario->operation,
            "actor outcome mismatch"
        );
    }
    if (
        memcmp(
            &fixture->objects.threads[fixture->primary_threads[2].slot],
            &scenario->peer_snapshot,
            sizeof(scenario->peer_snapshot)
        ) != 0
    ) {
        return close_scenario_fail(
            fixture,
            scenario->seed,
            scenario->case_index,
            scenario->operation,
            "unrelated peer changed"
        );
    }
    if (
        scenario->operation == CLOSE_SCENARIO_CLOSING_SEND
        && !ipc_close_test_thread_handle_is_zero(
            fixture->registry.endpoints[fixture->processes[1].slot].sender_head
        )
    ) {
        return close_scenario_fail(
            fixture,
            scenario->seed,
            scenario->case_index,
            scenario->operation,
            "closing sender remained queued"
        );
    }
    if (
        scenario->operation
            == CLOSE_SCENARIO_NOTIFICATION_FROM_CLOSING
        && (
            fixture->registry.endpoints[fixture->processes[1].slot]
                .pending_notification_sources != 0
            || fixture->registry.endpoints[fixture->processes[1].slot]
                .pending_events[fixture->processes[0].slot] != 0
        )
    ) {
        return close_scenario_fail(
            fixture,
            scenario->seed,
            scenario->case_index,
            scenario->operation,
            "closing notification remained pending"
        );
    }
    return true;
}

static bool check_close_model_coverage(
    struct ipc_close_test_context *fixture,
    uint32_t seed,
    const size_t coverage[CLOSE_SCENARIO_OPERATION_COUNT]
)
{
    size_t case_index;

    for (
        case_index = 0;
        case_index < CLOSE_SCENARIO_OPERATION_COUNT;
        ++case_index
    ) {
        if (coverage[case_index] == 0) {
            return close_scenario_fail(
                fixture,
                seed,
                CLOSE_SCENARIO_CASE_COUNT,
                (enum close_scenario_operation)case_index,
                "operation received no coverage"
            );
        }
    }
    return true;
}

bool test_seeded_close_scenarios(struct ipc_close_test_context *fixture)
{
    const uint32_t seed = UINT32_C(0x30c105e1);
    uint32_t random_state = seed;
    size_t coverage[CLOSE_SCENARIO_OPERATION_COUNT] = {0};
    size_t case_index;

    fixture->close_scenario_trace_count = 0;
    for (case_index = 0; case_index < CLOSE_SCENARIO_CASE_COUNT; ++case_index) {
        struct close_model_scenario scenario;

        scenario.seed = seed;
        scenario.case_index = case_index;
        select_close_model_scenario(&random_state, &scenario);
        close_scenario_record(fixture, case_index, scenario.operation);
        ++coverage[scenario.operation];
        if (!ipc_close_test_setup_fixture(fixture, 3, 1)) {
            return close_scenario_fail(
                fixture, seed, case_index, scenario.operation, "fixture setup failed"
            );
        }
        if (
            !prepare_close_model_operation(fixture, &scenario)
            || !close_and_validate_model_endpoint(fixture, &scenario)
            || !compare_close_actor_peer_and_pending_state(fixture, &scenario)
        ) {
            return false;
        }
    }
    return check_close_model_coverage(fixture, seed, coverage);
}
