#include "micros/ipc_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/scheduler_core.h"

#define REFERENCE_NONE MICROS_THREAD_CAPACITY

enum reference_deadlock_result {
    REFERENCE_DEADLOCK_CLEAR = 0,
    REFERENCE_DEADLOCK_CANDIDATE,
    REFERENCE_DEADLOCK_REPEATED,
    REFERENCE_DEADLOCK_CORRUPT,
};

static struct micros_endpoint_registry registry;
static struct micros_kernel_objects objects;
static struct micros_process_handle processes[MICROS_PROCESS_CAPACITY];
static struct micros_thread_handle primary_threads[MICROS_PROCESS_CAPACITY];
static micros_endpoint_t endpoints[MICROS_PROCESS_CAPACITY];
static struct micros_hart_handle hart;

bool micros_ipc_deadlock_test_run(void);

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

static bool thread_handle_is_zero(struct micros_thread_handle handle)
{
    return handle.slot == 0 && handle.generation == 0;
}

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

static struct micros_ipc_message message_pattern(
    uint32_t type,
    uint8_t payload_seed
)
{
    struct micros_ipc_message message;
    size_t index;

    memset(&message, 0, sizeof(message));
    message.source = MICROS_ENDPOINT_ANY;
    message.type = type;
    message.reply_token = UINT64_C(0xfeedfacecafebeef);
    for (index = 0; index < sizeof(message.payload); ++index) {
        message.payload[index] =
            (uint8_t)(payload_seed + (uint8_t)index);
    }
    return message;
}

