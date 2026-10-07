#include "micros/ipc_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/scheduler_core.h"

enum {
    REPLY_PROCESS_CLIENT = 0,
    REPLY_PROCESS_SERVER,
    REPLY_PROCESS_OTHER_SERVER,
    REPLY_PROCESS_NO_REPLY_SERVER,
    REPLY_PROCESS_REPLY_ONLY_SERVER,
    REPLY_PROCESS_REPLY_RECEIVE_ONLY_SERVER,
    REPLY_PROCESS_RESERVED_SERVER,
    REPLY_PROCESS_COUNT,
};

static struct micros_endpoint_registry registry;
static struct micros_kernel_objects objects;
static struct micros_process_handle processes[REPLY_PROCESS_COUNT];
static struct micros_thread_handle primary_threads[REPLY_PROCESS_COUNT];
static struct micros_thread_handle client_extra_thread;
static struct micros_thread_handle server_extra_thread;
static micros_endpoint_t endpoints[REPLY_PROCESS_COUNT];
static struct micros_hart_handle hart;

bool micros_ipc_reply_test_run(void);

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
    uint32_t source,
    uint32_t type,
    uint64_t reply_token,
    uint8_t payload_seed
)
{
    struct micros_ipc_message message;
    size_t index;

    memset(&message, 0, sizeof(message));
    message.source = source;
    message.type = type;
    message.reply_token = reply_token;
    for (index = 0; index < sizeof(message.payload); ++index) {
        message.payload[index] =
            (uint8_t)(payload_seed + (uint8_t)index);
    }
    return message;
}

static struct micros_ipc_message canonical_reply(
    const struct micros_ipc_message *message,
    micros_endpoint_t source
)
{
    struct micros_ipc_message canonical = *message;

    canonical.source = source;
    canonical.reply_token = 0;
    return canonical;
}

