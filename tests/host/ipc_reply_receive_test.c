#include "ipc_reply_test_internal.h"

struct reply_receive_blocks_and_later_receives_case {
    struct micros_ipc_message request;
    struct micros_ipc_message reply;
    struct micros_ipc_message next;
    struct micros_ipc_message reply_snapshot;
    struct micros_ipc_message next_snapshot;
    struct micros_ipc_message expected;
    struct micros_endpoint_record *server_endpoint;
    struct micros_thread *caller;
    struct micros_thread *server;
    struct micros_thread *sender;
    uintptr_t receive_buffer;
    uint64_t reply_token;
};

static bool reply_and_block_without_incoming_work(
    struct ipc_reply_test_context *fixture,
    struct reply_receive_blocks_and_later_receives_case *scenario
)
{
    EXPECT_TRUE(
        ipc_reply_test_setup_reply_fixture(fixture)
        && ipc_reply_test_prepare_delivered_call(
            fixture,
            fixture->primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &scenario->request,
            UINT64_C(0x61004000),
            &scenario->reply_token
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_reply_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[REPLY_PROCESS_SERVER],
            scenario->reply_token,
            &scenario->reply,
            MICROS_ENDPOINT_ANY,
            scenario->receive_buffer
        )
    );
    scenario->caller = &fixture->objects.threads[
        fixture->primary_threads[REPLY_PROCESS_CLIENT].slot
    ];
    scenario->server = &fixture->objects.threads[
        fixture->primary_threads[REPLY_PROCESS_SERVER].slot
    ];
    scenario->server_endpoint =
        &fixture->registry.endpoints[fixture->processes[REPLY_PROCESS_SERVER].slot];
    scenario->expected = ipc_reply_test_canonical_reply(
        &scenario->reply_snapshot,
        fixture->endpoints[REPLY_PROCESS_SERVER]
    );
    EXPECT_TRUE(
        scenario->caller->runtime_flags == 0
        && scenario->caller->ready_linked
        && scenario->caller->ipc_delivery_pending
        && scenario->caller->ipc_reply_token == 0
        && scenario->caller->ipc_reply_callee == 0
        && memcmp(
            &scenario->caller->ipc_inbound_message,
            &scenario->expected,
            sizeof(scenario->expected)
        ) == 0
        && scenario->server->runtime_flags
            == MICROS_THREAD_RTS_IPC_RECEIVE
        && !scenario->server->ready_linked
        && scenario->server->ipc_queue_kind
            == MICROS_IPC_QUEUE_RECEIVER
        && ipc_reply_test_thread_handle_is_zero(scenario->server->ipc_next)
        && scenario->server->ipc_receive_source == MICROS_ENDPOINT_ANY
        && scenario->server->ipc_receive_buffer == scenario->receive_buffer
        && !scenario->server->ipc_delivery_pending
        && scenario->server_endpoint->receiver_head.slot
            == fixture->primary_threads[REPLY_PROCESS_SERVER].slot
        && scenario->server_endpoint->receiver_head.generation
            == fixture->primary_threads[REPLY_PROCESS_SERVER].generation
        && scenario->server_endpoint->receiver_tail.slot
            == fixture->primary_threads[REPLY_PROCESS_SERVER].slot
        && scenario->server_endpoint->receiver_tail.generation
            == fixture->primary_threads[REPLY_PROCESS_SERVER].generation
        && memcmp(&scenario->reply, &scenario->reply_snapshot, sizeof(scenario->reply)) == 0
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    scenario->server->ipc_receive_source = MICROS_ENDPOINT_NONE;
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_ERROR_INVARIANT
    );
    scenario->server->ipc_receive_source = MICROS_ENDPOINT_ANY;

    return true;
}

