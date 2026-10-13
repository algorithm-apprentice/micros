#include "ipc_model_test_internal.h"

struct acceptance_prelude_state {
    uint64_t token_one;
    uint64_t token_two;
    uint64_t token_three;
    uint64_t token_four;
    uint64_t token_five;
    micros_endpoint_t old_endpoint;
    struct model_action malformed;
};

#define RUN(action, expected) \
    do { \
        if (!ipc_model_run_checked_action(fixture,  \
            model, \
            (action), \
            (expected), \
            step, \
            seed, \
            coverage \
        )) { \
            return false; \
        } \
    } while (false)

static bool prelude_immediate_kernel_and_blocked_delivery(
    struct ipc_model_test_context *fixture,
    struct model_state *model,
    size_t *step,
    uint32_t seed,
    struct model_coverage *coverage
)
{
    RUN(
        ipc_model_receive_action(
            2,
            MICROS_ENDPOINT_ANY,
            *step
        ),
        MICROS_IPC_OK
    );
    RUN(
        ipc_model_send_action(
            fixture,
            0,
            model->processes[1].primary_endpoint,
            *step,
            false
        ),
        MICROS_IPC_OK
    );
    RUN(ipc_model_thread_action(MODEL_OPERATION_DRAIN, 2), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 0), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 2), MICROS_IPC_OK);

    RUN(
        ipc_model_kernel_notify_action(
            model->processes[1].primary_endpoint,
            UINT64_C(0x01)
        ),
        MICROS_IPC_OK
    );
    RUN(
        ipc_model_kernel_notify_action(
            model->processes[1].primary_endpoint,
            UINT64_C(0x04)
        ),
        MICROS_IPC_OK
    );
    RUN(
        ipc_model_receive_action(2, MICROS_ENDPOINT_ANY, *step),
        MICROS_IPC_OK
    );
    RUN(ipc_model_thread_action(MODEL_OPERATION_DRAIN, 2), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 2), MICROS_IPC_OK);

    RUN(
        ipc_model_send_action(
            fixture,
            6,
            model->processes[4].primary_endpoint,
            *step,
            false
        ),
        MICROS_IPC_OK
    );
    RUN(
        ipc_model_receive_action(7, MICROS_ENDPOINT_ANY, *step),
        MICROS_IPC_OK
    );
    RUN(ipc_model_thread_action(MODEL_OPERATION_DRAIN, 7), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 7), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_DRAIN, 6), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 6), MICROS_IPC_OK);

    return true;
}

static bool prelude_concurrent_calls_and_request_return_rights(
    struct ipc_model_test_context *fixture,
    struct model_state *model,
    size_t *step,
    uint32_t seed,
    struct model_coverage *coverage,
    struct acceptance_prelude_state *state
)
{
    RUN(
        ipc_model_receive_action(2, MICROS_ENDPOINT_ANY, *step),
        MICROS_IPC_OK
    );
    RUN(
        ipc_model_call_action(
            fixture,
            0,
            model->processes[1].primary_endpoint,
            *step
        ),
        MICROS_IPC_OK
    );
    state->token_one = model->threads[0].ipc_reply_token;
    RUN(
        ipc_model_reply_action(fixture, 3, state->token_one, *step),
        MICROS_IPC_ERROR_REPLY_TOKEN
    );
    RUN(
        ipc_model_reply_receive_action(
            fixture,
            3,
            state->token_one,
            MICROS_ENDPOINT_ANY,
            *step
        ),
        MICROS_IPC_ERROR_REPLY_TOKEN
    );
    RUN(
        ipc_model_receive_action(3, MICROS_ENDPOINT_ANY, *step),
        MICROS_IPC_OK
    );
    RUN(
        ipc_model_call_action(
            fixture,
            1,
            model->processes[1].primary_endpoint,
            *step
        ),
        MICROS_IPC_OK
    );
    state->token_two = model->threads[1].ipc_reply_token;
    RUN(ipc_model_thread_action(MODEL_OPERATION_DRAIN, 2), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 2), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_DRAIN, 3), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 3), MICROS_IPC_OK);
    RUN(ipc_model_reply_action(fixture, 2, state->token_two, *step), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_DRAIN, 1), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 1), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 2), MICROS_IPC_OK);
    RUN(ipc_model_reply_action(fixture, 3, state->token_one, *step), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_DRAIN, 0), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 0), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 3), MICROS_IPC_OK);
    RUN(
        ipc_model_reply_action(fixture, 3, state->token_one, *step),
        MICROS_IPC_ERROR_REPLY_TOKEN
    );

    return true;
}