static bool setup_endpoints(
    size_t process_count,
    size_t max_threads_per_process
)
{
    static const struct micros_privilege_profile profiles[] = {
        {
            .id = 1,
            .name = "PEER",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_CALL
                | MICROS_PRIVILEGE_OPERATION_SEND
                | MICROS_PRIVILEGE_OPERATION_REPLY
                | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
            .call_targets = UINT32_C(1) << 1,
            .send_targets = UINT32_C(1) << 1,
        },
    };
    size_t index;

    if (
        process_count == 0
        || process_count > MICROS_PROCESS_CAPACITY
    ) {
        return false;
    }
    memset(&registry, 0, sizeof(registry));
    memset(&objects, 0, sizeof(objects));
    memset(processes, 0, sizeof(processes));
    memset(primary_threads, 0, sizeof(primary_threads));
    memset(endpoints, 0, sizeof(endpoints));
    if (
        micros_endpoint_registry_initialize(
            &registry,
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) != MICROS_ENDPOINT_OK
        || micros_kernel_objects_initialize(
            &objects,
            max_threads_per_process,
            1
        ) != MICROS_KERNEL_OBJECT_OK
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
    for (index = 0; index < process_count; ++index) {
        if (
            micros_process_create(&objects, &processes[index])
                != MICROS_KERNEL_OBJECT_OK
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
        ) {
            return false;
        }
    }
    return true;
}

static bool prepare_thread(
    size_t process_index,
    uint64_t context_base,
    uintptr_t stack_bottom,
    struct micros_thread_handle *thread
)
{
    struct micros_user_context context =
        context_pattern(context_base);

    return (
        micros_thread_create(
            &objects,
            processes[process_index],
            thread
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_thread_attach_execution_context(
            &objects,
            *thread,
            stack_bottom,
            stack_bottom + MICROS_THREAD_KERNEL_STACK_SIZE,
            &context
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_thread_scheduler_admit(
            &objects,
            hart,
            *thread,
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            100,
            true
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_thread_scheduler_hold(&objects, *thread)
            == MICROS_KERNEL_OBJECT_OK
    );
}

static bool setup_fixture(
    size_t process_count,
    size_t max_threads_per_process
)
{
    size_t index;

    if (!setup_endpoints(process_count, max_threads_per_process)) {
        return false;
    }
    for (index = 0; index < process_count; ++index) {
        if (
            !prepare_thread(
                index,
                UINT64_C(0x1000)
                    + index * UINT64_C(0x100),
                UINT64_C(0x10000000)
                    + index * UINT64_C(0x00008000),
                &primary_threads[index]
            )
        ) {
            return false;
        }
    }
    return micros_endpoint_registry_validate_objects(
        &registry,
        &objects
    ) == MICROS_ENDPOINT_OK;
}

static bool state_is_unchanged(
    const struct micros_endpoint_registry *registry_snapshot,
    const struct micros_kernel_objects *objects_snapshot
)
{
    return (
        memcmp(
            &registry,
            registry_snapshot,
            sizeof(registry)
        ) == 0
        && memcmp(
            &objects,
            objects_snapshot,
            sizeof(objects)
        ) == 0
    );
}

static bool expect_send_failure_unchanged(
    enum micros_ipc_error expected,
    struct micros_thread_handle sender,
    micros_endpoint_t destination
)
{
    struct micros_endpoint_registry registry_snapshot = registry;
    struct micros_kernel_objects objects_snapshot = objects;
    struct micros_ipc_message message =
        message_pattern(UINT32_C(0x7001), UINT8_C(0x10));
    struct micros_ipc_message message_snapshot = message;

    EXPECT_IPC_ERROR(
        expected,
        micros_ipc_send(
            &registry,
            &objects,
            sender,
            destination,
            &message
        )
    );
    EXPECT_TRUE(state_is_unchanged(
        &registry_snapshot,
        &objects_snapshot
    ));
    EXPECT_TRUE(
        memcmp(&message, &message_snapshot, sizeof(message)) == 0
    );
    return true;
}

static bool expect_call_failure_unchanged(
    enum micros_ipc_error expected,
    struct micros_thread_handle caller,
    micros_endpoint_t destination
)
{
    struct micros_endpoint_registry registry_snapshot = registry;
    struct micros_kernel_objects objects_snapshot = objects;
    struct micros_ipc_message message =
        message_pattern(UINT32_C(0x7002), UINT8_C(0x20));
    struct micros_ipc_message message_snapshot = message;

    EXPECT_IPC_ERROR(
        expected,
        micros_ipc_call(
            &registry,
            &objects,
            caller,
            destination,
            &message,
            (uintptr_t)&message
        )
    );
    EXPECT_TRUE(state_is_unchanged(
        &registry_snapshot,
        &objects_snapshot
    ));
    EXPECT_TRUE(
        memcmp(&message, &message_snapshot, sizeof(message)) == 0
    );
    return true;
}

static bool expect_receive_failure_unchanged(
    enum micros_ipc_error expected,
    struct micros_thread_handle receiver,
    micros_endpoint_t source
)
{
    struct micros_endpoint_registry registry_snapshot = registry;
    struct micros_kernel_objects objects_snapshot = objects;

    EXPECT_IPC_ERROR(
        expected,
        micros_ipc_receive(
            &registry,
            &objects,
            receiver,
            source,
            UINT64_C(0x50000000)
        )
    );
    EXPECT_TRUE(state_is_unchanged(
        &registry_snapshot,
        &objects_snapshot
    ));
    return true;
}

static bool direct_block_send(size_t sender_index, size_t destination_index)
{
    struct micros_thread *sender =
        &objects.threads[primary_threads[sender_index].slot];
    struct micros_endpoint_record *destination =
        &registry.endpoints[processes[destination_index].slot];
    struct micros_ipc_message message =
        message_pattern(
            UINT32_C(0x7100) + (uint32_t)sender_index,
            (uint8_t)sender_index
        );

    if (
        sender->runtime_flags != MICROS_THREAD_RTS_INACTIVE
        || sender->ready_linked
        || sender->ipc_queue_kind != MICROS_IPC_QUEUE_NONE
        || !thread_handle_is_zero(destination->sender_head)
        || !thread_handle_is_zero(destination->sender_tail)
    ) {
        return false;
    }
    message.source = endpoints[sender_index];
    message.reply_token = 0;
    sender->runtime_flags = MICROS_THREAD_RTS_IPC_SEND;
    sender->ipc_queue_kind = MICROS_IPC_QUEUE_SENDER;
    sender->ipc_next.slot = 0;
    sender->ipc_next.generation = 0;
    sender->ipc_outbound_message = message;
    sender->ipc_send_destination = endpoints[destination_index];
    destination->sender_head = primary_threads[sender_index];
    destination->sender_tail = primary_threads[sender_index];
    return true;
}

static bool clear_staged_request(
    struct micros_thread_handle receiver_handle,
    uint64_t *reply_token
)
{
    struct micros_thread *receiver =
        &objects.threads[receiver_handle.slot];

    if (
        !receiver->ipc_delivery_pending
        || receiver->ipc_inbound_message.reply_token == 0
        || reply_token == NULL
    ) {
        return false;
    }
    *reply_token = receiver->ipc_inbound_message.reply_token;
    if (
        micros_thread_scheduler_hold(&objects, receiver_handle)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    receiver->ipc_receive_buffer = 0;
    receiver->ipc_delivery_pending = false;
    memset(
        &receiver->ipc_inbound_message,
        0,
        sizeof(receiver->ipc_inbound_message)
    );
    receiver->ipc_staged_result = MICROS_IPC_OK;
    return micros_endpoint_registry_validate_objects(
        &registry,
        &objects
    ) == MICROS_ENDPOINT_OK;
}

static bool prepare_delivered_call(
    size_t caller_index,
    size_t server_index,
    uint64_t *reply_token
)
{
    struct micros_ipc_message request =
        message_pattern(UINT32_C(0x7200), UINT8_C(0x30));

    return (
        micros_ipc_receive(
            &registry,
            &objects,
            primary_threads[server_index],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x51000000)
                + server_index * UINT64_C(0x1000)
        ) == MICROS_IPC_OK
        && micros_ipc_call(
            &registry,
            &objects,
            primary_threads[caller_index],
            endpoints[server_index],
            &request,
            (uintptr_t)&request
) == MICROS_IPC_OK
        && clear_staged_request(
            primary_threads[server_index],
            reply_token
        )
    );
}

static enum reference_deadlock_result reference_deadlock_walk(
    size_t first,
    size_t candidate,
    const size_t dependencies[MICROS_THREAD_CAPACITY]
)
{
    bool visited[MICROS_THREAD_CAPACITY] = {false};
    size_t current = first;
    size_t steps;

    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        if (current == REFERENCE_NONE) {
            return REFERENCE_DEADLOCK_CLEAR;
        }
        if (current >= MICROS_THREAD_CAPACITY) {
            return REFERENCE_DEADLOCK_CORRUPT;
        }
        if (current == candidate) {
            return REFERENCE_DEADLOCK_CANDIDATE;
        }
        if (visited[current]) {
            return REFERENCE_DEADLOCK_REPEATED;
        }
        visited[current] = true;
        current = dependencies[current];
    }
    return current == REFERENCE_NONE
        ? REFERENCE_DEADLOCK_CLEAR
        : REFERENCE_DEADLOCK_CORRUPT;
}

static bool test_bounded_reference_model(void)
{
    size_t dependencies[MICROS_THREAD_CAPACITY];
    size_t length;
    size_t index;

    for (length = 1; length <= MICROS_THREAD_CAPACITY; ++length) {
        for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
            dependencies[index] = REFERENCE_NONE;
        }
        for (index = 0; index + 1 < length; ++index) {
            dependencies[index] = index + 1;
        }
        EXPECT_TRUE(
            reference_deadlock_walk(
                0,
                length - 1,
                dependencies
            ) == REFERENCE_DEADLOCK_CANDIDATE
        );
    }

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        dependencies[index] = index + 1 < MICROS_THREAD_CAPACITY
            ? index + 1
            : REFERENCE_NONE;
    }
    EXPECT_TRUE(
        reference_deadlock_walk(
            0,
            REFERENCE_NONE,
            dependencies
        ) == REFERENCE_DEADLOCK_CLEAR
    );
    dependencies[MICROS_THREAD_CAPACITY - 1] = 0;
    EXPECT_TRUE(
        reference_deadlock_walk(
            0,
            REFERENCE_NONE,
            dependencies
        ) == REFERENCE_DEADLOCK_CORRUPT
    );
    dependencies[0] = 1;
    dependencies[1] = 0;
    EXPECT_TRUE(
        reference_deadlock_walk(
            0,
            2,
            dependencies
        ) == REFERENCE_DEADLOCK_REPEATED
    );
    return true;
}

static bool test_all_endpoint_chain_lengths(void)
{
    struct micros_endpoint_registry baseline_registry;
    struct micros_kernel_objects baseline_objects;
    size_t length;
    size_t index;

    EXPECT_TRUE(setup_fixture(MICROS_PROCESS_CAPACITY, 1));
    baseline_registry = registry;
    baseline_objects = objects;

    for (length = 1; length <= MICROS_PROCESS_CAPACITY; ++length) {
        registry = baseline_registry;
        objects = baseline_objects;
        for (index = 0; index + 1 < length; ++index) {
            EXPECT_TRUE(direct_block_send(index, index + 1));
        }
        EXPECT_TRUE(
            micros_endpoint_registry_validate_objects(
                &registry,
                &objects
            ) == MICROS_ENDPOINT_OK
        );
        EXPECT_TRUE(
            expect_send_failure_unchanged(
                MICROS_IPC_ERROR_DEADLOCK,
                primary_threads[length - 1],
                endpoints[0]
            )
        );
    }

    registry = baseline_registry;
    objects = baseline_objects;
    for (index = 1; index + 1 < MICROS_PROCESS_CAPACITY; ++index) {
        EXPECT_TRUE(direct_block_send(index, index + 1));
    }
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    {
        struct micros_ipc_message message =
            message_pattern(UINT32_C(0x7300), UINT8_C(0x40));

        EXPECT_IPC_ERROR(
            MICROS_IPC_OK,
            micros_ipc_send(
                &registry,
                &objects,
                primary_threads[0],
                endpoints[1],
                &message
            )
        );
    }
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool test_receive_and_call_cycles(void)
{
    struct micros_ipc_message request =
        message_pattern(UINT32_C(0x7400), UINT8_C(0x50));

    EXPECT_TRUE(setup_fixture(2, 1));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            primary_threads[0],
            endpoints[1],
            UINT64_C(0x52000000)
        )
    );
    EXPECT_TRUE(
        expect_receive_failure_unchanged(
            MICROS_IPC_ERROR_DEADLOCK,
            primary_threads[1],
            endpoints[0]
        )
    );

    EXPECT_TRUE(setup_fixture(2, 1));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &registry,
            &objects,
            primary_threads[0],
            endpoints[1],
            &request,
            (uintptr_t)&request
        )
    );
    EXPECT_TRUE(registry.last_reply_token == 1);
    EXPECT_TRUE(
        expect_call_failure_unchanged(
            MICROS_IPC_ERROR_DEADLOCK,
            primary_threads[1],
            endpoints[0]
        )
    );
    return true;
}

