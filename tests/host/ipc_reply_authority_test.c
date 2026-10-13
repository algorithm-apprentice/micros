#include "ipc_reply_test_internal.h"

struct reply_requires_request_return_case {
    struct micros_ipc_message request;
    struct micros_ipc_message reply;
    struct micros_ipc_message reply_snapshot;
    struct micros_ipc_message staged_request;
    struct micros_ipc_message delivered_request;
    struct micros_ipc_message expected_reply;
    struct micros_thread *caller;
    struct micros_thread *replier;
    struct micros_endpoint_record *server_endpoint;
    uint64_t reply_token;
};

static bool reject_sibling_reply_before_request_return(
    struct ipc_reply_test_context *fixture,
    struct reply_requires_request_return_case *scenario
)
{
    EXPECT_TRUE(
        ipc_reply_test_setup_reply_fixture(fixture)
        && ipc_reply_test_stage_delivered_call(
            fixture,
            fixture->primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &scenario->request,
            UINT64_C(0x61000000),
            &scenario->reply_token
        )
        && ipc_reply_test_prepare_server_extra_thread(fixture)
    );
    scenario->staged_request = fixture->objects.threads[
        fixture->primary_threads[REPLY_PROCESS_SERVER].slot
    ].ipc_inbound_message;
    EXPECT_TRUE(ipc_reply_test_expect_reply_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_REPLY_TOKEN,
        fixture->server_extra_thread,
        scenario->reply_token,
        &scenario->reply
    ));
    return true;
}

static bool deliver_sibling_reply_after_request_return(
    struct ipc_reply_test_context *fixture,
    struct reply_requires_request_return_case *scenario
)
{
    EXPECT_TRUE(
        ipc_reply_test_clear_staged_request(
            fixture,
            fixture->primary_threads[REPLY_PROCESS_SERVER],
            &scenario->delivered_request
        )
        && memcmp(
            &scenario->delivered_request,
            &scenario->staged_request,
            sizeof(scenario->staged_request)
        ) == 0
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_reply(
            &fixture->registry,
            &fixture->objects,
            fixture->server_extra_thread,
            scenario->reply_token,
            &scenario->reply
        )
    );
    scenario->caller = &fixture->objects.threads[
        fixture->primary_threads[REPLY_PROCESS_CLIENT].slot
    ];
    scenario->replier = &fixture->objects.threads[fixture->server_extra_thread.slot];
    scenario->expected_reply = ipc_reply_test_canonical_reply(
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
            &scenario->expected_reply,
            sizeof(scenario->expected_reply)
        ) == 0
        && scenario->replier->runtime_flags == 0
        && scenario->replier->ready_linked
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
        && memcmp(&scenario->reply, &scenario->reply_snapshot, sizeof(scenario->reply)) == 0
    );
    EXPECT_TRUE(
        micros_thread_scheduler_hold(
            &fixture->objects,
            fixture->server_extra_thread
        ) == MICROS_KERNEL_OBJECT_OK
        && ipc_reply_test_expect_reply_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_REPLY_TOKEN,
            fixture->server_extra_thread,
            scenario->reply_token,
            &scenario->reply
        )
    );

    return true;
}

static bool check_combined_request_return_authority(
    struct ipc_reply_test_context *fixture,
    struct reply_requires_request_return_case *scenario
)
{
    EXPECT_TRUE(
        ipc_reply_test_setup_reply_fixture(fixture)
        && ipc_reply_test_stage_delivered_call(
            fixture,
            fixture->primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &scenario->request,
            UINT64_C(0x61000800),
            &scenario->reply_token
        )
        && ipc_reply_test_prepare_server_extra_thread(fixture)
        && ipc_reply_test_expect_reply_receive_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_REPLY_TOKEN,
            fixture->server_extra_thread,
            scenario->reply_token,
            &scenario->reply,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x61001000)
        )
        && ipc_reply_test_clear_staged_request(
            fixture,
            fixture->primary_threads[REPLY_PROCESS_SERVER],
            &scenario->delivered_request
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_reply_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->server_extra_thread,
            scenario->reply_token,
            &scenario->reply,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x61001000)
        )
    );
    scenario->caller = &fixture->objects.threads[
        fixture->primary_threads[REPLY_PROCESS_CLIENT].slot
    ];
    scenario->replier = &fixture->objects.threads[fixture->server_extra_thread.slot];
    scenario->server_endpoint = &fixture->registry.endpoints[REPLY_PROCESS_SERVER];
    EXPECT_TRUE(
        scenario->caller->runtime_flags == 0
        && scenario->caller->ready_linked
        && scenario->caller->ipc_delivery_pending
        && scenario->caller->ipc_reply_token == 0
        && scenario->caller->ipc_reply_callee == 0
        && scenario->replier->runtime_flags == MICROS_THREAD_RTS_IPC_RECEIVE
        && !scenario->replier->ready_linked
        && scenario->replier->ipc_queue_kind == MICROS_IPC_QUEUE_RECEIVER
        && scenario->replier->ipc_receive_source == MICROS_ENDPOINT_ANY
        && scenario->replier->ipc_receive_buffer == UINT64_C(0x61001000)
        && scenario->server_endpoint->receiver_head.slot
            == fixture->server_extra_thread.slot
        && scenario->server_endpoint->receiver_head.generation
            == fixture->server_extra_thread.generation
        && scenario->server_endpoint->receiver_tail.slot
            == fixture->server_extra_thread.slot
        && scenario->server_endpoint->receiver_tail.generation
            == fixture->server_extra_thread.generation
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
        && memcmp(&scenario->reply, &scenario->reply_snapshot, sizeof(scenario->reply)) == 0
    );
    return true;
}

