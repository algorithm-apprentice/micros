#include "ipc_close_test_internal.h"

bool test_source_only_notification_dependency_blocks_strict_close(struct ipc_close_test_context *fixture)
{
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;
    struct micros_endpoint_record *destination;
    uint64_t source_bit;

    EXPECT_TRUE(ipc_close_test_setup_fixture(fixture, 2, 1));
    destination =
        &fixture->registry.endpoints[fixture->processes[1].slot];
    source_bit = UINT64_C(1) << fixture->processes[0].slot;
    destination->state = MICROS_ENDPOINT_STATE_SOURCE_ONLY;
    destination->pending_notification_sources = source_bit;
    destination->pending_events[fixture->processes[0].slot] =
        UINT64_C(0x80);
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    registry_snapshot = fixture->registry;
    objects_snapshot = fixture->objects;
    EXPECT_TRUE(
        micros_endpoint_seal_source_only(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[0]
        ) == MICROS_ENDPOINT_ERROR_STATE
        && memcmp(
            &fixture->registry,
            &registry_snapshot,
            sizeof(fixture->registry)
        ) == 0
        && memcmp(
            &fixture->objects,
            &objects_snapshot,
            sizeof(fixture->objects)
        ) == 0
    );

    EXPECT_TRUE(ipc_close_test_setup_fixture(fixture, 2, 1));
    destination =
        &fixture->registry.endpoints[fixture->processes[1].slot];
    source_bit = UINT64_C(1) << fixture->processes[0].slot;
    destination->state = MICROS_ENDPOINT_STATE_SOURCE_ONLY;
    destination->pending_notification_sources = source_bit;
    destination->pending_events[fixture->processes[0].slot] =
        UINT64_C(0x80);
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    registry_snapshot = fixture->registry;
    objects_snapshot = fixture->objects;
    EXPECT_TRUE(
        micros_endpoint_close(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[0]
        ) == MICROS_ENDPOINT_ERROR_STATE
        && memcmp(
            &fixture->registry,
            &registry_snapshot,
            sizeof(fixture->registry)
        ) == 0
        && memcmp(
            &fixture->objects,
            &objects_snapshot,
            sizeof(fixture->objects)
        ) == 0
    );
    return true;
}

struct successful_staged_deliveries_block_close_atomically_case {
    struct micros_ipc_message message;
};

static bool reject_close_with_successful_message_until_drained(
    struct ipc_close_test_context *fixture,
    struct successful_staged_deliveries_block_close_atomically_case *scenario
)
{
    EXPECT_TRUE(ipc_close_test_setup_fixture(fixture, 3, 1));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CLOSE_PROCESS_CLOSING],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60005500)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CLOSE_PROCESS_FOREIGN_SEND],
            fixture->endpoints[CLOSE_PROCESS_CLOSING],
            &scenario->message
        )
    );
    EXPECT_TRUE(
        ipc_close_test_hold_thread_for_close(
            fixture,
            fixture->primary_threads[CLOSE_PROCESS_CLOSING]
        )
        && ipc_close_test_expect_close_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_STATE,
            fixture->endpoints[CLOSE_PROCESS_CLOSING]
        )
        && ipc_close_test_clear_staged_delivery(
            fixture,
            fixture->primary_threads[CLOSE_PROCESS_CLOSING]
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_endpoint_close(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[CLOSE_PROCESS_CLOSING]
        )
    );

    return true;
}

static bool reject_close_with_successful_notification_until_drained(
    struct ipc_close_test_context *fixture
)
{
    EXPECT_TRUE(ipc_close_test_setup_fixture(fixture, 3, 1));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CLOSE_PROCESS_CLOSING],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60005600)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CLOSE_PROCESS_FOREIGN_SEND],
            fixture->endpoints[CLOSE_PROCESS_CLOSING],
            UINT64_C(0x40)
        )
    );
    EXPECT_TRUE(
        ipc_close_test_hold_thread_for_close(
            fixture,
            fixture->primary_threads[CLOSE_PROCESS_CLOSING]
        )
        && ipc_close_test_expect_close_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_STATE,
            fixture->endpoints[CLOSE_PROCESS_CLOSING]
        )
        && ipc_close_test_clear_staged_delivery(
            fixture,
            fixture->primary_threads[CLOSE_PROCESS_CLOSING]
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_endpoint_close(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[CLOSE_PROCESS_CLOSING]
        )
    );

    return true;
}