static bool test_dependency_precedence_and_any_termination(void)
{
    struct micros_ipc_message request =
        message_pattern(UINT32_C(0x7500), UINT8_C(0x60));
    const struct micros_thread *queued_call;
    uint64_t reply_token;

    EXPECT_TRUE(setup_fixture(4, 1));
    EXPECT_TRUE(prepare_delivered_call(2, 3, &reply_token));
    EXPECT_TRUE(reply_token == 1);
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            primary_threads[3],
            endpoints[0],
            UINT64_C(0x53000000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &registry,
            &objects,
            primary_threads[1],
            endpoints[2],
            &request,
            (uintptr_t)&request
        )
    );
    queued_call = &objects.threads[primary_threads[1].slot];
    EXPECT_TRUE(
        (
            queued_call->runtime_flags
            & MICROS_THREAD_RTS_IPC_MASK
        ) == (
            MICROS_THREAD_RTS_IPC_SEND
            | MICROS_THREAD_RTS_IPC_REPLY
        )
    );
    EXPECT_TRUE(
        expect_send_failure_unchanged(
            MICROS_IPC_ERROR_DEADLOCK,
            primary_threads[0],
            endpoints[1]
        )
    );

    EXPECT_TRUE(setup_fixture(2, 1));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            primary_threads[1],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x53001000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            primary_threads[0],
            endpoints[1],
            UINT64_C(0x53002000)
        )
    );
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool test_reply_receive_uses_post_reply_graph(void)
{
    struct micros_ipc_message reply =
        message_pattern(UINT32_C(0x7600), UINT8_C(0x70));
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;
    struct micros_ipc_message reply_snapshot;
    const struct micros_thread *caller;
    const struct micros_thread *server;
    uint64_t reply_token;

    EXPECT_TRUE(setup_fixture(2, 1));
    EXPECT_TRUE(prepare_delivered_call(0, 1, &reply_token));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_reply_receive(
            &registry,
            &objects,
            primary_threads[1],
            reply_token,
            &reply,
            endpoints[0],
            UINT64_C(0x54000000)
        )
    );
    caller = &objects.threads[primary_threads[0].slot];
    server = &objects.threads[primary_threads[1].slot];
    EXPECT_TRUE(
        caller->runtime_flags == 0
        && caller->ready_linked
        && caller->ipc_reply_token == 0
        && caller->ipc_delivery_pending
        && server->runtime_flags == MICROS_THREAD_RTS_IPC_RECEIVE
        && server->ipc_queue_kind == MICROS_IPC_QUEUE_RECEIVER
        && server->ipc_receive_source == endpoints[0]
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );

    EXPECT_TRUE(setup_fixture(3, 1));
    EXPECT_TRUE(prepare_delivered_call(0, 1, &reply_token));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            primary_threads[2],
            endpoints[1],
            UINT64_C(0x54001000)
        )
    );
    registry_snapshot = registry;
    objects_snapshot = objects;
    reply_snapshot = reply;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_DEADLOCK,
        micros_ipc_reply_receive(
            &registry,
            &objects,
            primary_threads[1],
            reply_token,
            &reply,
            endpoints[2],
            UINT64_C(0x54002000)
        )
    );
    EXPECT_TRUE(state_is_unchanged(
        &registry_snapshot,
        &objects_snapshot
    ));
    EXPECT_TRUE(
        memcmp(&reply, &reply_snapshot, sizeof(reply)) == 0
    );
    return true;
}

