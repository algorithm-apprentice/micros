#include "micros/endpoint.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/scheduler_core.h"

enum {
    QUEUE_PROCESS_COUNT = 4,
};

static struct micros_endpoint_registry registry;
static struct micros_kernel_objects objects;
static struct micros_process_handle processes[QUEUE_PROCESS_COUNT];
static struct micros_thread_handle threads[QUEUE_PROCESS_COUNT];
static micros_endpoint_t endpoints[QUEUE_PROCESS_COUNT];
static struct micros_hart_handle hart;

bool micros_ipc_queue_test_run(void);

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

static struct micros_user_context context_pattern(uint64_t base)
{
    struct micros_user_context context;
    uint64_t words[
        sizeof(struct micros_user_context) / sizeof(uint64_t)
    ];
    size_t index;

    for (
        index = 0;
        index < sizeof(words) / sizeof(words[0]);
        ++index
    ) {
        words[index] = base + index;
    }
    memcpy(&context, words, sizeof(context));
    return context;
}

static bool setup_queue_fixture(void)
{
    static const struct micros_privilege_profile profile = {
        .id = 1,
        .name = "QUEUE",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_SEND,
        .send_targets = UINT32_C(1) << 1,
    };
    size_t index;

    memset(&registry, 0, sizeof(registry));
    memset(&objects, 0, sizeof(objects));
    if (
        micros_endpoint_registry_initialize(&registry, &profile, 1)
            != MICROS_ENDPOINT_OK
        || micros_kernel_objects_initialize(&objects, 1, 1)
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_register(&objects, 0, &hart)
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_install_trap_stacks(
            &objects,
            hart,
            UINT64_C(0x01000000),
            UINT64_C(0x01004000),
            UINT64_C(0x02000000),
            UINT64_C(0x02001000)
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    for (index = 0; index < QUEUE_PROCESS_COUNT; ++index) {
        struct micros_user_context context =
            context_pattern(UINT64_C(0x1000) + index * UINT64_C(0x100));
        uintptr_t stack_bottom =
            UINT64_C(0x10000000)
            + index * UINT64_C(0x00008000);

        if (
            micros_process_create(&objects, &processes[index])
                != MICROS_KERNEL_OBJECT_OK
            || micros_thread_create(
                &objects,
                processes[index],
                &threads[index]
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_endpoint_reserve(
                &registry,
                &objects,
                processes[index],
                &endpoints[index]
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_install_profile(
                &registry,
                &objects,
                processes[index],
                1
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_activate(
                &registry,
                &objects,
                endpoints[index]
            ) != MICROS_ENDPOINT_OK
            || micros_thread_attach_execution_context(
                &objects,
                threads[index],
                stack_bottom,
                stack_bottom + MICROS_THREAD_KERNEL_STACK_SIZE,
                &context
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_scheduler_admit(
                &objects,
                hart,
                threads[index],
                MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
                100,
                true
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_scheduler_hold(
                &objects,
                threads[index]
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
    }
    return true;
}

static void construct_valid_queues(void)
{
    struct micros_thread *first_sender =
        &objects.threads[threads[0].slot];
    struct micros_thread *second_sender =
        &objects.threads[threads[1].slot];
    struct micros_thread *receiver =
        &objects.threads[threads[3].slot];
    struct micros_endpoint_record *sender_queue =
        &registry.endpoints[processes[2].slot];
    struct micros_endpoint_record *receiver_queue =
        &registry.endpoints[processes[3].slot];

    registry.last_reply_token = 102;
    first_sender->runtime_flags =
        MICROS_THREAD_RTS_IPC_SEND
        | MICROS_THREAD_RTS_IPC_REPLY;
    first_sender->ipc_queue_kind = MICROS_IPC_QUEUE_SENDER;
    first_sender->ipc_next = threads[1];
    first_sender->ipc_send_destination = endpoints[2];
    first_sender->ipc_outbound_message.source = endpoints[0];
    first_sender->ipc_outbound_message.type = 10;
    first_sender->ipc_outbound_message.reply_token = 101;
    first_sender->ipc_outbound_message.payload[0] = 1;
    first_sender->ipc_receive_buffer = UINT64_C(0x40001000);
    first_sender->ipc_reply_token = 101;
    first_sender->ipc_reply_callee = endpoints[2];

    second_sender->runtime_flags = MICROS_THREAD_RTS_IPC_SEND;
    second_sender->ipc_queue_kind = MICROS_IPC_QUEUE_SENDER;
    second_sender->ipc_send_destination = endpoints[2];
    second_sender->ipc_outbound_message.source = endpoints[1];
    second_sender->ipc_outbound_message.type = 20;
    second_sender->ipc_outbound_message.payload[0] = 2;

    sender_queue->sender_head = threads[0];
    sender_queue->sender_tail = threads[1];

    receiver->runtime_flags = MICROS_THREAD_RTS_IPC_RECEIVE;
    receiver->ipc_queue_kind = MICROS_IPC_QUEUE_RECEIVER;
    receiver->ipc_receive_source = MICROS_ENDPOINT_ANY;
    receiver->ipc_receive_buffer = UINT64_C(0x40000000);

    receiver_queue->receiver_head = threads[3];
    receiver_queue->receiver_tail = threads[3];

    objects.threads[threads[2].slot].runtime_flags =
        MICROS_THREAD_RTS_IPC_REPLY;
    objects.threads[threads[2].slot].ipc_receive_buffer =
        UINT64_C(0x40002000);
    objects.threads[threads[2].slot].ipc_reply_token = 102;
    objects.threads[threads[2].slot].ipc_reply_callee = endpoints[0];
}

static bool test_valid_queue_topology(void)
{
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;

    EXPECT_TRUE(setup_queue_fixture());
    construct_valid_queues();
    EXPECT_ENDPOINT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        )
    );
    EXPECT_TRUE(
        micros_thread_scheduler_hold(
            &objects,
            threads[0]
        ) == MICROS_KERNEL_OBJECT_OK
    );
    EXPECT_ENDPOINT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        )
    );
    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_ENDPOINT_ERROR(
        MICROS_ENDPOINT_ERROR_STATE,
        micros_endpoint_close(
            &registry,
            &objects,
            endpoints[1]
        )
    );
    EXPECT_TRUE(
        memcmp(
            &registry,
            &registry_snapshot,
            sizeof(registry)
        ) == 0
        && memcmp(&objects, &objects_snapshot, sizeof(objects)) == 0
    );
    EXPECT_ENDPOINT_ERROR(
        MICROS_ENDPOINT_ERROR_STATE,
        micros_endpoint_close(
            &registry,
            &objects,
            endpoints[2]
        )
    );
    EXPECT_TRUE(
        memcmp(
            &registry,
            &registry_snapshot,
            sizeof(registry)
        ) == 0
        && memcmp(&objects, &objects_snapshot, sizeof(objects)) == 0
    );
    return true;
}

static bool test_queue_validator_rejects_corruption(void)
{
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;

    EXPECT_TRUE(setup_queue_fixture());
    construct_valid_queues();
    registry_snapshot = registry;
    objects_snapshot = objects;

#define EXPECT_QUEUE_CORRUPTION(statement) \
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

    EXPECT_QUEUE_CORRUPTION(
        registry.endpoints[processes[2].slot].sender_tail =
            threads[0]
    );
    EXPECT_QUEUE_CORRUPTION(
        objects.threads[threads[1].slot].ipc_next = threads[0]
    );
    EXPECT_QUEUE_CORRUPTION(
        ++objects.threads[threads[0].slot].ipc_next.generation
    );
    EXPECT_QUEUE_CORRUPTION(
        objects.threads[threads[0].slot].ipc_queue_kind =
            MICROS_IPC_QUEUE_RECEIVER
    );
    EXPECT_QUEUE_CORRUPTION(
        objects.threads[threads[0].slot].runtime_flags &=
            ~MICROS_THREAD_RTS_IPC_SEND
    );
    EXPECT_QUEUE_CORRUPTION(
        objects.threads[threads[0].slot].ipc_send_destination =
            endpoints[3]
    );
    EXPECT_QUEUE_CORRUPTION(
        objects.threads[threads[0].slot]
            .ipc_outbound_message.type |= MICROS_IPC_TYPE_KERNEL_MASK
    );
    EXPECT_QUEUE_CORRUPTION(
        registry.endpoints[processes[3].slot].receiver_head =
            threads[0]
    );
    EXPECT_QUEUE_CORRUPTION(
        objects.threads[threads[3].slot].ipc_receive_source =
            endpoints[2] + (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS)
    );
    EXPECT_QUEUE_CORRUPTION(
        objects.threads[threads[2].slot].runtime_flags =
            MICROS_THREAD_RTS_IPC_SEND;
        objects.threads[threads[2].slot].ipc_receive_buffer = 0;
        objects.threads[threads[2].slot].ipc_reply_token = 0;
        objects.threads[threads[2].slot].ipc_reply_callee = 0;
        objects.threads[threads[2].slot].ipc_queue_kind =
            MICROS_IPC_QUEUE_SENDER;
        objects.threads[threads[2].slot].ipc_send_destination =
            endpoints[2];
        objects.threads[threads[2].slot]
            .ipc_outbound_message.source = endpoints[2]
    );
    EXPECT_QUEUE_CORRUPTION(
        objects.threads[threads[2].slot].runtime_flags =
            MICROS_THREAD_RTS_IPC_SEND;
        objects.threads[threads[2].slot].ipc_receive_buffer = 0;
        objects.threads[threads[2].slot].ipc_reply_token = 0;
        objects.threads[threads[2].slot].ipc_reply_callee = 0
    );
    EXPECT_QUEUE_CORRUPTION(
        registry.endpoints[processes[3].slot].receiver_head =
            threads[0];
        registry.endpoints[processes[3].slot].receiver_tail =
            threads[0]
    );
    EXPECT_QUEUE_CORRUPTION(
        objects.threads[threads[1].slot].runtime_flags |=
            MICROS_THREAD_RTS_IPC_REPLY;
        objects.threads[threads[1].slot]
            .ipc_outbound_message.reply_token = 101;
        objects.threads[threads[1].slot].ipc_receive_buffer =
            UINT64_C(0x40003000);
        objects.threads[threads[1].slot].ipc_reply_token = 101;
        objects.threads[threads[1].slot].ipc_reply_callee =
            endpoints[2]
    );
    EXPECT_QUEUE_CORRUPTION(
        objects.threads[threads[2].slot].runtime_flags =
            MICROS_THREAD_RTS_IPC_RECEIVE;
        objects.threads[threads[2].slot].ipc_receive_buffer =
            UINT64_C(0x40004000);
        objects.threads[threads[2].slot].ipc_receive_source =
            MICROS_ENDPOINT_ANY;
        objects.threads[threads[2].slot].ipc_reply_token = 0;
        objects.threads[threads[2].slot].ipc_reply_callee = 0;
        objects.threads[threads[2].slot].ipc_queue_kind =
            MICROS_IPC_QUEUE_RECEIVER;
        registry.endpoints[processes[2].slot].receiver_head =
            threads[2];
        registry.endpoints[processes[2].slot].receiver_tail =
            threads[2]
    );

#undef EXPECT_QUEUE_CORRUPTION
    return true;
}

static bool test_close_rejects_foreign_waiter(void)
{
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;
    struct micros_thread *receiver;
    struct micros_endpoint_record *receiver_queue;

    EXPECT_TRUE(setup_queue_fixture());
    receiver = &objects.threads[threads[3].slot];
    receiver_queue = &registry.endpoints[processes[3].slot];
    receiver->runtime_flags = MICROS_THREAD_RTS_IPC_RECEIVE;
    receiver->ipc_queue_kind = MICROS_IPC_QUEUE_RECEIVER;
    receiver->ipc_receive_source = endpoints[2];
    receiver->ipc_receive_buffer = UINT64_C(0x40005000);
    receiver_queue->receiver_head = threads[3];
    receiver_queue->receiver_tail = threads[3];
    EXPECT_ENDPOINT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        )
    );
    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_ENDPOINT_ERROR(
        MICROS_ENDPOINT_ERROR_STATE,
        micros_endpoint_close(
            &registry,
            &objects,
            endpoints[2]
        )
    );
    EXPECT_TRUE(
        memcmp(
            &registry,
            &registry_snapshot,
            sizeof(registry)
        ) == 0
        && memcmp(&objects, &objects_snapshot, sizeof(objects)) == 0
    );
    return true;
}

static bool test_staged_delivery_state(void)
{
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;
    struct micros_thread *delivered;

    EXPECT_TRUE(setup_queue_fixture());
    EXPECT_TRUE(
        micros_thread_runtime_flags_unset(
            &objects,
            threads[0],
            MICROS_THREAD_RTS_INACTIVE
        ) == MICROS_KERNEL_OBJECT_OK
    );
    delivered = &objects.threads[threads[0].slot];
    delivered->ipc_receive_buffer = UINT64_C(0x40006000);
    delivered->ipc_delivery_pending = true;
    delivered->ipc_inbound_message.source = endpoints[1];
    delivered->ipc_inbound_message.type = 60;
    delivered->ipc_inbound_message.payload[0] = 6;
    EXPECT_ENDPOINT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        )
    );
    registry_snapshot = registry;
    objects_snapshot = objects;

#define EXPECT_STAGED_CORRUPTION(statement) \
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

    EXPECT_STAGED_CORRUPTION(
        objects.threads[threads[0].slot]
            .ipc_inbound_message.type |= MICROS_IPC_TYPE_KERNEL_MASK
    );
    EXPECT_STAGED_CORRUPTION(
        objects.threads[threads[0].slot]
            .ipc_inbound_message.source +=
                UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS
    );
    EXPECT_STAGED_CORRUPTION(
        objects.threads[threads[0].slot].ipc_delivery_pending = false
    );
    EXPECT_STAGED_CORRUPTION(
        objects.threads[threads[0].slot].ipc_receive_buffer += 4
    );
    EXPECT_STAGED_CORRUPTION(
        registry.last_reply_token = 1;
        objects.threads[threads[0].slot]
            .ipc_inbound_message.reply_token = 1
    );
    EXPECT_STAGED_CORRUPTION(
        objects.threads[threads[0].slot].runtime_flags |=
            MICROS_THREAD_RTS_IPC_RECEIVE
    );

#undef EXPECT_STAGED_CORRUPTION
    return true;
}

bool micros_ipc_queue_test_run(void)
{
    return (
        test_valid_queue_topology()
        && test_queue_validator_rejects_corruption()
        && test_close_rejects_foreign_waiter()
        && test_staged_delivery_state()
    );
}
