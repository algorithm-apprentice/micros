#include "ipc_notify_test_internal.h"

bool test_reply_receive_pending_failure_is_atomic(struct ipc_notify_test_context *fixture)
{
    struct micros_ipc_message request = ipc_notify_test_message_pattern(
        0,
        UINT32_C(0x4401),
        0,
        UINT8_C(0x90)
    );
    struct micros_ipc_message reply = ipc_notify_test_message_pattern(
        0,
        UINT32_C(0x4402),
        0,
        UINT8_C(0xa0)
    );
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;
    struct micros_thread *server;
    uint64_t reply_token;
    const uint64_t event_mask = UINT64_C(0x0000000000000400);

    EXPECT_TRUE(ipc_notify_test_setup_notify_fixture(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_SERVER],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x6000c000)
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
    server = &fixture->objects.threads[
        fixture->primary_threads[NOTIFY_PROCESS_SERVER].slot
    ];
    reply_token = server->ipc_inbound_message.reply_token;
    EXPECT_TRUE(
        reply_token != 0
        && ipc_notify_test_consume_staged_and_hold(
            fixture,
            fixture->primary_threads[NOTIFY_PROCESS_SERVER],
            NULL
        )
        && ipc_notify_test_make_reply_wait_current(
            fixture,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION]
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
            fixture->endpoints[NOTIFY_PROCESS_SERVER],
            event_mask
        )
    );
    registry_snapshot = fixture->registry;
    objects_snapshot = fixture->objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_STATE,
        micros_ipc_reply_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_SERVER],
            reply_token,
            &reply,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x6000d000)
        )
    );
    EXPECT_TRUE(
        ipc_notify_test_state_is_unchanged(fixture, &registry_snapshot, &objects_snapshot)
        && fixture->registry.endpoints[
            fixture->processes[NOTIFY_PROCESS_SERVER].slot
        ].pending_events[
            fixture->processes[NOTIFY_PROCESS_SOURCE_LOW].slot
        ] == event_mask
        && fixture->objects.threads[
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION].slot
        ].ipc_reply_token == reply_token
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

struct notification_validators_and_close_preflight_case {
    struct micros_endpoint_registry held_registry;
    struct micros_kernel_objects held_objects;
    struct micros_endpoint_registry pending_registry;
    struct micros_kernel_objects pending_objects;
    struct micros_endpoint_registry staged_registry;
    struct micros_kernel_objects staged_objects;
    struct micros_thread *receiver;
    uint64_t event_mask;
};

static bool check_pending_notification_close_dependencies(
    struct ipc_notify_test_context *fixture,
    struct notification_validators_and_close_preflight_case *scenario
)
{
    EXPECT_TRUE(ipc_notify_test_setup_notify_fixture(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            scenario->event_mask
        )
    );
    EXPECT_TRUE(
        ipc_notify_test_hold_runnable_thread(
            fixture,
            fixture->primary_threads[NOTIFY_PROCESS_SOURCE_LOW]
        )
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    scenario->pending_registry = fixture->registry;
    scenario->pending_objects = fixture->objects;
    EXPECT_ENDPOINT_ERROR(
        MICROS_ENDPOINT_ERROR_STATE,
        micros_endpoint_close(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[NOTIFY_PROCESS_SOURCE_LOW]
        )
    );
    EXPECT_TRUE(ipc_notify_test_state_is_unchanged(
        fixture,
        &scenario->pending_registry,
        &scenario->pending_objects
    ));
    EXPECT_ENDPOINT_ERROR(
        MICROS_ENDPOINT_ERROR_STATE,
        micros_endpoint_close(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION]
        )
    );
    EXPECT_TRUE(ipc_notify_test_state_is_unchanged(
        fixture,
        &scenario->pending_registry,
        &scenario->pending_objects
    ));

    return true;
}

