#include "ipc_notify_test_internal.h"

static const char *const notify_model_operation_names[
    NOTIFY_MODEL_OPERATION_COUNT
] = {
    "notify",
    "kernel-notify",
    "receive-any",
    "receive-specific",
    "zero-rejected",
    "operation-rejected",
    "target-rejected",
};

static uint32_t notify_model_next_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void notify_model_record(
    struct ipc_notify_test_context *fixture,
    size_t step,
    enum notify_model_operation operation,
    size_t source,
    uint64_t event_mask
)
{
    size_t index =
        fixture->notify_model_trace_count % NOTIFY_MODEL_TRACE_COUNT;

    fixture->notify_model_trace[index].step = step;
    fixture->notify_model_trace[index].operation = operation;
    fixture->notify_model_trace[index].source = source;
    fixture->notify_model_trace[index].event_mask = event_mask;
    ++fixture->notify_model_trace_count;
}

static bool notify_model_fail(
    struct ipc_notify_test_context *fixture,
    size_t step,
    enum notify_model_operation operation,
    const char *message
)
{
    size_t available =
        fixture->notify_model_trace_count < NOTIFY_MODEL_TRACE_COUNT
            ? fixture->notify_model_trace_count
            : NOTIFY_MODEL_TRACE_COUNT;
    size_t first = fixture->notify_model_trace_count - available;
    size_t offset;

    fprintf(
        stderr,
        "notify model seed=0x30c0a1e5 step=%zu operation=%s: %s\n",
        step,
        notify_model_operation_names[operation],
        message
    );
    for (offset = 0; offset < available; ++offset) {
        const struct notify_model_trace_entry *entry =
            &fixture->notify_model_trace[
                (first + offset) % NOTIFY_MODEL_TRACE_COUNT
            ];

        fprintf(
            stderr,
            "  trace step=%zu operation=%s source=%zu "
            "event=0x%016llx\n",
            entry->step,
            notify_model_operation_names[entry->operation],
            entry->source,
            (unsigned long long)entry->event_mask
        );
    }
    return false;
}

static bool notify_model_pending_matches(
    struct ipc_notify_test_context *fixture,
    const uint64_t expected[2],
    uint64_t expected_kernel
)
{
    const struct micros_endpoint_record *destination =
        &fixture->registry.endpoints[
            fixture->processes[NOTIFY_PROCESS_DESTINATION].slot
        ];
    uint64_t expected_bitmap = 0;
    size_t index;

    for (index = 0; index < 2; ++index) {
        size_t process_index =
            index == 0
                ? NOTIFY_PROCESS_SOURCE_LOW
                : NOTIFY_PROCESS_SOURCE_HIGH;
        size_t slot = fixture->processes[process_index].slot;

        if (expected[index] != 0) {
            expected_bitmap |= UINT64_C(1) << slot;
        }
        if (destination->pending_events[slot] != expected[index]) {
            return false;
        }
    }
    if (
        destination->pending_notification_sources != expected_bitmap
        || destination->pending_kernel_events != expected_kernel
    ) {
        return false;
    }
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        if (
            index != fixture->processes[NOTIFY_PROCESS_SOURCE_LOW].slot
            && index != fixture->processes[NOTIFY_PROCESS_SOURCE_HIGH].slot
            && destination->pending_events[index] != 0
        ) {
            return false;
        }
    }
    return true;
}

struct notify_model_step_state {
    uint64_t expected[2];
    uint64_t expected_kernel;
    struct notify_model_coverage coverage;
    uint32_t random_state;
    size_t step;
    enum notify_model_operation operation;
    size_t source;
    size_t process_index;
    uint64_t event_mask;
};

static void select_notify_model_operation(struct notify_model_step_state *state)
{
    static const enum notify_model_operation scripted_operations[] = {
        NOTIFY_MODEL_NOTIFY,
        NOTIFY_MODEL_NOTIFY,
        NOTIFY_MODEL_KERNEL_NOTIFY,
        NOTIFY_MODEL_KERNEL_NOTIFY,
        NOTIFY_MODEL_RECEIVE_ANY,
        NOTIFY_MODEL_NOTIFY,
        NOTIFY_MODEL_RECEIVE_ANY,
        NOTIFY_MODEL_RECEIVE_SPECIFIC,
        NOTIFY_MODEL_ZERO_REJECTED,
        NOTIFY_MODEL_OPERATION_REJECTED,
        NOTIFY_MODEL_TARGET_REJECTED,
    };
    static const size_t scripted_sources[] = {
        0,
        0,
        0,
        0,
        0,
        1,
        0,
        1,
        0,
        0,
        0,
    };

    uint32_t random = notify_model_next_random(&state->random_state);
    state->operation =
        (enum notify_model_operation)(
            (random >> 24) % NOTIFY_MODEL_OPERATION_COUNT
        );
    state->source = (random >> 16) & 1U;
    state->process_index =
        state->source == 0
            ? NOTIFY_PROCESS_SOURCE_LOW
            : NOTIFY_PROCESS_SOURCE_HIGH;
    state->event_mask =
        (
            (uint64_t)random << 32
        )
        | notify_model_next_random(&state->random_state);

    if (state->event_mask == 0) {
        state->event_mask = 1;
    }
    if (
        state->step
        < sizeof(scripted_operations)
            / sizeof(scripted_operations[0])
    ) {
        state->operation = scripted_operations[state->step];
        state->source = scripted_sources[state->step];
        state->process_index =
            state->source == 0
                ? NOTIFY_PROCESS_SOURCE_LOW
                : NOTIFY_PROCESS_SOURCE_HIGH;
        state->event_mask = UINT64_C(1) << state->step;
    }
    if (
        state->operation == NOTIFY_MODEL_RECEIVE_ANY
        && state->expected_kernel == 0
        && state->expected[0] == 0
        && state->expected[1] == 0
    ) {
        state->operation = NOTIFY_MODEL_NOTIFY;
    }
    if (
        state->operation == NOTIFY_MODEL_RECEIVE_SPECIFIC
        && state->expected[state->source] == 0
    ) {
        state->operation = NOTIFY_MODEL_NOTIFY;
    }
}

