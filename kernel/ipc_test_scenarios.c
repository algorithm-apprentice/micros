#include "ipc_test_internal.h"

bool ipc_test_immediate_send(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
)
{
    ipc_test_fill_message(&fixture->send_messages[0], UINT32_C(0x1001), UINT8_C(0x10));
    if (
        micros_ipc_receive(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_SERVER],
            state->endpoints[IPC_TEST_CLIENT],
            (uintptr_t)&fixture->receive_buffers[IPC_TEST_SERVER]
        ) != MICROS_IPC_OK
        || micros_ipc_send(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_CLIENT],
            state->endpoints[IPC_TEST_SERVER],
            &fixture->send_messages[0]
        ) != MICROS_IPC_OK
    ) {
        return false;
    }
    state->expected = ipc_test_canonical_message(
        &fixture->send_messages[0],
        state->endpoints[IPC_TEST_CLIENT],
        0
    );
    {
        const struct micros_thread_handle ready[] = {
            state->threads[IPC_TEST_CLIENT],
            state->threads[IPC_TEST_SERVER],
        };

        if (
            !ipc_test_ready_queue_matches(
                state->objects,
                state->hart,
                ready,
                sizeof(ready) / sizeof(ready[0])
            )
            || !ipc_test_consume_staged(
                state->objects,
                state->threads[IPC_TEST_SERVER],
                MICROS_IPC_OK,
                &state->expected
            )
            || !ipc_test_hold_thread(state->objects, state->threads[IPC_TEST_CLIENT])
            || !ipc_test_hold_thread(state->objects, state->threads[IPC_TEST_SERVER])
        ) {
            return false;
        }
    }
    state->failure_stage = 3;

    return true;
}

bool ipc_test_blocked_send(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
)
{
    ipc_test_fill_message(&fixture->send_messages[1], UINT32_C(0x1002), UINT8_C(0x20));
    if (
        micros_ipc_send(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_PEER],
            state->endpoints[IPC_TEST_SERVER],
            &fixture->send_messages[1]
        ) != MICROS_IPC_OK
        || state->objects->threads[state->threads[IPC_TEST_PEER].slot].runtime_flags
            != MICROS_THREAD_RTS_IPC_SEND
        || !ipc_test_thread_handles_equal(
            fixture->registry->endpoints[state->processes[IPC_TEST_SERVER].slot]
                .sender_head,
            state->threads[IPC_TEST_PEER]
        )
        || micros_ipc_receive(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_SERVER],
            MICROS_ENDPOINT_ANY,
            (uintptr_t)&fixture->receive_buffers[IPC_TEST_SERVER]
        ) != MICROS_IPC_OK
    ) {
        return false;
    }
    state->expected = ipc_test_canonical_message(
        &fixture->send_messages[1],
        state->endpoints[IPC_TEST_PEER],
        0
    );
    {
        const struct micros_thread_handle ready[] = {
            state->threads[IPC_TEST_SERVER],
            state->threads[IPC_TEST_PEER],
        };

        if (
            !ipc_test_ready_queue_matches(
                state->objects,
                state->hart,
                ready,
                sizeof(ready) / sizeof(ready[0])
            )
            || !ipc_test_consume_staged(
                state->objects,
                state->threads[IPC_TEST_SERVER],
                MICROS_IPC_OK,
                &state->expected
            )
            || !ipc_test_consume_staged(
                state->objects,
                state->threads[IPC_TEST_PEER],
                MICROS_IPC_OK,
                NULL
            )
            || !ipc_test_hold_thread(state->objects, state->threads[IPC_TEST_SERVER])
            || !ipc_test_hold_thread(state->objects, state->threads[IPC_TEST_PEER])
        ) {
            return false;
        }
    }
    state->failure_stage = 4;

    return true;
}

