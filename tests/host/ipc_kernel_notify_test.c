#include "ipc_notify_test_internal.h"

bool test_kernel_notify_coalesces_and_any_receives(struct ipc_notify_test_context *fixture)
{
    struct micros_endpoint_record *destination;
    struct micros_thread *receiver;
    uint64_t zero_events[MICROS_PROCESS_CAPACITY] = {0};
    const uint64_t first_mask = UINT64_C(0x0000000000000015);
    const uint64_t second_mask = UINT64_C(0x800000000000002a);
    const uint64_t combined_mask = first_mask | second_mask;

    EXPECT_TRUE(ipc_notify_test_setup_notify_fixture(fixture));
    destination = &fixture->registry.endpoints[
        fixture->processes[NOTIFY_PROCESS_DESTINATION].slot
    ];
    receiver = &fixture->objects.threads[
        fixture->primary_threads[NOTIFY_PROCESS_DESTINATION].slot
    ];

    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_inject_kernel_notification(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            first_mask
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_inject_kernel_notification(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            second_mask
        )
    );
    EXPECT_TRUE(
        destination->pending_kernel_events == combined_mask
        && destination->pending_notification_sources == 0
        && memcmp(
            destination->pending_events,
            zero_events,
            sizeof(destination->pending_events)
        ) == 0
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x6000e000)
        )
    );
    EXPECT_TRUE(
        destination->pending_kernel_events == 0
        && receiver->runtime_flags == 0
        && receiver->ready_linked
        && receiver->ipc_delivery_pending
        && ipc_notify_test_notification_message_matches(
            &receiver->ipc_inbound_message,
            MICROS_ENDPOINT_NONE,
            combined_mask
        )
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );

    return true;
}

bool test_kernel_notify_wakes_only_any_receiver(struct ipc_notify_test_context *fixture)
{
    struct micros_endpoint_record *destination;
    struct micros_thread *receiver;
    const uint64_t event_mask = UINT64_C(0x0000000000000080);

    EXPECT_TRUE(ipc_notify_test_setup_notify_fixture(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x6000f000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_inject_kernel_notification(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            event_mask
        )
    );
    destination = &fixture->registry.endpoints[
        fixture->processes[NOTIFY_PROCESS_DESTINATION].slot
    ];
    receiver = &fixture->objects.threads[
        fixture->primary_threads[NOTIFY_PROCESS_DESTINATION].slot
    ];
    EXPECT_TRUE(
        destination->pending_kernel_events == 0
        && ipc_notify_test_thread_handle_is_zero(destination->receiver_head)
        && ipc_notify_test_thread_handle_is_zero(destination->receiver_tail)
        && receiver->runtime_flags == 0
        && receiver->ready_linked
        && receiver->ipc_delivery_pending
        && ipc_notify_test_notification_message_matches(
            &receiver->ipc_inbound_message,
            MICROS_ENDPOINT_NONE,
            event_mask
        )
    );

    EXPECT_TRUE(ipc_notify_test_setup_notify_fixture(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_inject_kernel_notification(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            event_mask
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            fixture->endpoints[NOTIFY_PROCESS_SOURCE_LOW],
            UINT64_C(0x60010000)
        )
    );
    destination = &fixture->registry.endpoints[
        fixture->processes[NOTIFY_PROCESS_DESTINATION].slot
    ];
    receiver = &fixture->objects.threads[
        fixture->primary_threads[NOTIFY_PROCESS_DESTINATION].slot
    ];
    EXPECT_TRUE(
        destination->pending_kernel_events == event_mask
        && ipc_notify_test_thread_handles_equal(
            destination->receiver_head,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION]
        )
        && ipc_notify_test_thread_handles_equal(
            destination->receiver_tail,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION]
        )
        && receiver->runtime_flags == MICROS_THREAD_RTS_IPC_RECEIVE
        && !receiver->ipc_delivery_pending
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

struct kernel_notify_prepare_commit_is_retained_case {
    struct micros_ipc_kernel_notification_plan plan;
    struct micros_ipc_kernel_notification_plan sentinel;
    struct micros_ipc_kernel_notification_plan zero_plan;
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;
    struct micros_endpoint_record *destination;
    struct micros_thread *receiver;
    uint64_t event_mask;
};

static bool check_prepared_coalescing_plan_and_commit(
    struct ipc_notify_test_context *fixture,
    struct kernel_notify_prepare_commit_is_retained_case *scenario
)
{
    EXPECT_TRUE(ipc_notify_test_setup_notify_fixture(fixture));
    memset(&scenario->sentinel, 0xa5, sizeof(scenario->sentinel));
    scenario->plan = scenario->sentinel;
    scenario->registry_snapshot = fixture->registry;
    scenario->objects_snapshot = fixture->objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_ARGUMENT,
        micros_ipc_prepare_kernel_notification(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            0,
            &scenario->plan
        )
    );
    EXPECT_TRUE(
        memcmp(&scenario->plan, &scenario->sentinel, sizeof(scenario->plan)) == 0
        && ipc_notify_test_state_is_unchanged(
            fixture,
            &scenario->registry_snapshot,
            &scenario->objects_snapshot
        )
    );

    scenario->plan = scenario->sentinel;
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_prepare_kernel_notification(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            scenario->event_mask,
            &scenario->plan
        )
    );
    EXPECT_TRUE(
        scenario->plan.active
        && !scenario->plan.deliver_to_receiver
        && ipc_notify_test_state_is_unchanged(
            fixture,
            &scenario->registry_snapshot,
            &scenario->objects_snapshot
        )
    );
    micros_ipc_commit_kernel_notification_prevalidated(
        &fixture->registry,
        &fixture->objects,
        &scenario->plan
    );
    scenario->destination = &fixture->registry.endpoints[
        fixture->processes[NOTIFY_PROCESS_DESTINATION].slot
    ];
    EXPECT_TRUE(
        memcmp(&scenario->plan, &scenario->zero_plan, sizeof(scenario->plan)) == 0
        && scenario->destination->pending_kernel_events == scenario->event_mask
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );

    return true;
}

