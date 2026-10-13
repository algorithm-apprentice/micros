#include "ipc_close_test_internal.h"

struct close_cancels_complete_transaction_case {
    struct micros_thread_handle closing_extra[CLOSE_EXTRA_THREAD_COUNT];
    struct micros_thread_handle closing_threads[CLOSE_EXTRA_THREAD_COUNT + 1];
    struct micros_thread_handle affected_threads[4];
    struct micros_ipc_message messages[5];
    struct micros_thread any_snapshot;
    struct micros_endpoint_record any_endpoint_snapshot;
    uint64_t last_reply_token;
    size_t index;
};

static bool prepare_foreign_delivered_and_queued_calls(
    struct ipc_close_test_context *fixture,
    struct close_cancels_complete_transaction_case *scenario
)
{
    EXPECT_TRUE(ipc_close_test_setup_fixture(fixture, CLOSE_PROCESS_COUNT, 6));
    scenario->messages[0] = ipc_close_test_message_pattern(UINT32_C(0x1001), UINT8_C(0x10));
    scenario->messages[1] = ipc_close_test_message_pattern(UINT32_C(0x1002), UINT8_C(0x20));
    scenario->messages[2] = ipc_close_test_message_pattern(UINT32_C(0x1003), UINT8_C(0x30));
    scenario->messages[3] = ipc_close_test_message_pattern(UINT32_C(0x1004), UINT8_C(0x40));
    scenario->messages[4] = ipc_close_test_message_pattern(UINT32_C(0x1005), UINT8_C(0x50));

    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CLOSE_PROCESS_CLOSING],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60001000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CLOSE_PROCESS_FOREIGN_CALL_DELIVERED],
            fixture->endpoints[CLOSE_PROCESS_CLOSING],
            &scenario->messages[0],
            (uintptr_t)&scenario->messages[0]
        )
    );
    EXPECT_TRUE(
        ipc_close_test_hold_thread_for_close(
            fixture,
            fixture->primary_threads[CLOSE_PROCESS_CLOSING]
        )
        && ipc_close_test_clear_staged_delivery(
            fixture,
            fixture->primary_threads[CLOSE_PROCESS_CLOSING]
        )
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );

    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CLOSE_PROCESS_FOREIGN_SEND],
            fixture->endpoints[CLOSE_PROCESS_CLOSING],
            &scenario->messages[1]
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CLOSE_PROCESS_FOREIGN_CALL_QUEUED],
            fixture->endpoints[CLOSE_PROCESS_CLOSING],
            &scenario->messages[2],
            (uintptr_t)&scenario->messages[2]
        )
    );
    return true;
}

static bool prepare_owned_closing_thread_dependencies(
    struct ipc_close_test_context *fixture,
    struct close_cancels_complete_transaction_case *scenario
)
{
    for (scenario->index = 0; scenario->index < CLOSE_EXTRA_THREAD_COUNT; ++scenario->index) {
        EXPECT_TRUE(
            ipc_close_test_prepare_thread(
                fixture,
                CLOSE_PROCESS_CLOSING,
                MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
                &scenario->closing_extra[scenario->index]
            )
        );
    }
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &fixture->registry,
            &fixture->objects,
            scenario->closing_extra[0],
            fixture->endpoints[CLOSE_PROCESS_FOREIGN_DESTINATION],
            &scenario->messages[3]
        )
    );
    EXPECT_TRUE(ipc_close_test_hold_thread_for_close(fixture, scenario->closing_extra[0]));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &fixture->registry,
            &fixture->objects,
            scenario->closing_extra[1],
            fixture->endpoints[CLOSE_PROCESS_FOREIGN_DESTINATION],
            &scenario->messages[4],
            (uintptr_t)&scenario->messages[4]
        )
    );
    EXPECT_TRUE(ipc_close_test_hold_thread_for_close(fixture, scenario->closing_extra[1]));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            scenario->closing_extra[3],
            fixture->endpoints[CLOSE_PROCESS_WAIT_SOURCE],
            UINT64_C(0x60002000)
        )
    );
    EXPECT_TRUE(ipc_close_test_hold_thread_for_close(fixture, scenario->closing_extra[3]));

    return true;
}

