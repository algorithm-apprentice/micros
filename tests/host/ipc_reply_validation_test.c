#include "ipc_reply_test_internal.h"

struct reply_rejections_are_atomic_case {
    union { uint64_t alignment; unsigned char bytes[sizeof(struct micros_ipc_message) + 1]; } misaligned_storage;
    struct micros_ipc_message request;
    struct micros_ipc_message reply;
    struct micros_ipc_message kernel_reply;
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;
    struct micros_thread *caller;
    uint64_t reply_token;
};

static bool reject_reply_buffer_shapes(
    struct ipc_reply_test_context *fixture,
    struct reply_rejections_are_atomic_case *scenario
)
{
    EXPECT_TRUE(
        ipc_reply_test_setup_reply_fixture(fixture)
        && ipc_reply_test_prepare_delivered_call(
            fixture,
            fixture->primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &scenario->request,
            UINT64_C(0x61001800),
            &scenario->reply_token
        )
    );
    scenario->registry_snapshot = fixture->registry;
    scenario->objects_snapshot = fixture->objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_ARGUMENT,
        micros_ipc_reply(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[REPLY_PROCESS_SERVER],
            scenario->reply_token,
            NULL
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
        micros_ipc_reply(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[REPLY_PROCESS_SERVER],
            scenario->reply_token,
            (const struct micros_ipc_message *)(
                (const void *)&scenario->misaligned_storage.bytes[1]
            )
        )
    );
    EXPECT_TRUE(ipc_reply_test_complete_state_is_unchanged(
        fixture,
        &scenario->registry_snapshot,
        &scenario->objects_snapshot
    ));

    return true;
}

static bool reject_reply_tokens_and_foreign_endpoint(
    struct ipc_reply_test_context *fixture,
    struct reply_rejections_are_atomic_case *scenario
)
{
    scenario->kernel_reply.type |= MICROS_IPC_TYPE_KERNEL_MASK;
    EXPECT_TRUE(
        ipc_reply_test_expect_reply_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_ARGUMENT,
            fixture->primary_threads[REPLY_PROCESS_SERVER],
            scenario->reply_token,
            &scenario->kernel_reply
        )
        && ipc_reply_test_expect_reply_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_REPLY_TOKEN,
            fixture->primary_threads[REPLY_PROCESS_SERVER],
            0,
            &scenario->reply
        )
        && ipc_reply_test_expect_reply_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_REPLY_TOKEN,
            fixture->primary_threads[REPLY_PROCESS_SERVER],
            scenario->reply_token + 100,
            &scenario->reply
        )
        && ipc_reply_test_expect_reply_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_STATE,
            fixture->primary_threads[REPLY_PROCESS_RESERVED_SERVER],
            scenario->reply_token,
            &scenario->reply
        )
        && ipc_reply_test_expect_reply_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_REPLY_TOKEN,
            fixture->primary_threads[REPLY_PROCESS_OTHER_SERVER],
            scenario->reply_token,
            &scenario->reply
        )
    );

    return true;
}

static bool reject_queued_unauthorized_and_current_reply(
    struct ipc_reply_test_context *fixture,
    struct reply_rejections_are_atomic_case *scenario
)
{
    EXPECT_TRUE(ipc_reply_test_setup_reply_fixture(fixture));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[REPLY_PROCESS_CLIENT],
            fixture->endpoints[REPLY_PROCESS_SERVER],
            &scenario->request,
            (uintptr_t)&scenario->request
        )
    );
    scenario->caller = &fixture->objects.threads[
        fixture->primary_threads[REPLY_PROCESS_CLIENT].slot
    ];
    EXPECT_TRUE(
        scenario->caller->runtime_flags
            == (
                MICROS_THREAD_RTS_IPC_SEND
                | MICROS_THREAD_RTS_IPC_REPLY
            )
        && ipc_reply_test_expect_reply_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_REPLY_TOKEN,
            fixture->primary_threads[REPLY_PROCESS_SERVER],
            scenario->caller->ipc_reply_token,
            &scenario->reply
        )
    );

    EXPECT_TRUE(
        ipc_reply_test_setup_reply_fixture(fixture)
        && ipc_reply_test_prepare_delivered_call(
            fixture,
            fixture->primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_NO_REPLY_SERVER,
            &scenario->request,
            UINT64_C(0x61002000),
            &scenario->reply_token
        )
        && ipc_reply_test_expect_reply_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_UNAUTHORIZED,
            fixture->primary_threads[REPLY_PROCESS_NO_REPLY_SERVER],
            scenario->reply_token,
            &scenario->reply
        )
    );

    EXPECT_TRUE(
        ipc_reply_test_setup_reply_fixture(fixture)
        && ipc_reply_test_prepare_delivered_call(
            fixture,
            fixture->primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &scenario->request,
            UINT64_C(0x61002800),
            &scenario->reply_token
        )
        && micros_thread_runtime_flags_unset(
            &fixture->objects,
            fixture->primary_threads[REPLY_PROCESS_SERVER],
            MICROS_THREAD_RTS_INACTIVE
        ) == MICROS_KERNEL_OBJECT_OK
        && ipc_reply_test_expect_reply_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_STATE,
            fixture->primary_threads[REPLY_PROCESS_SERVER],
            scenario->reply_token,
            &scenario->reply
        )
    );
    return true;
}