bool ipc_test_call_reply_and_stale_token(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
)
{
    ipc_test_fill_message(&fixture->send_messages[2], UINT32_C(0x2001), UINT8_C(0x30));
    if (
        micros_ipc_receive(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_SERVER],
            MICROS_ENDPOINT_ANY,
            (uintptr_t)&fixture->receive_buffers[IPC_TEST_SERVER]
        ) != MICROS_IPC_OK
        || micros_ipc_call(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_CLIENT],
            state->endpoints[IPC_TEST_SERVER],
            &fixture->send_messages[2],
            (uintptr_t)&fixture->send_messages[2]
) != MICROS_IPC_OK
    ) {
        return false;
    }
    state->request = &state->objects->threads[
        state->threads[IPC_TEST_SERVER].slot
    ].ipc_inbound_message;
    state->reply_token = state->request->reply_token;
    state->expected = ipc_test_canonical_message(
        &fixture->send_messages[2],
        state->endpoints[IPC_TEST_CLIENT],
        state->reply_token
    );
    if (
        state->reply_token == 0
        || !ipc_test_bytes_equal(state->request, &state->expected, sizeof(state->expected))
        || state->objects->threads[
            state->threads[IPC_TEST_CLIENT].slot
        ].runtime_flags != MICROS_THREAD_RTS_IPC_REPLY
        || !ipc_test_consume_staged(
            state->objects,
            state->threads[IPC_TEST_SERVER],
            MICROS_IPC_OK,
            &state->expected
        )
        || !ipc_test_hold_thread(state->objects, state->threads[IPC_TEST_SERVER])
    ) {
        return false;
    }
    ipc_test_fill_message(&state->reply, UINT32_C(0x2002), UINT8_C(0x40));
    if (
        micros_ipc_reply(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_SERVER],
            state->reply_token,
            &state->reply
        ) != MICROS_IPC_OK
    ) {
        return false;
    }
    state->expected = ipc_test_canonical_message(
        &state->reply,
        state->endpoints[IPC_TEST_SERVER],
        0
    );
    if (
        !ipc_test_consume_staged(
            state->objects,
            state->threads[IPC_TEST_CLIENT],
            MICROS_IPC_OK,
            &state->expected
        )
        || !ipc_test_hold_thread(state->objects, state->threads[IPC_TEST_SERVER])
        || !ipc_test_hold_thread(state->objects, state->threads[IPC_TEST_CLIENT])
    ) {
        return false;
    }
    ipc_test_snapshot_state(fixture, state->objects);
    if (
        micros_ipc_reply(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_SERVER],
            state->reply_token,
            &state->reply
        ) != MICROS_IPC_ERROR_REPLY_TOKEN
        || !ipc_test_state_matches_snapshot(fixture, state->objects)
    ) {
        return false;
    }
    state->failure_stage = 5;

    return true;
}

bool ipc_test_prepare_combined_reply(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
)
{
    ipc_test_fill_message(&fixture->send_messages[3], UINT32_C(0x3001), UINT8_C(0x50));
    if (
        micros_ipc_receive(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_SERVER],
            MICROS_ENDPOINT_ANY,
            (uintptr_t)&fixture->receive_buffers[IPC_TEST_SERVER]
        ) != MICROS_IPC_OK
        || micros_ipc_call(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_CLIENT],
            state->endpoints[IPC_TEST_SERVER],
            &fixture->send_messages[3],
            (uintptr_t)&fixture->send_messages[3]
) != MICROS_IPC_OK
    ) {
        return false;
    }
    state->request = &state->objects->threads[
        state->threads[IPC_TEST_SERVER].slot
    ].ipc_inbound_message;
    state->reply_token = state->request->reply_token;
    state->expected = ipc_test_canonical_message(
        &fixture->send_messages[3],
        state->endpoints[IPC_TEST_CLIENT],
        state->reply_token
    );
    if (
        !ipc_test_consume_staged(
            state->objects,
            state->threads[IPC_TEST_SERVER],
            MICROS_IPC_OK,
            &state->expected
        )
        || !ipc_test_hold_thread(state->objects, state->threads[IPC_TEST_SERVER])
    ) {
        return false;
    }
    state->failure_stage = 6;
    return true;
}