static bool check_prepared_receiver_plan_and_commit(
    struct ipc_notify_test_context *fixture,
    struct kernel_notify_prepare_commit_is_retained_case *scenario
)
{
    EXPECT_TRUE(ipc_notify_test_setup_notify_fixture(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60010800)
        )
    );
    scenario->registry_snapshot = fixture->registry;
    scenario->objects_snapshot = fixture->objects;
    scenario->plan = scenario->sentinel;
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_prepare_kernel_notification(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            scenario->event_mask,
            &scenario->plan
        )
    );
    EXPECT_TRUE(
        scenario->plan.active
        && scenario->plan.deliver_to_receiver
        && ipc_notify_test_state_is_unchanged(
            fixture,
            &scenario->registry_snapshot,
            &scenario->objects_snapshot
        )
    );
    micros_ipc_commit_kernel_notification_prevalidated(
        &fixture->registry,
        &fixture->objects,
        &scenario->plan
    );
    scenario->destination = &fixture->registry.endpoints[
        fixture->processes[NOTIFY_PROCESS_DESTINATION].slot
    ];
    scenario->receiver = &fixture->objects.threads[
        fixture->primary_threads[NOTIFY_PROCESS_DESTINATION].slot
    ];
    EXPECT_TRUE(
        memcmp(&scenario->plan, &scenario->zero_plan, sizeof(scenario->plan)) == 0
        && scenario->destination->pending_kernel_events == 0
        && ipc_notify_test_thread_handle_is_zero(scenario->destination->receiver_head)
        && ipc_notify_test_thread_handle_is_zero(scenario->destination->receiver_tail)
        && scenario->receiver->runtime_flags == 0
        && scenario->receiver->ready_linked
        && scenario->receiver->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && scenario->receiver->ipc_receive_source == 0
        && scenario->receiver->ipc_delivery_pending
        && ipc_notify_test_notification_message_matches(
            &scenario->receiver->ipc_inbound_message,
            MICROS_ENDPOINT_NONE,
            scenario->event_mask
        )
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );

    return true;
}