static bool prepare_close_receivers_events_and_unrelated_snapshot(
    struct ipc_close_test_context *fixture,
    struct close_cancels_complete_transaction_case *scenario
)
{
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CLOSE_PROCESS_FOREIGN_SPECIFIC],
            fixture->endpoints[CLOSE_PROCESS_CLOSING],
            UINT64_C(0x60003000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CLOSE_PROCESS_FOREIGN_ANY],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60004000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &fixture->registry,
            &fixture->objects,
            scenario->closing_extra[2],
            fixture->endpoints[CLOSE_PROCESS_NOTIFY_DESTINATION],
            UINT64_C(0x55)
        )
    );
    EXPECT_TRUE(ipc_close_test_hold_thread_for_close(fixture, scenario->closing_extra[2]));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CLOSE_PROCESS_NOTIFY_SOURCE],
            fixture->endpoints[CLOSE_PROCESS_CLOSING],
            UINT64_C(0xaa)
        )
    );

    scenario->any_snapshot = fixture->objects.threads[
        fixture->primary_threads[CLOSE_PROCESS_FOREIGN_ANY].slot
    ];
    scenario->any_endpoint_snapshot = fixture->registry.endpoints[
        fixture->processes[CLOSE_PROCESS_FOREIGN_ANY].slot
    ];
    scenario->last_reply_token = fixture->registry.last_reply_token;
    EXPECT_TRUE(
        scenario->last_reply_token != 0
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );

    return true;
}