bool ipc_test_reply_receive_and_peer_send(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
)
{
    ipc_test_fill_message(&state->reply, UINT32_C(0x3002), UINT8_C(0x60));
    if (
        micros_ipc_reply_receive(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_SERVER],
            state->reply_token,
            &state->reply,
            MICROS_ENDPOINT_ANY,
            (uintptr_t)&fixture->receive_buffers[IPC_TEST_SERVER]
        ) != MICROS_IPC_OK
        || state->objects->threads[
            state->threads[IPC_TEST_SERVER].slot
        ].runtime_flags != MICROS_THREAD_RTS_IPC_RECEIVE
        || !ipc_test_thread_handles_equal(
            fixture->registry->endpoints[state->processes[IPC_TEST_SERVER].slot]
                .receiver_head,
            state->threads[IPC_TEST_SERVER]
        )
    ) {
        return false;
    }
    ipc_test_fill_message(&fixture->send_messages[4], UINT32_C(0x3003), UINT8_C(0x70));
    if (
        micros_ipc_send(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_PEER],
            state->endpoints[IPC_TEST_SERVER],
            &fixture->send_messages[4]
        ) != MICROS_IPC_OK
    ) {
        return false;
    }
    state->expected = ipc_test_canonical_message(
        &state->reply,
        state->endpoints[IPC_TEST_SERVER],
        0
    );
    if (
        !ipc_test_consume_staged(
            state->objects,
            state->threads[IPC_TEST_CLIENT],
            MICROS_IPC_OK,
            &state->expected
        )
        || !ipc_test_hold_thread(state->objects, state->threads[IPC_TEST_CLIENT])
    ) {
        return false;
    }
    state->failure_stage = 7;
    return true;
}

bool ipc_test_finish_combined_delivery(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
)
{
    state->expected = ipc_test_canonical_message(
        &fixture->send_messages[4],
        state->endpoints[IPC_TEST_PEER],
        0
    );
    if (
        !ipc_test_consume_staged(
            state->objects,
            state->threads[IPC_TEST_SERVER],
            MICROS_IPC_OK,
            &state->expected
        )
        || !ipc_test_hold_thread(state->objects, state->threads[IPC_TEST_PEER])
        || !ipc_test_hold_thread(state->objects, state->threads[IPC_TEST_SERVER])
    ) {
        return false;
    }

    return true;
}

static bool prepare_call_and_coalesce_peer_notifications(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
)
{
    ipc_test_fill_message(&fixture->send_messages[5], UINT32_C(0x4001), UINT8_C(0x80));
    if (
        micros_ipc_receive(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_SERVER],
            MICROS_ENDPOINT_ANY,
            (uintptr_t)&fixture->receive_buffers[IPC_TEST_SERVER]
        ) != MICROS_IPC_OK
        || micros_ipc_call(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_CLIENT],
            state->endpoints[IPC_TEST_SERVER],
            &fixture->send_messages[5],
            (uintptr_t)&fixture->send_messages[5]
) != MICROS_IPC_OK
    ) {
        return false;
    }
    state->request = &state->objects->threads[
        state->threads[IPC_TEST_SERVER].slot
    ].ipc_inbound_message;
    state->reply_token = state->request->reply_token;
    state->expected = ipc_test_canonical_message(
        &fixture->send_messages[5],
        state->endpoints[IPC_TEST_CLIENT],
        state->reply_token
    );
    if (
        !ipc_test_consume_staged(
            state->objects,
            state->threads[IPC_TEST_SERVER],
            MICROS_IPC_OK,
            &state->expected
        )
        || !ipc_test_hold_thread(state->objects, state->threads[IPC_TEST_SERVER])
        || micros_ipc_notify(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_PEER],
            state->endpoints[IPC_TEST_CLIENT],
            UINT64_C(0x01)
        ) != MICROS_IPC_OK
        || !ipc_test_hold_thread(state->objects, state->threads[IPC_TEST_PEER])
        || micros_ipc_notify(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_PEER],
            state->endpoints[IPC_TEST_CLIENT],
            UINT64_C(0x04)
        ) != MICROS_IPC_OK
        || !ipc_test_hold_thread(state->objects, state->threads[IPC_TEST_PEER])
        || state->objects->threads[
            state->threads[IPC_TEST_CLIENT].slot
        ].runtime_flags != MICROS_THREAD_RTS_IPC_REPLY
        || state->objects->threads[
            state->threads[IPC_TEST_CLIENT].slot
        ].ipc_delivery_pending
        || fixture->registry->endpoints[state->processes[IPC_TEST_CLIENT].slot]
            .pending_events[state->processes[IPC_TEST_PEER].slot]
            != UINT64_C(0x05)
    ) {
        return false;
    }
    return true;
}