static bool prelude_reply_receive_then_later_sender(
    struct ipc_model_test_context *fixture,
    struct model_state *model,
    size_t *step,
    uint32_t seed,
    struct model_coverage *coverage,
    struct acceptance_prelude_state *state
)
{
    RUN(
        ipc_model_receive_action(2, MICROS_ENDPOINT_ANY, *step),
        MICROS_IPC_OK
    );
    RUN(
        ipc_model_call_action(
            fixture,
            0,
            model->processes[1].primary_endpoint,
            *step
        ),
        MICROS_IPC_OK
    );
    state->token_three = model->threads[0].ipc_reply_token;
    RUN(ipc_model_thread_action(MODEL_OPERATION_DRAIN, 2), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 2), MICROS_IPC_OK);
    RUN(
        ipc_model_reply_receive_action(
            fixture,
            2,
            state->token_three,
            MICROS_ENDPOINT_ANY,
            *step
        ),
        MICROS_IPC_OK
    );
    RUN(
        ipc_model_send_action(
            fixture,
            4,
            model->processes[1].primary_endpoint,
            *step,
            false
        ),
        MICROS_IPC_OK
    );
    RUN(ipc_model_thread_action(MODEL_OPERATION_DRAIN, 0), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 0), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_DRAIN, 2), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 2), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 4), MICROS_IPC_OK);

    return true;
}

static bool prelude_notifications_excluded_from_reply_wait(
    struct ipc_model_test_context *fixture,
    struct model_state *model,
    size_t *step,
    uint32_t seed,
    struct model_coverage *coverage,
    struct acceptance_prelude_state *state
)
{
    RUN(
        ipc_model_receive_action(2, MICROS_ENDPOINT_ANY, *step),
        MICROS_IPC_OK
    );
    RUN(
        ipc_model_call_action(
            fixture,
            0,
            model->processes[1].primary_endpoint,
            *step
        ),
        MICROS_IPC_OK
    );
    state->token_four = model->threads[0].ipc_reply_token;
    RUN(ipc_model_thread_action(MODEL_OPERATION_DRAIN, 2), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 2), MICROS_IPC_OK);
    RUN(
        ipc_model_notify_action(
            4,
            model->processes[0].primary_endpoint,
            UINT64_C(0x01)
        ),
        MICROS_IPC_OK
    );
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 4), MICROS_IPC_OK);
    RUN(
        ipc_model_notify_action(
            4,
            model->processes[0].primary_endpoint,
            UINT64_C(0x04)
        ),
        MICROS_IPC_OK
    );
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 4), MICROS_IPC_OK);
    RUN(ipc_model_reply_action(fixture, 2, state->token_four, *step), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_DRAIN, 0), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 0), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 2), MICROS_IPC_OK);
    RUN(
        ipc_model_receive_action(0, MICROS_ENDPOINT_ANY, *step),
        MICROS_IPC_OK
    );
    RUN(ipc_model_thread_action(MODEL_OPERATION_DRAIN, 0), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 0), MICROS_IPC_OK);

    return true;
}

static bool prelude_deadlock_and_matching_recovery(
    struct ipc_model_test_context *fixture,
    struct model_state *model,
    size_t *step,
    uint32_t seed,
    struct model_coverage *coverage
)
{
    RUN(
        ipc_model_send_action(
            fixture,
            6,
            model->processes[4].primary_endpoint,
            *step,
            false
        ),
        MICROS_IPC_OK
    );
    RUN(
        ipc_model_send_action(
            fixture,
            7,
            model->processes[3].primary_endpoint,
            *step,
            false
        ),
        MICROS_IPC_ERROR_DEADLOCK
    );
    RUN(
        ipc_model_receive_action(7, MICROS_ENDPOINT_ANY, *step),
        MICROS_IPC_OK
    );
    RUN(ipc_model_thread_action(MODEL_OPERATION_DRAIN, 7), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 7), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_DRAIN, 6), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 6), MICROS_IPC_OK);

    return true;
}