static bool wake_combined_receiver_with_later_send(
    struct ipc_reply_test_context *fixture,
    struct reply_receive_blocks_and_later_receives_case *scenario
)
{
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[REPLY_PROCESS_OTHER_SERVER],
            fixture->endpoints[REPLY_PROCESS_SERVER],
            &scenario->next
        )
    );
    scenario->sender = &fixture->objects.threads[
        fixture->primary_threads[REPLY_PROCESS_OTHER_SERVER].slot
    ];
    scenario->expected = ipc_reply_test_canonical_reply(
        &scenario->next_snapshot,
        fixture->endpoints[REPLY_PROCESS_OTHER_SERVER]
    );
    EXPECT_TRUE(
        scenario->server->runtime_flags == 0
        && scenario->server->ready_linked
        && scenario->server->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && scenario->server->ipc_receive_source == 0
        && scenario->server->ipc_receive_buffer == scenario->receive_buffer
        && scenario->server->ipc_delivery_pending
        && memcmp(
            &scenario->server->ipc_inbound_message,
            &scenario->expected,
            sizeof(scenario->expected)
        ) == 0
        && ipc_reply_test_thread_handle_is_zero(
            scenario->server_endpoint->receiver_head
        )
        && ipc_reply_test_thread_handle_is_zero(
            scenario->server_endpoint->receiver_tail
        )
        && scenario->sender->runtime_flags == 0
        && scenario->sender->ready_linked
        && micros_thread_ipc_state_is_clear(scenario->sender)
        && memcmp(&scenario->next, &scenario->next_snapshot, sizeof(scenario->next)) == 0
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

bool test_reply_receive_blocks_and_later_receives(struct ipc_reply_test_context *fixture)
{
    struct reply_receive_blocks_and_later_receives_case scenario;

    scenario.request = ipc_reply_test_message_pattern(
        0, UINT32_C(0x5601), 0, UINT8_C(0xb0)
    );
    scenario.reply = ipc_reply_test_message_pattern(
        UINT32_C(0xaaaaaaaa), UINT32_C(0x5602), UINT64_C(0xaaaaaaaaaaaaaaaa), UINT8_C(0xc0)
    );
    scenario.next = ipc_reply_test_message_pattern(
        UINT32_C(0xbbbbbbbb), UINT32_C(0x5603), UINT64_C(0xbbbbbbbbbbbbbbbb), UINT8_C(0xd0)
    );
    scenario.reply_snapshot = scenario.reply;
    scenario.next_snapshot = scenario.next;
    scenario.receive_buffer = UINT64_C(0x62000000);
    return (
        reply_and_block_without_incoming_work(fixture, &scenario)
        && wake_combined_receiver_with_later_send(fixture, &scenario)
    );
}

struct reply_receive_selects_specific_sender_atomically_case {
    struct micros_ipc_message first_request;
    struct micros_ipc_message second_request;
    struct micros_ipc_message unmatched_message;
    struct micros_ipc_message reply;
    struct micros_ipc_message reply_snapshot;
    struct micros_ipc_message expected_reply;
    struct micros_ipc_message expected_request;
    struct micros_endpoint_record *server_endpoint;
    struct micros_thread *first_caller;
    struct micros_thread *second_caller;
    struct micros_thread *server;
    struct micros_thread *unmatched_sender;
    uintptr_t receive_buffer;
    uint64_t first_token;
    uint64_t second_token;
    uint64_t saved_token;
    micros_endpoint_t saved_source;
};

static bool prepare_specific_sender_and_unmatched_fifo(
    struct ipc_reply_test_context *fixture,
    struct reply_receive_selects_specific_sender_atomically_case *scenario
)
{
    EXPECT_TRUE(
        ipc_reply_test_setup_reply_fixture(fixture)
        && ipc_reply_test_prepare_delivered_call(
            fixture,
            fixture->primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &scenario->first_request,
            UINT64_C(0x61004800),
            &scenario->first_token
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[REPLY_PROCESS_NO_REPLY_SERVER],
            fixture->endpoints[REPLY_PROCESS_SERVER],
            &scenario->unmatched_message
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &fixture->registry,
            &fixture->objects,
            fixture->client_extra_thread,
            fixture->endpoints[REPLY_PROCESS_SERVER],
            &scenario->second_request,
            (uintptr_t)&scenario->second_request
        )
    );
    scenario->second_token =
        fixture->objects.threads[fixture->client_extra_thread.slot].ipc_reply_token;
    EXPECT_TRUE(scenario->first_token == 1 && scenario->second_token == 2);
    scenario->expected_request =
        fixture->objects.threads[
            fixture->client_extra_thread.slot
        ].ipc_outbound_message;

    return true;
}

static bool reply_and_consume_only_specific_sender(
    struct ipc_reply_test_context *fixture,
    struct reply_receive_selects_specific_sender_atomically_case *scenario
)
{
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_reply_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[REPLY_PROCESS_SERVER],
            scenario->first_token,
            &scenario->reply,
            fixture->endpoints[REPLY_PROCESS_CLIENT],
            scenario->receive_buffer
        )
    );
    scenario->first_caller = &fixture->objects.threads[
        fixture->primary_threads[REPLY_PROCESS_CLIENT].slot
    ];
    scenario->second_caller = &fixture->objects.threads[fixture->client_extra_thread.slot];
    scenario->server = &fixture->objects.threads[
        fixture->primary_threads[REPLY_PROCESS_SERVER].slot
    ];
    scenario->unmatched_sender = &fixture->objects.threads[
        fixture->primary_threads[REPLY_PROCESS_NO_REPLY_SERVER].slot
    ];
    scenario->server_endpoint =
        &fixture->registry.endpoints[fixture->processes[REPLY_PROCESS_SERVER].slot];
    scenario->expected_reply = ipc_reply_test_canonical_reply(
        &scenario->reply_snapshot,
        fixture->endpoints[REPLY_PROCESS_SERVER]
    );
    EXPECT_TRUE(
        scenario->first_caller->runtime_flags == 0
        && scenario->first_caller->ready_linked
        && scenario->first_caller->ipc_delivery_pending
        && scenario->first_caller->ipc_reply_token == 0
        && memcmp(
            &scenario->first_caller->ipc_inbound_message,
            &scenario->expected_reply,
            sizeof(scenario->expected_reply)
        ) == 0
        && scenario->server->runtime_flags == 0
        && scenario->server->ready_linked
        && scenario->server->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && scenario->server->ipc_receive_source == 0
        && scenario->server->ipc_receive_buffer == scenario->receive_buffer
        && scenario->server->ipc_delivery_pending
        && memcmp(
            &scenario->server->ipc_inbound_message,
            &scenario->expected_request,
            sizeof(scenario->expected_request)
        ) == 0
        && scenario->second_caller->runtime_flags
            == MICROS_THREAD_RTS_IPC_REPLY
        && !scenario->second_caller->ready_linked
        && scenario->second_caller->ipc_queue_kind
            == MICROS_IPC_QUEUE_NONE
        && scenario->second_caller->ipc_reply_token == scenario->second_token
        && scenario->second_caller->ipc_reply_callee
            == fixture->endpoints[REPLY_PROCESS_SERVER]
        && scenario->second_caller->ipc_receive_buffer
            == (uintptr_t)&scenario->second_request
        && scenario->unmatched_sender->runtime_flags
            == MICROS_THREAD_RTS_IPC_SEND
        && scenario->unmatched_sender->ipc_queue_kind
            == MICROS_IPC_QUEUE_SENDER
        && ipc_reply_test_thread_handle_is_zero(scenario->unmatched_sender->ipc_next)
        && scenario->server_endpoint->sender_head.slot
            == fixture->primary_threads[REPLY_PROCESS_NO_REPLY_SERVER].slot
        && scenario->server_endpoint->sender_head.generation
            == fixture->primary_threads[
                REPLY_PROCESS_NO_REPLY_SERVER
            ].generation
        && scenario->server_endpoint->sender_tail.slot
            == fixture->primary_threads[REPLY_PROCESS_NO_REPLY_SERVER].slot
        && scenario->server_endpoint->sender_tail.generation
            == fixture->primary_threads[
                REPLY_PROCESS_NO_REPLY_SERVER
            ].generation
        && memcmp(&scenario->reply, &scenario->reply_snapshot, sizeof(scenario->reply)) == 0
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool validate_specific_reply_and_request_tokens(
    struct ipc_reply_test_context *fixture,
    struct reply_receive_selects_specific_sender_atomically_case *scenario
)
{
    scenario->saved_token = scenario->first_caller->ipc_inbound_message.reply_token;
    scenario->first_caller->ipc_inbound_message.reply_token = scenario->first_token;
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_ERROR_INVARIANT
    );
    scenario->first_caller->ipc_inbound_message.reply_token = scenario->saved_token;
    scenario->saved_source = scenario->server->ipc_inbound_message.source;
    scenario->server->ipc_inbound_message.source =
        fixture->endpoints[REPLY_PROCESS_RESERVED_SERVER];
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_ERROR_INVARIANT
    );
    scenario->server->ipc_inbound_message.source = scenario->saved_source;
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