static bool complete_reply_then_receive_pending_notification(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
)
{
    ipc_test_fill_message(&state->reply, UINT32_C(0x4002), UINT8_C(0x90));
    if (
        micros_ipc_reply(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_SERVER],
            state->reply_token,
            &state->reply
        ) != MICROS_IPC_OK
    ) {
        return false;
    }
    state->expected = ipc_test_canonical_message(
        &state->reply,
        state->endpoints[IPC_TEST_SERVER],
        0
    );
    if (
        !ipc_test_consume_staged(
            state->objects,
            state->threads[IPC_TEST_CLIENT],
            MICROS_IPC_OK,
            &state->expected
        )
        || !ipc_test_hold_thread(state->objects, state->threads[IPC_TEST_SERVER])
        || !ipc_test_hold_thread(state->objects, state->threads[IPC_TEST_CLIENT])
        || micros_ipc_receive(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_CLIENT],
            MICROS_ENDPOINT_ANY,
            (uintptr_t)&fixture->receive_buffers[IPC_TEST_CLIENT]
        ) != MICROS_IPC_OK
    ) {
        return false;
    }
    if (
        !ipc_test_notification_matches(
            &state->objects->threads[
                state->threads[IPC_TEST_CLIENT].slot
            ].ipc_inbound_message,
            state->endpoints[IPC_TEST_PEER],
            UINT64_C(0x05)
        )
        || !ipc_test_consume_staged(
            state->objects,
            state->threads[IPC_TEST_CLIENT],
            MICROS_IPC_OK,
            &state->objects->threads[
                state->threads[IPC_TEST_CLIENT].slot
            ].ipc_inbound_message
        )
        || !ipc_test_hold_thread(state->objects, state->threads[IPC_TEST_CLIENT])
    ) {
        return false;
    }
    state->failure_stage = 8;

    return true;
}

bool ipc_test_notification_reply_exclusion(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
)
{
    return (
        prepare_call_and_coalesce_peer_notifications(fixture, state)
        && complete_reply_then_receive_pending_notification(fixture, state)
    );
}

bool ipc_test_kernel_notifications(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
)
{
    if (
        micros_ipc_inject_kernel_notification(
            fixture->registry,
            state->objects,
            state->endpoints[IPC_TEST_CLIENT],
            UINT64_C(0x10)
        ) != MICROS_IPC_OK
        || micros_ipc_inject_kernel_notification(
            fixture->registry,
            state->objects,
            state->endpoints[IPC_TEST_CLIENT],
            UINT64_C(0x40)
        ) != MICROS_IPC_OK
        || fixture->registry->endpoints[state->processes[IPC_TEST_CLIENT].slot]
            .pending_kernel_events != UINT64_C(0x50)
        || micros_ipc_receive(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_CLIENT],
            MICROS_ENDPOINT_ANY,
            (uintptr_t)&fixture->receive_buffers[IPC_TEST_CLIENT]
        ) != MICROS_IPC_OK
        || !ipc_test_notification_matches(
            &state->objects->threads[
                state->threads[IPC_TEST_CLIENT].slot
            ].ipc_inbound_message,
            MICROS_ENDPOINT_NONE,
            UINT64_C(0x50)
        )
        || !ipc_test_consume_staged(
            state->objects,
            state->threads[IPC_TEST_CLIENT],
            MICROS_IPC_OK,
            &state->objects->threads[
                state->threads[IPC_TEST_CLIENT].slot
            ].ipc_inbound_message
        )
        || !ipc_test_hold_thread(state->objects, state->threads[IPC_TEST_CLIENT])
        || micros_ipc_receive(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_CLIENT],
            MICROS_ENDPOINT_ANY,
            (uintptr_t)&fixture->receive_buffers[IPC_TEST_CLIENT]
        ) != MICROS_IPC_OK
        || micros_ipc_inject_kernel_notification(
            fixture->registry,
            state->objects,
            state->endpoints[IPC_TEST_CLIENT],
            UINT64_C(0x80)
        ) != MICROS_IPC_OK
        || fixture->registry->endpoints[state->processes[IPC_TEST_CLIENT].slot]
            .pending_kernel_events != 0
        || !ipc_test_notification_matches(
            &state->objects->threads[
                state->threads[IPC_TEST_CLIENT].slot
            ].ipc_inbound_message,
            MICROS_ENDPOINT_NONE,
            UINT64_C(0x80)
        )
        || !ipc_test_consume_staged(
            state->objects,
            state->threads[IPC_TEST_CLIENT],
            MICROS_IPC_OK,
            &state->objects->threads[
                state->threads[IPC_TEST_CLIENT].slot
            ].ipc_inbound_message
        )
        || !ipc_test_hold_thread(state->objects, state->threads[IPC_TEST_CLIENT])
    ) {
        state->failure_stage = UINT64_C(0x801);
        return false;
    }

    return true;
}

