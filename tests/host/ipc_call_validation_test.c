#include "ipc_call_test_internal.h"

bool test_low_level_enqueue_rejects_reply_tokens(struct ipc_call_test_context *fixture)
{
    struct micros_ipc_message request = ipc_call_test_message_pattern(
        0,
        UINT32_C(0x3000),
        0,
        UINT8_C(0x4f)
    );
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;

    EXPECT_TRUE(ipc_call_test_setup_call_fixture(fixture));
    fixture->registry.last_reply_token = 41;
    registry_snapshot = fixture->registry;
    objects_snapshot = fixture->objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_REPLY_TOKEN,
        micros_ipc_sender_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CALL_PROCESS_CLIENT],
            fixture->endpoints[CALL_PROCESS_SERVER],
            &request,
            41,
            (uintptr_t)&request
        )
    );
    EXPECT_TRUE(ipc_call_test_complete_state_is_unchanged(
        fixture,
        &registry_snapshot,
        &objects_snapshot
    ));
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_REPLY_TOKEN,
        micros_ipc_sender_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CALL_PROCESS_CLIENT],
            fixture->endpoints[CALL_PROCESS_SERVER],
            &request,
            42,
            (uintptr_t)&request
        )
    );
    EXPECT_TRUE(ipc_call_test_complete_state_is_unchanged(
        fixture,
        &registry_snapshot,
        &objects_snapshot
    ));
    return true;
}

bool test_token_exhaustion_is_preflighted(struct ipc_call_test_context *fixture)
{
    struct micros_ipc_message first_request = ipc_call_test_message_pattern(
        0,
        UINT32_C(0x3001),
        0,
        UINT8_C(0x50)
    );
    struct micros_ipc_message second_request = ipc_call_test_message_pattern(
        0,
        UINT32_C(0x3002),
        0,
        UINT8_C(0x60)
    );
    const struct micros_thread *first_caller;

    EXPECT_TRUE(ipc_call_test_setup_call_fixture(fixture));
    fixture->registry.last_reply_token = UINT64_MAX - 1;
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CALL_PROCESS_CLIENT],
            fixture->endpoints[CALL_PROCESS_SERVER],
            &first_request,
            (uintptr_t)&first_request
        )
    );
    first_caller = &fixture->objects.threads[
        fixture->primary_threads[CALL_PROCESS_CLIENT].slot
    ];
    EXPECT_TRUE(
        fixture->registry.last_reply_token == UINT64_MAX
        && first_caller->ipc_reply_token == UINT64_MAX
        && first_caller->ipc_outbound_message.reply_token
            == UINT64_MAX
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    EXPECT_TRUE(ipc_call_test_expect_call_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_REPLY_TOKEN_EXHAUSTED,
        fixture->client_extra_thread,
        fixture->endpoints[CALL_PROCESS_SERVER],
        &second_request
    ));
    return true;
}

struct call_rejection_case {
    struct micros_ipc_message request;
    struct micros_ipc_message kernel_request;
    micros_endpoint_t stale_destination;
    struct micros_thread_handle stale_caller;
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;
    struct micros_scheduler_return_plan plan;
    _Alignas(struct micros_ipc_message)
        unsigned char unaligned_storage[sizeof(struct micros_ipc_message) + 1];
};

static bool reject_call_shape_and_authority(
    struct ipc_call_test_context *fixture,
    struct call_rejection_case *scenario
)
{
    EXPECT_TRUE(ipc_call_test_setup_call_fixture(fixture));
    scenario->stale_destination =
        fixture->endpoints[CALL_PROCESS_SERVER]
        + (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS);
    EXPECT_TRUE(ipc_call_test_expect_call_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_UNAUTHORIZED,
        fixture->primary_threads[CALL_PROCESS_NO_CALL],
        fixture->endpoints[CALL_PROCESS_SERVER],
        &scenario->request
    ));
    EXPECT_TRUE(ipc_call_test_expect_call_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_UNAUTHORIZED,
        fixture->primary_threads[CALL_PROCESS_ISOLATED_CALL],
        fixture->endpoints[CALL_PROCESS_SERVER],
        &scenario->request
    ));
    EXPECT_TRUE(ipc_call_test_expect_call_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_STATE,
        fixture->primary_threads[CALL_PROCESS_CLIENT],
        fixture->endpoints[CALL_PROCESS_RESERVED],
        &scenario->request
    ));
    EXPECT_TRUE(ipc_call_test_expect_call_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_DEAD_ENDPOINT,
        fixture->primary_threads[CALL_PROCESS_CLIENT],
        scenario->stale_destination,
        &scenario->request
    ));
    scenario->stale_caller = fixture->primary_threads[CALL_PROCESS_CLIENT];
    ++scenario->stale_caller.generation;
    EXPECT_TRUE(ipc_call_test_expect_call_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_DEAD_ENDPOINT,
        scenario->stale_caller,
        fixture->endpoints[CALL_PROCESS_SERVER],
        &scenario->request
    ));
    scenario->kernel_request.type |= MICROS_IPC_TYPE_KERNEL_MASK;
    EXPECT_TRUE(ipc_call_test_expect_call_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_ARGUMENT,
        fixture->primary_threads[CALL_PROCESS_CLIENT],
        fixture->endpoints[CALL_PROCESS_SERVER],
        &scenario->kernel_request
    ));
    memset(scenario->unaligned_storage, UINT8_C(0xa5), sizeof(scenario->unaligned_storage));
    memset(scenario->unaligned_storage, UINT8_C(0xa5), sizeof(scenario->unaligned_storage));
    scenario->registry_snapshot = fixture->registry;
    scenario->objects_snapshot = fixture->objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_ARGUMENT,
        micros_ipc_call(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CALL_PROCESS_CLIENT],
            fixture->endpoints[CALL_PROCESS_SERVER],
            (struct micros_ipc_message *)(scenario->unaligned_storage + 1),
            (uintptr_t)(struct micros_ipc_message *)(scenario->unaligned_storage + 1)
        )
    );
    EXPECT_TRUE(ipc_call_test_complete_state_is_unchanged(
        fixture,
        &scenario->registry_snapshot,
        &scenario->objects_snapshot
    ));

    return true;
}