static bool reject_pending_event_corruption_then_stage_delivery(
    struct ipc_notify_test_context *fixture,
    struct notification_validators_and_close_preflight_case *scenario
)
{
#define EXPECT_PENDING_CORRUPTION(statement) \
    do { \
        fixture->registry = scenario->pending_registry; \
        fixture->objects = scenario->pending_objects; \
        statement; \
        EXPECT_ENDPOINT_ERROR( \
            MICROS_ENDPOINT_ERROR_INVARIANT, \
            micros_endpoint_registry_validate_objects( \
                &fixture->registry, \
                &fixture->objects \
            ) \
        ); \
    } while (false)

    EXPECT_PENDING_CORRUPTION(
        fixture->registry.endpoints[
            fixture->processes[NOTIFY_PROCESS_DESTINATION].slot
        ].pending_notification_sources = 0
    );
    EXPECT_PENDING_CORRUPTION(
        fixture->registry.endpoints[
            fixture->processes[NOTIFY_PROCESS_DESTINATION].slot
        ].pending_events[
            fixture->processes[NOTIFY_PROCESS_SOURCE_LOW].slot
        ] = 0
    );
    EXPECT_PENDING_CORRUPTION(
        fixture->registry.endpoints[
            fixture->processes[NOTIFY_PROCESS_SOURCE_LOW].slot
        ].state = MICROS_ENDPOINT_STATE_RESERVED
    );
    EXPECT_PENDING_CORRUPTION(
        fixture->registry.endpoints[
            fixture->processes[NOTIFY_PROCESS_DESTINATION].slot
        ].pending_notification_sources |= UINT64_C(1) << 63;
        fixture->registry.endpoints[
            fixture->processes[NOTIFY_PROCESS_DESTINATION].slot
        ].pending_events[63] = 1
    );

#undef EXPECT_PENDING_CORRUPTION

    fixture->registry = scenario->pending_registry;
    fixture->objects = scenario->pending_objects;
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
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    scenario->staged_registry = fixture->registry;
    scenario->staged_objects = fixture->objects;
    scenario->receiver = &fixture->objects.threads[
        fixture->primary_threads[NOTIFY_PROCESS_DESTINATION].slot
    ];

    return true;
}

static bool reject_staged_notification_corruption(
    struct ipc_notify_test_context *fixture,
    struct notification_validators_and_close_preflight_case *scenario
)
{
#define EXPECT_STAGED_CORRUPTION(statement) \
    do { \
        fixture->registry = scenario->staged_registry; \
        fixture->objects = scenario->staged_objects; \
        scenario->receiver = &fixture->objects.threads[ \
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION].slot \
        ]; \
        statement; \
        EXPECT_ENDPOINT_ERROR( \
            MICROS_ENDPOINT_ERROR_INVARIANT, \
            micros_endpoint_registry_validate_objects( \
                &fixture->registry, \
                &fixture->objects \
            ) \
        ); \
    } while (false)

    EXPECT_STAGED_CORRUPTION(
        scenario->receiver->ipc_inbound_message.type =
            MICROS_IPC_TYPE_KERNEL_MASK | UINT32_C(2)
    );
    EXPECT_STAGED_CORRUPTION(
        scenario->receiver->ipc_inbound_message.reply_token = 1
    );
    EXPECT_STAGED_CORRUPTION(
        memset(scenario->receiver->ipc_inbound_message.payload, 0, 8)
    );
    EXPECT_STAGED_CORRUPTION(
        scenario->receiver->ipc_inbound_message.payload[8] = 1
    );
    EXPECT_STAGED_CORRUPTION(
        scenario->receiver->ipc_inbound_message.source +=
            UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS
    );

#undef EXPECT_STAGED_CORRUPTION

    return true;
}

