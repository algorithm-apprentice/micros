#include "ipc_notify_test_internal.h"

bool test_notify_coalesces_and_never_blocks(struct ipc_notify_test_context *fixture)
{
    struct micros_endpoint_record *destination;
    struct micros_thread *source;
    struct micros_thread *receiver;
    uint64_t source_bit;
    const uint64_t first_mask = UINT64_C(0x0000000000000015);
    const uint64_t second_mask = UINT64_C(0x800000000000002a);
    const uint64_t combined_mask = first_mask | second_mask;

    EXPECT_TRUE(ipc_notify_test_setup_notify_fixture(fixture));
    destination = &fixture->registry.endpoints[
        fixture->processes[NOTIFY_PROCESS_DESTINATION].slot
    ];
    source = &fixture->objects.threads[
        fixture->primary_threads[NOTIFY_PROCESS_SOURCE_LOW].slot
    ];
    receiver = &fixture->objects.threads[
        fixture->primary_threads[NOTIFY_PROCESS_DESTINATION].slot
    ];
    source_bit = UINT64_C(1)
        << fixture->processes[NOTIFY_PROCESS_SOURCE_LOW].slot;

    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            first_mask
        )
    );
    EXPECT_TRUE(
        source->runtime_flags == 0
        && source->ready_linked
        && source->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && (
            source->runtime_flags
            & MICROS_THREAD_RTS_IPC_MASK
        ) == 0
        && destination->pending_notification_sources == source_bit
        && destination->pending_events[
            fixture->processes[NOTIFY_PROCESS_SOURCE_LOW].slot
        ] == first_mask
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
        && ipc_notify_test_hold_runnable_thread(
            fixture,
            fixture->primary_threads[NOTIFY_PROCESS_SOURCE_LOW]
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            second_mask
        )
    );
    EXPECT_TRUE(
        source->runtime_flags == 0
        && source->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && destination->pending_notification_sources == source_bit
        && destination->pending_events[
            fixture->processes[NOTIFY_PROCESS_SOURCE_LOW].slot
        ] == combined_mask
        && ipc_notify_test_hold_runnable_thread(
            fixture,
            fixture->primary_threads[NOTIFY_PROCESS_SOURCE_LOW]
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60000000)
        )
    );
    EXPECT_TRUE(
        receiver->runtime_flags == 0
        && receiver->ready_linked
        && receiver->ipc_delivery_pending
        && ipc_notify_test_notification_message_matches(
            &receiver->ipc_inbound_message,
            fixture->endpoints[NOTIFY_PROCESS_SOURCE_LOW],
            combined_mask
        )
        && destination->pending_notification_sources == 0
        && destination->pending_events[
            fixture->processes[NOTIFY_PROCESS_SOURCE_LOW].slot
        ] == 0
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );

    return true;
}