bool test_reply_requires_request_return(struct ipc_reply_test_context *fixture)
{
    struct reply_requires_request_return_case scenario;

    scenario.request = ipc_reply_test_message_pattern(
        UINT32_C(0x11111111), UINT32_C(0x5001), UINT64_C(0x1111111111111111), UINT8_C(0x10)
    );
    scenario.reply = ipc_reply_test_message_pattern(
        UINT32_C(0x22222222), UINT32_C(0x5002), UINT64_C(0x2222222222222222), UINT8_C(0x20)
    );
    scenario.reply_snapshot = scenario.reply;
    return (
        reject_sibling_reply_before_request_return(fixture, &scenario)
        && deliver_sibling_reply_after_request_return(fixture, &scenario)
        && check_combined_request_return_authority(fixture, &scenario)
    );
}

bool test_staged_call_tokens_are_unique(struct ipc_reply_test_context *fixture)
{
    struct micros_ipc_message request = ipc_reply_test_message_pattern(
        UINT32_C(0x33333333),
        UINT32_C(0x5003),
        UINT64_C(0x3333333333333333),
        UINT8_C(0x30)
    );
    struct micros_thread *receiver;
    struct micros_thread *duplicate;
    uint64_t reply_token;

    EXPECT_TRUE(
        ipc_reply_test_setup_reply_fixture(fixture)
        && ipc_reply_test_stage_delivered_call(
            fixture,
            fixture->primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &request,
            UINT64_C(0x61001800),
            &reply_token
        )
        && ipc_reply_test_prepare_server_extra_thread(fixture)
        && reply_token != 0
        && micros_thread_runtime_flags_unset(
            &fixture->objects,
            fixture->server_extra_thread,
            MICROS_THREAD_RTS_INACTIVE
        ) == MICROS_KERNEL_OBJECT_OK
    );
    receiver = &fixture->objects.threads[
        fixture->primary_threads[REPLY_PROCESS_SERVER].slot
    ];
    duplicate = &fixture->objects.threads[fixture->server_extra_thread.slot];
    duplicate->ipc_receive_buffer = UINT64_C(0x61002000);
    duplicate->ipc_delivery_pending = true;
    duplicate->ipc_inbound_message =
        receiver->ipc_inbound_message;
    duplicate->ipc_staged_result = MICROS_IPC_OK;
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_ERROR_INVARIANT
    );
    return true;
}

struct reply_routes_exact_tokens_without_send_target_case {
    struct micros_ipc_message first_request;
    struct micros_ipc_message second_request;
    struct micros_ipc_message first_reply;
    struct micros_ipc_message second_reply;
    struct micros_ipc_message first_reply_snapshot;
    struct micros_ipc_message second_reply_snapshot;
    struct micros_ipc_message expected;
    struct micros_thread *first_caller;
    struct micros_thread *second_caller;
    struct micros_thread *other_server;
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;
    uint64_t first_token;
    uint64_t second_token;
    uint64_t saved_token;
    micros_endpoint_t saved_source;
};

