#include "ipc_call_test_internal.h"

bool test_call_delivers_to_waiting_receiver(struct ipc_call_test_context *fixture)
{
    struct micros_ipc_message request = ipc_call_test_message_pattern(
        UINT32_C(0xaaaaaaaa),
        UINT32_C(0x1234),
        UINT64_C(0xbbbbbbbbbbbbbbbb),
        UINT8_C(0x20)
    );
    struct micros_ipc_message request_snapshot = request;
    struct micros_ipc_message expected;
    const uintptr_t receive_buffer = UINT64_C(0x60000000);
    const struct micros_thread *caller;
    const struct micros_thread *receiver;
    const struct micros_endpoint_record *server;
    uint32_t saved_callee;

    EXPECT_TRUE(ipc_call_test_setup_call_fixture(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CALL_PROCESS_SERVER],
            fixture->endpoints[CALL_PROCESS_CLIENT],
            receive_buffer
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CALL_PROCESS_CLIENT],
            fixture->endpoints[CALL_PROCESS_SERVER],
            &request,
            (uintptr_t)&request
        )
    );
    expected = ipc_call_test_canonical_request(
        &request_snapshot,
        fixture->endpoints[CALL_PROCESS_CLIENT],
        1
    );
    caller = &fixture->objects.threads[
        fixture->primary_threads[CALL_PROCESS_CLIENT].slot
    ];
    receiver = &fixture->objects.threads[
        fixture->primary_threads[CALL_PROCESS_SERVER].slot
    ];
    server = &fixture->registry.endpoints[CALL_PROCESS_SERVER];

    EXPECT_TRUE(
        fixture->registry.last_reply_token == 1
        && caller->runtime_flags == MICROS_THREAD_RTS_IPC_REPLY
        && caller->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && ipc_call_test_thread_handle_is_zero(caller->ipc_next)
        && !caller->ready_linked
        && ipc_call_test_bytes_are_zero(
            &caller->ipc_outbound_message,
            sizeof(caller->ipc_outbound_message)
        )
        && caller->ipc_send_destination == 0
        && caller->ipc_receive_buffer == (uintptr_t)&request
        && caller->ipc_reply_token == 1
        && caller->ipc_reply_callee
            == fixture->endpoints[CALL_PROCESS_SERVER]
        && fixture->objects.threads[fixture->client_extra_thread.slot]
            .ipc_reply_token == 0
        && receiver->runtime_flags == 0
        && receiver->ready_linked
        && receiver->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && receiver->ipc_receive_source == 0
        && receiver->ipc_receive_buffer == receive_buffer
        && receiver->ipc_delivery_pending
        && receiver->ipc_staged_result == MICROS_IPC_OK
        && memcmp(
            &receiver->ipc_inbound_message,
            &expected,
            sizeof(expected)
        ) == 0
        && ipc_call_test_thread_handle_is_zero(server->receiver_head)
        && ipc_call_test_thread_handle_is_zero(server->receiver_tail)
        && memcmp(
            &request,
            &request_snapshot,
            sizeof(request)
        ) == 0
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );

    saved_callee = fixture->objects.threads[
        fixture->primary_threads[CALL_PROCESS_CLIENT].slot
    ].ipc_reply_callee;
    fixture->objects.threads[
        fixture->primary_threads[CALL_PROCESS_CLIENT].slot
    ].ipc_reply_callee +=
        UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS;
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_ERROR_INVARIANT
    );
    fixture->objects.threads[
        fixture->primary_threads[CALL_PROCESS_CLIENT].slot
    ].ipc_reply_callee = saved_callee;
    return true;
}