static bool close_and_check_owned_and_foreign_cancellation(
    struct ipc_close_test_context *fixture,
    struct close_cancels_complete_transaction_case *scenario
)
{
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_endpoint_close(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[CLOSE_PROCESS_CLOSING]
        )
    );

    scenario->closing_threads[0] =
        fixture->primary_threads[CLOSE_PROCESS_CLOSING];
    for (scenario->index = 0; scenario->index < CLOSE_EXTRA_THREAD_COUNT; ++scenario->index) {
        scenario->closing_threads[scenario->index + 1] = scenario->closing_extra[scenario->index];
    }
    for (
        scenario->index = 0;
        scenario->index < sizeof(scenario->closing_threads) / sizeof(scenario->closing_threads[0]);
        ++scenario->index
    ) {
        EXPECT_TRUE(ipc_close_test_closing_thread_is_detached(fixture, scenario->closing_threads[scenario->index]));
    }
    scenario->affected_threads[0] =
        fixture->primary_threads[CLOSE_PROCESS_FOREIGN_SEND];
    scenario->affected_threads[1] =
        fixture->primary_threads[CLOSE_PROCESS_FOREIGN_CALL_QUEUED];
    scenario->affected_threads[2] =
        fixture->primary_threads[CLOSE_PROCESS_FOREIGN_CALL_DELIVERED];
    scenario->affected_threads[3] =
        fixture->primary_threads[CLOSE_PROCESS_FOREIGN_SPECIFIC];
    for (
        scenario->index = 0;
        scenario->index < sizeof(scenario->affected_threads) / sizeof(scenario->affected_threads[0]);
        ++scenario->index
    ) {
        EXPECT_TRUE(
            ipc_close_test_thread_has_dead_endpoint_result(fixture, scenario->affected_threads[scenario->index])
        );
    }
    EXPECT_TRUE(
        memcmp(
            &fixture->objects.threads[
                fixture->primary_threads[CLOSE_PROCESS_FOREIGN_ANY].slot
            ],
            &scenario->any_snapshot,
            sizeof(scenario->any_snapshot)
        ) == 0
        && memcmp(
            &fixture->registry.endpoints[
                fixture->processes[CLOSE_PROCESS_FOREIGN_ANY].slot
            ],
            &scenario->any_endpoint_snapshot,
            sizeof(scenario->any_endpoint_snapshot)
        ) == 0
        && ipc_close_test_endpoint_queue_is_empty(
            &fixture->registry.endpoints[
                fixture->processes[CLOSE_PROCESS_FOREIGN_DESTINATION].slot
            ]
        )
        && (
            fixture->registry.endpoints[
                fixture->processes[CLOSE_PROCESS_NOTIFY_DESTINATION].slot
            ].pending_notification_sources
            & (
                UINT64_C(1)
                << fixture->processes[CLOSE_PROCESS_CLOSING].slot
            )
        ) == 0
        && fixture->registry.endpoints[
            fixture->processes[CLOSE_PROCESS_NOTIFY_DESTINATION].slot
        ].pending_events[fixture->processes[CLOSE_PROCESS_CLOSING].slot]
            == 0
        && fixture->registry.endpoints[
            fixture->processes[CLOSE_PROCESS_CLOSING].slot
        ].state == MICROS_ENDPOINT_STATE_FREE
        && fixture->objects.processes[
            fixture->processes[CLOSE_PROCESS_CLOSING].slot
        ].primary_endpoint == MICROS_PROCESS_ENDPOINT_NONE
        && fixture->objects.processes[
            fixture->processes[CLOSE_PROCESS_CLOSING].slot
        ].privilege_profile == 0
        && fixture->registry.last_reply_token == scenario->last_reply_token
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

bool test_close_cancels_complete_transaction(struct ipc_close_test_context *fixture)
{
    struct close_cancels_complete_transaction_case scenario;

    return (
        prepare_foreign_delivered_and_queued_calls(fixture, &scenario)
        && prepare_owned_closing_thread_dependencies(fixture, &scenario)
        && prepare_close_receivers_events_and_unrelated_snapshot(fixture, &scenario)
        && close_and_check_owned_and_foreign_cancellation(fixture, &scenario)
    );
}

bool test_cascading_close_discards_owned_dead_endpoint_result(struct ipc_close_test_context *fixture)
{
    struct micros_ipc_message first_message =
        ipc_close_test_message_pattern(UINT32_C(0x1701), UINT8_C(0x58));
    struct micros_ipc_message second_message =
        ipc_close_test_message_pattern(UINT32_C(0x1702), UINT8_C(0x60));

    EXPECT_TRUE(ipc_close_test_setup_fixture(fixture, 3, 1));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[1],
            fixture->endpoints[0],
            &first_message
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[2],
            fixture->endpoints[1],
            &second_message
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_endpoint_close(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[0]
        )
    );
    EXPECT_TRUE(
        ipc_close_test_thread_has_dead_endpoint_result(fixture, fixture->primary_threads[1])
        && ipc_close_test_hold_thread_for_close(fixture, fixture->primary_threads[1])
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_endpoint_close(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[1]
        )
    );
    EXPECT_TRUE(
        ipc_close_test_closing_thread_is_detached(fixture, fixture->primary_threads[1])
        && ipc_close_test_thread_has_dead_endpoint_result(fixture, fixture->primary_threads[2])
        && fixture->registry.endpoints[fixture->processes[0].slot].state
            == MICROS_ENDPOINT_STATE_FREE
        && fixture->registry.endpoints[fixture->processes[1].slot].state
            == MICROS_ENDPOINT_STATE_FREE
        && fixture->registry.endpoints[fixture->processes[2].slot].state
            == MICROS_ENDPOINT_STATE_ACTIVE
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

bool test_source_only_close_cancels_queued_sender(struct ipc_close_test_context *fixture)
{
    struct micros_ipc_message message =
        ipc_close_test_message_pattern(UINT32_C(0x1751), UINT8_C(0x5c));

    EXPECT_TRUE(
        ipc_close_test_setup_fixture(fixture, 2, 1)
        && ipc_close_test_hold_thread_for_close(fixture, fixture->primary_threads[0])
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[1],
            fixture->endpoints[0],
            &message
        )
    );
    EXPECT_TRUE(
        fixture->registry.endpoints[fixture->processes[0].slot].sender_head.slot
            == fixture->primary_threads[1].slot
        && fixture->registry.endpoints[fixture->processes[0].slot].sender_tail.slot
            == fixture->primary_threads[1].slot
    );
    fixture->registry.endpoints[fixture->processes[0].slot].state =
        MICROS_ENDPOINT_STATE_SOURCE_ONLY;
    EXPECT_TRUE(
        fixture->registry.endpoints[fixture->processes[0].slot].state
            == MICROS_ENDPOINT_STATE_SOURCE_ONLY
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_endpoint_close(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[0]
        )
    );
    EXPECT_TRUE(
        ipc_close_test_thread_has_dead_endpoint_result(fixture, fixture->primary_threads[1])
        && fixture->registry.endpoints[fixture->processes[0].slot].state
            == MICROS_ENDPOINT_STATE_FREE
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}