static bool model_notify_and_coalesce(
    struct ipc_notify_test_context *fixture,
    struct notify_model_step_state *state
)
{
    bool was_pending = state->expected[state->source] != 0;

    if (
        micros_ipc_notify(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[state->process_index],
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            state->event_mask
        ) != MICROS_IPC_OK
    ) {
        return notify_model_fail(
            fixture,
            state->step,
            state->operation,
            "authorized notify failed"
        );
    }
    state->expected[state->source] |= state->event_mask;
    ++state->coverage.notified;
    if (was_pending) {
        ++state->coverage.coalesced;
    }
    if (!ipc_notify_test_hold_runnable_thread(fixture, fixture->primary_threads[state->process_index])) {
        return notify_model_fail(
            fixture,
            state->step,
            state->operation,
            "notifier did not remain nonblocking"
        );
    }
    return true;
}

static bool model_inject_kernel_event(
    struct ipc_notify_test_context *fixture,
    struct notify_model_step_state *state
)
{
    bool was_pending = state->expected_kernel != 0;

    if (
        micros_ipc_inject_kernel_notification(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            state->event_mask
        ) != MICROS_IPC_OK
    ) {
        return notify_model_fail(
            fixture,
            state->step,
            state->operation,
            "kernel notify failed"
        );
    }
    state->expected_kernel |= state->event_mask;
    ++state->coverage.kernel_notified;
    if (was_pending) {
        ++state->coverage.kernel_coalesced;
    }
    return true;
}

static bool model_receive_selected_event(
    struct ipc_notify_test_context *fixture,
    struct notify_model_step_state *state
)
{
    struct micros_ipc_message delivered;
    size_t selected = state->source;
    micros_endpoint_t receive_source =
        fixture->endpoints[state->process_index];
    micros_endpoint_t expected_source;
    uint64_t expected_mask;

    if (state->operation == NOTIFY_MODEL_RECEIVE_ANY) {
        receive_source = MICROS_ENDPOINT_ANY;
        if (state->expected_kernel != 0) {
            expected_source = MICROS_ENDPOINT_NONE;
            expected_mask = state->expected_kernel;
        } else {
            selected = state->expected[0] != 0 ? 0 : 1;
            expected_source = fixture->endpoints[
                selected == 0
                    ? NOTIFY_PROCESS_SOURCE_LOW
                    : NOTIFY_PROCESS_SOURCE_HIGH
            ];
            expected_mask = state->expected[selected];
        }
    } else {
        expected_source = fixture->endpoints[state->process_index];
        expected_mask = state->expected[selected];
    }
    if (
        micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            receive_source,
            UINT64_C(0x61000000)
                + state->step * UINT64_C(0x100)
        ) != MICROS_IPC_OK
        || !ipc_notify_test_consume_staged_and_hold(
            fixture,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            &delivered
        )
        || !ipc_notify_test_notification_message_matches(
            &delivered,
            expected_source,
            expected_mask
        )
    ) {
        return notify_model_fail(
            fixture,
            state->step,
            state->operation,
            "receive selection diverged"
        );
    }
    if (state->operation == NOTIFY_MODEL_RECEIVE_ANY) {
        if (expected_source == MICROS_ENDPOINT_NONE) {
            state->expected_kernel = 0;
            ++state->coverage.kernel_received_any;
        } else {
            state->expected[selected] = 0;
        }
        ++state->coverage.received_any;
    } else {
        state->expected[selected] = 0;
        ++state->coverage.received_specific;
    }
    return true;
}