bool test_reply_receive_selects_specific_sender_atomically(struct ipc_reply_test_context *fixture)
{
    struct reply_receive_selects_specific_sender_atomically_case scenario;

    scenario.first_request = ipc_reply_test_message_pattern(
        0, UINT32_C(0x5701), 0, UINT8_C(0x10)
    );
    scenario.second_request = ipc_reply_test_message_pattern(
        UINT32_C(0x11111111), UINT32_C(0x5702), UINT64_C(0x1111111111111111), UINT8_C(0x20)
    );
    scenario.unmatched_message = ipc_reply_test_message_pattern(
        UINT32_C(0x22222222), UINT32_C(0x5703), UINT64_C(0x2222222222222222), UINT8_C(0x30)
    );
    scenario.reply = ipc_reply_test_message_pattern(
        UINT32_C(0x33333333), UINT32_C(0x5704), UINT64_C(0x3333333333333333), UINT8_C(0x40)
    );
    scenario.reply_snapshot = scenario.reply;
    scenario.receive_buffer = UINT64_C(0x62001000);
    return (
        prepare_specific_sender_and_unmatched_fifo(fixture, &scenario)
        && reply_and_consume_only_specific_sender(fixture, &scenario)
        && validate_specific_reply_and_request_tokens(fixture, &scenario)
    );
}