static bool stage_exact_calls_and_reject_foreign_rights(
    struct ipc_reply_test_context *fixture,
    struct reply_routes_exact_tokens_without_send_target_case *scenario
)
{
    EXPECT_TRUE(
        ipc_reply_test_setup_reply_fixture(fixture)
        && ipc_reply_test_prepare_delivered_call(
            fixture,
            fixture->primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &scenario->first_request,
            UINT64_C(0x61000000),
            &scenario->first_token
        )
        && ipc_reply_test_prepare_delivered_call(
            fixture,
            fixture->client_extra_thread,
            REPLY_PROCESS_OTHER_SERVER,
            &scenario->second_request,
            UINT64_C(0x61000800),
            &scenario->second_token
        )
    );
    EXPECT_TRUE(scenario->first_token == 1 && scenario->second_token == 2);
    EXPECT_TRUE(ipc_reply_test_expect_send_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_UNAUTHORIZED,
        fixture->primary_threads[REPLY_PROCESS_OTHER_SERVER],
        fixture->endpoints[REPLY_PROCESS_CLIENT],
        &scenario->second_reply
    ));
    EXPECT_TRUE(ipc_reply_test_expect_reply_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_REPLY_TOKEN,
        fixture->primary_threads[REPLY_PROCESS_SERVER],
        scenario->second_token,
        &scenario->second_reply
    ));
    scenario->registry_snapshot = fixture->registry;
    scenario->objects_snapshot = fixture->objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_REPLY_TOKEN,
        micros_ipc_reply_expected_caller(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[REPLY_PROCESS_OTHER_SERVER],
            scenario->second_token,
            fixture->endpoints[REPLY_PROCESS_SERVER],
            &scenario->second_reply
        )
    );
    EXPECT_TRUE(
        memcmp(
            &fixture->registry,
            &scenario->registry_snapshot,
            sizeof(fixture->registry)
        ) == 0
        && memcmp(
            &fixture->objects,
            &scenario->objects_snapshot,
            sizeof(fixture->objects)
        ) == 0
        && memcmp(
            &scenario->second_reply,
            &scenario->second_reply_snapshot,
            sizeof(scenario->second_reply)
        ) == 0
    );

    return true;
}

static bool reply_to_second_exact_caller(
    struct ipc_reply_test_context *fixture,
    struct reply_routes_exact_tokens_without_send_target_case *scenario
)
{
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_reply_expected_caller(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[REPLY_PROCESS_OTHER_SERVER],
            scenario->second_token,
            fixture->endpoints[REPLY_PROCESS_CLIENT],
            &scenario->second_reply
        )
    );
    scenario->expected = ipc_reply_test_canonical_reply(
        &scenario->second_reply_snapshot,
        fixture->endpoints[REPLY_PROCESS_OTHER_SERVER]
    );
    scenario->first_caller = &fixture->objects.threads[
        fixture->primary_threads[REPLY_PROCESS_CLIENT].slot
    ];
    scenario->second_caller = &fixture->objects.threads[fixture->client_extra_thread.slot];
    scenario->other_server = &fixture->objects.threads[
        fixture->primary_threads[REPLY_PROCESS_OTHER_SERVER].slot
    ];
    EXPECT_TRUE(
        fixture->registry.last_reply_token == 2
        && scenario->first_caller->runtime_flags
            == MICROS_THREAD_RTS_IPC_REPLY
        && scenario->first_caller->ipc_reply_token == scenario->first_token
        && !scenario->first_caller->ipc_delivery_pending
        && scenario->second_caller->runtime_flags == 0
        && scenario->second_caller->ready_linked
        && scenario->second_caller->ipc_delivery_pending
        && scenario->second_caller->ipc_receive_buffer
            == (uintptr_t)&scenario->second_request
        && scenario->second_caller->ipc_reply_token == 0
        && scenario->second_caller->ipc_reply_callee == 0
        && memcmp(
            &scenario->second_caller->ipc_inbound_message,
            &scenario->expected,
            sizeof(scenario->expected)
        ) == 0
        && scenario->other_server->runtime_flags == 0
        && scenario->other_server->ready_linked
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
        && memcmp(
            &scenario->second_reply,
            &scenario->second_reply_snapshot,
            sizeof(scenario->second_reply)
        ) == 0
    );

    return true;
}

