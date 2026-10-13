#include "ipc_operation_test_internal.h"

bool test_authorized_send_blocks_with_snapshot(struct ipc_operation_test_context *fixture)
{
    struct micros_ipc_message message =
        ipc_operation_test_message_pattern(
            MICROS_ENDPOINT_ANY,
            UINT32_C(0x1234),
            UINT64_C(0x1122334455667788),
            UINT8_C(0x20)
        );
    const struct micros_ipc_message original = message;
    struct micros_ipc_message expected;
    const struct micros_endpoint_record *destination;
    const struct micros_thread *sender;

    EXPECT_TRUE(ipc_operation_test_setup_operation_fixture(fixture));
    expected = ipc_operation_test_canonical_message(
        &message,
        fixture->endpoints[OPERATION_PROCESS_SOURCE_A]
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[OPERATION_PROCESS_SOURCE_A],
            fixture->endpoints[OPERATION_PROCESS_DESTINATION],
            &message
        )
    );
    sender = &fixture->objects.threads[
        fixture->primary_threads[OPERATION_PROCESS_SOURCE_A].slot
    ];
    destination = &fixture->registry.endpoints[
        fixture->processes[OPERATION_PROCESS_DESTINATION].slot
    ];
    EXPECT_TRUE(
        memcmp(&message, &original, sizeof(message)) == 0
        && sender->runtime_flags == MICROS_THREAD_RTS_IPC_SEND
        && sender->ipc_queue_kind == MICROS_IPC_QUEUE_SENDER
        && ipc_operation_test_thread_handles_equal(
            destination->sender_head,
            fixture->primary_threads[OPERATION_PROCESS_SOURCE_A]
        )
        && ipc_operation_test_thread_handles_equal(
            destination->sender_tail,
            fixture->primary_threads[OPERATION_PROCESS_SOURCE_A]
        )
        && memcmp(
            &sender->ipc_outbound_message,
            &expected,
            sizeof(expected)
        ) == 0
    );
    memset(&message, 0xa5, sizeof(message));
    EXPECT_TRUE(
        memcmp(
            &sender->ipc_outbound_message,
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

bool test_authorized_send_matches_first_receiver(struct ipc_operation_test_context *fixture)
{
    struct micros_ipc_message message =
        ipc_operation_test_message_pattern(
            MICROS_ENDPOINT_NONE,
            UINT32_C(0x2234),
            UINT64_C(0x8877665544332211),
            UINT8_C(0x30)
        );
    struct micros_ipc_message expected;
    const struct micros_endpoint_record *destination;
    const struct micros_thread *unmatched;
    const struct micros_thread *matched;
    const struct micros_thread *sender;

    EXPECT_TRUE(ipc_operation_test_setup_operation_fixture(fixture));
    EXPECT_TRUE(ipc_operation_test_prepare_destination_extra_thread(fixture));
    expected = ipc_operation_test_canonical_message(
        &message,
        fixture->endpoints[OPERATION_PROCESS_SOURCE_A]
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[OPERATION_PROCESS_DESTINATION],
            fixture->endpoints[OPERATION_PROCESS_SOURCE_B],
            UINT64_C(0x40000000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->destination_extra_thread,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x40001000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[OPERATION_PROCESS_SOURCE_A],
            fixture->endpoints[OPERATION_PROCESS_DESTINATION],
            &message
        )
    );
    destination = &fixture->registry.endpoints[
        fixture->processes[OPERATION_PROCESS_DESTINATION].slot
    ];
    unmatched = &fixture->objects.threads[
        fixture->primary_threads[OPERATION_PROCESS_DESTINATION].slot
    ];
    matched = &fixture->objects.threads[fixture->destination_extra_thread.slot];
    sender = &fixture->objects.threads[
        fixture->primary_threads[OPERATION_PROCESS_SOURCE_A].slot
    ];
    EXPECT_TRUE(
        ipc_operation_test_thread_handles_equal(
            destination->receiver_head,
            fixture->primary_threads[OPERATION_PROCESS_DESTINATION]
        )
        && ipc_operation_test_thread_handles_equal(
            destination->receiver_tail,
            fixture->primary_threads[OPERATION_PROCESS_DESTINATION]
        )
        && unmatched->runtime_flags
            == MICROS_THREAD_RTS_IPC_RECEIVE
        && unmatched->ipc_receive_source
            == fixture->endpoints[OPERATION_PROCESS_SOURCE_B]
        && matched->runtime_flags == 0
        && matched->ready_linked
        && matched->ipc_delivery_pending
        && memcmp(
            &matched->ipc_inbound_message,
            &expected,
            sizeof(expected)
        ) == 0
        && sender->runtime_flags == 0
        && sender->ready_linked
        && destination->sender_head.slot == 0
        && destination->sender_head.generation == 0
        && destination->sender_tail.slot == 0
        && destination->sender_tail.generation == 0
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

bool test_send_rejections_are_atomic(struct ipc_operation_test_context *fixture)
{
    struct micros_ipc_message message =
        ipc_operation_test_message_pattern(0, UINT32_C(0x3234), 0, UINT8_C(0x40));
    struct micros_ipc_message kernel_message = message;
    micros_endpoint_t stale_destination;

    EXPECT_TRUE(ipc_operation_test_setup_operation_fixture(fixture));
    stale_destination =
        fixture->endpoints[OPERATION_PROCESS_DESTINATION]
        + (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS);
    EXPECT_TRUE(ipc_operation_test_expect_send_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_UNAUTHORIZED,
        fixture->primary_threads[OPERATION_PROCESS_NO_SEND],
        fixture->endpoints[OPERATION_PROCESS_DESTINATION],
        &message
    ));
    EXPECT_TRUE(ipc_operation_test_expect_send_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_UNAUTHORIZED,
        fixture->primary_threads[OPERATION_PROCESS_ISOLATED_SEND],
        fixture->endpoints[OPERATION_PROCESS_DESTINATION],
        &message
    ));
    EXPECT_TRUE(ipc_operation_test_expect_send_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_STATE,
        fixture->primary_threads[OPERATION_PROCESS_RESERVED],
        fixture->endpoints[OPERATION_PROCESS_DESTINATION],
        &message
    ));
    EXPECT_TRUE(ipc_operation_test_expect_send_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_STATE,
        fixture->primary_threads[OPERATION_PROCESS_SOURCE_A],
        fixture->endpoints[OPERATION_PROCESS_RESERVED],
        &message
    ));
    EXPECT_TRUE(ipc_operation_test_expect_send_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_DEAD_ENDPOINT,
        fixture->primary_threads[OPERATION_PROCESS_SOURCE_A],
        stale_destination,
        &message
    ));
    kernel_message.type |= MICROS_IPC_TYPE_KERNEL_MASK;
    EXPECT_TRUE(ipc_operation_test_expect_send_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_ARGUMENT,
        fixture->primary_threads[OPERATION_PROCESS_SOURCE_A],
        fixture->endpoints[OPERATION_PROCESS_DESTINATION],
        &kernel_message
    ));
    EXPECT_TRUE(ipc_operation_test_setup_operation_fixture(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[OPERATION_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x40002000)
        )
    );
    EXPECT_TRUE(ipc_operation_test_expect_send_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_UNAUTHORIZED,
        fixture->primary_threads[OPERATION_PROCESS_NO_SEND],
        fixture->endpoints[OPERATION_PROCESS_DESTINATION],
        &message
    ));

    EXPECT_TRUE(
        ipc_operation_test_setup_operation_fixture(fixture)
        && ipc_operation_test_make_current_held(
            fixture,
            fixture->primary_threads[OPERATION_PROCESS_SOURCE_A]
        )
    );
    EXPECT_TRUE(ipc_operation_test_expect_send_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_STATE,
        fixture->primary_threads[OPERATION_PROCESS_SOURCE_A],
        fixture->endpoints[OPERATION_PROCESS_DESTINATION],
        &message
    ));
    EXPECT_TRUE(ipc_operation_test_setup_operation_fixture(fixture));
    fixture->objects.threads[
        fixture->primary_threads[OPERATION_PROCESS_SOURCE_A].slot
    ].owner.slot = MICROS_PROCESS_CAPACITY;
    EXPECT_TRUE(ipc_operation_test_expect_send_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_INVARIANT,
        fixture->primary_threads[OPERATION_PROCESS_SOURCE_A],
        fixture->endpoints[OPERATION_PROCESS_DESTINATION],
        &message
    ));
    return true;
}