bool test_reply_receive_requires_combined_operation(struct ipc_reply_test_context *fixture)
{
    struct micros_ipc_message request = ipc_reply_test_message_pattern(
        0,
        UINT32_C(0x5801),
        0,
        UINT8_C(0x50)
    );
    struct micros_ipc_message reply = ipc_reply_test_message_pattern(
        UINT32_C(0x44444444),
        UINT32_C(0x5802),
        UINT64_C(0x4444444444444444),
        UINT8_C(0x60)
    );
    struct micros_thread *server;
    uint64_t reply_token;

    EXPECT_TRUE(
        ipc_reply_test_setup_reply_fixture(fixture)
        && ipc_reply_test_prepare_delivered_call(
            fixture,
            fixture->primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_REPLY_RECEIVE_ONLY_SERVER,
            &request,
            UINT64_C(0x61005000),
            &reply_token
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_reply_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[
                REPLY_PROCESS_REPLY_RECEIVE_ONLY_SERVER
            ],
            reply_token,
            &reply,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x62002000)
        )
    );
    server = &fixture->objects.threads[
        fixture->primary_threads[
            REPLY_PROCESS_REPLY_RECEIVE_ONLY_SERVER
        ].slot
    ];
    EXPECT_TRUE(
        server->runtime_flags
            == MICROS_THREAD_RTS_IPC_RECEIVE
        && server->ipc_queue_kind
            == MICROS_IPC_QUEUE_RECEIVER
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );

    EXPECT_TRUE(
        ipc_reply_test_setup_reply_fixture(fixture)
        && ipc_reply_test_prepare_delivered_call(
            fixture,
            fixture->primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_REPLY_ONLY_SERVER,
            &request,
            UINT64_C(0x61005800),
            &reply_token
        )
        && ipc_reply_test_expect_reply_receive_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_UNAUTHORIZED,
            fixture->primary_threads[REPLY_PROCESS_REPLY_ONLY_SERVER],
            reply_token,
            &reply,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x62002800)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_reply(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[REPLY_PROCESS_REPLY_ONLY_SERVER],
            reply_token,
            &reply
        )
    );
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