static bool reject_close_with_successful_no_message_until_drained(
    struct ipc_close_test_context *fixture,
    struct successful_staged_deliveries_block_close_atomically_case *scenario
)
{
    EXPECT_TRUE(ipc_close_test_setup_fixture(fixture, 3, 1));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CLOSE_PROCESS_FOREIGN_SEND],
            fixture->endpoints[CLOSE_PROCESS_CLOSING],
            UINT64_C(0x60005700)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CLOSE_PROCESS_CLOSING],
            fixture->endpoints[CLOSE_PROCESS_FOREIGN_SEND],
            &scenario->message
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_stage_no_message_completion(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CLOSE_PROCESS_CLOSING],
            MICROS_IPC_OK
        )
    );
    EXPECT_TRUE(
        ipc_close_test_hold_thread_for_close(
            fixture,
            fixture->primary_threads[CLOSE_PROCESS_CLOSING]
        )
        && ipc_close_test_expect_close_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_STATE,
            fixture->endpoints[CLOSE_PROCESS_CLOSING]
        )
        && ipc_close_test_clear_staged_delivery(
            fixture,
            fixture->primary_threads[CLOSE_PROCESS_FOREIGN_SEND]
        )
        && ipc_close_test_expect_close_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_STATE,
            fixture->endpoints[CLOSE_PROCESS_CLOSING]
        )
        && ipc_close_test_clear_staged_delivery(
            fixture,
            fixture->primary_threads[CLOSE_PROCESS_CLOSING]
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_endpoint_close(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[CLOSE_PROCESS_CLOSING]
        )
    );
    return true;
}

bool test_successful_staged_deliveries_block_close_atomically(struct ipc_close_test_context *fixture)
{
    struct successful_staged_deliveries_block_close_atomically_case scenario;

    scenario.message = ipc_close_test_message_pattern(UINT32_C(0x1801), UINT8_C(0x68));
    return (
        reject_close_with_successful_message_until_drained(fixture, &scenario)
        && reject_close_with_successful_notification_until_drained(fixture)
        && reject_close_with_successful_no_message_until_drained(fixture, &scenario)
    );
}

struct close_failures_are_atomic_case {
    struct micros_ipc_message message;
    struct micros_process_handle stale_process;
    micros_endpoint_t stale_endpoint;
    struct micros_endpoint_record *closing;
    struct micros_thread *thread;
    uint64_t source_bit;
};

static bool reject_stale_or_runnable_closing_identity(
    struct ipc_close_test_context *fixture,
    struct close_failures_are_atomic_case *scenario
)
{
    EXPECT_TRUE(ipc_close_test_setup_fixture(fixture, 3, 1));
    scenario->stale_process = fixture->processes[CLOSE_PROCESS_CLOSING];
    ++scenario->stale_process.generation;
    EXPECT_TRUE(
        micros_endpoint_pack(scenario->stale_process, &scenario->stale_endpoint)
            == MICROS_ENDPOINT_OK
        && ipc_close_test_expect_close_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_DEAD_ENDPOINT,
            scenario->stale_endpoint
        )
    );

    EXPECT_TRUE(ipc_close_test_setup_fixture(fixture, 3, 1));
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_runtime_flags_unset(
            &fixture->objects,
            fixture->primary_threads[CLOSE_PROCESS_CLOSING],
            MICROS_THREAD_RTS_INACTIVE
        )
    );
    EXPECT_TRUE(ipc_close_test_expect_close_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_STATE,
        fixture->endpoints[CLOSE_PROCESS_CLOSING]
    ));

    return true;
}

static bool reject_sender_and_receiver_queue_generations(
    struct ipc_close_test_context *fixture,
    struct close_failures_are_atomic_case *scenario
)
{
    EXPECT_TRUE(ipc_close_test_setup_fixture(fixture, 3, 1));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CLOSE_PROCESS_FOREIGN_SEND],
            fixture->endpoints[CLOSE_PROCESS_CLOSING],
            &scenario->message
        )
    );
    scenario->closing = &fixture->registry.endpoints[
        fixture->processes[CLOSE_PROCESS_CLOSING].slot
    ];
    ++scenario->closing->sender_head.generation;
    EXPECT_TRUE(ipc_close_test_expect_close_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_INVARIANT,
        fixture->endpoints[CLOSE_PROCESS_CLOSING]
    ));

    EXPECT_TRUE(ipc_close_test_setup_fixture(fixture, 3, 1));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CLOSE_PROCESS_FOREIGN_SEND],
            fixture->endpoints[CLOSE_PROCESS_CLOSING],
            UINT64_C(0x60005800)
        )
    );
    ++fixture->registry.endpoints[
        fixture->processes[CLOSE_PROCESS_FOREIGN_SEND].slot
    ].receiver_head.generation;
    EXPECT_TRUE(ipc_close_test_expect_close_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_INVARIANT,
        fixture->endpoints[CLOSE_PROCESS_CLOSING]
    ));

    return true;
}