static bool reject_close_until_staged_event_is_drained(
    struct ipc_notify_test_context *fixture,
    struct notification_validators_and_close_preflight_case *scenario
)
{
    fixture->registry = scenario->staged_registry;
    fixture->objects = scenario->staged_objects;
    EXPECT_TRUE(
        ipc_notify_test_hold_runnable_thread(
            fixture,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION]
        )
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    scenario->held_registry = fixture->registry;
    scenario->held_objects = fixture->objects;
    EXPECT_ENDPOINT_ERROR(
        MICROS_ENDPOINT_ERROR_STATE,
        micros_endpoint_close(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION]
        )
    );
    EXPECT_TRUE(ipc_notify_test_state_is_unchanged(
        fixture,
        &scenario->held_registry,
        &scenario->held_objects
    ));

    scenario->receiver = &fixture->objects.threads[
        fixture->primary_threads[NOTIFY_PROCESS_DESTINATION].slot
    ];
    EXPECT_TRUE(
        ipc_notify_test_clear_staged_delivery(
            fixture,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            NULL
        )
        && scenario->receiver->runtime_flags == MICROS_THREAD_RTS_INACTIVE
        && micros_thread_ipc_state_is_clear(scenario->receiver)
    );
    EXPECT_ENDPOINT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_close(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION]
        )
    );
    return true;
}

bool test_notification_validators_and_close_preflight(struct ipc_notify_test_context *fixture)
{
    struct notification_validators_and_close_preflight_case scenario;

    scenario.event_mask = UINT64_C(0x0102030405060708);
    return (
        check_pending_notification_close_dependencies(fixture, &scenario)
        && reject_pending_event_corruption_then_stage_delivery(fixture, &scenario)
        && reject_staged_notification_corruption(fixture, &scenario)
        && reject_close_until_staged_event_is_drained(fixture, &scenario)
    );
}

bool test_notify_rejections_preserve_state(struct ipc_notify_test_context *fixture)
{
    micros_endpoint_t stale_destination;

    EXPECT_TRUE(ipc_notify_test_setup_notify_fixture(fixture));
    stale_destination =
        fixture->endpoints[NOTIFY_PROCESS_DESTINATION]
        + (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS);
    EXPECT_TRUE(ipc_notify_test_expect_notify_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_ARGUMENT,
        fixture->primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
        fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
        0
    ));
    EXPECT_TRUE(ipc_notify_test_expect_notify_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_UNAUTHORIZED,
        fixture->primary_threads[NOTIFY_PROCESS_NO_NOTIFY],
        fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
        1
    ));
    EXPECT_TRUE(ipc_notify_test_expect_notify_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_UNAUTHORIZED,
        fixture->primary_threads[NOTIFY_PROCESS_ISOLATED_NOTIFY],
        fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
        1
    ));
    EXPECT_TRUE(ipc_notify_test_expect_notify_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_STATE,
        fixture->primary_threads[NOTIFY_PROCESS_RESERVED],
        fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
        1
    ));
    EXPECT_TRUE(ipc_notify_test_expect_notify_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_STATE,
        fixture->primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
        fixture->endpoints[NOTIFY_PROCESS_RESERVED],
        1
    ));
    EXPECT_TRUE(ipc_notify_test_expect_notify_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_DEAD_ENDPOINT,
        fixture->primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
        stale_destination,
        1
    ));

    EXPECT_TRUE(
        ipc_notify_test_setup_notify_fixture(fixture)
        && ipc_notify_test_make_current_held(
            fixture,
            fixture->primary_threads[NOTIFY_PROCESS_SOURCE_LOW]
        )
        && ipc_notify_test_expect_notify_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_STATE,
            fixture->primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            1
        )
    );

    EXPECT_TRUE(ipc_notify_test_setup_notify_fixture(fixture));
    fixture->objects.threads[
        fixture->primary_threads[NOTIFY_PROCESS_SOURCE_LOW].slot
    ].owner.slot = MICROS_PROCESS_CAPACITY;
    EXPECT_TRUE(ipc_notify_test_expect_notify_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_INVARIANT,
        fixture->primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
        fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
        1
    ));
    return true;
}

bool test_notify_scheduler_failure_is_atomic(struct ipc_notify_test_context *fixture)
{
    EXPECT_TRUE(ipc_notify_test_setup_notify_fixture(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60009000)
        )
    );
    EXPECT_TRUE(
        ipc_notify_test_make_single_receiver_current_blocked(
            fixture,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION]
        )
        && ipc_notify_test_expect_notify_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_STATE,
            fixture->primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            UINT64_C(0x80)
        )
    );
    return true;
}