struct reply_receive_rejections_are_atomic_case {
    union { uint64_t alignment; unsigned char bytes[sizeof(struct micros_ipc_message) + 1]; } misaligned_storage;
    struct micros_ipc_message request;
    struct micros_ipc_message reply;
    struct micros_ipc_message kernel_reply;
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;
    uint64_t reply_token;
};

static bool reject_combined_source_and_receive_buffer(
    struct ipc_reply_test_context *fixture,
    struct reply_receive_rejections_are_atomic_case *scenario
)
{
    EXPECT_TRUE(
        ipc_reply_test_setup_reply_fixture(fixture)
        && ipc_reply_test_prepare_delivered_call(
            fixture,
            fixture->primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &scenario->request,
            UINT64_C(0x61006000),
            &scenario->reply_token
        )
        && ipc_reply_test_expect_reply_receive_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_ARGUMENT,
            fixture->primary_threads[REPLY_PROCESS_SERVER],
            scenario->reply_token,
            &scenario->reply,
            MICROS_ENDPOINT_NONE,
            UINT64_C(0x62003000)
        )
        && ipc_reply_test_expect_reply_receive_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_ARGUMENT,
            fixture->primary_threads[REPLY_PROCESS_SERVER],
            scenario->reply_token,
            &scenario->reply,
            MICROS_ENDPOINT_ANY,
            0
        )
        && ipc_reply_test_expect_reply_receive_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_ARGUMENT,
            fixture->primary_threads[REPLY_PROCESS_SERVER],
            scenario->reply_token,
            &scenario->reply,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x62003001)
        )
        && ipc_reply_test_expect_reply_receive_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_STATE,
            fixture->primary_threads[REPLY_PROCESS_SERVER],
            scenario->reply_token,
            &scenario->reply,
            fixture->endpoints[REPLY_PROCESS_RESERVED_SERVER],
            UINT64_C(0x62003800)
        )
    );
    return true;
}

static bool reject_combined_reply_buffer_and_preserve_token(
    struct ipc_reply_test_context *fixture,
    struct reply_receive_rejections_are_atomic_case *scenario
)
{
    scenario->kernel_reply.type |= MICROS_IPC_TYPE_KERNEL_MASK;
    EXPECT_TRUE(ipc_reply_test_expect_reply_receive_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_ARGUMENT,
        fixture->primary_threads[REPLY_PROCESS_SERVER],
        scenario->reply_token,
        &scenario->kernel_reply,
        MICROS_ENDPOINT_ANY,
        UINT64_C(0x62004000)
    ));

    scenario->registry_snapshot = fixture->registry;
    scenario->objects_snapshot = fixture->objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_ARGUMENT,
        micros_ipc_reply_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[REPLY_PROCESS_SERVER],
            scenario->reply_token,
            NULL,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x62004800)
        )
    );
    EXPECT_TRUE(ipc_reply_test_complete_state_is_unchanged(
        fixture,
        &scenario->registry_snapshot,
        &scenario->objects_snapshot
    ));

    memset(&scenario->misaligned_storage, 0, sizeof(scenario->misaligned_storage));
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_ARGUMENT,
        micros_ipc_reply_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[REPLY_PROCESS_SERVER],
            scenario->reply_token,
            (const struct micros_ipc_message *)(
                (const void *)&scenario->misaligned_storage.bytes[1]
            ),
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x62005000)
        )
    );
    EXPECT_TRUE(
        ipc_reply_test_complete_state_is_unchanged(
            fixture,
            &scenario->registry_snapshot,
            &scenario->objects_snapshot
        )
        && fixture->objects.threads[
            fixture->primary_threads[REPLY_PROCESS_CLIENT].slot
        ].ipc_reply_token == scenario->reply_token
    );
    return true;
}