bool test_call_preserves_independent_receiver_flags(struct ipc_call_test_context *fixture)
{
    struct micros_ipc_message request = ipc_call_test_message_pattern(
        0,
        UINT32_C(0x1235),
        0,
        UINT8_C(0x21)
    );
    struct micros_ipc_message request_snapshot = request;
    struct micros_ipc_message expected;
    const uintptr_t receive_buffer = UINT64_C(0x60000800);
    const struct micros_thread *caller;
    struct micros_thread *receiver;

    EXPECT_TRUE(ipc_call_test_setup_call_fixture(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CALL_PROCESS_SERVER],
            fixture->endpoints[CALL_PROCESS_CLIENT],
            receive_buffer
        )
    );
    receiver = &fixture->objects.threads[
        fixture->primary_threads[CALL_PROCESS_SERVER].slot
    ];
    receiver->runtime_flags |= MICROS_THREAD_RTS_INACTIVE;
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CALL_PROCESS_CLIENT],
            fixture->endpoints[CALL_PROCESS_SERVER],
            &request,
            (uintptr_t)&request
        )
    );
    expected = ipc_call_test_canonical_request(
        &request_snapshot,
        fixture->endpoints[CALL_PROCESS_CLIENT],
        1
    );
    caller = &fixture->objects.threads[
        fixture->primary_threads[CALL_PROCESS_CLIENT].slot
    ];
    receiver = &fixture->objects.threads[
        fixture->primary_threads[CALL_PROCESS_SERVER].slot
    ];
    EXPECT_TRUE(
        fixture->registry.last_reply_token == 1
        && caller->runtime_flags == MICROS_THREAD_RTS_IPC_REPLY
        && caller->ipc_reply_token == 1
        && receiver->runtime_flags == MICROS_THREAD_RTS_INACTIVE
        && !receiver->ready_linked
        && receiver->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && ipc_call_test_thread_handle_is_zero(receiver->ipc_next)
        && receiver->ipc_receive_source == 0
        && receiver->ipc_receive_buffer == receive_buffer
        && receiver->ipc_delivery_pending
        && memcmp(
            &receiver->ipc_inbound_message,
            &expected,
            sizeof(expected)
        ) == 0
        && ipc_call_test_thread_handle_is_zero(
            fixture->registry.endpoints[CALL_PROCESS_SERVER].receiver_head
        )
        && ipc_call_test_thread_handle_is_zero(
            fixture->registry.endpoints[CALL_PROCESS_SERVER].receiver_tail
        )
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool queue_calls_and_check_bindings(
    struct ipc_call_test_context *fixture,
    const struct micros_ipc_message *first_request,
    const struct micros_ipc_message *second_request,
    const struct micros_ipc_message *first_snapshot,
    const struct micros_ipc_message *second_snapshot
)
{
    const struct micros_endpoint_record *server;
    const struct micros_thread *first_caller;
    const struct micros_thread *second_caller;

    EXPECT_TRUE(ipc_call_test_setup_call_fixture(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CALL_PROCESS_CLIENT],
            fixture->endpoints[CALL_PROCESS_SERVER],
            first_request,
            (uintptr_t)first_request
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &fixture->registry,
            &fixture->objects,
            fixture->client_extra_thread,
            fixture->endpoints[CALL_PROCESS_SERVER],
            second_request,
            (uintptr_t)second_request
        )
    );

    server = &fixture->registry.endpoints[CALL_PROCESS_SERVER];
    first_caller = &fixture->objects.threads[
        fixture->primary_threads[CALL_PROCESS_CLIENT].slot
    ];
    second_caller = &fixture->objects.threads[fixture->client_extra_thread.slot];
    EXPECT_TRUE(
        fixture->registry.last_reply_token == 2
        && ipc_call_test_thread_handles_equal(
            server->sender_head,
            fixture->primary_threads[CALL_PROCESS_CLIENT]
        )
        && ipc_call_test_thread_handles_equal(
            server->sender_tail,
            fixture->client_extra_thread
        )
        && ipc_call_test_thread_handles_equal(
            first_caller->ipc_next,
            fixture->client_extra_thread
        )
        && ipc_call_test_thread_handle_is_zero(second_caller->ipc_next)
        && first_caller->runtime_flags
            == (
                MICROS_THREAD_RTS_IPC_SEND
                | MICROS_THREAD_RTS_IPC_REPLY
            )
        && second_caller->runtime_flags
            == (
                MICROS_THREAD_RTS_IPC_SEND
                | MICROS_THREAD_RTS_IPC_REPLY
            )
        && first_caller->ipc_reply_token == 1
        && second_caller->ipc_reply_token == 2
        && first_caller->ipc_receive_buffer
            == (uintptr_t)first_request
        && second_caller->ipc_receive_buffer
            == (uintptr_t)second_request
        && first_caller->ipc_reply_callee
            == fixture->endpoints[CALL_PROCESS_SERVER]
        && second_caller->ipc_reply_callee
            == fixture->endpoints[CALL_PROCESS_SERVER]
        && first_caller->ipc_outbound_message.source
            == fixture->endpoints[CALL_PROCESS_CLIENT]
        && first_caller->ipc_outbound_message.reply_token == 1
        && second_caller->ipc_outbound_message.source
            == fixture->endpoints[CALL_PROCESS_CLIENT]
        && second_caller->ipc_outbound_message.reply_token == 2
        && memcmp(
            first_request,
            first_snapshot,
            sizeof(*first_request)
        ) == 0
        && memcmp(
            second_request,
            second_snapshot,
            sizeof(*second_request)
        ) == 0
    );

    return true;
}