static bool prelude_authorization_and_malformed_requests(
    struct ipc_model_test_context *fixture,
    struct model_state *model,
    size_t *step,
    uint32_t seed,
    struct model_coverage *coverage,
    struct acceptance_prelude_state *state
)
{
    RUN(
        ipc_model_send_action(
            fixture,
            9,
            model->processes[0].primary_endpoint,
            *step,
            false
        ),
        MICROS_IPC_ERROR_UNAUTHORIZED
    );
    RUN(
        ipc_model_notify_action(
            0,
            model->processes[1].primary_endpoint,
            0
        ),
        MICROS_IPC_ERROR_ARGUMENT
    );
    RUN(
        ipc_model_send_action(
            fixture,
            0,
            model->processes[1].primary_endpoint,
            *step,
            true
        ),
        MICROS_IPC_ERROR_ARGUMENT
    );
    state->malformed = ipc_model_receive_action(0, MICROS_ENDPOINT_ANY, *step);
    state->malformed.buffer |= 1;
    RUN(state->malformed, MICROS_IPC_ERROR_ARGUMENT);

    return true;
}

static bool prelude_close_reuse_and_stale_endpoint(
    struct ipc_model_test_context *fixture,
    struct model_state *model,
    size_t *step,
    uint32_t seed,
    struct model_coverage *coverage,
    struct acceptance_prelude_state *state
)
{
    RUN(
        ipc_model_call_action(
            fixture,
            6,
            model->processes[5].primary_endpoint,
            *step
        ),
        MICROS_IPC_OK
    );
    state->token_five = model->threads[6].ipc_reply_token;
    (void)state->token_five;
    RUN(
        ipc_model_receive_action(
            7,
            model->processes[5].primary_endpoint,
            *step
        ),
        MICROS_IPC_OK
    );
    RUN(
        ipc_model_notify_action(
            8,
            model->processes[0].primary_endpoint,
            UINT64_C(0x10)
        ),
        MICROS_IPC_OK
    );
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 8), MICROS_IPC_OK);
    state->old_endpoint = model->processes[5].primary_endpoint;
    RUN(ipc_model_close_reuse_action(model, 5), MICROS_IPC_OK);
    RUN(
        ipc_model_send_action(fixture, 0, state->old_endpoint, *step, false),
        MICROS_IPC_ERROR_DEAD_ENDPOINT
    );
    RUN(ipc_model_thread_action(MODEL_OPERATION_DRAIN, 6), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 6), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_DRAIN, 7), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 7), MICROS_IPC_OK);

    return true;
}

static bool prelude_reply_receive_immediate_queued_sender(
    struct ipc_model_test_context *fixture,
    struct model_state *model,
    size_t *step,
    uint32_t seed,
    struct model_coverage *coverage,
    struct acceptance_prelude_state *state
)
{
    RUN(
        ipc_model_receive_action(7, MICROS_ENDPOINT_ANY, *step),
        MICROS_IPC_OK
    );
    RUN(
        ipc_model_call_action(
            fixture,
            6,
            model->processes[4].primary_endpoint,
            *step
        ),
        MICROS_IPC_OK
    );
    state->token_five = model->threads[6].ipc_reply_token;
    RUN(ipc_model_thread_action(MODEL_OPERATION_DRAIN, 7), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 7), MICROS_IPC_OK);
    RUN(
        ipc_model_send_action(
            fixture,
            8,
            model->processes[4].primary_endpoint,
            *step,
            false
        ),
        MICROS_IPC_OK
    );
    RUN(
        ipc_model_reply_receive_action(
            fixture,
            7,
            state->token_five,
            MICROS_ENDPOINT_ANY,
            *step
        ),
        MICROS_IPC_OK
    );
    RUN(ipc_model_thread_action(MODEL_OPERATION_DRAIN, 6), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 6), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_DRAIN, 7), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 7), MICROS_IPC_OK);
    RUN(ipc_model_thread_action(MODEL_OPERATION_HOLD, 8), MICROS_IPC_OK);

    return true;
}

#undef RUN

bool ipc_model_run_model_prelude(
    struct ipc_model_test_context *fixture,
    struct model_state *model,
    size_t *step,
    uint32_t seed,
    struct model_coverage *coverage
)
{
    struct acceptance_prelude_state state;

    return (
        prelude_immediate_kernel_and_blocked_delivery(fixture, model, step, seed, coverage)
        && prelude_concurrent_calls_and_request_return_rights(fixture, model, step, seed, coverage, &state)
        && prelude_reply_receive_then_later_sender(fixture, model, step, seed, coverage, &state)
        && prelude_notifications_excluded_from_reply_wait(fixture, model, step, seed, coverage, &state)
        && prelude_deadlock_and_matching_recovery(fixture, model, step, seed, coverage)
        && prelude_authorization_and_malformed_requests(fixture, model, step, seed, coverage, &state)
        && prelude_close_reuse_and_stale_endpoint(fixture, model, step, seed, coverage, &state)
        && prelude_reply_receive_immediate_queued_sender(fixture, model, step, seed, coverage, &state)
    );
}