static bool reject_reply_token_and_notification_corruption(
    struct ipc_close_test_context *fixture,
    struct close_failures_are_atomic_case *scenario
)
{
    EXPECT_TRUE(ipc_close_test_setup_fixture(fixture, 3, 1));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CLOSE_PROCESS_FOREIGN_SEND],
            fixture->endpoints[CLOSE_PROCESS_CLOSING],
            &scenario->message,
            (uintptr_t)&scenario->message
        )
    );
    scenario->stale_process = fixture->processes[CLOSE_PROCESS_CLOSING];
    ++scenario->stale_process.generation;
    EXPECT_TRUE(
        micros_endpoint_pack(scenario->stale_process, &scenario->stale_endpoint)
            == MICROS_ENDPOINT_OK
    );
    fixture->objects.threads[
        fixture->primary_threads[CLOSE_PROCESS_FOREIGN_SEND].slot
    ].ipc_reply_callee = scenario->stale_endpoint;
    EXPECT_TRUE(ipc_close_test_expect_close_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_INVARIANT,
        fixture->endpoints[CLOSE_PROCESS_CLOSING]
    ));

    EXPECT_TRUE(ipc_close_test_setup_fixture(fixture, 3, 1));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CLOSE_PROCESS_FOREIGN_SEND],
            fixture->endpoints[CLOSE_PROCESS_CLOSING],
            UINT64_C(0x80)
        )
    );
    scenario->closing = &fixture->registry.endpoints[
        fixture->processes[CLOSE_PROCESS_CLOSING].slot
    ];
    scenario->source_bit =
        UINT64_C(1) << fixture->processes[CLOSE_PROCESS_FOREIGN_SEND].slot;
    scenario->closing->pending_notification_sources &= ~scenario->source_bit;
    scenario->closing->pending_events[
        fixture->processes[CLOSE_PROCESS_FOREIGN_SEND].slot
    ] = 0;
    scenario->closing->pending_notification_sources |= UINT64_C(1) << 63;
    scenario->closing->pending_events[63] = UINT64_C(1);
    EXPECT_TRUE(ipc_close_test_expect_close_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_INVARIANT,
        fixture->endpoints[CLOSE_PROCESS_CLOSING]
    ));

    return true;
}

static bool reject_staged_source_and_current_scheduler_state(
    struct ipc_close_test_context *fixture,
    struct close_failures_are_atomic_case *scenario
)
{
    EXPECT_TRUE(ipc_close_test_setup_fixture(fixture, 3, 1));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CLOSE_PROCESS_FOREIGN_SEND],
            fixture->endpoints[CLOSE_PROCESS_CLOSING],
            UINT64_C(0x60006000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CLOSE_PROCESS_CLOSING],
            fixture->endpoints[CLOSE_PROCESS_FOREIGN_SEND],
            &scenario->message
        )
    );
    EXPECT_TRUE(
        ipc_close_test_hold_thread_for_close(
            fixture,
            fixture->primary_threads[CLOSE_PROCESS_CLOSING]
        )
    );
    scenario->stale_process = fixture->processes[CLOSE_PROCESS_CLOSING];
    ++scenario->stale_process.generation;
    EXPECT_TRUE(
        micros_endpoint_pack(scenario->stale_process, &scenario->stale_endpoint)
            == MICROS_ENDPOINT_OK
    );
    scenario->thread = &fixture->objects.threads[
        fixture->primary_threads[CLOSE_PROCESS_FOREIGN_SEND].slot
    ];
    scenario->thread->ipc_inbound_message.source = scenario->stale_endpoint;
    EXPECT_TRUE(ipc_close_test_expect_close_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_INVARIANT,
        fixture->endpoints[CLOSE_PROCESS_CLOSING]
    ));

    EXPECT_TRUE(ipc_close_test_setup_fixture(fixture, 3, 1));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CLOSE_PROCESS_FOREIGN_SEND],
            fixture->endpoints[CLOSE_PROCESS_CLOSING],
            &scenario->message
        )
    );
    EXPECT_TRUE(
        ipc_close_test_make_single_sender_current_blocked(
            fixture,
            fixture->primary_threads[CLOSE_PROCESS_FOREIGN_SEND],
            fixture->endpoints[CLOSE_PROCESS_CLOSING]
        )
        && ipc_close_test_expect_close_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_STATE,
            fixture->endpoints[CLOSE_PROCESS_CLOSING]
        )
    );
    return true;
}

bool test_close_failures_are_atomic(struct ipc_close_test_context *fixture)
{
    struct close_failures_are_atomic_case scenario;

    scenario.message = ipc_close_test_message_pattern(UINT32_C(0x2001), UINT8_C(0x70));
    return (
        reject_stale_or_runnable_closing_identity(fixture, &scenario)
        && reject_sender_and_receiver_queue_generations(fixture, &scenario)
        && reject_reply_token_and_notification_corruption(fixture, &scenario)
        && reject_staged_source_and_current_scheduler_state(fixture, &scenario)
    );
}
