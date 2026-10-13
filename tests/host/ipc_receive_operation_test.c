#include "ipc_operation_test_internal.h"

bool test_authorized_receive_blocks(struct ipc_operation_test_context *fixture)
{
    const struct micros_endpoint_record *destination;
    const struct micros_thread *receiver;

    EXPECT_TRUE(ipc_operation_test_setup_operation_fixture(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[OPERATION_PROCESS_DESTINATION],
            fixture->endpoints[OPERATION_PROCESS_SOURCE_A],
            UINT64_C(0x50000000)
        )
    );
    destination = &fixture->registry.endpoints[
        fixture->processes[OPERATION_PROCESS_DESTINATION].slot
    ];
    receiver = &fixture->objects.threads[
        fixture->primary_threads[OPERATION_PROCESS_DESTINATION].slot
    ];
    EXPECT_TRUE(
        receiver->runtime_flags == MICROS_THREAD_RTS_IPC_RECEIVE
        && receiver->ipc_queue_kind == MICROS_IPC_QUEUE_RECEIVER
        && receiver->ipc_receive_source
            == fixture->endpoints[OPERATION_PROCESS_SOURCE_A]
        && receiver->ipc_receive_buffer == UINT64_C(0x50000000)
        && ipc_operation_test_thread_handles_equal(
            destination->receiver_head,
            fixture->primary_threads[OPERATION_PROCESS_DESTINATION]
        )
        && ipc_operation_test_thread_handles_equal(
            destination->receiver_tail,
            fixture->primary_threads[OPERATION_PROCESS_DESTINATION]
        )
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool prepare_ordered_senders(
    struct ipc_operation_test_context *fixture,
    const struct micros_ipc_message *first,
    const struct micros_ipc_message *second
)
{
    EXPECT_TRUE(ipc_operation_test_setup_operation_fixture(fixture));
    EXPECT_TRUE(ipc_operation_test_prepare_destination_extra_thread(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_sender_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[OPERATION_PROCESS_SOURCE_B],
            fixture->endpoints[OPERATION_PROCESS_DESTINATION],
            first,
            0,
            0
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_sender_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[OPERATION_PROCESS_SOURCE_A],
            fixture->endpoints[OPERATION_PROCESS_DESTINATION],
            second,
            0,
            0
        )
    );
    return true;
}

bool test_receive_specific_and_any_fifo(struct ipc_operation_test_context *fixture)
{
    struct micros_ipc_message first =
        ipc_operation_test_message_pattern(0, UINT32_C(0x4234), 0, UINT8_C(0x50));
    struct micros_ipc_message second =
        ipc_operation_test_message_pattern(0, UINT32_C(0x5234), 0, UINT8_C(0x60));
    struct micros_ipc_message expected;
    const struct micros_endpoint_record *destination;
    const struct micros_thread *receiver;

    EXPECT_TRUE(prepare_ordered_senders(fixture, &first, &second));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[OPERATION_PROCESS_DESTINATION],
            fixture->endpoints[OPERATION_PROCESS_SOURCE_A],
            UINT64_C(0x50001000)
        )
    );
    destination = &fixture->registry.endpoints[
        fixture->processes[OPERATION_PROCESS_DESTINATION].slot
    ];
    receiver = &fixture->objects.threads[
        fixture->primary_threads[OPERATION_PROCESS_DESTINATION].slot
    ];
    expected = ipc_operation_test_canonical_message(
        &second,
        fixture->endpoints[OPERATION_PROCESS_SOURCE_A]
    );
    EXPECT_TRUE(
        ipc_operation_test_thread_handles_equal(
            destination->sender_head,
            fixture->primary_threads[OPERATION_PROCESS_SOURCE_B]
        )
        && ipc_operation_test_thread_handles_equal(
            destination->sender_tail,
            fixture->primary_threads[OPERATION_PROCESS_SOURCE_B]
        )
        && receiver->ipc_delivery_pending
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

    EXPECT_TRUE(prepare_ordered_senders(fixture, &first, &second));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->destination_extra_thread,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x50002000)
        )
    );
    destination = &fixture->registry.endpoints[
        fixture->processes[OPERATION_PROCESS_DESTINATION].slot
    ];
    receiver = &fixture->objects.threads[fixture->destination_extra_thread.slot];
    expected = ipc_operation_test_canonical_message(
        &first,
        fixture->endpoints[OPERATION_PROCESS_SOURCE_B]
    );
    EXPECT_TRUE(
        ipc_operation_test_thread_handles_equal(
            destination->sender_head,
            fixture->primary_threads[OPERATION_PROCESS_SOURCE_A]
        )
        && ipc_operation_test_thread_handles_equal(
            destination->sender_tail,
            fixture->primary_threads[OPERATION_PROCESS_SOURCE_A]
        )
        && receiver->ipc_delivery_pending
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

static bool reject_receive_shape_and_authority(struct ipc_operation_test_context *fixture)
{
    micros_endpoint_t stale_source;

    EXPECT_TRUE(ipc_operation_test_setup_operation_fixture(fixture));
    stale_source =
        fixture->endpoints[OPERATION_PROCESS_SOURCE_A]
        + (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS);
    EXPECT_TRUE(ipc_operation_test_expect_receive_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_UNAUTHORIZED,
        fixture->primary_threads[OPERATION_PROCESS_ISOLATED_SEND],
        MICROS_ENDPOINT_ANY,
        UINT64_C(0x60000000)
    ));
    EXPECT_TRUE(ipc_operation_test_expect_receive_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_STATE,
        fixture->primary_threads[OPERATION_PROCESS_RESERVED],
        MICROS_ENDPOINT_ANY,
        UINT64_C(0x60001000)
    ));
    EXPECT_TRUE(ipc_operation_test_expect_receive_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_STATE,
        fixture->primary_threads[OPERATION_PROCESS_DESTINATION],
        fixture->endpoints[OPERATION_PROCESS_RESERVED],
        UINT64_C(0x60002000)
    ));
    EXPECT_TRUE(ipc_operation_test_expect_receive_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_DEAD_ENDPOINT,
        fixture->primary_threads[OPERATION_PROCESS_DESTINATION],
        stale_source,
        UINT64_C(0x60003000)
    ));
    EXPECT_TRUE(ipc_operation_test_expect_receive_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_ARGUMENT,
        fixture->primary_threads[OPERATION_PROCESS_DESTINATION],
        MICROS_ENDPOINT_NONE,
        UINT64_C(0x60004000)
    ));
    EXPECT_TRUE(ipc_operation_test_expect_receive_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_ARGUMENT,
        fixture->primary_threads[OPERATION_PROCESS_DESTINATION],
        MICROS_ENDPOINT_ANY,
        UINT64_C(0x60005001)
    ));
    return true;
}

