#include "endpoint_test_internal.h"

#include <string.h>

#include "micros/bootstrap_control.h"

bool endpoint_test_source_only_seal_preserves_staged_source(void)
{
    struct endpoint_test_fixture *fixture = endpoint_test_context();
    micros_endpoint_t endpoints[2];
    const struct micros_endpoint_record *record = NULL;
    struct micros_thread *receiver;
    size_t index;

    EXPECT_TRUE(endpoint_test_setup_lifecycle_fixture(fixture));
    for (index = 0; index < 2; ++index) {
        EXPECT_ERROR(
            MICROS_ENDPOINT_OK,
            micros_endpoint_reserve(
                &fixture->registry,
                &fixture->objects,
                fixture->processes[index],
                &endpoints[index]
            )
        );
        EXPECT_ERROR(
            MICROS_ENDPOINT_OK,
            micros_endpoint_install_profile(
                &fixture->registry,
                &fixture->objects,
                fixture->processes[index],
                (uint8_t)(index + 1)
            )
        );
        EXPECT_ERROR(
            MICROS_ENDPOINT_OK,
            micros_endpoint_activate(
                &fixture->registry,
                &fixture->objects,
                endpoints[index]
            )
        );
    }
    receiver = &fixture->objects.threads[fixture->threads[1].slot];
    receiver->ipc_receive_buffer = UINT64_C(0x61002000);
    receiver->ipc_delivery_pending = true;
    receiver->ipc_inbound_message.source = endpoints[0];
    receiver->ipc_inbound_message.type =
        MICROS_BOOTSTRAP_MESSAGE_READY_ACK;
    endpoint_test_write_u32_le(
        &receiver->ipc_inbound_message.payload[0],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    endpoint_test_write_u32_le(
        &receiver->ipc_inbound_message.payload[4],
        2
    );
    endpoint_test_write_u32_le(
        &receiver->ipc_inbound_message.payload[8],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    endpoint_test_write_u32_le(
        &receiver->ipc_inbound_message.payload[16],
        endpoints[1]
    );

    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_seal_source_only(
            &fixture->registry,
            &fixture->objects,
            endpoints[0]
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        )
    );
    receiver->ipc_inbound_message.type = UINT32_C(60);
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_INVARIANT,
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        )
    );
    receiver->ipc_inbound_message.type =
        MICROS_BOOTSTRAP_MESSAGE_READY_ACK;
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_resolve_internal(
            &fixture->registry,
            &fixture->objects,
            endpoints[0],
            &record
        )
    );
    EXPECT_TRUE(record->state == MICROS_ENDPOINT_STATE_SOURCE_ONLY);
    record = (const struct micros_endpoint_record *)(uintptr_t)1;
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_CLOSING,
        micros_endpoint_resolve_active(
            &fixture->registry,
            &fixture->objects,
            endpoints[0],
            &record
        )
    );
    EXPECT_TRUE(
        record == (const struct micros_endpoint_record *)(uintptr_t)1
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_CLOSING,
        micros_endpoint_authorize_target(
            &fixture->registry,
            &fixture->objects,
            endpoints[1],
            MICROS_PRIVILEGE_OPERATION_SEND,
            endpoints[0]
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_STATE,
        micros_endpoint_close(
            &fixture->registry,
            &fixture->objects,
            endpoints[0]
        )
    );

    receiver->ipc_receive_buffer = 0;
    receiver->ipc_delivery_pending = false;
    memset(
        &receiver->ipc_inbound_message,
        0,
        sizeof(receiver->ipc_inbound_message)
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_close(
            &fixture->registry,
            &fixture->objects,
            endpoints[0]
        )
    );
    return true;
}
