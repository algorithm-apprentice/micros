#include "micros/endpoint.h"
#include "micros/ipc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/scheduler_core.h"

static struct micros_endpoint_registry registry;
static struct micros_kernel_objects objects;
static struct micros_process_handle process;
static struct micros_thread_handle thread;

bool micros_ipc_state_test_run(void);

#define EXPECT_TRUE(expression) \
    do { \
        if (!(expression)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %s\n", \
                __FILE__, \
                __LINE__, \
                #expression \
            ); \
            return false; \
        } \
    } while (false)

#define EXPECT_ENDPOINT_ERROR(expected, expression) \
    do { \
        enum micros_endpoint_error actual = (expression); \
        if (actual != (expected)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %d got %d\n", \
                __FILE__, \
                __LINE__, \
                (int)(expected), \
                (int)actual \
            ); \
            return false; \
        } \
    } while (false)

#define EXPECT_OBJECT_ERROR(expected, expression) \
    do { \
        enum micros_kernel_object_error actual = (expression); \
        if (actual != (expected)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %d got %d\n", \
                __FILE__, \
                __LINE__, \
                (int)(expected), \
                (int)actual \
            ); \
            return false; \
        } \
    } while (false)

static bool bytes_are_zero(const void *storage, size_t size)
{
    const unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool setup_fixture(void)
{
    static const struct micros_privilege_profile profile = {
        .id = 1,
        .name = "IPC_STATE",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_SEND,
        .send_targets = UINT32_C(1) << 1,
    };

    memset(&registry, 0, sizeof(registry));
    memset(&objects, 0, sizeof(objects));
    return (
        micros_endpoint_registry_initialize(&registry, &profile, 1)
            == MICROS_ENDPOINT_OK
        && micros_kernel_objects_initialize(&objects, 1, 1)
            == MICROS_KERNEL_OBJECT_OK
        && micros_process_create(&objects, &process)
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_create(&objects, process, &thread)
            == MICROS_KERNEL_OBJECT_OK
    );
}

static bool test_message_layout(void)
{
    struct micros_ipc_message message;

    memset(&message, 0, sizeof(message));
    message.source = UINT32_C(0x11223344);
    message.type = UINT32_C(0x55667788);
    message.reply_token = UINT64_C(0x99aabbccddeeff00);
    message.payload[0] = UINT8_C(0x5a);
    message.payload[47] = UINT8_C(0xa5);

    EXPECT_TRUE(sizeof(message) == 64);
    EXPECT_TRUE(_Alignof(struct micros_ipc_message) == 8);
    EXPECT_TRUE(offsetof(struct micros_ipc_message, source) == 0);
    EXPECT_TRUE(offsetof(struct micros_ipc_message, type) == 4);
    EXPECT_TRUE(offsetof(struct micros_ipc_message, reply_token) == 8);
    EXPECT_TRUE(offsetof(struct micros_ipc_message, payload) == 16);
    EXPECT_TRUE(
        message.source == UINT32_C(0x11223344)
        && message.type == UINT32_C(0x55667788)
        && message.reply_token
            == UINT64_C(0x99aabbccddeeff00)
        && message.payload[0] == UINT8_C(0x5a)
        && message.payload[47] == UINT8_C(0xa5)
    );
    return true;
}

static bool test_dormant_state_is_zero(void)
{
    const struct micros_thread *resolved;
    micros_endpoint_t endpoint;
    const struct micros_endpoint_record *record;

    EXPECT_TRUE(setup_fixture());
    EXPECT_TRUE(
        MICROS_THREAD_RTS_IPC_SEND == UINT32_C(1) << 3
        && MICROS_THREAD_RTS_IPC_RECEIVE == UINT32_C(1) << 4
        && MICROS_THREAD_RTS_IPC_REPLY == UINT32_C(1) << 5
        && (
            MICROS_THREAD_RTS_DEFINED_MASK
            & MICROS_THREAD_RTS_IPC_MASK
        ) == MICROS_THREAD_RTS_IPC_MASK
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_resolve(&objects, thread, &resolved)
    );
    EXPECT_TRUE(
        resolved->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && resolved->ipc_next.slot == 0
        && resolved->ipc_next.generation == 0
        && bytes_are_zero(
            &resolved->ipc_outbound_message,
            sizeof(resolved->ipc_outbound_message)
        )
        && resolved->ipc_send_destination == 0
        && resolved->ipc_receive_source == 0
        && resolved->ipc_receive_buffer == 0
        && !resolved->ipc_delivery_pending
        && bytes_are_zero(
            &resolved->ipc_inbound_message,
            sizeof(resolved->ipc_inbound_message)
        )
        && resolved->ipc_staged_result == MICROS_IPC_OK
        && resolved->ipc_reply_token == 0
        && resolved->ipc_reply_callee == 0
    );
    EXPECT_ENDPOINT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_reserve(
            &registry,
            &objects,
            process,
            &endpoint
        )
    );
    EXPECT_ENDPOINT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_resolve_internal(
            &registry,
            &objects,
            endpoint,
            &record
        )
    );
    EXPECT_TRUE(
        record->sender_head.generation == 0
        && record->sender_tail.generation == 0
        && record->receiver_head.generation == 0
        && record->receiver_tail.generation == 0
        && record->pending_notification_sources == 0
        && bytes_are_zero(
            record->pending_events,
            sizeof(record->pending_events)
        )
    );
    return true;
}

static bool test_dormant_validator_rejects_corruption(void)
{
    micros_endpoint_t endpoint;
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;

    EXPECT_TRUE(setup_fixture());
    EXPECT_ENDPOINT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_reserve(
            &registry,
            &objects,
            process,
            &endpoint
        )
    );
    registry_snapshot = registry;
    objects_snapshot = objects;

#define EXPECT_CORRUPTION(statement) \
    do { \
        registry = registry_snapshot; \
        objects = objects_snapshot; \
        statement; \
        EXPECT_ENDPOINT_ERROR( \
            MICROS_ENDPOINT_ERROR_INVARIANT, \
            micros_endpoint_registry_validate_objects( \
                &registry, \
                &objects \
            ) \
        ); \
    } while (false)

    EXPECT_CORRUPTION(
        objects.threads[thread.slot].ipc_queue_kind =
            MICROS_IPC_QUEUE_SENDER
    );
    EXPECT_CORRUPTION(
        objects.threads[thread.slot].ipc_next.generation = 1
    );
    EXPECT_CORRUPTION(
        objects.threads[thread.slot].ipc_outbound_message.type = 1
    );
    EXPECT_CORRUPTION(
        objects.threads[thread.slot].ipc_send_destination = 1
    );
    EXPECT_CORRUPTION(
        objects.threads[thread.slot].ipc_receive_source =
            MICROS_ENDPOINT_ANY
    );
    EXPECT_CORRUPTION(
        objects.threads[thread.slot].ipc_receive_buffer = 8
    );
    EXPECT_CORRUPTION(
        objects.threads[thread.slot].ipc_delivery_pending = true
    );
    EXPECT_CORRUPTION(
        objects.threads[thread.slot].ipc_inbound_message.payload[0] = 1
    );
    EXPECT_CORRUPTION(
        objects.threads[thread.slot].ipc_staged_result =
            MICROS_IPC_ERROR_DEAD_ENDPOINT
    );
    EXPECT_CORRUPTION(
        objects.threads[thread.slot].ipc_reply_token = 1
    );
    EXPECT_CORRUPTION(
        objects.threads[thread.slot].ipc_reply_callee = 1
    );
    EXPECT_CORRUPTION(
        objects.threads[thread.slot].runtime_flags |=
            MICROS_THREAD_RTS_IPC_SEND
    );
    EXPECT_CORRUPTION(
        registry.endpoints[process.slot].sender_head.generation = 1
    );
    EXPECT_CORRUPTION(
        registry.endpoints[process.slot].receiver_tail.generation = 1
    );
    EXPECT_CORRUPTION(
        registry.endpoints[process.slot]
            .pending_notification_sources = 1
    );
    EXPECT_CORRUPTION(
        registry.endpoints[process.slot].pending_events[0] = 1
    );

#undef EXPECT_CORRUPTION
    return true;
}

static bool test_scheduler_api_cannot_forge_ipc_state(void)
{
    struct micros_kernel_objects snapshot;

    EXPECT_TRUE(setup_fixture());
    snapshot = objects;
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_thread_runtime_flags_set(
            &objects,
            thread,
            MICROS_THREAD_RTS_IPC_SEND
        )
    );
    EXPECT_TRUE(memcmp(&objects, &snapshot, sizeof(objects)) == 0);
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_thread_runtime_flags_unset(
            &objects,
            thread,
            MICROS_THREAD_RTS_IPC_RECEIVE
        )
    );
    EXPECT_TRUE(memcmp(&objects, &snapshot, sizeof(objects)) == 0);
    return true;
}

bool micros_ipc_state_test_run(void)
{
    return (
        test_message_layout()
        && test_dormant_state_is_zero()
        && test_dormant_validator_rejects_corruption()
        && test_scheduler_api_cannot_forge_ipc_state()
    );
}