static bool reject_prepared_notification_current_receiver(
    struct ipc_notify_test_context *fixture,
    struct kernel_notify_prepare_commit_is_retained_case *scenario
)
{
    EXPECT_TRUE(ipc_notify_test_setup_notify_fixture(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60011000)
        )
    );
    EXPECT_TRUE(
        ipc_notify_test_make_single_receiver_current_blocked(
            fixture,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION]
        )
    );
    scenario->registry_snapshot = fixture->registry;
    scenario->objects_snapshot = fixture->objects;
    scenario->plan = scenario->sentinel;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_STATE,
        micros_ipc_prepare_kernel_notification(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            scenario->event_mask,
            &scenario->plan
        )
    );
    EXPECT_TRUE(
        memcmp(&scenario->plan, &scenario->sentinel, sizeof(scenario->plan)) == 0
        && ipc_notify_test_state_is_unchanged(
            fixture,
            &scenario->registry_snapshot,
            &scenario->objects_snapshot
        )
    );
    return true;
}

bool test_kernel_notify_prepare_commit_is_retained(struct ipc_notify_test_context *fixture)
{
    struct kernel_notify_prepare_commit_is_retained_case scenario;

    scenario.zero_plan = (struct micros_ipc_kernel_notification_plan){0};
    scenario.event_mask = UINT64_C(0x0000000000000400);
    return (
        check_prepared_coalescing_plan_and_commit(fixture, &scenario)
        && check_prepared_receiver_plan_and_commit(fixture, &scenario)
        && reject_prepared_notification_current_receiver(fixture, &scenario)
    );
}

bool test_kernel_notify_rejections_are_atomic(struct ipc_notify_test_context *fixture)
{
    micros_endpoint_t stale_destination;

    EXPECT_TRUE(ipc_notify_test_setup_notify_fixture(fixture));
    stale_destination =
        fixture->endpoints[NOTIFY_PROCESS_DESTINATION]
        + (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS);
    EXPECT_TRUE(ipc_notify_test_expect_kernel_notify_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_ARGUMENT,
        fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
        0
    ));
    EXPECT_TRUE(ipc_notify_test_expect_kernel_notify_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_ARGUMENT,
        MICROS_ENDPOINT_NONE,
        1
    ));
    EXPECT_TRUE(ipc_notify_test_expect_kernel_notify_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_ARGUMENT,
        MICROS_ENDPOINT_ANY,
        1
    ));
    EXPECT_TRUE(ipc_notify_test_expect_kernel_notify_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_STATE,
        fixture->endpoints[NOTIFY_PROCESS_RESERVED],
        1
    ));
    EXPECT_TRUE(ipc_notify_test_expect_kernel_notify_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_DEAD_ENDPOINT,
        stale_destination,
        1
    ));

    EXPECT_TRUE(ipc_notify_test_setup_notify_fixture(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60011000)
        )
    );
    EXPECT_TRUE(
        ipc_notify_test_make_single_receiver_current_blocked(
            fixture,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION]
        )
        && ipc_notify_test_expect_kernel_notify_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_STATE,
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            UINT64_C(0x100)
        )
    );
    return true;
}

struct kernel_notify_validation_and_close_rules_case {
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;
    struct micros_thread *receiver;
    uint64_t event_mask;
};