bool test_reply_rejections_are_atomic(struct ipc_reply_test_context *fixture)
{
    struct reply_rejections_are_atomic_case scenario;

    scenario.request = ipc_reply_test_message_pattern(
        0, UINT32_C(0x5401), 0, UINT8_C(0x70)
    );
    scenario.reply = ipc_reply_test_message_pattern(
        UINT32_C(0xdddddddd), UINT32_C(0x5402), UINT64_C(0xdddddddddddddddd), UINT8_C(0x80)
    );
    scenario.kernel_reply = scenario.reply;
    return (
        reject_reply_buffer_shapes(fixture, &scenario)
        && reject_reply_tokens_and_foreign_endpoint(fixture, &scenario)
        && reject_queued_unauthorized_and_current_reply(fixture, &scenario)
    );
}

bool test_reply_scheduler_failure_preserves_token(struct ipc_reply_test_context *fixture)
{
    struct micros_ipc_message request = ipc_reply_test_message_pattern(
        0,
        UINT32_C(0x5501),
        0,
        UINT8_C(0x90)
    );
    struct micros_ipc_message reply = ipc_reply_test_message_pattern(
        UINT32_C(0xeeeeeeee),
        UINT32_C(0x5502),
        UINT64_C(0xeeeeeeeeeeeeeeee),
        UINT8_C(0xa0)
    );
    struct micros_thread *caller;
    uint64_t reply_token;

    EXPECT_TRUE(
        ipc_reply_test_setup_reply_fixture(fixture)
        && ipc_reply_test_prepare_delivered_call(
            fixture,
            fixture->primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &request,
            UINT64_C(0x61003000),
            &reply_token
        )
        && ipc_reply_test_make_reply_wait_current(
            fixture,
            fixture->primary_threads[REPLY_PROCESS_CLIENT]
        )
        && ipc_reply_test_expect_reply_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_STATE,
            fixture->primary_threads[REPLY_PROCESS_SERVER],
            reply_token,
            &reply
        )
    );
    caller = &fixture->objects.threads[
        fixture->primary_threads[REPLY_PROCESS_CLIENT].slot
    ];
    EXPECT_TRUE(
        caller->runtime_flags == MICROS_THREAD_RTS_IPC_REPLY
        && caller->ipc_reply_token == reply_token
        && caller->ipc_reply_callee
            == fixture->endpoints[REPLY_PROCESS_SERVER]
        && !caller->ipc_delivery_pending
        && ipc_reply_test_thread_handle_is_zero(caller->ipc_next)
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

bool test_reply_rejects_source_only_caller(struct ipc_reply_test_context *fixture)
{
    struct micros_ipc_message request =
        ipc_reply_test_message_pattern(
            MICROS_ENDPOINT_ANY,
            UINT32_C(0x4008),
            UINT64_C(0x1111222233334444),
            UINT8_C(0x68)
        );
    struct micros_ipc_message reply =
        ipc_reply_test_message_pattern(
            MICROS_ENDPOINT_ANY,
            UINT32_C(0x4009),
            UINT64_C(0x5555666677778888),
            UINT8_C(0x78)
        );
    struct micros_thread *caller;
    uint64_t reply_token;

    EXPECT_TRUE(
        ipc_reply_test_setup_reply_fixture(fixture)
        && ipc_reply_test_prepare_delivered_call(
            fixture,
            fixture->primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &request,
            UINT64_C(0x50008000),
            &reply_token
        )
    );
    caller = &fixture->objects.threads[
        fixture->primary_threads[REPLY_PROCESS_CLIENT].slot
    ];
    fixture->registry.endpoints[
        fixture->processes[REPLY_PROCESS_CLIENT].slot
    ].state = MICROS_ENDPOINT_STATE_SOURCE_ONLY;
    caller->runtime_flags |= MICROS_THREAD_RTS_INACTIVE;
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
        && ipc_reply_test_expect_reply_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_REPLY_TOKEN,
            fixture->primary_threads[REPLY_PROCESS_SERVER],
            reply_token,
            &reply
        )
        && ipc_reply_test_expect_reply_receive_failure_unchanged(
            fixture,
            MICROS_IPC_ERROR_REPLY_TOKEN,
            fixture->primary_threads[REPLY_PROCESS_SERVER],
            reply_token,
            &reply,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x50009000)
        )
        && micros_ipc_endpoint_close(
            &fixture->registry,
            &fixture->objects,
            fixture->endpoints[REPLY_PROCESS_CLIENT]
        ) == MICROS_IPC_OK
        && fixture->registry.endpoints[
            fixture->processes[REPLY_PROCESS_CLIENT].slot
        ].state == MICROS_ENDPOINT_STATE_FREE
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}