static bool model_reject_zero_event(
    struct ipc_notify_test_context *fixture,
    struct notify_model_step_state *state
)
{
    struct micros_endpoint_registry registry_snapshot = fixture->registry;
    struct micros_kernel_objects objects_snapshot = fixture->objects;

    if (
        micros_ipc_notify(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[state->process_index],
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            0
        ) != MICROS_IPC_ERROR_ARGUMENT
        || !ipc_notify_test_state_is_unchanged(
            fixture,
            &registry_snapshot,
            &objects_snapshot
        )
    ) {
        return notify_model_fail(
            fixture,
            state->step,
            state->operation,
            "zero event mask changed state"
        );
    }
    ++state->coverage.zero_rejected;
    return true;
}

static bool model_reject_notification_operation(
    struct ipc_notify_test_context *fixture,
    struct notify_model_step_state *state
)
{
    struct micros_endpoint_registry registry_snapshot = fixture->registry;
    struct micros_kernel_objects objects_snapshot = fixture->objects;

    if (
        micros_ipc_notify(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_NO_NOTIFY],
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            state->event_mask
        ) != MICROS_IPC_ERROR_UNAUTHORIZED
        || !ipc_notify_test_state_is_unchanged(
            fixture,
            &registry_snapshot,
            &objects_snapshot
        )
    ) {
        return notify_model_fail(
            fixture,
            state->step,
            state->operation,
            "operation denial changed state"
        );
    }
    ++state->coverage.operation_rejected;
    return true;
}

static bool model_reject_notification_target(
    struct ipc_notify_test_context *fixture,
    struct notify_model_step_state *state
)
{
    struct micros_endpoint_registry registry_snapshot = fixture->registry;
    struct micros_kernel_objects objects_snapshot = fixture->objects;

    if (
        micros_ipc_notify(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[
                NOTIFY_PROCESS_ISOLATED_NOTIFY
            ],
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            state->event_mask
        ) != MICROS_IPC_ERROR_UNAUTHORIZED
        || !ipc_notify_test_state_is_unchanged(
            fixture,
            &registry_snapshot,
            &objects_snapshot
        )
    ) {
        return notify_model_fail(
            fixture,
            state->step,
            state->operation,
            "target denial changed state"
        );
    }
    ++state->coverage.target_rejected;
    return true;
}

static bool apply_notify_model_operation(
    struct ipc_notify_test_context *fixture,
    struct notify_model_step_state *state
)
{
    switch (state->operation) {
    case NOTIFY_MODEL_NOTIFY:
        return model_notify_and_coalesce(fixture, state);
    case NOTIFY_MODEL_KERNEL_NOTIFY:
        return model_inject_kernel_event(fixture, state);
    case NOTIFY_MODEL_RECEIVE_ANY:
    case NOTIFY_MODEL_RECEIVE_SPECIFIC:
        return model_receive_selected_event(fixture, state);
    case NOTIFY_MODEL_ZERO_REJECTED:
        return model_reject_zero_event(fixture, state);
    case NOTIFY_MODEL_OPERATION_REJECTED:
        return model_reject_notification_operation(fixture, state);
    case NOTIFY_MODEL_TARGET_REJECTED:
        return model_reject_notification_target(fixture, state);
    default:
        return notify_model_fail(fixture, state->step, state->operation, "unknown operation");
    }
}

static bool compare_notify_model_state(
    struct ipc_notify_test_context *fixture,
    struct notify_model_step_state *state
)
{
    if (
        !notify_model_pending_matches(
            fixture,
            state->expected,
            state->expected_kernel
        )
        || micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) != MICROS_ENDPOINT_OK
    ) {
        return notify_model_fail(
            fixture,
            state->step,
            state->operation,
            "state or validator diverged"
        );
    }
    return true;
}

static bool check_notify_model_coverage(const struct notify_model_step_state *state)
{
    EXPECT_TRUE(
        state->coverage.notified != 0
        && state->coverage.coalesced != 0
        && state->coverage.kernel_notified != 0
        && state->coverage.kernel_coalesced != 0
        && state->coverage.kernel_received_any != 0
        && state->coverage.received_any != 0
        && state->coverage.received_specific != 0
        && state->coverage.zero_rejected != 0
        && state->coverage.operation_rejected != 0
        && state->coverage.target_rejected != 0
    );
    return true;
}

bool test_seeded_notify_model(struct ipc_notify_test_context *fixture)
{
    const uint32_t seed = UINT32_C(0x30c0a1e5);
    struct notify_model_step_state state;

    state.expected[0] = 0;
    state.expected[1] = 0;
    state.expected_kernel = 0;
    state.random_state = seed;
    EXPECT_TRUE(ipc_notify_test_setup_notify_fixture(fixture));
    memset(&state.coverage, 0, sizeof(state.coverage));
    memset(fixture->notify_model_trace, 0, sizeof(fixture->notify_model_trace));
    fixture->notify_model_trace_count = 0;
    for (state.step = 0; state.step < NOTIFY_MODEL_STEPS; ++state.step) {
        select_notify_model_operation(&state);
        notify_model_record(fixture, state.step, state.operation, state.source, state.event_mask);
        if (
            !apply_notify_model_operation(fixture, &state)
            || !compare_notify_model_state(fixture, &state)
        ) {
            return false;
        }
    }
    return check_notify_model_coverage(&state);
}