static bool check_staged_kernel_event_shape_and_close(
    struct ipc_notify_test_context *fixture,
    struct kernel_notify_validation_and_close_rules_case *scenario
)
{
    EXPECT_TRUE(ipc_notify_test_setup_notify_fixture(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60012000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_inject_kernel_notification(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            scenario->event_mask
        )
    );
    scenario->receiver = &fixture->objects.threads[
        fixture->primary_threads[NOTIFY_PROCESS_DESTINATION].slot
    ];
    scenario->registry_snapshot = fixture->registry;
    scenario->objects_snapshot = fixture->objects;
    scenario->receiver->ipc_inbound_message.type = UINT32_C(0x2001);
    EXPECT_ENDPOINT_ERROR(
        MICROS_ENDPOINT_ERROR_INVARIANT,
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        )
    );
    fixture->registry = scenario->registry_snapshot;
    fixture->objects = scenario->objects_snapshot;
    EXPECT_TRUE(
        ipc_notify_test_hold_runnable_thread(
            fixture,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION]
        )
    );
    scenario->registry_snapshot = fixture->registry;
    scenario->objects_snapshot = fixture->objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_STATE,
        micros_ipc_endpoint_close(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION]
        )
    );
    EXPECT_TRUE(ipc_notify_test_state_is_unchanged(
        fixture,
        &scenario->registry_snapshot,
        &scenario->objects_snapshot
    ));
    EXPECT_TRUE(
        ipc_notify_test_clear_staged_delivery(
            fixture,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            NULL
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_endpoint_close(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION]
        )
    );

    return true;
}

static bool close_pending_kernel_event_without_delivery(
    struct ipc_notify_test_context *fixture,
    struct kernel_notify_validation_and_close_rules_case *scenario
)
{
    EXPECT_TRUE(ipc_notify_test_setup_notify_fixture(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_inject_kernel_notification(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            scenario->event_mask
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_endpoint_close(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION]
        )
    );
    EXPECT_TRUE(
        fixture->registry.endpoints[
            fixture->processes[NOTIFY_PROCESS_DESTINATION].slot
        ].state == MICROS_ENDPOINT_STATE_FREE
        && fixture->registry.endpoints[
            fixture->processes[NOTIFY_PROCESS_DESTINATION].slot
        ].pending_kernel_events == 0
    );

    return true;
}

static bool reject_kernel_events_in_reserved_or_matchable_endpoint(
    struct ipc_notify_test_context *fixture,
    struct kernel_notify_validation_and_close_rules_case *scenario
)
{
    EXPECT_TRUE(ipc_notify_test_setup_notify_fixture(fixture));
    fixture->registry.endpoints[
        fixture->processes[NOTIFY_PROCESS_RESERVED].slot
    ].pending_kernel_events = scenario->event_mask;
    EXPECT_ENDPOINT_ERROR(
        MICROS_ENDPOINT_ERROR_INVARIANT,
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        )
    );

    EXPECT_TRUE(ipc_notify_test_setup_notify_fixture(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60013000)
        )
    );
    fixture->registry.endpoints[
        fixture->processes[NOTIFY_PROCESS_DESTINATION].slot
    ].pending_kernel_events = scenario->event_mask;
    EXPECT_ENDPOINT_ERROR(
        MICROS_ENDPOINT_ERROR_INVARIANT,
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        )
    );
    return true;
}

bool test_kernel_notify_validation_and_close_rules(struct ipc_notify_test_context *fixture)
{
    struct kernel_notify_validation_and_close_rules_case scenario;

    scenario.event_mask = UINT64_C(0x0000000000000200);
    return (
        check_staged_kernel_event_shape_and_close(fixture, &scenario)
        && close_pending_kernel_event_without_delivery(fixture, &scenario)
        && reject_kernel_events_in_reserved_or_matchable_endpoint(fixture, &scenario)
    );
}

bool test_staged_kernel_notification_is_detected(struct ipc_notify_test_context *fixture)
{
    const struct micros_thread *destination;

    EXPECT_TRUE(
        ipc_notify_test_setup_notify_fixture(fixture)
        && micros_thread_resolve(
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            &destination
        ) == MICROS_KERNEL_OBJECT_OK
        && !micros_ipc_thread_has_staged_kernel_notification(
            destination
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x6100a000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_inject_kernel_notification(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[NOTIFY_PROCESS_DESTINATION],
            UINT64_C(0x0000000000000400)
        )
    );
    EXPECT_TRUE(
        micros_thread_resolve(
            &fixture->objects,
            fixture->primary_threads[NOTIFY_PROCESS_DESTINATION],
            &destination
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_ipc_thread_has_staged_kernel_notification(
            destination
        )
    );
    return true;
}