bool ipc_test_deadlock_and_matching_recovery(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
)
{
    ipc_test_fill_message(&fixture->send_messages[6], UINT32_C(0x5001), UINT8_C(0xa0));
    if (
        micros_ipc_send(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_CLIENT],
            state->endpoints[IPC_TEST_SERVER],
            &fixture->send_messages[6]
        ) != MICROS_IPC_OK
    ) {
        return false;
    }
    ipc_test_snapshot_state(fixture, state->objects);
    ipc_test_fill_message(&fixture->send_messages[7], UINT32_C(0x5002), UINT8_C(0xb0));
    if (
        micros_ipc_send(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_SERVER],
            state->endpoints[IPC_TEST_CLIENT],
            &fixture->send_messages[7]
        ) != MICROS_IPC_ERROR_DEADLOCK
        || !ipc_test_state_matches_snapshot(fixture, state->objects)
        || micros_ipc_receive(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_SERVER],
            state->endpoints[IPC_TEST_CLIENT],
            (uintptr_t)&fixture->receive_buffers[IPC_TEST_SERVER]
        ) != MICROS_IPC_OK
    ) {
        return false;
    }
    state->expected = ipc_test_canonical_message(
        &fixture->send_messages[6],
        state->endpoints[IPC_TEST_CLIENT],
        0
    );
    if (
        !ipc_test_consume_staged(
            state->objects,
            state->threads[IPC_TEST_SERVER],
            MICROS_IPC_OK,
            &state->expected
        )
        || !ipc_test_consume_staged(
            state->objects,
            state->threads[IPC_TEST_CLIENT],
            MICROS_IPC_OK,
            NULL
        )
        || !ipc_test_hold_thread(state->objects, state->threads[IPC_TEST_SERVER])
        || !ipc_test_hold_thread(state->objects, state->threads[IPC_TEST_CLIENT])
    ) {
        return false;
    }

    return true;
}

