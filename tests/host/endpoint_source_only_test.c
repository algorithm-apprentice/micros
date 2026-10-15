#include "endpoint_test_internal.h"

#include <string.h>

#include "micros/bootstrap_control.h"

struct endpoint_source_only_case {
    micros_endpoint_t endpoints[2];
    const struct micros_endpoint_record *record;
    struct micros_thread *receiver;
    size_t index;
};

static bool source_only_prepare_acknowledgment(
    struct endpoint_test_fixture *fixture,
    struct endpoint_source_only_case *work
)
{
    EXPECT_TRUE(endpoint_test_setup_lifecycle_fixture(fixture));
    for (work->index = 0; work->index < 2; ++work->index) {
        EXPECT_ERROR(
            MICROS_ENDPOINT_OK,
            micros_endpoint_reserve(
                &fixture->registry,
                &fixture->objects,
                fixture->processes[work->index],
                &work->endpoints[work->index]
            )
        );
        EXPECT_ERROR(
            MICROS_ENDPOINT_OK,
            micros_endpoint_install_profile(
                &fixture->registry,
                &fixture->objects,
                fixture->processes[work->index],
                (uint8_t)(work->index + 1)
            )
        );
        EXPECT_ERROR(
            MICROS_ENDPOINT_OK,
            micros_endpoint_activate(
                &fixture->registry,
                &fixture->objects,
                work->endpoints[work->index]
            )
        );
    }
    work->receiver = &fixture->objects.threads[fixture->threads[1].slot];
    work->receiver->ipc_receive_buffer = UINT64_C(0x61002000);
    work->receiver->ipc_delivery_pending = true;
    work->receiver->ipc_inbound_message.source = work->endpoints[0];
    work->receiver->ipc_inbound_message.type =
        MICROS_BOOTSTRAP_MESSAGE_READY_ACK;
    endpoint_test_write_u32_le(
        &work->receiver->ipc_inbound_message.payload[0],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    endpoint_test_write_u32_le(
        &work->receiver->ipc_inbound_message.payload[4],
        2
    );
    endpoint_test_write_u32_le(
        &work->receiver->ipc_inbound_message.payload[8],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    endpoint_test_write_u32_le(
        &work->receiver->ipc_inbound_message.payload[16],
        work->endpoints[1]
    );
    return true;
}

static bool source_only_seal_and_validate_source(
    struct endpoint_test_fixture *fixture,
    struct endpoint_source_only_case *work
)
{
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_seal_source_only(
            &fixture->registry,
            &fixture->objects,
            work->endpoints[0]
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        )
    );
    work->receiver->ipc_inbound_message.type = UINT32_C(60);
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_INVARIANT,
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        )
    );
    work->receiver->ipc_inbound_message.type =
        MICROS_BOOTSTRAP_MESSAGE_READY_ACK;
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        )
    );
    return true;
}

static bool source_only_reject_new_authority(
    struct endpoint_test_fixture *fixture,
    struct endpoint_source_only_case *work
)
{
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_resolve_internal(
            &fixture->registry,
            &fixture->objects,
            work->endpoints[0],
            &work->record
        )
    );
    EXPECT_TRUE(work->record->state == MICROS_ENDPOINT_STATE_SOURCE_ONLY);
    work->record = (const struct micros_endpoint_record *)(uintptr_t)1;
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_CLOSING,
        micros_endpoint_resolve_active(
            &fixture->registry,
            &fixture->objects,
            work->endpoints[0],
            &work->record
        )
    );
    EXPECT_TRUE(
        work->record == (const struct micros_endpoint_record *)(uintptr_t)1
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_CLOSING,
        micros_endpoint_authorize_target(
            &fixture->registry,
            &fixture->objects,
            work->endpoints[1],
            MICROS_PRIVILEGE_OPERATION_SEND,
            work->endpoints[0]
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_STATE,
        micros_endpoint_close(
            &fixture->registry,
            &fixture->objects,
            work->endpoints[0]
        )
    );
    return true;
}

static bool source_only_drain_and_close(
    struct endpoint_test_fixture *fixture,
    struct endpoint_source_only_case *work
)
{
    work->receiver->ipc_receive_buffer = 0;
    work->receiver->ipc_delivery_pending = false;
    memset(
        &work->receiver->ipc_inbound_message,
        0,
        sizeof(work->receiver->ipc_inbound_message)
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_close(
            &fixture->registry,
            &fixture->objects,
            work->endpoints[0]
        )
    );
    return true;
}

bool endpoint_test_source_only_seal_preserves_staged_source(void)
{
    struct endpoint_test_fixture *fixture = endpoint_test_context();
    struct endpoint_source_only_case work;

    work.record = NULL;
    return source_only_prepare_acknowledgment(fixture, &work)
        && source_only_seal_and_validate_source(fixture, &work)
        && source_only_reject_new_authority(fixture, &work)
        && source_only_drain_and_close(fixture, &work);
}