static bool reject_receive_current_and_corruption(struct ipc_operation_test_context *fixture)
{
    struct micros_thread_handle corrupt = {
        MICROS_THREAD_CAPACITY - 1,
        UINT32_C(0x55aa55aa),
    };

    EXPECT_TRUE(ipc_operation_test_setup_operation_fixture(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_sender_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[OPERATION_PROCESS_SOURCE_A],
            fixture->endpoints[OPERATION_PROCESS_ISOLATED_SEND],
            &(struct micros_ipc_message){
                .type = UINT32_C(0x6234),
                .payload = {UINT8_C(0x70)},
            },
            0,
            0
        )
    );
    EXPECT_TRUE(ipc_operation_test_expect_receive_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_UNAUTHORIZED,
        fixture->primary_threads[OPERATION_PROCESS_ISOLATED_SEND],
        MICROS_ENDPOINT_ANY,
        UINT64_C(0x60005500)
    ));

    EXPECT_TRUE(
        ipc_operation_test_setup_operation_fixture(fixture)
        && ipc_operation_test_make_current_held(
            fixture,
            fixture->primary_threads[OPERATION_PROCESS_DESTINATION]
        )
    );
    EXPECT_TRUE(ipc_operation_test_expect_receive_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_STATE,
        fixture->primary_threads[OPERATION_PROCESS_DESTINATION],
        MICROS_ENDPOINT_ANY,
        UINT64_C(0x60006000)
    ));

    EXPECT_TRUE(ipc_operation_test_setup_operation_fixture(fixture));
    fixture->registry.endpoints[
        fixture->processes[OPERATION_PROCESS_DESTINATION].slot
    ].sender_head = corrupt;
    fixture->registry.endpoints[
        fixture->processes[OPERATION_PROCESS_DESTINATION].slot
    ].sender_tail = corrupt;
    EXPECT_TRUE(ipc_operation_test_expect_receive_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_INVARIANT,
        fixture->primary_threads[OPERATION_PROCESS_DESTINATION],
        MICROS_ENDPOINT_ANY,
        UINT64_C(0x60007000)
    ));
    EXPECT_TRUE(ipc_operation_test_setup_operation_fixture(fixture));
    fixture->objects.threads[
        fixture->primary_threads[OPERATION_PROCESS_DESTINATION].slot
    ].owner.slot = MICROS_PROCESS_CAPACITY;
    EXPECT_TRUE(ipc_operation_test_expect_receive_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_INVARIANT,
        fixture->primary_threads[OPERATION_PROCESS_DESTINATION],
        MICROS_ENDPOINT_ANY,
        UINT64_C(0x60008000)
    ));
    return true;
}

bool test_receive_rejections_are_atomic(struct ipc_operation_test_context *fixture)
{
    return (
        reject_receive_shape_and_authority(fixture)
        && reject_receive_current_and_corruption(fixture)
    );
}