static bool reject_current_and_pending_calls(
    struct ipc_call_test_context *fixture,
    struct call_rejection_case *scenario
)
{
    EXPECT_TRUE(
        ipc_call_test_setup_call_fixture(fixture)
        && ipc_call_test_make_current_held(
            fixture,
            fixture->primary_threads[CALL_PROCESS_CLIENT]
        )
    );
    EXPECT_TRUE(ipc_call_test_expect_call_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_STATE,
        fixture->primary_threads[CALL_PROCESS_CLIENT],
        fixture->endpoints[CALL_PROCESS_SERVER],
        &scenario->request
    ));

    EXPECT_TRUE(ipc_call_test_setup_call_fixture(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CALL_PROCESS_CLIENT],
            fixture->endpoints[CALL_PROCESS_SERVER],
            &scenario->request,
            (uintptr_t)&scenario->request
        )
    );
    EXPECT_TRUE(ipc_call_test_expect_call_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_STATE,
        fixture->primary_threads[CALL_PROCESS_CLIENT],
        fixture->endpoints[CALL_PROCESS_SERVER],
        &scenario->request
    ));

    return true;
}

static bool reject_late_call_scheduler_state(
    struct ipc_call_test_context *fixture,
    struct call_rejection_case *scenario
)
{
    EXPECT_TRUE(ipc_call_test_setup_call_fixture(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CALL_PROCESS_SERVER],
            fixture->endpoints[CALL_PROCESS_CLIENT],
            UINT64_C(0x60002000)
        )
    );
    EXPECT_TRUE(
        micros_thread_install_policy(
            &fixture->objects,
            fixture->primary_threads[CALL_PROCESS_NO_CALL],
            MICROS_SCHEDULER_PRIORITY_LOWEST,
            100
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_scheduler_accounting_initialize(
            &fixture->objects,
            fixture->hart,
            100
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_thread_runtime_flags_unset(
            &fixture->objects,
            fixture->primary_threads[CALL_PROCESS_NO_CALL],
            MICROS_THREAD_RTS_INACTIVE
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_hart_plan_user_return(&fixture->objects, fixture->hart, &scenario->plan)
            == MICROS_KERNEL_OBJECT_OK
        && micros_hart_commit_user_return(&fixture->objects, &scenario->plan)
            == MICROS_KERNEL_OBJECT_OK
        && micros_scheduler_account_enter_thread(
            &fixture->objects,
            fixture->hart,
            fixture->primary_threads[CALL_PROCESS_NO_CALL],
            120
        ) == MICROS_KERNEL_OBJECT_OK
    );
    EXPECT_TRUE(ipc_call_test_expect_call_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_STATE,
        fixture->primary_threads[CALL_PROCESS_CLIENT],
        fixture->endpoints[CALL_PROCESS_SERVER],
        &scenario->request
    ));

    return true;
}

static bool reject_call_corruption_and_null_registry(
    struct ipc_call_test_context *fixture,
    struct call_rejection_case *scenario
)
{
    EXPECT_TRUE(ipc_call_test_setup_call_fixture(fixture));
    fixture->registry.endpoints[CALL_PROCESS_SERVER].sender_head =
        (struct micros_thread_handle){
            MICROS_THREAD_CAPACITY - 1,
            UINT32_C(0x55aa55aa),
        };
    fixture->registry.endpoints[CALL_PROCESS_SERVER].sender_tail =
        fixture->registry.endpoints[CALL_PROCESS_SERVER].sender_head;
    EXPECT_TRUE(ipc_call_test_expect_call_failure_unchanged(
        fixture,
        MICROS_IPC_ERROR_INVARIANT,
        fixture->primary_threads[CALL_PROCESS_CLIENT],
        fixture->endpoints[CALL_PROCESS_SERVER],
        &scenario->request
    ));

    EXPECT_TRUE(ipc_call_test_setup_call_fixture(fixture));
    scenario->registry_snapshot = fixture->registry;
    scenario->objects_snapshot = fixture->objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_ARGUMENT,
        micros_ipc_call(
            NULL,
            &fixture->objects,
            fixture->primary_threads[CALL_PROCESS_CLIENT],
            fixture->endpoints[CALL_PROCESS_SERVER],
            &scenario->request,
            (uintptr_t)&scenario->request
        )
    );
    EXPECT_TRUE(ipc_call_test_complete_state_is_unchanged(
        fixture,
        &scenario->registry_snapshot,
        &scenario->objects_snapshot
    ));
    return true;
}