static bool prepare_thread(
    struct micros_process_handle owner,
    uint64_t context_base,
    uintptr_t stack_bottom,
    struct micros_thread_handle *thread
)
{
    struct micros_user_context context =
        context_pattern(context_base);

    return (
        micros_thread_create(&objects, owner, thread)
            == MICROS_KERNEL_OBJECT_OK
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

static bool setup_reply_fixture(void)
{
    static const struct micros_privilege_profile profiles[] = {
        {
            .id = 1,
            .name = "CLIENT",
            .operations = MICROS_PRIVILEGE_OPERATION_CALL,
            .call_targets =
                (UINT32_C(1) << 2)
                | (UINT32_C(1) << 3)
                | (UINT32_C(1) << 4)
                | (UINT32_C(1) << 5),
        },
        {
            .id = 2,
            .name = "REPLY_SERVER",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_SEND
                | MICROS_PRIVILEGE_OPERATION_REPLY
                | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
            .send_targets = UINT32_C(1) << 2,
        },
        {
            .id = 3,
            .name = "NO_REPLY_SERVER",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_SEND,
            .send_targets = UINT32_C(1) << 2,
        },
        {
            .id = 4,
            .name = "REPLY_ONLY_SERVER",
            .operations = MICROS_PRIVILEGE_OPERATION_REPLY,
        },
        {
            .id = 5,
            .name = "REPLY_RECEIVE_ONLY_SERVER",
            .operations =
                MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
        },
    };
    static const uint8_t profile_ids[REPLY_PROCESS_COUNT] = {
        1,
        2,
        2,
        3,
        4,
        5,
        2,
    };
    size_t index;

    memset(&registry, 0, sizeof(registry));
    memset(&objects, 0, sizeof(objects));
    if (
        micros_endpoint_registry_initialize(
            &registry,
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) != MICROS_ENDPOINT_OK
        || micros_kernel_objects_initialize(&objects, 2, 1)
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
    for (index = 0; index < REPLY_PROCESS_COUNT; ++index) {
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
                profile_ids[index]
            ) != MICROS_ENDPOINT_OK
            || (
                index != REPLY_PROCESS_RESERVED_SERVER
                && micros_endpoint_activate(
                    &registry,
                    &objects,
                    endpoints[index]
                ) != MICROS_ENDPOINT_OK
            )
        ) {
            return false;
        }
    }
    for (index = 0; index < REPLY_PROCESS_COUNT; ++index) {
        if (
            !prepare_thread(
                processes[index],
                UINT64_C(0x3000) + index * UINT64_C(0x100),
                UINT64_C(0x12000000)
                    + index * UINT64_C(0x00008000),
                &primary_threads[index]
            )
        ) {
            return false;
        }
    }
    return (
        prepare_thread(
            processes[REPLY_PROCESS_CLIENT],
            UINT64_C(0x4000),
            UINT64_C(0x13000000),
            &client_extra_thread
        )
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
}

static bool prepare_server_extra_thread(void)
{
    return prepare_thread(
        processes[REPLY_PROCESS_SERVER],
        UINT64_C(0x4100),
        UINT64_C(0x13008000),
        &server_extra_thread
    );
}

static bool clear_staged_request(
    struct micros_thread_handle receiver_handle,
    struct micros_ipc_message *delivered
)
{
    struct micros_thread *receiver =
        &objects.threads[receiver_handle.slot];

    if (
        !receiver->ipc_delivery_pending
        || receiver->ipc_inbound_message.reply_token == 0
    ) {
        return false;
    }
    if (delivered != NULL) {
        *delivered = receiver->ipc_inbound_message;
    }
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

static bool stage_delivered_call(
    struct micros_thread_handle caller,
    size_t server_index,
    struct micros_ipc_message *request,
    uintptr_t receive_buffer,
    uint64_t *reply_token
)
{
    struct micros_thread *receiver =
        &objects.threads[primary_threads[server_index].slot];

    if (
        reply_token == NULL
        || micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            primary_threads[server_index],
            endpoints[REPLY_PROCESS_CLIENT],
            receive_buffer
        ) != MICROS_IPC_OK
        || micros_ipc_call(
            &registry,
            &objects,
            caller,
            endpoints[server_index],
            request
        ) != MICROS_IPC_OK
        || !receiver->ipc_delivery_pending
        || receiver->ipc_receive_buffer != receive_buffer
        || receiver->ipc_inbound_message.source
            != endpoints[REPLY_PROCESS_CLIENT]
        || receiver->ipc_inbound_message.reply_token == 0
        || micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) != MICROS_ENDPOINT_OK
    ) {
        return false;
    }
    *reply_token = receiver->ipc_inbound_message.reply_token;
    return true;
}

static bool prepare_delivered_call(
    struct micros_thread_handle caller,
    size_t server_index,
    struct micros_ipc_message *request,
    uintptr_t receive_buffer,
    uint64_t *reply_token
)
{
    struct micros_ipc_message delivered;

    if (
        !stage_delivered_call(
            caller,
            server_index,
            request,
            receive_buffer,
            reply_token
        )
        || !clear_staged_request(
            primary_threads[server_index],
            &delivered
        )
        || delivered.source != endpoints[REPLY_PROCESS_CLIENT]
        || delivered.reply_token != *reply_token
    ) {
        return false;
    }
    return true;
}

static bool complete_state_is_unchanged(
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

static bool expect_reply_failure_unchanged(
    enum micros_ipc_error expected,
    struct micros_thread_handle replier,
    uint64_t reply_token,
    const struct micros_ipc_message *message
)
{
    struct micros_endpoint_registry registry_snapshot = registry;
    struct micros_kernel_objects objects_snapshot = objects;
    struct micros_ipc_message message_snapshot = *message;

    EXPECT_IPC_ERROR(
        expected,
        micros_ipc_reply(
            &registry,
            &objects,
            replier,
            reply_token,
            message
        )
    );
    EXPECT_TRUE(complete_state_is_unchanged(
        &registry_snapshot,
        &objects_snapshot
    ));
    EXPECT_TRUE(
        memcmp(message, &message_snapshot, sizeof(*message)) == 0
    );
    return true;
}

static bool expect_reply_receive_failure_unchanged(
    enum micros_ipc_error expected,
    struct micros_thread_handle replier,
    uint64_t reply_token,
    const struct micros_ipc_message *reply_message,
    micros_endpoint_t source,
    uintptr_t receive_buffer
)
{
    struct micros_endpoint_registry registry_snapshot = registry;
    struct micros_kernel_objects objects_snapshot = objects;
    struct micros_ipc_message message_snapshot = *reply_message;

    EXPECT_IPC_ERROR(
        expected,
        micros_ipc_reply_receive(
            &registry,
            &objects,
            replier,
            reply_token,
            reply_message,
            source,
            receive_buffer
        )
    );
    EXPECT_TRUE(complete_state_is_unchanged(
        &registry_snapshot,
        &objects_snapshot
    ));
    EXPECT_TRUE(
        memcmp(
            reply_message,
            &message_snapshot,
            sizeof(*reply_message)
        ) == 0
    );
    return true;
}

static bool expect_send_failure_unchanged(
    enum micros_ipc_error expected,
    struct micros_thread_handle sender,
    micros_endpoint_t destination,
    const struct micros_ipc_message *message
)
{
    struct micros_endpoint_registry registry_snapshot = registry;
    struct micros_kernel_objects objects_snapshot = objects;
    struct micros_ipc_message message_snapshot = *message;

    EXPECT_IPC_ERROR(
        expected,
        micros_ipc_send(
            &registry,
            &objects,
            sender,
            destination,
            message
        )
    );
    EXPECT_TRUE(complete_state_is_unchanged(
        &registry_snapshot,
        &objects_snapshot
    ));
    EXPECT_TRUE(
        memcmp(message, &message_snapshot, sizeof(*message)) == 0
    );
    return true;
}

static bool test_reply_requires_request_return(void)
{
    struct micros_ipc_message request = message_pattern(
        UINT32_C(0x11111111),
        UINT32_C(0x5001),
        UINT64_C(0x1111111111111111),
        UINT8_C(0x10)
    );
    struct micros_ipc_message reply = message_pattern(
        UINT32_C(0x22222222),
        UINT32_C(0x5002),
        UINT64_C(0x2222222222222222),
        UINT8_C(0x20)
    );
    struct micros_ipc_message reply_snapshot = reply;
    struct micros_ipc_message staged_request;
    struct micros_ipc_message delivered_request;
    struct micros_ipc_message expected_reply;
    struct micros_thread *caller;
    struct micros_thread *replier;
    struct micros_endpoint_record *server_endpoint;
    uint64_t reply_token;

    EXPECT_TRUE(
        setup_reply_fixture()
        && stage_delivered_call(
            primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &request,
            UINT64_C(0x61000000),
            &reply_token
        )
        && prepare_server_extra_thread()
    );
    staged_request = objects.threads[
        primary_threads[REPLY_PROCESS_SERVER].slot
    ].ipc_inbound_message;
    EXPECT_TRUE(expect_reply_failure_unchanged(
        MICROS_IPC_ERROR_REPLY_TOKEN,
        server_extra_thread,
        reply_token,
        &reply
    ));
    EXPECT_TRUE(
        clear_staged_request(
            primary_threads[REPLY_PROCESS_SERVER],
            &delivered_request
        )
        && memcmp(
            &delivered_request,
            &staged_request,
            sizeof(staged_request)
        ) == 0
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_reply(
            &registry,
            &objects,
            server_extra_thread,
            reply_token,
            &reply
        )
    );
    caller = &objects.threads[
        primary_threads[REPLY_PROCESS_CLIENT].slot
    ];
    replier = &objects.threads[server_extra_thread.slot];
    expected_reply = canonical_reply(
        &reply_snapshot,
        endpoints[REPLY_PROCESS_SERVER]
    );
    EXPECT_TRUE(
        caller->runtime_flags == 0
        && caller->ready_linked
        && caller->ipc_delivery_pending
        && caller->ipc_reply_token == 0
        && caller->ipc_reply_callee == 0
        && memcmp(
            &caller->ipc_inbound_message,
            &expected_reply,
            sizeof(expected_reply)
        ) == 0
        && replier->runtime_flags == 0
        && replier->ready_linked
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
        && memcmp(&reply, &reply_snapshot, sizeof(reply)) == 0
    );
    EXPECT_TRUE(
        micros_thread_scheduler_hold(
            &objects,
            server_extra_thread
        ) == MICROS_KERNEL_OBJECT_OK
        && expect_reply_failure_unchanged(
            MICROS_IPC_ERROR_REPLY_TOKEN,
            server_extra_thread,
            reply_token,
            &reply
        )
    );

    EXPECT_TRUE(
        setup_reply_fixture()
        && stage_delivered_call(
            primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &request,
            UINT64_C(0x61000800),
            &reply_token
        )
        && prepare_server_extra_thread()
        && expect_reply_receive_failure_unchanged(
            MICROS_IPC_ERROR_REPLY_TOKEN,
            server_extra_thread,
            reply_token,
            &reply,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x61001000)
        )
        && clear_staged_request(
            primary_threads[REPLY_PROCESS_SERVER],
            &delivered_request
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_reply_receive(
            &registry,
            &objects,
            server_extra_thread,
            reply_token,
            &reply,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x61001000)
        )
    );
    caller = &objects.threads[
        primary_threads[REPLY_PROCESS_CLIENT].slot
    ];
    replier = &objects.threads[server_extra_thread.slot];
    server_endpoint = &registry.endpoints[REPLY_PROCESS_SERVER];
    EXPECT_TRUE(
        caller->runtime_flags == 0
        && caller->ready_linked
        && caller->ipc_delivery_pending
        && caller->ipc_reply_token == 0
        && caller->ipc_reply_callee == 0
        && replier->runtime_flags == MICROS_THREAD_RTS_IPC_RECEIVE
        && !replier->ready_linked
        && replier->ipc_queue_kind == MICROS_IPC_QUEUE_RECEIVER
        && replier->ipc_receive_source == MICROS_ENDPOINT_ANY
        && replier->ipc_receive_buffer == UINT64_C(0x61001000)
        && server_endpoint->receiver_head.slot
            == server_extra_thread.slot
        && server_endpoint->receiver_head.generation
            == server_extra_thread.generation
        && server_endpoint->receiver_tail.slot
            == server_extra_thread.slot
        && server_endpoint->receiver_tail.generation
            == server_extra_thread.generation
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
        && memcmp(&reply, &reply_snapshot, sizeof(reply)) == 0
    );
    return true;
}

static bool test_staged_call_tokens_are_unique(void)
{
    struct micros_ipc_message request = message_pattern(
        UINT32_C(0x33333333),
        UINT32_C(0x5003),
        UINT64_C(0x3333333333333333),
        UINT8_C(0x30)
    );
    struct micros_thread *receiver;
    struct micros_thread *duplicate;
    uint64_t reply_token;

    EXPECT_TRUE(
        setup_reply_fixture()
        && stage_delivered_call(
            primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &request,
            UINT64_C(0x61001800),
            &reply_token
        )
        && prepare_server_extra_thread()
        && reply_token != 0
        && micros_thread_runtime_flags_unset(
            &objects,
            server_extra_thread,
            MICROS_THREAD_RTS_INACTIVE
        ) == MICROS_KERNEL_OBJECT_OK
    );
    receiver = &objects.threads[
        primary_threads[REPLY_PROCESS_SERVER].slot
    ];
    duplicate = &objects.threads[server_extra_thread.slot];
    duplicate->ipc_receive_buffer = UINT64_C(0x61002000);
    duplicate->ipc_delivery_pending = true;
    duplicate->ipc_inbound_message =
        receiver->ipc_inbound_message;
    duplicate->ipc_staged_result = MICROS_IPC_OK;
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_ERROR_INVARIANT
    );
    return true;
}