static bool validate_reply_shape_and_complete_first_caller(
    struct ipc_reply_test_context *fixture,
    struct reply_routes_exact_tokens_without_send_target_case *scenario
)
{
    scenario->saved_token = scenario->second_caller->ipc_inbound_message.reply_token;
    scenario->second_caller->ipc_inbound_message.reply_token = scenario->second_token;
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_ERROR_INVARIANT
    );
    scenario->second_caller->ipc_inbound_message.reply_token = scenario->saved_token;
    scenario->saved_source = scenario->second_caller->ipc_inbound_message.source;
    scenario->second_caller->ipc_inbound_message.source =
        fixture->endpoints[REPLY_PROCESS_RESERVED_SERVER];
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_ERROR_INVARIANT
    );
    scenario->second_caller->ipc_inbound_message.source = scenario->saved_source;

    EXPECT_TRUE(
        micros_thread_scheduler_hold(
            &fixture->objects,
            fixture->primary_threads[REPLY_PROCESS_OTHER_SERVER]
        ) == MICROS_KERNEL_OBJECT_OK
        && ipc_reply_test_expect_reply_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_REPLY_TOKEN,
            fixture->primary_threads[REPLY_PROCESS_OTHER_SERVER],
            scenario->second_token,
            &scenario->second_reply
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_reply(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[REPLY_PROCESS_SERVER],
            scenario->first_token,
            &scenario->first_reply
        )
    );
    scenario->expected = ipc_reply_test_canonical_reply(
        &scenario->first_reply_snapshot,
        fixture->endpoints[REPLY_PROCESS_SERVER]
    );
    EXPECT_TRUE(
        scenario->first_caller->runtime_flags == 0
        && scenario->first_caller->ready_linked
        && scenario->first_caller->ipc_delivery_pending
        && scenario->first_caller->ipc_receive_buffer
            == (uintptr_t)&scenario->first_request
        && scenario->first_caller->ipc_reply_token == 0
        && scenario->first_caller->ipc_reply_callee == 0
        && memcmp(
            &scenario->first_caller->ipc_inbound_message,
            &scenario->expected,
            sizeof(scenario->expected)
        ) == 0
        && memcmp(
            &scenario->first_reply,
            &scenario->first_reply_snapshot,
            sizeof(scenario->first_reply)
        ) == 0
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

bool test_reply_routes_exact_tokens_without_send_target(struct ipc_reply_test_context *fixture)
{
    struct reply_routes_exact_tokens_without_send_target_case scenario;

    scenario.first_request = ipc_reply_test_message_pattern(
        UINT32_C(0x11111111), UINT32_C(0x5101), UINT64_C(0x1111111111111111), UINT8_C(0x10)
    );
    scenario.second_request = ipc_reply_test_message_pattern(
        UINT32_C(0x22222222), UINT32_C(0x5102), UINT64_C(0x2222222222222222), UINT8_C(0x20)
    );
    scenario.first_reply = ipc_reply_test_message_pattern(
        UINT32_C(0xaaaaaaaa), UINT32_C(0x5201), UINT64_C(0xaaaaaaaaaaaaaaaa), UINT8_C(0x30)
    );
    scenario.second_reply = ipc_reply_test_message_pattern(
        UINT32_C(0xbbbbbbbb), UINT32_C(0x5202), UINT64_C(0xbbbbbbbbbbbbbbbb), UINT8_C(0x40)
    );
    scenario.first_reply_snapshot = scenario.first_reply;
    scenario.second_reply_snapshot = scenario.second_reply;
    return (
        stage_exact_calls_and_reject_foreign_rights(fixture, &scenario)
        && reply_to_second_exact_caller(fixture, &scenario)
        && validate_reply_shape_and_complete_first_caller(fixture, &scenario)
    );
}

bool test_reply_preserves_independent_caller_flags(struct ipc_reply_test_context *fixture)
{
    struct micros_ipc_message request = ipc_reply_test_message_pattern(
        0,
        UINT32_C(0x5301),
        0,
        UINT8_C(0x50)
    );
    struct micros_ipc_message reply = ipc_reply_test_message_pattern(
        UINT32_C(0xcccccccc),
        UINT32_C(0x5302),
        UINT64_C(0xcccccccccccccccc),
        UINT8_C(0x60)
    );
    struct micros_ipc_message expected;
    struct micros_thread *caller;
    uint64_t reply_token;

    EXPECT_TRUE(
        ipc_reply_test_setup_reply_fixture(fixture)
        && ipc_reply_test_prepare_delivered_call(
            fixture,
            fixture->primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &request,
            UINT64_C(0x61001000),
            &reply_token
        )
        && micros_thread_scheduler_hold(
            &fixture->objects,
            fixture->primary_threads[REPLY_PROCESS_CLIENT]
        ) == MICROS_KERNEL_OBJECT_OK
    );
    caller = &fixture->objects.threads[
        fixture->primary_threads[REPLY_PROCESS_CLIENT].slot
    ];
    EXPECT_TRUE(
        caller->runtime_flags
            == (
                MICROS_THREAD_RTS_INACTIVE
                | MICROS_THREAD_RTS_IPC_REPLY
            )
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_reply(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[REPLY_PROCESS_SERVER],
            reply_token,
            &reply
        )
    );
    expected = ipc_reply_test_canonical_reply(
        &reply,
        fixture->endpoints[REPLY_PROCESS_SERVER]
    );
    EXPECT_TRUE(
        caller->runtime_flags == MICROS_THREAD_RTS_INACTIVE
        && !caller->ready_linked
        && caller->ipc_delivery_pending
        && caller->ipc_reply_token == 0
        && caller->ipc_reply_callee == 0
        && memcmp(
            &caller->ipc_inbound_message,
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
