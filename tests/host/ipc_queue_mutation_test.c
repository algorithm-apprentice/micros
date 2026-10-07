#include "micros/ipc_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/scheduler_core.h"

enum {
    MUTATION_PROCESS_COUNT = 4,
};

static struct micros_endpoint_registry registry;
static struct micros_kernel_objects objects;
static struct micros_process_handle processes[MUTATION_PROCESS_COUNT];
static struct micros_thread_handle threads[MUTATION_PROCESS_COUNT];
static micros_endpoint_t endpoints[MUTATION_PROCESS_COUNT];
static struct micros_hart_handle hart;

bool micros_ipc_queue_mutation_test_run(void);

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

#define EXPECT_IPC_ERROR(expected, expression) \
    do { \
        enum micros_ipc_error actual = (expression); \
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

static bool setup_mutation_fixture(void)
{
    static const struct micros_privilege_profile profile = {
        .id = 1,
        .name = "MUTATION",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL
            | MICROS_PRIVILEGE_OPERATION_SEND,
        .call_targets = UINT32_C(1) << 1,
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
    for (index = 0; index < MUTATION_PROCESS_COUNT; ++index) {
        struct micros_user_context context =
            context_pattern(UINT64_C(0x3000) + index * UINT64_C(0x100));
        uintptr_t stack_bottom =
            UINT64_C(0x20000000)
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

static struct micros_ipc_message message_pattern(
    uint32_t source,
    uint32_t type,
    uint64_t token,
    uint8_t payload
)
{
    struct micros_ipc_message message;

    memset(&message, 0, sizeof(message));
    message.source = source;
    message.type = type;
    message.reply_token = token;
    message.payload[0] = payload;
    message.payload[47] = (uint8_t)(payload + 1);
    return message;
}

static bool test_sender_and_receiver_enqueue(void)
{
    struct micros_ipc_message ordinary =
        message_pattern(MICROS_ENDPOINT_ANY, 10, 999, 1);
    struct micros_ipc_message call =
        message_pattern(MICROS_ENDPOINT_NONE, 20, 888, 2);
    const struct micros_thread *first;
    const struct micros_thread *second;
    const struct micros_thread *receiver;

    EXPECT_TRUE(setup_mutation_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_sender_enqueue(
            &registry,
            &objects,
            threads[0],
            endpoints[2],
            &ordinary,
            0,
            0
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_sender_enqueue(
            &registry,
            &objects,
            threads[1],
            endpoints[2],
            &call,
            77,
            UINT64_C(0x40001000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            threads[3],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x40002000)
        )
    );
    first = &objects.threads[threads[0].slot];
    second = &objects.threads[threads[1].slot];
    receiver = &objects.threads[threads[3].slot];
    EXPECT_TRUE(
        registry.endpoints[processes[2].slot].sender_head.slot
            == threads[0].slot
        && registry.endpoints[processes[2].slot].sender_tail.slot
            == threads[1].slot
        && first->ipc_next.slot == threads[1].slot
        && first->ipc_outbound_message.source == endpoints[0]
        && first->ipc_outbound_message.reply_token == 0
        && first->ipc_outbound_message.type == ordinary.type
        && first->ipc_outbound_message.payload[0] == 1
        && first->runtime_flags == MICROS_THREAD_RTS_IPC_SEND
        && second->ipc_outbound_message.source == endpoints[1]
        && second->ipc_outbound_message.reply_token == 77
        && second->ipc_reply_token == 77
        && second->ipc_reply_callee == endpoints[2]
        && second->ipc_receive_buffer == UINT64_C(0x40001000)
        && second->runtime_flags
            == (
                MICROS_THREAD_RTS_IPC_SEND
                | MICROS_THREAD_RTS_IPC_REPLY
            )
        && registry.endpoints[processes[3].slot].receiver_head.slot
            == threads[3].slot
        && registry.endpoints[processes[3].slot].receiver_tail.slot
            == threads[3].slot
        && receiver->ipc_receive_source == MICROS_ENDPOINT_ANY
        && receiver->ipc_receive_buffer == UINT64_C(0x40002000)
        && receiver->runtime_flags
            == MICROS_THREAD_RTS_IPC_RECEIVE
    );
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(&registry, &objects)
            == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool test_enqueue_failures_preserve_state(void)
{
    struct micros_ipc_message message =
        message_pattern(123, 30, 456, 3);
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;

    EXPECT_TRUE(setup_mutation_fixture());
    registry_snapshot = registry;
    objects_snapshot = objects;

#define EXPECT_REJECTED(expected, expression) \
    do { \
        registry = registry_snapshot; \
        objects = objects_snapshot; \
        EXPECT_IPC_ERROR((expected), (expression)); \
        EXPECT_TRUE( \
            memcmp( \
                &registry, \
                &registry_snapshot, \
                sizeof(registry) \
            ) == 0 \
            && memcmp( \
                &objects, \
                &objects_snapshot, \
                sizeof(objects) \
            ) == 0 \
        ); \
    } while (false)

    EXPECT_REJECTED(
        MICROS_IPC_ERROR_ARGUMENT,
        micros_ipc_sender_enqueue(
            &registry,
            &objects,
            threads[0],
            endpoints[2],
            &message,
            1,
            0
        )
    );
    message.type |= MICROS_IPC_TYPE_KERNEL_MASK;
    EXPECT_REJECTED(
        MICROS_IPC_ERROR_ARGUMENT,
        micros_ipc_sender_enqueue(
            &registry,
            &objects,
            threads[0],
            endpoints[2],
            &message,
            0,
            0
        )
    );
    message = message_pattern(123, 30, 456, 3);
    EXPECT_REJECTED(
        MICROS_IPC_ERROR_ARGUMENT,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            threads[3],
            MICROS_ENDPOINT_NONE,
            UINT64_C(0x40002000)
        )
    );
    EXPECT_REJECTED(
        MICROS_IPC_ERROR_ARGUMENT,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            threads[3],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x40002004)
        )
    );

#undef EXPECT_REJECTED
    return true;
}

static bool test_matching_peer_is_not_enqueued(void)
{
    struct micros_ipc_message message =
        message_pattern(0, 40, 0, 4);
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;

    EXPECT_TRUE(setup_mutation_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            threads[3],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x40003000)
        )
    );
    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_NOT_READY,
        micros_ipc_sender_enqueue(
            &registry,
            &objects,
            threads[0],
            endpoints[3],
            &message,
            0,
            0
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

    EXPECT_TRUE(setup_mutation_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_sender_enqueue(
            &registry,
            &objects,
            threads[0],
            endpoints[3],
            &message,
            0,
            0
        )
    );
    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_NOT_READY,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            threads[3],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x40003000)
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

static bool test_duplicate_token_and_thread_are_rejected(void)
{
    struct micros_ipc_message message =
        message_pattern(0, 50, 0, 5);
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;

    EXPECT_TRUE(setup_mutation_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_sender_enqueue(
            &registry,
            &objects,
            threads[0],
            endpoints[2],
            &message,
            77,
            UINT64_C(0x40004000)
        )
    );
    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_REPLY_TOKEN,
        micros_ipc_sender_enqueue(
            &registry,
            &objects,
            threads[1],
            endpoints[2],
            &message,
            77,
            UINT64_C(0x40005000)
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
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_STATE,
        micros_ipc_sender_enqueue(
            &registry,
            &objects,
            threads[0],
            endpoints[2],
            &message,
            0,
            0
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

bool micros_ipc_queue_mutation_test_run(void)
{
    return (
        test_sender_and_receiver_enqueue()
        && test_enqueue_failures_preserve_state()
        && test_matching_peer_is_not_enqueued()
        && test_duplicate_token_and_thread_are_rejected()
    );
}