static bool test_projection_and_corruption_are_atomic(void)
{
    struct micros_thread_handle extra_thread;
    micros_endpoint_t stale_endpoint;

    EXPECT_TRUE(setup_endpoints(2, 2));
    EXPECT_TRUE(
        prepare_thread(
            0,
            UINT64_C(0x8000),
            UINT64_C(0x18000000),
            &primary_threads[0]
        )
    );
    EXPECT_TRUE(
        expect_send_failure_unchanged(
            MICROS_IPC_ERROR_INVARIANT,
            primary_threads[0],
            endpoints[1]
        )
    );

    EXPECT_TRUE(setup_fixture(2, 2));
    EXPECT_TRUE(
        prepare_thread(
            1,
            UINT64_C(0x8100),
            UINT64_C(0x18008000),
            &extra_thread
        )
    );
    EXPECT_TRUE(
        expect_send_failure_unchanged(
            MICROS_IPC_ERROR_INVARIANT,
            primary_threads[0],
            endpoints[1]
        )
    );

    EXPECT_TRUE(setup_fixture(3, 1));
    EXPECT_TRUE(
        direct_block_send(1, 2)
        && direct_block_send(2, 1)
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    EXPECT_TRUE(
        expect_send_failure_unchanged(
            MICROS_IPC_ERROR_INVARIANT,
            primary_threads[0],
            endpoints[1]
        )
    );

    EXPECT_TRUE(setup_fixture(2, 1));
    stale_endpoint =
        endpoints[1] + (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS);
    EXPECT_TRUE(
        expect_send_failure_unchanged(
            MICROS_IPC_ERROR_DEAD_ENDPOINT,
            primary_threads[0],
            stale_endpoint
        )
    );
    return true;
}

bool micros_ipc_deadlock_test_run(void)
{
    static const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {
            "bounded reference model",
            test_bounded_reference_model,
        },
        {
            "all endpoint chain lengths",
            test_all_endpoint_chain_lengths,
        },
        {
            "receive and call cycles",
            test_receive_and_call_cycles,
        },
        {
            "dependency precedence and ANY termination",
            test_dependency_precedence_and_any_termination,
        },
        {
            "reply_receive post-reply graph",
            test_reply_receive_uses_post_reply_graph,
        },
        {
            "projection and corruption atomicity",
            test_projection_and_corruption_are_atomic,
        },
    };
    size_t index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run()) {
            fprintf(
                stderr,
                "IPC deadlock subtest failed: %s\n",
                tests[index].name
            );
            return false;
        }
    }
    return true;
}