bool test_notify_wakes_first_matching_receiver(struct ipc_notify_test_context *fixture)
{
    struct micros_endpoint_record *destination;
    struct micros_thread *unmatched;
    struct micros_thread *matched;
    struct micros_thread *later;
    const uint64_t event_mask = UINT64_C(0x00000000000000a5);

    EXPECT_TRUE(ipc_notify_test_setup_notify_fixture(fixture));
    EXPECT_TRUE(ipc_notify_test_prepare_destination_extra_threads(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            fixture->endpoints[NOTIFY_PROCESS_SOURCE_HIGH],
            UINT64_C(0x60001000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->destination_extra_threads[0],
            fixture->endpoints[NOTIFY_PROCESS_SOURCE_LOW],
            UINT64_C(0x60002000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->destination_extra_threads[1],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60003000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            event_mask
        )
    );

    destination = &fixture->registry.endpoints[
        fixture->processes[NOTIFY_PROCESS_DESTINATION].slot
    ];
    unmatched = &fixture->objects.threads[
        fixture->primary_threads[NOTIFY_PROCESS_DESTINATION].slot
    ];
    matched = &fixture->objects.threads[fixture->destination_extra_threads[0].slot];
    later = &fixture->objects.threads[fixture->destination_extra_threads[1].slot];
    EXPECT_TRUE(
        ipc_notify_test_thread_handles_equal(
            destination->receiver_head,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION]
        )
        && ipc_notify_test_thread_handles_equal(
            destination->receiver_tail,
            fixture->destination_extra_threads[1]
        )
        && ipc_notify_test_thread_handles_equal(
            unmatched->ipc_next,
            fixture->destination_extra_threads[1]
        )
        && unmatched->runtime_flags
            == MICROS_THREAD_RTS_IPC_RECEIVE
        && later->runtime_flags == MICROS_THREAD_RTS_IPC_RECEIVE
        && matched->runtime_flags == 0
        && matched->ready_linked
        && matched->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && matched->ipc_receive_source == 0
        && matched->ipc_delivery_pending
        && ipc_notify_test_notification_message_matches(
            &matched->ipc_inbound_message,
            fixture->endpoints[NOTIFY_PROCESS_SOURCE_LOW],
            event_mask
        )
        && destination->pending_notification_sources == 0
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

bool test_call_reply_wait_does_not_consume_notification(struct ipc_notify_test_context *fixture)
{
    struct micros_ipc_message request = ipc_notify_test_message_pattern(
        MICROS_ENDPOINT_ANY,
        UINT32_C(0x4101),
        UINT64_C(0xffffffffffffffff),
        UINT8_C(0x40)
    );
    struct micros_endpoint_record *destination;
    struct micros_thread *caller;
    uint64_t source_bit;
    const uint64_t event_mask = UINT64_C(0x0000000000000040);
    const uint64_t kernel_mask = UINT64_C(0x0000000000000080);

    EXPECT_TRUE(ipc_notify_test_setup_notify_fixture(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_SERVER],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60004000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            fixture->endpoints[NOTIFY_PROCESS_SERVER],
            &request,
            (uintptr_t)&request
        )
    );
    caller = &fixture->objects.threads[
        fixture->primary_threads[NOTIFY_PROCESS_DESTINATION].slot
    ];
    EXPECT_TRUE(
        caller->runtime_flags == MICROS_THREAD_RTS_IPC_REPLY
        && caller->ipc_reply_token != 0
        && !caller->ipc_delivery_pending
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            event_mask
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_inject_kernel_notification(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            kernel_mask
        )
    );
    destination = &fixture->registry.endpoints[
        fixture->processes[NOTIFY_PROCESS_DESTINATION].slot
    ];
    source_bit = UINT64_C(1)
        << fixture->processes[NOTIFY_PROCESS_SOURCE_LOW].slot;
    EXPECT_TRUE(
        caller->runtime_flags == MICROS_THREAD_RTS_IPC_REPLY
        && caller->ipc_reply_token != 0
        && !caller->ipc_delivery_pending
        && destination->pending_notification_sources == source_bit
        && destination->pending_events[
            fixture->processes[NOTIFY_PROCESS_SOURCE_LOW].slot
        ] == event_mask
        && destination->pending_kernel_events == kernel_mask
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

struct receive_prefers_lowest_notification_before_sender_case {
    struct micros_ipc_message sent;
    struct micros_ipc_message delivered;
    struct micros_ipc_message expected_sent;
    struct micros_endpoint_record *destination;
    struct micros_thread *sender;
    uint64_t kernel_mask;
    uint64_t low_mask;
    uint64_t high_mask;
};

static bool stage_kernel_endpoint_events_and_sender(
    struct ipc_notify_test_context *fixture,
    struct receive_prefers_lowest_notification_before_sender_case *scenario
)
{
    EXPECT_TRUE(ipc_notify_test_setup_notify_fixture(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_inject_kernel_notification(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            scenario->kernel_mask
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_SOURCE_HIGH],
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            scenario->high_mask
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            scenario->low_mask
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_SENDER],
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            &scenario->sent
        )
    );
    scenario->destination = &fixture->registry.endpoints[
        fixture->processes[NOTIFY_PROCESS_DESTINATION].slot
    ];
    scenario->sender = &fixture->objects.threads[
        fixture->primary_threads[NOTIFY_PROCESS_SENDER].slot
    ];
    EXPECT_TRUE(
        scenario->sender->runtime_flags == MICROS_THREAD_RTS_IPC_SEND
        && scenario->sender->ipc_queue_kind == MICROS_IPC_QUEUE_SENDER
        && ipc_notify_test_thread_handles_equal(
            scenario->destination->sender_head,
            fixture->primary_threads[NOTIFY_PROCESS_SENDER]
        )
    );

    return true;
}

static bool consume_kernel_event_before_endpoint_events(
    struct ipc_notify_test_context *fixture,
    struct receive_prefers_lowest_notification_before_sender_case *scenario
)
{
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60005000)
        )
    );
    EXPECT_TRUE(
        ipc_notify_test_consume_staged_and_hold(
            fixture,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            &scenario->delivered
        )
        && ipc_notify_test_notification_message_matches(
            &scenario->delivered,
            MICROS_ENDPOINT_NONE,
            scenario->kernel_mask
        )
        && scenario->destination->pending_kernel_events == 0
        && scenario->sender->runtime_flags == MICROS_THREAD_RTS_IPC_SEND
        && scenario->destination->pending_events[
            fixture->processes[NOTIFY_PROCESS_SOURCE_LOW].slot
        ] == scenario->low_mask
        && scenario->destination->pending_events[
            fixture->processes[NOTIFY_PROCESS_SOURCE_HIGH].slot
        ] == scenario->high_mask
    );
    return true;
}