static bool deliver_first_call_and_check_fifo(
    struct ipc_call_test_context *fixture,
    const struct micros_ipc_message *first_request,
    const struct micros_ipc_message *first_snapshot,
    uintptr_t receive_buffer
)
{
    struct micros_ipc_message expected;
    const struct micros_thread *receiver;
    const struct micros_endpoint_record *server;
    const struct micros_thread *first_caller;
    const struct micros_thread *second_caller;

    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CALL_PROCESS_SERVER],
            MICROS_ENDPOINT_ANY,
            receive_buffer
        )
    );
    expected = ipc_call_test_canonical_request(
        first_snapshot,
        fixture->endpoints[CALL_PROCESS_CLIENT],
        1
    );
    server = &fixture->registry.endpoints[CALL_PROCESS_SERVER];
    first_caller = &fixture->objects.threads[
        fixture->primary_threads[CALL_PROCESS_CLIENT].slot
    ];
    second_caller = &fixture->objects.threads[fixture->client_extra_thread.slot];
    receiver = &fixture->objects.threads[
        fixture->primary_threads[CALL_PROCESS_SERVER].slot
    ];
    EXPECT_TRUE(
        first_caller->runtime_flags == MICROS_THREAD_RTS_IPC_REPLY
        && first_caller->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && ipc_call_test_thread_handle_is_zero(first_caller->ipc_next)
        && first_caller->ipc_receive_buffer
            == (uintptr_t)first_request
        && first_caller->ipc_reply_token == 1
        && first_caller->ipc_reply_callee
            == fixture->endpoints[CALL_PROCESS_SERVER]
        && ipc_call_test_bytes_are_zero(
            &first_caller->ipc_outbound_message,
            sizeof(first_caller->ipc_outbound_message)
        )
        && first_caller->ipc_send_destination == 0
        && second_caller->runtime_flags
            == (
                MICROS_THREAD_RTS_IPC_SEND
                | MICROS_THREAD_RTS_IPC_REPLY
            )
        && ipc_call_test_thread_handles_equal(
            server->sender_head,
            fixture->client_extra_thread
        )
        && ipc_call_test_thread_handles_equal(
            server->sender_tail,
            fixture->client_extra_thread
        )
        && receiver->runtime_flags == 0
        && receiver->ipc_delivery_pending
        && receiver->ipc_receive_buffer == receive_buffer
        && memcmp(
            &receiver->ipc_inbound_message,
            &expected,
            sizeof(expected)
        ) == 0
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

bool test_queued_calls_bind_exact_threads_and_deliver_fifo(
    struct ipc_call_test_context *fixture
)
{
    struct micros_ipc_message first_request = ipc_call_test_message_pattern(
        UINT32_C(0x11111111),
        UINT32_C(0x2001),
        UINT64_C(0x1111111111111111),
        UINT8_C(0x30)
    );
    struct micros_ipc_message second_request = ipc_call_test_message_pattern(
        UINT32_C(0x22222222),
        UINT32_C(0x2002),
        UINT64_C(0x2222222222222222),
        UINT8_C(0x40)
    );
    struct micros_ipc_message first_snapshot = first_request;
    struct micros_ipc_message second_snapshot = second_request;

    return (
        queue_calls_and_check_bindings(
            fixture, &first_request, &second_request,
            &first_snapshot, &second_snapshot
        )
        && deliver_first_call_and_check_fifo(
            fixture, &first_request, &first_snapshot, UINT64_C(0x60001000)
        )
    );
}