static bool test_reply_routes_exact_tokens_without_send_target(void)
{
    struct micros_ipc_message first_request = message_pattern(
        UINT32_C(0x11111111),
        UINT32_C(0x5101),
        UINT64_C(0x1111111111111111),
        UINT8_C(0x10)
    );
    struct micros_ipc_message second_request = message_pattern(
        UINT32_C(0x22222222),
        UINT32_C(0x5102),
        UINT64_C(0x2222222222222222),
        UINT8_C(0x20)
    );
    struct micros_ipc_message first_reply = message_pattern(
        UINT32_C(0xaaaaaaaa),
        UINT32_C(0x5201),
        UINT64_C(0xaaaaaaaaaaaaaaaa),
        UINT8_C(0x30)
    );
    struct micros_ipc_message second_reply = message_pattern(
        UINT32_C(0xbbbbbbbb),
        UINT32_C(0x5202),
        UINT64_C(0xbbbbbbbbbbbbbbbb),
        UINT8_C(0x40)
    );
    struct micros_ipc_message first_reply_snapshot = first_reply;
    struct micros_ipc_message second_reply_snapshot = second_reply;
    struct micros_ipc_message expected;
    struct micros_thread *first_caller;
    struct micros_thread *second_caller;
    struct micros_thread *other_server;
    uint64_t first_token;
    uint64_t second_token;
    uint64_t saved_token;
    micros_endpoint_t saved_source;

    EXPECT_TRUE(
        setup_reply_fixture()
        && prepare_delivered_call(
            primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &first_request,
            UINT64_C(0x61000000),
            &first_token
        )
        && prepare_delivered_call(
            client_extra_thread,
            REPLY_PROCESS_OTHER_SERVER,
            &second_request,
            UINT64_C(0x61000800),
            &second_token
        )
    );
    EXPECT_TRUE(first_token == 1 && second_token == 2);
    EXPECT_TRUE(expect_send_failure_unchanged(
        MICROS_IPC_ERROR_UNAUTHORIZED,
        primary_threads[REPLY_PROCESS_OTHER_SERVER],
        endpoints[REPLY_PROCESS_CLIENT],
        &second_reply
    ));
    EXPECT_TRUE(expect_reply_failure_unchanged(
        MICROS_IPC_ERROR_REPLY_TOKEN,
        primary_threads[REPLY_PROCESS_SERVER],
        second_token,
        &second_reply
    ));

    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_reply(
            &registry,
            &objects,
            primary_threads[REPLY_PROCESS_OTHER_SERVER],
            second_token,
            &second_reply
        )
    );
    expected = canonical_reply(
        &second_reply_snapshot,
        endpoints[REPLY_PROCESS_OTHER_SERVER]
    );
    first_caller = &objects.threads[
        primary_threads[REPLY_PROCESS_CLIENT].slot
    ];
    second_caller = &objects.threads[client_extra_thread.slot];
    other_server = &objects.threads[
        primary_threads[REPLY_PROCESS_OTHER_SERVER].slot
    ];
    EXPECT_TRUE(
        registry.last_reply_token == 2
        && first_caller->runtime_flags
            == MICROS_THREAD_RTS_IPC_REPLY
        && first_caller->ipc_reply_token == first_token
        && !first_caller->ipc_delivery_pending
        && second_caller->runtime_flags == 0
        && second_caller->ready_linked
        && second_caller->ipc_delivery_pending
        && second_caller->ipc_receive_buffer
            == (uintptr_t)&second_request
        && second_caller->ipc_reply_token == 0
        && second_caller->ipc_reply_callee == 0
        && memcmp(
            &second_caller->ipc_inbound_message,
            &expected,
            sizeof(expected)
        ) == 0
        && other_server->runtime_flags == 0
        && other_server->ready_linked
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
        && memcmp(
            &second_reply,
            &second_reply_snapshot,
            sizeof(second_reply)
        ) == 0
    );

    saved_token = second_caller->ipc_inbound_message.reply_token;
    second_caller->ipc_inbound_message.reply_token = second_token;
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_ERROR_INVARIANT
    );
    second_caller->ipc_inbound_message.reply_token = saved_token;
    saved_source = second_caller->ipc_inbound_message.source;
    second_caller->ipc_inbound_message.source =
        endpoints[REPLY_PROCESS_RESERVED_SERVER];
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_ERROR_INVARIANT
    );
    second_caller->ipc_inbound_message.source = saved_source;

    EXPECT_TRUE(
        micros_thread_scheduler_hold(
            &objects,
            primary_threads[REPLY_PROCESS_OTHER_SERVER]
        ) == MICROS_KERNEL_OBJECT_OK
        && expect_reply_failure_unchanged(
            MICROS_IPC_ERROR_REPLY_TOKEN,
            primary_threads[REPLY_PROCESS_OTHER_SERVER],
            second_token,
            &second_reply
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_reply(
            &registry,
            &objects,
            primary_threads[REPLY_PROCESS_SERVER],
            first_token,
            &first_reply
        )
    );
    expected = canonical_reply(
        &first_reply_snapshot,
        endpoints[REPLY_PROCESS_SERVER]
    );
    EXPECT_TRUE(
        first_caller->runtime_flags == 0
        && first_caller->ready_linked
        && first_caller->ipc_delivery_pending
        && first_caller->ipc_receive_buffer
            == (uintptr_t)&first_request
        && first_caller->ipc_reply_token == 0
        && first_caller->ipc_reply_callee == 0
        && memcmp(
            &first_caller->ipc_inbound_message,
            &expected,
            sizeof(expected)
        ) == 0
        && memcmp(
            &first_reply,
            &first_reply_snapshot,
            sizeof(first_reply)
        ) == 0
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool test_reply_preserves_independent_caller_flags(void)
{
    struct micros_ipc_message request = message_pattern(
        0,
        UINT32_C(0x5301),
        0,
        UINT8_C(0x50)
    );
    struct micros_ipc_message reply = message_pattern(
        UINT32_C(0xcccccccc),
        UINT32_C(0x5302),
        UINT64_C(0xcccccccccccccccc),
        UINT8_C(0x60)
    );
    struct micros_ipc_message expected;
    struct micros_thread *caller;
    uint64_t reply_token;

    EXPECT_TRUE(
        setup_reply_fixture()
        && prepare_delivered_call(
            primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &request,
            UINT64_C(0x61001000),
            &reply_token
        )
        && micros_thread_scheduler_hold(
            &objects,
            primary_threads[REPLY_PROCESS_CLIENT]
        ) == MICROS_KERNEL_OBJECT_OK
    );
    caller = &objects.threads[
        primary_threads[REPLY_PROCESS_CLIENT].slot
    ];
    EXPECT_TRUE(
        caller->runtime_flags
            == (
                MICROS_THREAD_RTS_INACTIVE
                | MICROS_THREAD_RTS_IPC_REPLY
            )
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_reply(
            &registry,
            &objects,
            primary_threads[REPLY_PROCESS_SERVER],
            reply_token,
            &reply
        )
    );
    expected = canonical_reply(
        &reply,
        endpoints[REPLY_PROCESS_SERVER]
    );
    EXPECT_TRUE(
        caller->runtime_flags == MICROS_THREAD_RTS_INACTIVE
        && !caller->ready_linked
        && caller->ipc_delivery_pending
        && caller->ipc_reply_token == 0
        && caller->ipc_reply_callee == 0
        && memcmp(
            &caller->ipc_inbound_message,
            &expected,
            sizeof(expected)
        ) == 0
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool test_reply_rejections_are_atomic(void)
{
    union {
        uint64_t alignment;
        unsigned char bytes[sizeof(struct micros_ipc_message) + 1];
    } misaligned_storage;
    struct micros_ipc_message request = message_pattern(
        0,
        UINT32_C(0x5401),
        0,
        UINT8_C(0x70)
    );
    struct micros_ipc_message reply = message_pattern(
        UINT32_C(0xdddddddd),
        UINT32_C(0x5402),
        UINT64_C(0xdddddddddddddddd),
        UINT8_C(0x80)
    );
    struct micros_ipc_message kernel_reply = reply;
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;
    struct micros_thread *caller;
    uint64_t reply_token;

    EXPECT_TRUE(
        setup_reply_fixture()
        && prepare_delivered_call(
            primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &request,
            UINT64_C(0x61001800),
            &reply_token
        )
    );
    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_ARGUMENT,
        micros_ipc_reply(
            &registry,
            &objects,
            primary_threads[REPLY_PROCESS_SERVER],
            reply_token,
            NULL
        )
    );
    EXPECT_TRUE(complete_state_is_unchanged(
        &registry_snapshot,
        &objects_snapshot
    ));

    memset(&misaligned_storage, 0, sizeof(misaligned_storage));
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_ARGUMENT,
        micros_ipc_reply(
            &registry,
            &objects,
            primary_threads[REPLY_PROCESS_SERVER],
            reply_token,
            (const struct micros_ipc_message *)(
                (const void *)&misaligned_storage.bytes[1]
            )
        )
    );
    EXPECT_TRUE(complete_state_is_unchanged(
        &registry_snapshot,
        &objects_snapshot
    ));

    kernel_reply.type |= MICROS_IPC_TYPE_KERNEL_MASK;
    EXPECT_TRUE(
        expect_reply_failure_unchanged(
            MICROS_IPC_ERROR_ARGUMENT,
            primary_threads[REPLY_PROCESS_SERVER],
            reply_token,
            &kernel_reply
        )
        && expect_reply_failure_unchanged(
            MICROS_IPC_ERROR_REPLY_TOKEN,
            primary_threads[REPLY_PROCESS_SERVER],
            0,
            &reply
        )
        && expect_reply_failure_unchanged(
            MICROS_IPC_ERROR_REPLY_TOKEN,
            primary_threads[REPLY_PROCESS_SERVER],
            reply_token + 100,
            &reply
        )
        && expect_reply_failure_unchanged(
            MICROS_IPC_ERROR_STATE,
            primary_threads[REPLY_PROCESS_RESERVED_SERVER],
            reply_token,
            &reply
        )
        && expect_reply_failure_unchanged(
            MICROS_IPC_ERROR_REPLY_TOKEN,
            primary_threads[REPLY_PROCESS_OTHER_SERVER],
            reply_token,
            &reply
        )
    );

    EXPECT_TRUE(setup_reply_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &registry,
            &objects,
            primary_threads[REPLY_PROCESS_CLIENT],
            endpoints[REPLY_PROCESS_SERVER],
            &request
        )
    );
    caller = &objects.threads[
        primary_threads[REPLY_PROCESS_CLIENT].slot
    ];
    EXPECT_TRUE(
        caller->runtime_flags
            == (
                MICROS_THREAD_RTS_IPC_SEND
                | MICROS_THREAD_RTS_IPC_REPLY
            )
        && expect_reply_failure_unchanged(
            MICROS_IPC_ERROR_REPLY_TOKEN,
            primary_threads[REPLY_PROCESS_SERVER],
            caller->ipc_reply_token,
            &reply
        )
    );

    EXPECT_TRUE(
        setup_reply_fixture()
        && prepare_delivered_call(
            primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_NO_REPLY_SERVER,
            &request,
            UINT64_C(0x61002000),
            &reply_token
        )
        && expect_reply_failure_unchanged(
            MICROS_IPC_ERROR_UNAUTHORIZED,
            primary_threads[REPLY_PROCESS_NO_REPLY_SERVER],
            reply_token,
            &reply
        )
    );

    EXPECT_TRUE(
        setup_reply_fixture()
        && prepare_delivered_call(
            primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &request,
            UINT64_C(0x61002800),
            &reply_token
        )
        && micros_thread_runtime_flags_unset(
            &objects,
            primary_threads[REPLY_PROCESS_SERVER],
            MICROS_THREAD_RTS_INACTIVE
        ) == MICROS_KERNEL_OBJECT_OK
        && expect_reply_failure_unchanged(
            MICROS_IPC_ERROR_STATE,
            primary_threads[REPLY_PROCESS_SERVER],
            reply_token,
            &reply
        )
    );
    return true;
}

static bool make_reply_wait_current(
    struct micros_thread_handle caller_handle
)
{
    struct micros_scheduler_return_plan plan;
    struct micros_thread *caller =
        &objects.threads[caller_handle.slot];
    uintptr_t reply_buffer = caller->ipc_receive_buffer;
    uint64_t reply_token = caller->ipc_reply_token;
    micros_endpoint_t reply_callee = caller->ipc_reply_callee;

    caller->runtime_flags = MICROS_THREAD_RTS_INACTIVE;
    caller->ipc_receive_buffer = 0;
    caller->ipc_reply_token = 0;
    caller->ipc_reply_callee = 0;
    if (
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) != MICROS_ENDPOINT_OK
        || micros_scheduler_accounting_initialize(
            &objects,
            hart,
            100
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_thread_runtime_flags_unset(
            &objects,
            caller_handle,
            MICROS_THREAD_RTS_INACTIVE
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_hart_plan_user_return(&objects, hart, &plan)
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_commit_user_return(&objects, &plan)
            != MICROS_KERNEL_OBJECT_OK
        || micros_thread_scheduler_hold(&objects, caller_handle)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    caller->runtime_flags = MICROS_THREAD_RTS_IPC_REPLY;
    caller->ipc_receive_buffer = reply_buffer;
    caller->ipc_reply_token = reply_token;
    caller->ipc_reply_callee = reply_callee;
    return micros_endpoint_registry_validate_objects(
        &registry,
        &objects
    ) == MICROS_ENDPOINT_OK;
}

static bool make_single_queued_sender_current(
    struct micros_thread_handle sender_handle,
    size_t destination_index
)
{
    struct micros_scheduler_return_plan plan;
    struct micros_endpoint_record *destination =
        &registry.endpoints[processes[destination_index].slot];
    struct micros_thread *sender =
        &objects.threads[sender_handle.slot];
    struct micros_ipc_message outbound =
        sender->ipc_outbound_message;
    micros_endpoint_t send_destination =
        sender->ipc_send_destination;

    if (
        destination->sender_head.slot != sender_handle.slot
        || destination->sender_head.generation
            != sender_handle.generation
        || destination->sender_tail.slot != sender_handle.slot
        || destination->sender_tail.generation
            != sender_handle.generation
        || sender->runtime_flags != MICROS_THREAD_RTS_IPC_SEND
        || sender->ipc_queue_kind != MICROS_IPC_QUEUE_SENDER
        || !thread_handle_is_zero(sender->ipc_next)
    ) {
        return false;
    }
    destination->sender_head.slot = 0;
    destination->sender_head.generation = 0;
    destination->sender_tail.slot = 0;
    destination->sender_tail.generation = 0;
    sender->runtime_flags = MICROS_THREAD_RTS_INACTIVE;
    sender->ipc_queue_kind = MICROS_IPC_QUEUE_NONE;
    memset(
        &sender->ipc_outbound_message,
        0,
        sizeof(sender->ipc_outbound_message)
    );
    sender->ipc_send_destination = 0;
    if (
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) != MICROS_ENDPOINT_OK
        || micros_scheduler_accounting_initialize(
            &objects,
            hart,
            100
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_thread_runtime_flags_unset(
            &objects,
            sender_handle,
            MICROS_THREAD_RTS_INACTIVE
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_hart_plan_user_return(&objects, hart, &plan)
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_commit_user_return(&objects, &plan)
            != MICROS_KERNEL_OBJECT_OK
        || micros_thread_scheduler_hold(&objects, sender_handle)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    sender->runtime_flags = MICROS_THREAD_RTS_IPC_SEND;
    sender->ipc_queue_kind = MICROS_IPC_QUEUE_SENDER;
    sender->ipc_outbound_message = outbound;
    sender->ipc_send_destination = send_destination;
    destination->sender_head = sender_handle;
    destination->sender_tail = sender_handle;
    return micros_endpoint_registry_validate_objects(
        &registry,
        &objects
    ) == MICROS_ENDPOINT_OK;
}

static bool test_reply_scheduler_failure_preserves_token(void)
{
    struct micros_ipc_message request = message_pattern(
        0,
        UINT32_C(0x5501),
        0,
        UINT8_C(0x90)
    );
    struct micros_ipc_message reply = message_pattern(
        UINT32_C(0xeeeeeeee),
        UINT32_C(0x5502),
        UINT64_C(0xeeeeeeeeeeeeeeee),
        UINT8_C(0xa0)
    );
    struct micros_thread *caller;
    uint64_t reply_token;

    EXPECT_TRUE(
        setup_reply_fixture()
        && prepare_delivered_call(
            primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &request,
            UINT64_C(0x61003000),
            &reply_token
        )
        && make_reply_wait_current(
            primary_threads[REPLY_PROCESS_CLIENT]
        )
        && expect_reply_failure_unchanged(
            MICROS_IPC_ERROR_STATE,
            primary_threads[REPLY_PROCESS_SERVER],
            reply_token,
            &reply
        )
    );
    caller = &objects.threads[
        primary_threads[REPLY_PROCESS_CLIENT].slot
    ];
    EXPECT_TRUE(
        caller->runtime_flags == MICROS_THREAD_RTS_IPC_REPLY
        && caller->ipc_reply_token == reply_token
        && caller->ipc_reply_callee
            == endpoints[REPLY_PROCESS_SERVER]
        && !caller->ipc_delivery_pending
        && thread_handle_is_zero(caller->ipc_next)
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool test_reply_receive_blocks_and_later_receives(void)
{
    struct micros_ipc_message request = message_pattern(
        0,
        UINT32_C(0x5601),
        0,
        UINT8_C(0xb0)
    );
    struct micros_ipc_message reply = message_pattern(
        UINT32_C(0xaaaaaaaa),
        UINT32_C(0x5602),
        UINT64_C(0xaaaaaaaaaaaaaaaa),
        UINT8_C(0xc0)
    );
    struct micros_ipc_message next = message_pattern(
        UINT32_C(0xbbbbbbbb),
        UINT32_C(0x5603),
        UINT64_C(0xbbbbbbbbbbbbbbbb),
        UINT8_C(0xd0)
    );
    struct micros_ipc_message reply_snapshot = reply;
    struct micros_ipc_message next_snapshot = next;
    struct micros_ipc_message expected;
    struct micros_endpoint_record *server_endpoint;
    struct micros_thread *caller;
    struct micros_thread *server;
    struct micros_thread *sender;
    uintptr_t receive_buffer = UINT64_C(0x62000000);
    uint64_t reply_token;

    EXPECT_TRUE(
        setup_reply_fixture()
        && prepare_delivered_call(
            primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &request,
            UINT64_C(0x61004000),
            &reply_token
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_reply_receive(
            &registry,
            &objects,
            primary_threads[REPLY_PROCESS_SERVER],
            reply_token,
            &reply,
            MICROS_ENDPOINT_ANY,
            receive_buffer
        )
    );
    caller = &objects.threads[
        primary_threads[REPLY_PROCESS_CLIENT].slot
    ];
    server = &objects.threads[
        primary_threads[REPLY_PROCESS_SERVER].slot
    ];
    server_endpoint =
        &registry.endpoints[processes[REPLY_PROCESS_SERVER].slot];
    expected = canonical_reply(
        &reply_snapshot,
        endpoints[REPLY_PROCESS_SERVER]
    );
    EXPECT_TRUE(
        caller->runtime_flags == 0
        && caller->ready_linked
        && caller->ipc_delivery_pending
        && caller->ipc_reply_token == 0
        && caller->ipc_reply_callee == 0
        && memcmp(
            &caller->ipc_inbound_message,
            &expected,
            sizeof(expected)
        ) == 0
        && server->runtime_flags
            == MICROS_THREAD_RTS_IPC_RECEIVE
        && !server->ready_linked
        && server->ipc_queue_kind
            == MICROS_IPC_QUEUE_RECEIVER
        && thread_handle_is_zero(server->ipc_next)
        && server->ipc_receive_source == MICROS_ENDPOINT_ANY
        && server->ipc_receive_buffer == receive_buffer
        && !server->ipc_delivery_pending
        && server_endpoint->receiver_head.slot
            == primary_threads[REPLY_PROCESS_SERVER].slot
        && server_endpoint->receiver_head.generation
            == primary_threads[REPLY_PROCESS_SERVER].generation
        && server_endpoint->receiver_tail.slot
            == primary_threads[REPLY_PROCESS_SERVER].slot
        && server_endpoint->receiver_tail.generation
            == primary_threads[REPLY_PROCESS_SERVER].generation
        && memcmp(&reply, &reply_snapshot, sizeof(reply)) == 0
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    server->ipc_receive_source = MICROS_ENDPOINT_NONE;
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_ERROR_INVARIANT
    );
    server->ipc_receive_source = MICROS_ENDPOINT_ANY;

    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &registry,
            &objects,
            primary_threads[REPLY_PROCESS_OTHER_SERVER],
            endpoints[REPLY_PROCESS_SERVER],
            &next
        )
    );
    sender = &objects.threads[
        primary_threads[REPLY_PROCESS_OTHER_SERVER].slot
    ];
    expected = canonical_reply(
        &next_snapshot,
        endpoints[REPLY_PROCESS_OTHER_SERVER]
    );
    EXPECT_TRUE(
        server->runtime_flags == 0
        && server->ready_linked
        && server->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && server->ipc_receive_source == 0
        && server->ipc_receive_buffer == receive_buffer
        && server->ipc_delivery_pending
        && memcmp(
            &server->ipc_inbound_message,
            &expected,
            sizeof(expected)
        ) == 0
        && thread_handle_is_zero(
            server_endpoint->receiver_head
        )
        && thread_handle_is_zero(
            server_endpoint->receiver_tail
        )
        && sender->runtime_flags == 0
        && sender->ready_linked
        && micros_thread_ipc_state_is_clear(sender)
        && memcmp(&next, &next_snapshot, sizeof(next)) == 0
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool test_reply_receive_selects_specific_sender_atomically(void)
{
    struct micros_ipc_message first_request = message_pattern(
        0,
        UINT32_C(0x5701),
        0,
        UINT8_C(0x10)
    );
    struct micros_ipc_message second_request = message_pattern(
        UINT32_C(0x11111111),
        UINT32_C(0x5702),
        UINT64_C(0x1111111111111111),
        UINT8_C(0x20)
    );
    struct micros_ipc_message unmatched_message = message_pattern(
        UINT32_C(0x22222222),
        UINT32_C(0x5703),
        UINT64_C(0x2222222222222222),
        UINT8_C(0x30)
    );
    struct micros_ipc_message reply = message_pattern(
        UINT32_C(0x33333333),
        UINT32_C(0x5704),
        UINT64_C(0x3333333333333333),
        UINT8_C(0x40)
    );
    struct micros_ipc_message reply_snapshot = reply;
    struct micros_ipc_message expected_reply;
    struct micros_ipc_message expected_request;
    struct micros_endpoint_record *server_endpoint;
    struct micros_thread *first_caller;
    struct micros_thread *second_caller;
    struct micros_thread *server;
    struct micros_thread *unmatched_sender;
    uintptr_t receive_buffer = UINT64_C(0x62001000);
    uint64_t first_token;
    uint64_t second_token;
    uint64_t saved_token;
    micros_endpoint_t saved_source;

    EXPECT_TRUE(
        setup_reply_fixture()
        && prepare_delivered_call(
            primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &first_request,
            UINT64_C(0x61004800),
            &first_token
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &registry,
            &objects,
            primary_threads[REPLY_PROCESS_NO_REPLY_SERVER],
            endpoints[REPLY_PROCESS_SERVER],
            &unmatched_message
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &registry,
            &objects,
            client_extra_thread,
            endpoints[REPLY_PROCESS_SERVER],
            &second_request
        )
    );
    second_token =
        objects.threads[client_extra_thread.slot].ipc_reply_token;
    EXPECT_TRUE(first_token == 1 && second_token == 2);
    expected_request =
        objects.threads[
            client_extra_thread.slot
        ].ipc_outbound_message;

    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_reply_receive(
            &registry,
            &objects,
            primary_threads[REPLY_PROCESS_SERVER],
            first_token,
            &reply,
            endpoints[REPLY_PROCESS_CLIENT],
            receive_buffer
        )
    );
    first_caller = &objects.threads[
        primary_threads[REPLY_PROCESS_CLIENT].slot
    ];
    second_caller = &objects.threads[client_extra_thread.slot];
    server = &objects.threads[
        primary_threads[REPLY_PROCESS_SERVER].slot
    ];
    unmatched_sender = &objects.threads[
        primary_threads[REPLY_PROCESS_NO_REPLY_SERVER].slot
    ];
    server_endpoint =
        &registry.endpoints[processes[REPLY_PROCESS_SERVER].slot];
    expected_reply = canonical_reply(
        &reply_snapshot,
        endpoints[REPLY_PROCESS_SERVER]
    );
    EXPECT_TRUE(
        first_caller->runtime_flags == 0
        && first_caller->ready_linked
        && first_caller->ipc_delivery_pending
        && first_caller->ipc_reply_token == 0
        && memcmp(
            &first_caller->ipc_inbound_message,
            &expected_reply,
            sizeof(expected_reply)
        ) == 0
        && server->runtime_flags == 0
        && server->ready_linked
        && server->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && server->ipc_receive_source == 0
        && server->ipc_receive_buffer == receive_buffer
        && server->ipc_delivery_pending
        && memcmp(
            &server->ipc_inbound_message,
            &expected_request,
            sizeof(expected_request)
        ) == 0
        && second_caller->runtime_flags
            == MICROS_THREAD_RTS_IPC_REPLY
        && !second_caller->ready_linked
        && second_caller->ipc_queue_kind
            == MICROS_IPC_QUEUE_NONE
        && second_caller->ipc_reply_token == second_token
        && second_caller->ipc_reply_callee
            == endpoints[REPLY_PROCESS_SERVER]
        && second_caller->ipc_receive_buffer
            == (uintptr_t)&second_request
        && unmatched_sender->runtime_flags
            == MICROS_THREAD_RTS_IPC_SEND
        && unmatched_sender->ipc_queue_kind
            == MICROS_IPC_QUEUE_SENDER
        && thread_handle_is_zero(unmatched_sender->ipc_next)
        && server_endpoint->sender_head.slot
            == primary_threads[REPLY_PROCESS_NO_REPLY_SERVER].slot
        && server_endpoint->sender_head.generation
            == primary_threads[
                REPLY_PROCESS_NO_REPLY_SERVER
            ].generation
        && server_endpoint->sender_tail.slot
            == primary_threads[REPLY_PROCESS_NO_REPLY_SERVER].slot
        && server_endpoint->sender_tail.generation
            == primary_threads[
                REPLY_PROCESS_NO_REPLY_SERVER
            ].generation
        && memcmp(&reply, &reply_snapshot, sizeof(reply)) == 0
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    saved_token = first_caller->ipc_inbound_message.reply_token;
    first_caller->ipc_inbound_message.reply_token = first_token;
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_ERROR_INVARIANT
    );
    first_caller->ipc_inbound_message.reply_token = saved_token;
    saved_source = server->ipc_inbound_message.source;
    server->ipc_inbound_message.source =
        endpoints[REPLY_PROCESS_RESERVED_SERVER];
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_ERROR_INVARIANT
    );
    server->ipc_inbound_message.source = saved_source;
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool test_reply_receive_requires_combined_operation(void)
{
    struct micros_ipc_message request = message_pattern(
        0,
        UINT32_C(0x5801),
        0,
        UINT8_C(0x50)
    );
    struct micros_ipc_message reply = message_pattern(
        UINT32_C(0x44444444),
        UINT32_C(0x5802),
        UINT64_C(0x4444444444444444),
        UINT8_C(0x60)
    );
    struct micros_thread *server;
    uint64_t reply_token;

    EXPECT_TRUE(
        setup_reply_fixture()
        && prepare_delivered_call(
            primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_REPLY_RECEIVE_ONLY_SERVER,
            &request,
            UINT64_C(0x61005000),
            &reply_token
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_reply_receive(
            &registry,
            &objects,
            primary_threads[
                REPLY_PROCESS_REPLY_RECEIVE_ONLY_SERVER
            ],
            reply_token,
            &reply,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x62002000)
        )
    );
    server = &objects.threads[
        primary_threads[
            REPLY_PROCESS_REPLY_RECEIVE_ONLY_SERVER
        ].slot
    ];
    EXPECT_TRUE(
        server->runtime_flags
            == MICROS_THREAD_RTS_IPC_RECEIVE
        && server->ipc_queue_kind
            == MICROS_IPC_QUEUE_RECEIVER
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );

    EXPECT_TRUE(
        setup_reply_fixture()
        && prepare_delivered_call(
            primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_REPLY_ONLY_SERVER,
            &request,
            UINT64_C(0x61005800),
            &reply_token
        )
        && expect_reply_receive_failure_unchanged(
            MICROS_IPC_ERROR_UNAUTHORIZED,
            primary_threads[REPLY_PROCESS_REPLY_ONLY_SERVER],
            reply_token,
            &reply,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x62002800)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_reply(
            &registry,
            &objects,
            primary_threads[REPLY_PROCESS_REPLY_ONLY_SERVER],
            reply_token,
            &reply
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

static bool test_reply_receive_rejections_are_atomic(void)
{
    union {
        uint64_t alignment;
        unsigned char bytes[sizeof(struct micros_ipc_message) + 1];
    } misaligned_storage;
    struct micros_ipc_message request = message_pattern(
        0,
        UINT32_C(0x5901),
        0,
        UINT8_C(0x70)
    );
    struct micros_ipc_message reply = message_pattern(
        UINT32_C(0x55555555),
        UINT32_C(0x5902),
        UINT64_C(0x5555555555555555),
        UINT8_C(0x80)
    );
    struct micros_ipc_message kernel_reply = reply;
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;
    uint64_t reply_token;

    EXPECT_TRUE(
        setup_reply_fixture()
        && prepare_delivered_call(
            primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &request,
            UINT64_C(0x61006000),
            &reply_token
        )
        && expect_reply_receive_failure_unchanged(
            MICROS_IPC_ERROR_ARGUMENT,
            primary_threads[REPLY_PROCESS_SERVER],
            reply_token,
            &reply,
            MICROS_ENDPOINT_NONE,
            UINT64_C(0x62003000)
        )
        && expect_reply_receive_failure_unchanged(
            MICROS_IPC_ERROR_ARGUMENT,
            primary_threads[REPLY_PROCESS_SERVER],
            reply_token,
            &reply,
            MICROS_ENDPOINT_ANY,
            0
        )
        && expect_reply_receive_failure_unchanged(
            MICROS_IPC_ERROR_ARGUMENT,
            primary_threads[REPLY_PROCESS_SERVER],
            reply_token,
            &reply,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x62003001)
        )
        && expect_reply_receive_failure_unchanged(
            MICROS_IPC_ERROR_STATE,
            primary_threads[REPLY_PROCESS_SERVER],
            reply_token,
            &reply,
            endpoints[REPLY_PROCESS_RESERVED_SERVER],
            UINT64_C(0x62003800)
        )
    );
    kernel_reply.type |= MICROS_IPC_TYPE_KERNEL_MASK;
    EXPECT_TRUE(expect_reply_receive_failure_unchanged(
        MICROS_IPC_ERROR_ARGUMENT,
        primary_threads[REPLY_PROCESS_SERVER],
        reply_token,
        &kernel_reply,
        MICROS_ENDPOINT_ANY,
        UINT64_C(0x62004000)
    ));

    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_ARGUMENT,
        micros_ipc_reply_receive(
            &registry,
            &objects,
            primary_threads[REPLY_PROCESS_SERVER],
            reply_token,
            NULL,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x62004800)
        )
    );
    EXPECT_TRUE(complete_state_is_unchanged(
        &registry_snapshot,
        &objects_snapshot
    ));

    memset(&misaligned_storage, 0, sizeof(misaligned_storage));
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_ARGUMENT,
        micros_ipc_reply_receive(
            &registry,
            &objects,
            primary_threads[REPLY_PROCESS_SERVER],
            reply_token,
            (const struct micros_ipc_message *)(
                (const void *)&misaligned_storage.bytes[1]
            ),
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x62005000)
        )
    );
    EXPECT_TRUE(
        complete_state_is_unchanged(
            &registry_snapshot,
            &objects_snapshot
        )
        && objects.threads[
            primary_threads[REPLY_PROCESS_CLIENT].slot
        ].ipc_reply_token == reply_token
    );
    return true;
}

static bool test_reply_receive_scheduler_failures_are_atomic(void)
{
    struct micros_ipc_message request = message_pattern(
        0,
        UINT32_C(0x5a01),
        0,
        UINT8_C(0x90)
    );
    struct micros_ipc_message queued = message_pattern(
        0,
        UINT32_C(0x5a02),
        0,
        UINT8_C(0xa0)
    );
    struct micros_ipc_message reply = message_pattern(
        UINT32_C(0x66666666),
        UINT32_C(0x5a03),
        UINT64_C(0x6666666666666666),
        UINT8_C(0xb0)
    );
    struct micros_thread *caller;
    struct micros_thread *sender;
    uint64_t reply_token;

    EXPECT_TRUE(
        setup_reply_fixture()
        && prepare_delivered_call(
            primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &request,
            UINT64_C(0x61006800),
            &reply_token
        )
        && make_reply_wait_current(
            primary_threads[REPLY_PROCESS_CLIENT]
        )
        && expect_reply_receive_failure_unchanged(
            MICROS_IPC_ERROR_STATE,
            primary_threads[REPLY_PROCESS_SERVER],
            reply_token,
            &reply,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x62005800)
        )
    );
    caller = &objects.threads[
        primary_threads[REPLY_PROCESS_CLIENT].slot
    ];
    EXPECT_TRUE(
        caller->runtime_flags == MICROS_THREAD_RTS_IPC_REPLY
        && caller->ipc_reply_token == reply_token
        && !caller->ipc_delivery_pending
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );

    EXPECT_TRUE(
        setup_reply_fixture()
        && prepare_delivered_call(
            primary_threads[REPLY_PROCESS_CLIENT],
            REPLY_PROCESS_SERVER,
            &request,
            UINT64_C(0x61007000),
            &reply_token
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &registry,
            &objects,
            primary_threads[REPLY_PROCESS_OTHER_SERVER],
            endpoints[REPLY_PROCESS_SERVER],
            &queued
        )
    );
    EXPECT_TRUE(
        make_single_queued_sender_current(
            primary_threads[REPLY_PROCESS_OTHER_SERVER],
            REPLY_PROCESS_SERVER
        )
        && expect_reply_receive_failure_unchanged(
            MICROS_IPC_ERROR_STATE,
            primary_threads[REPLY_PROCESS_SERVER],
            reply_token,
            &reply,
            endpoints[REPLY_PROCESS_OTHER_SERVER],
            UINT64_C(0x62006000)
        )
    );
    caller = &objects.threads[
        primary_threads[REPLY_PROCESS_CLIENT].slot
    ];
    sender = &objects.threads[
        primary_threads[REPLY_PROCESS_OTHER_SERVER].slot
    ];
    EXPECT_TRUE(
        caller->ipc_reply_token == reply_token
        && caller->runtime_flags == MICROS_THREAD_RTS_IPC_REPLY
        && !caller->ipc_delivery_pending
        && sender->runtime_flags == MICROS_THREAD_RTS_IPC_SEND
        && sender->ipc_queue_kind == MICROS_IPC_QUEUE_SENDER
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

bool micros_ipc_reply_test_run(void)
{
    static const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {
            "staged call tokens are unique",
            test_staged_call_tokens_are_unique,
        },
        {
            "reply requires request return",
            test_reply_requires_request_return,
        },
        {
            "reply routes exact tokens without send target",
            test_reply_routes_exact_tokens_without_send_target,
        },
        {
            "reply preserves independent caller flags",
            test_reply_preserves_independent_caller_flags,
        },
        {
            "reply rejections are atomic",
            test_reply_rejections_are_atomic,
        },
        {
            "reply scheduler failure preserves token",
            test_reply_scheduler_failure_preserves_token,
        },
        {
            "reply receive blocks and later receives",
            test_reply_receive_blocks_and_later_receives,
        },
        {
            "reply receive selects specific sender atomically",
            test_reply_receive_selects_specific_sender_atomically,
        },
        {
            "reply receive requires combined operation",
            test_reply_receive_requires_combined_operation,
        },
        {
            "reply receive rejections are atomic",
            test_reply_receive_rejections_are_atomic,
        },
        {
            "reply receive scheduler failures are atomic",
            test_reply_receive_scheduler_failures_are_atomic,
        },
    };
    size_t index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run()) {
            fprintf(
                stderr,
                "reply subtest failed: %s\n",
                tests[index].name
            );
            return false;
        }
    }
    return true;
}