static bool consume_lowest_endpoint_event_before_sender(
    struct ipc_notify_test_context *fixture,
    struct receive_prefers_lowest_notification_before_sender_case *scenario
)
{
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60006000)
        )
    );
    EXPECT_TRUE(
        ipc_notify_test_consume_staged_and_hold(
            fixture,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            &scenario->delivered
        )
        && ipc_notify_test_notification_message_matches(
            &scenario->delivered,
            fixture->endpoints[NOTIFY_PROCESS_SOURCE_LOW],
            scenario->low_mask
        )
        && scenario->sender->runtime_flags == MICROS_THREAD_RTS_IPC_SEND
        && scenario->destination->pending_events[
            fixture->processes[NOTIFY_PROCESS_SOURCE_LOW].slot
        ] == 0
        && scenario->destination->pending_events[
            fixture->processes[NOTIFY_PROCESS_SOURCE_HIGH].slot
        ] == scenario->high_mask
    );
    return true;
}

static bool consume_higher_endpoint_event_before_sender(
    struct ipc_notify_test_context *fixture,
    struct receive_prefers_lowest_notification_before_sender_case *scenario
)
{
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60007000)
        )
    );
    EXPECT_TRUE(
        ipc_notify_test_consume_staged_and_hold(
            fixture,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            &scenario->delivered
        )
        && ipc_notify_test_notification_message_matches(
            &scenario->delivered,
            fixture->endpoints[NOTIFY_PROCESS_SOURCE_HIGH],
            scenario->high_mask
        )
        && scenario->sender->runtime_flags == MICROS_THREAD_RTS_IPC_SEND
        && scenario->destination->pending_notification_sources == 0
    );
    return true;
}

static bool consume_sender_after_every_pending_event(
    struct ipc_notify_test_context *fixture,
    struct receive_prefers_lowest_notification_before_sender_case *scenario
)
{
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60008000)
        )
    );
    scenario->expected_sent.source = fixture->endpoints[NOTIFY_PROCESS_SENDER];
    scenario->expected_sent.reply_token = 0;
    EXPECT_TRUE(
        ipc_notify_test_clear_staged_delivery(
            fixture,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            &scenario->delivered
        )
        && memcmp(&scenario->delivered, &scenario->expected_sent, sizeof(scenario->delivered)) == 0
        && scenario->sender->runtime_flags == 0
        && scenario->sender->ready_linked
        && scenario->sender->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && ipc_notify_test_thread_handle_is_zero(scenario->destination->sender_head)
        && ipc_notify_test_thread_handle_is_zero(scenario->destination->sender_tail)
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

bool test_receive_prefers_lowest_notification_before_sender(struct ipc_notify_test_context *fixture)
{
    struct receive_prefers_lowest_notification_before_sender_case scenario;

    scenario.sent = ipc_notify_test_message_pattern(
        MICROS_ENDPOINT_ANY, UINT32_C(0x4201), UINT64_C(0xffffffffffffffff), UINT8_C(0x50)
    );
    scenario.expected_sent = scenario.sent;
    scenario.kernel_mask = UINT64_C(0x0000000000000100);
    scenario.low_mask = UINT64_C(0x0000000000000001);
    scenario.high_mask = UINT64_C(0x8000000000000000);
    return (
        stage_kernel_endpoint_events_and_sender(fixture, &scenario)
        && consume_kernel_event_before_endpoint_events(fixture, &scenario)
        && consume_lowest_endpoint_event_before_sender(fixture, &scenario)
        && consume_higher_endpoint_event_before_sender(fixture, &scenario)
        && consume_sender_after_every_pending_event(fixture, &scenario)
    );
}

struct reply_receive_prefers_pending_notification_case {
    struct micros_ipc_message request;
    struct micros_ipc_message reply;
    struct micros_ipc_message ordinary;
    struct micros_ipc_message delivered_request;
    struct micros_ipc_message expected_reply;
    struct micros_endpoint_record *server_endpoint;
    struct micros_thread *caller;
    struct micros_thread *server;
    struct micros_thread *sender;
    uint64_t reply_token;
    uint64_t event_mask;
    uint64_t kernel_mask;
};

static bool prepare_call_pending_events_and_ordinary_sender(
    struct ipc_notify_test_context *fixture,
    struct reply_receive_prefers_pending_notification_case *scenario
)
{
    EXPECT_TRUE(ipc_notify_test_setup_notify_fixture(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_SERVER],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x6000a000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            fixture->endpoints[NOTIFY_PROCESS_SERVER],
            &scenario->request,
            (uintptr_t)&scenario->request
        )
    );
    scenario->server = &fixture->objects.threads[
        fixture->primary_threads[NOTIFY_PROCESS_SERVER].slot
    ];
    scenario->reply_token = scenario->server->ipc_inbound_message.reply_token;
    EXPECT_TRUE(
        scenario->reply_token != 0
        && ipc_notify_test_consume_staged_and_hold(
            fixture,
            fixture->primary_threads[NOTIFY_PROCESS_SERVER],
            &scenario->delivered_request
        )
        && scenario->delivered_request.source
            == fixture->endpoints[NOTIFY_PROCESS_DESTINATION]
        && scenario->delivered_request.reply_token == scenario->reply_token
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
            fixture->endpoints[NOTIFY_PROCESS_SERVER],
            scenario->event_mask
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_inject_kernel_notification(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[NOTIFY_PROCESS_SERVER],
            scenario->kernel_mask
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_SENDER],
            fixture->endpoints[NOTIFY_PROCESS_SERVER],
            &scenario->ordinary
        )
    );
    return true;
}