bool ipc_test_close_cancellation(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
)
{
    ipc_test_fill_message(&fixture->send_messages[0], UINT32_C(0x6001), UINT8_C(0xc0));
    if (
        micros_ipc_call(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_CLIENT],
            state->endpoints[IPC_TEST_SERVER],
            &fixture->send_messages[0],
            (uintptr_t)&fixture->send_messages[0]
) != MICROS_IPC_OK
    ) {
        return false;
    }
    state->close_token = state->objects->threads[
        state->threads[IPC_TEST_CLIENT].slot
    ].ipc_reply_token;
    if (
        state->close_token == 0
        || micros_ipc_receive(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_PEER],
            state->endpoints[IPC_TEST_SERVER],
            (uintptr_t)&fixture->receive_buffers[IPC_TEST_PEER]
        ) != MICROS_IPC_OK
        || micros_ipc_notify(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_SERVER],
            state->endpoints[IPC_TEST_CLIENT],
            UINT64_C(0x20)
        ) != MICROS_IPC_OK
        || !ipc_test_hold_thread(state->objects, state->threads[IPC_TEST_SERVER])
        || micros_ipc_endpoint_close(
            fixture->registry,
            state->objects,
            state->endpoints[IPC_TEST_SERVER]
        ) != MICROS_IPC_OK
        || state->objects->threads[
            state->threads[IPC_TEST_CLIENT].slot
        ].ipc_reply_token != 0
        || fixture->registry->endpoints[state->processes[IPC_TEST_CLIENT].slot]
            .pending_events[state->processes[IPC_TEST_SERVER].slot] != 0
        || !ipc_test_consume_staged(
            state->objects,
            state->threads[IPC_TEST_CLIENT],
            MICROS_IPC_ERROR_DEAD_ENDPOINT,
            NULL
        )
        || !ipc_test_consume_staged(
            state->objects,
            state->threads[IPC_TEST_PEER],
            MICROS_IPC_ERROR_DEAD_ENDPOINT,
            NULL
        )
        || !ipc_test_hold_thread(state->objects, state->threads[IPC_TEST_CLIENT])
        || !ipc_test_hold_thread(state->objects, state->threads[IPC_TEST_PEER])
    ) {
        return false;
    }
    state->failure_stage = 9;

    return true;
}

bool ipc_test_generation_reuse_and_stale_identity(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
)
{
    if (
        !ipc_test_execution_state_matches(fixture, state->objects, state->processes, state->threads)
        || !ipc_test_cleanup_thread(state->objects, state->threads[IPC_TEST_SERVER])
        || micros_user_address_space_destroy(
            state->processes[IPC_TEST_SERVER]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_process_release(
            state->objects,
            state->processes[IPC_TEST_SERVER]
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(state->objects, &state->replacement_process)
            != MICROS_KERNEL_OBJECT_OK
        || state->replacement_process.slot
            != state->processes[IPC_TEST_SERVER].slot
        || state->replacement_process.generation
            != state->processes[IPC_TEST_SERVER].generation + 1
        || micros_user_address_space_create(state->replacement_process)
            != MICROS_USER_ADDRESS_SPACE_OK
        || micros_endpoint_reserve(
            fixture->registry,
            state->objects,
            state->replacement_process,
            &state->replacement_endpoint
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_install_profile(
            fixture->registry,
            state->objects,
            state->replacement_process,
            IPC_TEST_SERVER_PROFILE
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_activate(
            fixture->registry,
            state->objects,
            state->replacement_endpoint
        ) != MICROS_ENDPOINT_OK
        || !ipc_test_prepare_thread(
            fixture,
            state->objects,
            state->hart,
            state->replacement_process,
            IPC_TEST_SERVER,
            &state->replacement_thread
        )
        || state->replacement_thread.slot != state->threads[IPC_TEST_SERVER].slot
        || state->replacement_thread.generation
            != state->threads[IPC_TEST_SERVER].generation + 1
    ) {
        return false;
    }
    state->threads[IPC_TEST_SERVER] = state->replacement_thread;
    state->processes[IPC_TEST_SERVER] = state->replacement_process;
    if (!ipc_test_snapshot_execution_state(fixture, state->objects, state->processes, state->threads)) {
        return false;
    }
    ipc_test_snapshot_state(fixture, state->objects);
    if (
        micros_ipc_send(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_CLIENT],
            state->endpoints[IPC_TEST_SERVER],
            &fixture->send_messages[0]
        ) != MICROS_IPC_ERROR_DEAD_ENDPOINT
        || !ipc_test_state_matches_snapshot(fixture, state->objects)
    ) {
        return false;
    }
    state->failure_stage = 10;
    state->endpoints[IPC_TEST_SERVER] = state->replacement_endpoint;
    ipc_test_snapshot_state(fixture, state->objects);
    if (
        micros_ipc_send(
            fixture->registry,
            state->objects,
            state->threads[IPC_TEST_CLIENT],
            state->endpoints[IPC_TEST_PEER],
            &fixture->send_messages[0]
        ) != MICROS_IPC_ERROR_UNAUTHORIZED
        || !ipc_test_state_matches_snapshot(fixture, state->objects)
    ) {
        return false;
    }

    return true;
}
