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
    REPLY_PROCESS_RESERVED_SERVER,
    REPLY_PROCESS_COUNT,
};

static struct micros_endpoint_registry registry;
static struct micros_kernel_objects objects;
static struct micros_process_handle processes[REPLY_PROCESS_COUNT];
static struct micros_thread_handle primary_threads[REPLY_PROCESS_COUNT];
static struct micros_thread_handle client_extra_thread;
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
                (UINT32_C(1) << 2) | (UINT32_C(1) << 3),
        },
        {
            .id = 2,
            .name = "REPLY_SERVER",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_SEND
                | MICROS_PRIVILEGE_OPERATION_REPLY,
        },
        {
            .id = 3,
            .name = "NO_REPLY_SERVER",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_SEND,
        },
    };
    static const uint8_t profile_ids[REPLY_PROCESS_COUNT] = {
        1,
        2,
        2,
        3,
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
        micros_ipc_receiver_enqueue(
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
        || !clear_staged_request(
            primary_threads[server_index],
            &delivered
        )
        || delivered.source != endpoints[REPLY_PROCESS_CLIENT]
        || delivered.reply_token == 0
    ) {
        return false;
    }
    *reply_token = delivered.reply_token;
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

bool micros_ipc_reply_test_run(void)
{
    static const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
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