bool test_reply_receive_rejections_are_atomic(struct ipc_reply_test_context *fixture)
{
    struct reply_receive_rejections_are_atomic_case scenario;

    scenario.request = ipc_reply_test_message_pattern(
        0, UINT32_C(0x5901), 0, UINT8_C(0x70)
    );
    scenario.reply = ipc_reply_test_message_pattern(
        UINT32_C(0x55555555), UINT32_C(0x5902), UINT64_C(0x5555555555555555), UINT8_C(0x80)
    );
    scenario.kernel_reply = scenario.reply;
    return (
        reject_combined_source_and_receive_buffer(fixture, &scenario)
        && reject_combined_reply_buffer_and_preserve_token(fixture, &scenario)
    );
}

bool test_reply_receive_scheduler_failures_are_atomic(struct ipc_reply_test_context *fixture)
{
    struct micros_ipc_message request = ipc_reply_test_message_pattern(
        0,
        UINT32_C(0x5a01),
        0,
        UINT8_C(0x90)
    );
    struct micros_ipc_message queued = ipc_reply_test_message_pattern(
        0,
        UINT32_C(0x5a02),
        0,
        UINT8_C(0xa0)
    );
    struct micros_ipc_message reply = ipc_reply_test_message_pattern(
        UINT32_C(0x66666666),
        UINT32_C(0x5a03),
        UINT64_C(0x6666666666666666),
        UINT8_C(0xb0)
    );
    struct micros_thread *caller;
    struct micros_thread *sender;
    uint64_t reply_token;

    EXPECT_TRUE(
        ipc_reply_test_setup_reply_fixture(fixture)
        && ipc_reply_test_prepare_delivered_call(
            fixture,
            fixture->primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &request,
            UINT64_C(0x61006800),
            &reply_token
        )
        && ipc_reply_test_make_reply_wait_current(
            fixture,
            fixture->primary_threads[REPLY_PROCESS_CLIENT]
        )
        && ipc_reply_test_expect_reply_receive_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_STATE,
            fixture->primary_threads[REPLY_PROCESS_SERVER],
            reply_token,
            &reply,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x62005800)
        )
    );
    caller = &fixture->objects.threads[
        fixture->primary_threads[REPLY_PROCESS_CLIENT].slot
    ];
    EXPECT_TRUE(
        caller->runtime_flags == MICROS_THREAD_RTS_IPC_REPLY
        && caller->ipc_reply_token == reply_token
        && !caller->ipc_delivery_pending
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );

    EXPECT_TRUE(
        ipc_reply_test_setup_reply_fixture(fixture)
        && ipc_reply_test_prepare_delivered_call(
            fixture,
            fixture->primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &request,
            UINT64_C(0x61007000),
            &reply_token
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[REPLY_PROCESS_OTHER_SERVER],
            fixture->endpoints[REPLY_PROCESS_SERVER],
            &queued
        )
    );
    EXPECT_TRUE(
        ipc_reply_test_make_single_queued_sender_current(
            fixture,
            fixture->primary_threads[REPLY_PROCESS_OTHER_SERVER],
            REPLY_PROCESS_SERVER
        )
        && ipc_reply_test_expect_reply_receive_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_STATE,
            fixture->primary_threads[REPLY_PROCESS_SERVER],
            reply_token,
            &reply,
            fixture->endpoints[REPLY_PROCESS_OTHER_SERVER],
            UINT64_C(0x62006000)
        )
    );
    caller = &fixture->objects.threads[
        fixture->primary_threads[REPLY_PROCESS_CLIENT].slot
    ];
    sender = &fixture->objects.threads[
        fixture->primary_threads[REPLY_PROCESS_OTHER_SERVER].slot
    ];
    EXPECT_TRUE(
        caller->ipc_reply_token == reply_token
        && caller->runtime_flags == MICROS_THREAD_RTS_IPC_REPLY
        && !caller->ipc_delivery_pending
        && sender->runtime_flags == MICROS_THREAD_RTS_IPC_SEND
        && sender->ipc_queue_kind == MICROS_IPC_QUEUE_SENDER
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}