static bool reply_and_receive_kernel_event_before_other_work(
    struct ipc_notify_test_context *fixture,
    struct reply_receive_prefers_pending_notification_case *scenario
)
{
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_reply_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_SERVER],
            scenario->reply_token,
            &scenario->reply,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x6000b000)
        )
    );

    scenario->server_endpoint = &fixture->registry.endpoints[
        fixture->processes[NOTIFY_PROCESS_SERVER].slot
    ];
    scenario->caller = &fixture->objects.threads[
        fixture->primary_threads[NOTIFY_PROCESS_DESTINATION].slot
    ];
    scenario->sender = &fixture->objects.threads[
        fixture->primary_threads[NOTIFY_PROCESS_SENDER].slot
    ];
    scenario->expected_reply.source = fixture->endpoints[NOTIFY_PROCESS_SERVER];
    scenario->expected_reply.reply_token = 0;
    EXPECT_TRUE(
        scenario->caller->runtime_flags == 0
        && scenario->caller->ready_linked
        && scenario->caller->ipc_reply_token == 0
        && scenario->caller->ipc_reply_callee == 0
        && scenario->caller->ipc_delivery_pending
        && memcmp(
            &scenario->caller->ipc_inbound_message,
            &scenario->expected_reply,
            sizeof(scenario->expected_reply)
        ) == 0
        && scenario->server->runtime_flags == 0
        && scenario->server->ready_linked
        && scenario->server->ipc_delivery_pending
        && ipc_notify_test_notification_message_matches(
            &scenario->server->ipc_inbound_message,
            MICROS_ENDPOINT_NONE,
            scenario->kernel_mask
        )
        && scenario->server_endpoint->pending_kernel_events == 0
        && scenario->server_endpoint->pending_notification_sources
            == (
                UINT64_C(1)
                << fixture->processes[NOTIFY_PROCESS_SOURCE_LOW].slot
            )
        && scenario->server_endpoint->pending_events[
            fixture->processes[NOTIFY_PROCESS_SOURCE_LOW].slot
        ] == scenario->event_mask
        && scenario->sender->runtime_flags == MICROS_THREAD_RTS_IPC_SEND
        && scenario->sender->ipc_queue_kind == MICROS_IPC_QUEUE_SENDER
        && ipc_notify_test_thread_handles_equal(
            scenario->server_endpoint->sender_head,
            fixture->primary_threads[NOTIFY_PROCESS_SENDER]
        )
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

bool test_reply_receive_prefers_pending_notification(struct ipc_notify_test_context *fixture)
{
    struct reply_receive_prefers_pending_notification_case scenario;

    scenario.request = ipc_notify_test_message_pattern(
        0, UINT32_C(0x4301), 0, UINT8_C(0x60)
    );
    scenario.reply = ipc_notify_test_message_pattern(
        MICROS_ENDPOINT_ANY, UINT32_C(0x4302), UINT64_C(0xffffffffffffffff), UINT8_C(0x70)
    );
    scenario.ordinary = ipc_notify_test_message_pattern(
        MICROS_ENDPOINT_ANY, UINT32_C(0x4303), UINT64_C(0xffffffffffffffff), UINT8_C(0x80)
    );
    scenario.expected_reply = scenario.reply;
    scenario.event_mask = UINT64_C(0x00000000a5a55a5a);
    scenario.kernel_mask = UINT64_C(0x000000005a5aa5a5);
    return (
        prepare_call_pending_events_and_ordinary_sender(fixture, &scenario)
        && reply_and_receive_kernel_event_before_other_work(fixture, &scenario)
    );
}