bool test_call_rejections_are_atomic(struct ipc_call_test_context *fixture)
{
    struct call_rejection_case scenario;

    scenario.request = ipc_call_test_message_pattern(
        0, UINT32_C(0x4001), 0, UINT8_C(0x70)
    );
    scenario.kernel_request = scenario.request;
    return (
        reject_call_shape_and_authority(fixture, &scenario)
        && reject_current_and_pending_calls(fixture, &scenario)
        && reject_late_call_scheduler_state(fixture, &scenario)
        && reject_call_corruption_and_null_registry(fixture, &scenario)
    );
}

bool test_staged_call_token_binding_is_validated(struct ipc_call_test_context *fixture)
{
    struct micros_ipc_message request = ipc_call_test_message_pattern(
        0,
        UINT32_C(0x5000),
        0,
        UINT8_C(0x7f)
    );
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;
    struct micros_thread *caller;
    struct micros_thread *receiver;
    struct micros_endpoint_record *server;

    EXPECT_TRUE(ipc_call_test_setup_call_fixture(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[CALL_PROCESS_SERVER],
            fixture->endpoints[CALL_PROCESS_CLIENT],
            UINT64_C(0x60003000)
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
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    registry_snapshot = fixture->registry;
    objects_snapshot = fixture->objects;

#define EXPECT_STAGED_TOKEN_CORRUPTION(statement) \
    do { \
        fixture->registry = registry_snapshot; \
        fixture->objects = objects_snapshot; \
        statement; \
        EXPECT_TRUE( \
            micros_endpoint_registry_validate_objects( \
                &fixture->registry, \
                &fixture->objects \
            ) == MICROS_ENDPOINT_ERROR_INVARIANT \
        ); \
    } while (false)

    EXPECT_STAGED_TOKEN_CORRUPTION(
        fixture->objects.threads[
            fixture->primary_threads[CALL_PROCESS_SERVER].slot
        ].ipc_inbound_message.reply_token = 2
    );
    EXPECT_STAGED_TOKEN_CORRUPTION(
        fixture->objects.threads[
            fixture->primary_threads[CALL_PROCESS_SERVER].slot
        ].ipc_inbound_message.source =
            fixture->endpoints[CALL_PROCESS_NO_CALL]
    );
    EXPECT_STAGED_TOKEN_CORRUPTION(
        fixture->objects.threads[
            fixture->primary_threads[CALL_PROCESS_CLIENT].slot
        ].ipc_reply_callee = fixture->endpoints[CALL_PROCESS_NO_CALL]
    );
    fixture->registry = registry_snapshot;
    fixture->objects = objects_snapshot;
    caller = &fixture->objects.threads[
        fixture->primary_threads[CALL_PROCESS_CLIENT].slot
    ];
    receiver = &fixture->objects.threads[
        fixture->primary_threads[CALL_PROCESS_SERVER].slot
    ];
    server = &fixture->registry.endpoints[CALL_PROCESS_SERVER];
    caller->runtime_flags |= MICROS_THREAD_RTS_IPC_SEND;
    caller->ipc_queue_kind = MICROS_IPC_QUEUE_SENDER;
    caller->ipc_outbound_message = receiver->ipc_inbound_message;
    caller->ipc_send_destination = fixture->endpoints[CALL_PROCESS_SERVER];
    server->sender_head = fixture->primary_threads[CALL_PROCESS_CLIENT];
    server->sender_tail = fixture->primary_threads[CALL_PROCESS_CLIENT];
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_ERROR_INVARIANT
    );

#undef EXPECT_STAGED_TOKEN_CORRUPTION
    return true;
}

bool test_allocator_validator_rejects_unissued_token(struct ipc_call_test_context *fixture)
{
    struct micros_ipc_message request = ipc_call_test_message_pattern(
        0,
        UINT32_C(0x5001),
        0,
        UINT8_C(0x80)
    );
    struct micros_thread *caller;

    EXPECT_TRUE(ipc_call_test_setup_call_fixture(fixture));
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
    caller = &fixture->objects.threads[
        fixture->primary_threads[CALL_PROCESS_CLIENT].slot
    ];
    caller->ipc_reply_token = 2;
    caller->ipc_outbound_message.reply_token = 2;
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_ERROR_INVARIANT
    );
    return true;
}
