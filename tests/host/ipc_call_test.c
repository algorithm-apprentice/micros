#include "micros/ipc_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/scheduler_core.h"

enum {
    CALL_PROCESS_CLIENT = 0,
    CALL_PROCESS_SERVER,
    CALL_PROCESS_NO_CALL,
    CALL_PROCESS_ISOLATED_CALL,
    CALL_PROCESS_RESERVED,
    CALL_PROCESS_COUNT,
};

static struct micros_endpoint_registry registry;
static struct micros_kernel_objects objects;
static struct micros_process_handle processes[CALL_PROCESS_COUNT];
static struct micros_thread_handle primary_threads[CALL_PROCESS_COUNT];
static struct micros_thread_handle client_extra_thread;
static micros_endpoint_t endpoints[CALL_PROCESS_COUNT];
static struct micros_hart_handle hart;

bool micros_ipc_call_test_run(void);

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

static bool thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

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

static struct micros_ipc_message canonical_request(
    const struct micros_ipc_message *message,
    micros_endpoint_t source,
    uint64_t reply_token
)
{
    struct micros_ipc_message canonical = *message;

    canonical.source = source;
    canonical.reply_token = reply_token;
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

static bool setup_call_fixture(void)
{
    static const struct micros_privilege_profile profiles[] = {
        {
            .id = 1,
            .name = "CLIENT",
            .operations = MICROS_PRIVILEGE_OPERATION_CALL,
            .call_targets = UINT32_C(1) << 2,
        },
        {
            .id = 2,
            .name = "SERVER",
            .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
        },
        {
            .id = 3,
            .name = "NO_CALL",
            .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
        },
        {
            .id = 4,
            .name = "ISOLATED_CALL",
            .operations = MICROS_PRIVILEGE_OPERATION_CALL,
            .call_targets = UINT32_C(1) << 4,
        },
    };
    static const uint8_t profile_ids[CALL_PROCESS_COUNT] = {
        1,
        2,
        3,
        4,
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
    for (index = 0; index < CALL_PROCESS_COUNT; ++index) {
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
                index != CALL_PROCESS_RESERVED
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
    for (index = 0; index < CALL_PROCESS_COUNT; ++index) {
        if (
            !prepare_thread(
                processes[index],
                UINT64_C(0x1000) + index * UINT64_C(0x100),
                UINT64_C(0x10000000)
                    + index * UINT64_C(0x00008000),
                &primary_threads[index]
            )
        ) {
            return false;
        }
    }
    return (
        prepare_thread(
            processes[CALL_PROCESS_CLIENT],
            UINT64_C(0x2000),
            UINT64_C(0x11000000),
            &client_extra_thread
        )
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
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

static bool expect_call_failure_unchanged(
    enum micros_ipc_error expected,
    struct micros_thread_handle caller,
    micros_endpoint_t destination,
    struct micros_ipc_message *message
)
{
    struct micros_endpoint_registry registry_snapshot = registry;
    struct micros_kernel_objects objects_snapshot = objects;
    struct micros_ipc_message message_snapshot = *message;

    EXPECT_IPC_ERROR(
        expected,
        micros_ipc_call(
            &registry,
            &objects,
            caller,
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

static bool make_current_held(struct micros_thread_handle thread)
{
    struct micros_scheduler_return_plan plan;

    return (
        micros_scheduler_accounting_initialize(&objects, hart, 100)
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_runtime_flags_unset(
            &objects,
            thread,
            MICROS_THREAD_RTS_INACTIVE
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_hart_plan_user_return(&objects, hart, &plan)
            == MICROS_KERNEL_OBJECT_OK
        && micros_hart_commit_user_return(&objects, &plan)
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_scheduler_hold(&objects, thread)
            == MICROS_KERNEL_OBJECT_OK
    );
}

static bool test_call_delivers_to_waiting_receiver(void)
{
    struct micros_ipc_message request = message_pattern(
        UINT32_C(0xaaaaaaaa),
        UINT32_C(0x1234),
        UINT64_C(0xbbbbbbbbbbbbbbbb),
        UINT8_C(0x20)
    );
    struct micros_ipc_message request_snapshot = request;
    struct micros_ipc_message expected;
    const uintptr_t receive_buffer = UINT64_C(0x60000000);
    const struct micros_thread *caller;
    const struct micros_thread *receiver;
    const struct micros_endpoint_record *server;
    uint32_t saved_callee;

    EXPECT_TRUE(setup_call_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            primary_threads[CALL_PROCESS_SERVER],
            endpoints[CALL_PROCESS_CLIENT],
            receive_buffer
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &registry,
            &objects,
            primary_threads[CALL_PROCESS_CLIENT],
            endpoints[CALL_PROCESS_SERVER],
            &request
        )
    );
    expected = canonical_request(
        &request_snapshot,
        endpoints[CALL_PROCESS_CLIENT],
        1
    );
    caller = &objects.threads[
        primary_threads[CALL_PROCESS_CLIENT].slot
    ];
    receiver = &objects.threads[
        primary_threads[CALL_PROCESS_SERVER].slot
    ];
    server = &registry.endpoints[CALL_PROCESS_SERVER];

    EXPECT_TRUE(
        registry.last_reply_token == 1
        && caller->runtime_flags == MICROS_THREAD_RTS_IPC_REPLY
        && caller->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && thread_handle_is_zero(caller->ipc_next)
        && !caller->ready_linked
        && bytes_are_zero(
            &caller->ipc_outbound_message,
            sizeof(caller->ipc_outbound_message)
        )
        && caller->ipc_send_destination == 0
        && caller->ipc_receive_buffer == (uintptr_t)&request
        && caller->ipc_reply_token == 1
        && caller->ipc_reply_callee
            == endpoints[CALL_PROCESS_SERVER]
        && objects.threads[client_extra_thread.slot]
            .ipc_reply_token == 0
        && receiver->runtime_flags == 0
        && receiver->ready_linked
        && receiver->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && receiver->ipc_receive_source == 0
        && receiver->ipc_receive_buffer == receive_buffer
        && receiver->ipc_delivery_pending
        && receiver->ipc_staged_result == MICROS_IPC_OK
        && memcmp(
            &receiver->ipc_inbound_message,
            &expected,
            sizeof(expected)
        ) == 0
        && thread_handle_is_zero(server->receiver_head)
        && thread_handle_is_zero(server->receiver_tail)
        && memcmp(
            &request,
            &request_snapshot,
            sizeof(request)
        ) == 0
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );

    saved_callee = objects.threads[
        primary_threads[CALL_PROCESS_CLIENT].slot
    ].ipc_reply_callee;
    objects.threads[
        primary_threads[CALL_PROCESS_CLIENT].slot
    ].ipc_reply_callee +=
        UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS;
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_ERROR_INVARIANT
    );
    objects.threads[
        primary_threads[CALL_PROCESS_CLIENT].slot
    ].ipc_reply_callee = saved_callee;
    return true;
}

static bool test_call_preserves_independent_receiver_flags(void)
{
    struct micros_ipc_message request = message_pattern(
        0,
        UINT32_C(0x1235),
        0,
        UINT8_C(0x21)
    );
    struct micros_ipc_message request_snapshot = request;
    struct micros_ipc_message expected;
    const uintptr_t receive_buffer = UINT64_C(0x60000800);
    const struct micros_thread *caller;
    struct micros_thread *receiver;

    EXPECT_TRUE(setup_call_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            primary_threads[CALL_PROCESS_SERVER],
            endpoints[CALL_PROCESS_CLIENT],
            receive_buffer
        )
    );
    receiver = &objects.threads[
        primary_threads[CALL_PROCESS_SERVER].slot
    ];
    receiver->runtime_flags |= MICROS_THREAD_RTS_INACTIVE;
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &registry,
            &objects,
            primary_threads[CALL_PROCESS_CLIENT],
            endpoints[CALL_PROCESS_SERVER],
            &request
        )
    );
    expected = canonical_request(
        &request_snapshot,
        endpoints[CALL_PROCESS_CLIENT],
        1
    );
    caller = &objects.threads[
        primary_threads[CALL_PROCESS_CLIENT].slot
    ];
    receiver = &objects.threads[
        primary_threads[CALL_PROCESS_SERVER].slot
    ];
    EXPECT_TRUE(
        registry.last_reply_token == 1
        && caller->runtime_flags == MICROS_THREAD_RTS_IPC_REPLY
        && caller->ipc_reply_token == 1
        && receiver->runtime_flags == MICROS_THREAD_RTS_INACTIVE
        && !receiver->ready_linked
        && receiver->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && thread_handle_is_zero(receiver->ipc_next)
        && receiver->ipc_receive_source == 0
        && receiver->ipc_receive_buffer == receive_buffer
        && receiver->ipc_delivery_pending
        && memcmp(
            &receiver->ipc_inbound_message,
            &expected,
            sizeof(expected)
        ) == 0
        && thread_handle_is_zero(
            registry.endpoints[CALL_PROCESS_SERVER].receiver_head
        )
        && thread_handle_is_zero(
            registry.endpoints[CALL_PROCESS_SERVER].receiver_tail
        )
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool test_queued_calls_bind_exact_threads_and_deliver_fifo(void)
{
    struct micros_ipc_message first_request = message_pattern(
        UINT32_C(0x11111111),
        UINT32_C(0x2001),
        UINT64_C(0x1111111111111111),
        UINT8_C(0x30)
    );
    struct micros_ipc_message second_request = message_pattern(
        UINT32_C(0x22222222),
        UINT32_C(0x2002),
        UINT64_C(0x2222222222222222),
        UINT8_C(0x40)
    );
    struct micros_ipc_message first_snapshot = first_request;
    struct micros_ipc_message second_snapshot = second_request;
    struct micros_ipc_message expected;
    const uintptr_t receive_buffer = UINT64_C(0x60001000);
    const struct micros_endpoint_record *server;
    const struct micros_thread *first_caller;
    const struct micros_thread *second_caller;
    const struct micros_thread *receiver;

    EXPECT_TRUE(setup_call_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &registry,
            &objects,
            primary_threads[CALL_PROCESS_CLIENT],
            endpoints[CALL_PROCESS_SERVER],
            &first_request
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &registry,
            &objects,
            client_extra_thread,
            endpoints[CALL_PROCESS_SERVER],
            &second_request
        )
    );

    server = &registry.endpoints[CALL_PROCESS_SERVER];
    first_caller = &objects.threads[
        primary_threads[CALL_PROCESS_CLIENT].slot
    ];
    second_caller = &objects.threads[client_extra_thread.slot];
    EXPECT_TRUE(
        registry.last_reply_token == 2
        && thread_handles_equal(
            server->sender_head,
            primary_threads[CALL_PROCESS_CLIENT]
        )
        && thread_handles_equal(
            server->sender_tail,
            client_extra_thread
        )
        && thread_handles_equal(
            first_caller->ipc_next,
            client_extra_thread
        )
        && thread_handle_is_zero(second_caller->ipc_next)
        && first_caller->runtime_flags
            == (
                MICROS_THREAD_RTS_IPC_SEND
                | MICROS_THREAD_RTS_IPC_REPLY
            )
        && second_caller->runtime_flags
            == (
                MICROS_THREAD_RTS_IPC_SEND
                | MICROS_THREAD_RTS_IPC_REPLY
            )
        && first_caller->ipc_reply_token == 1
        && second_caller->ipc_reply_token == 2
        && first_caller->ipc_receive_buffer
            == (uintptr_t)&first_request
        && second_caller->ipc_receive_buffer
            == (uintptr_t)&second_request
        && first_caller->ipc_reply_callee
            == endpoints[CALL_PROCESS_SERVER]
        && second_caller->ipc_reply_callee
            == endpoints[CALL_PROCESS_SERVER]
        && first_caller->ipc_outbound_message.source
            == endpoints[CALL_PROCESS_CLIENT]
        && first_caller->ipc_outbound_message.reply_token == 1
        && second_caller->ipc_outbound_message.source
            == endpoints[CALL_PROCESS_CLIENT]
        && second_caller->ipc_outbound_message.reply_token == 2
        && memcmp(
            &first_request,
            &first_snapshot,
            sizeof(first_request)
        ) == 0
        && memcmp(
            &second_request,
            &second_snapshot,
            sizeof(second_request)
        ) == 0
    );

    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            primary_threads[CALL_PROCESS_SERVER],
            MICROS_ENDPOINT_ANY,
            receive_buffer
        )
    );
    expected = canonical_request(
        &first_snapshot,
        endpoints[CALL_PROCESS_CLIENT],
        1
    );
    server = &registry.endpoints[CALL_PROCESS_SERVER];
    first_caller = &objects.threads[
        primary_threads[CALL_PROCESS_CLIENT].slot
    ];
    second_caller = &objects.threads[client_extra_thread.slot];
    receiver = &objects.threads[
        primary_threads[CALL_PROCESS_SERVER].slot
    ];
    EXPECT_TRUE(
        first_caller->runtime_flags == MICROS_THREAD_RTS_IPC_REPLY
        && first_caller->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && thread_handle_is_zero(first_caller->ipc_next)
        && first_caller->ipc_receive_buffer
            == (uintptr_t)&first_request
        && first_caller->ipc_reply_token == 1
        && first_caller->ipc_reply_callee
            == endpoints[CALL_PROCESS_SERVER]
        && bytes_are_zero(
            &first_caller->ipc_outbound_message,
            sizeof(first_caller->ipc_outbound_message)
        )
        && first_caller->ipc_send_destination == 0
        && second_caller->runtime_flags
            == (
                MICROS_THREAD_RTS_IPC_SEND
                | MICROS_THREAD_RTS_IPC_REPLY
            )
        && thread_handles_equal(
            server->sender_head,
            client_extra_thread
        )
        && thread_handles_equal(
            server->sender_tail,
            client_extra_thread
        )
        && receiver->runtime_flags == 0
        && receiver->ipc_delivery_pending
        && receiver->ipc_receive_buffer == receive_buffer
        && memcmp(
            &receiver->ipc_inbound_message,
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

static bool test_low_level_enqueue_rejects_reply_tokens(void)
{
    struct micros_ipc_message request = message_pattern(
        0,
        UINT32_C(0x3000),
        0,
        UINT8_C(0x4f)
    );
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;

    EXPECT_TRUE(setup_call_fixture());
    registry.last_reply_token = 41;
    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_REPLY_TOKEN,
        micros_ipc_sender_enqueue(
            &registry,
            &objects,
            primary_threads[CALL_PROCESS_CLIENT],
            endpoints[CALL_PROCESS_SERVER],
            &request,
            41,
            (uintptr_t)&request
        )
    );
    EXPECT_TRUE(complete_state_is_unchanged(
        &registry_snapshot,
        &objects_snapshot
    ));
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_REPLY_TOKEN,
        micros_ipc_sender_enqueue(
            &registry,
            &objects,
            primary_threads[CALL_PROCESS_CLIENT],
            endpoints[CALL_PROCESS_SERVER],
            &request,
            42,
            (uintptr_t)&request
        )
    );
    EXPECT_TRUE(complete_state_is_unchanged(
        &registry_snapshot,
        &objects_snapshot
    ));
    return true;
}

static bool test_token_exhaustion_is_preflighted(void)
{
    struct micros_ipc_message first_request = message_pattern(
        0,
        UINT32_C(0x3001),
        0,
        UINT8_C(0x50)
    );
    struct micros_ipc_message second_request = message_pattern(
        0,
        UINT32_C(0x3002),
        0,
        UINT8_C(0x60)
    );
    const struct micros_thread *first_caller;

    EXPECT_TRUE(setup_call_fixture());
    registry.last_reply_token = UINT64_MAX - 1;
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &registry,
            &objects,
            primary_threads[CALL_PROCESS_CLIENT],
            endpoints[CALL_PROCESS_SERVER],
            &first_request
        )
    );
    first_caller = &objects.threads[
        primary_threads[CALL_PROCESS_CLIENT].slot
    ];
    EXPECT_TRUE(
        registry.last_reply_token == UINT64_MAX
        && first_caller->ipc_reply_token == UINT64_MAX
        && first_caller->ipc_outbound_message.reply_token
            == UINT64_MAX
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    EXPECT_TRUE(expect_call_failure_unchanged(
        MICROS_IPC_ERROR_REPLY_TOKEN_EXHAUSTED,
        client_extra_thread,
        endpoints[CALL_PROCESS_SERVER],
        &second_request
    ));
    return true;
}

static bool test_call_rejections_are_atomic(void)
{
    struct micros_ipc_message request = message_pattern(
        0,
        UINT32_C(0x4001),
        0,
        UINT8_C(0x70)
    );
    struct micros_ipc_message kernel_request = request;
    micros_endpoint_t stale_destination;
    struct micros_thread_handle stale_caller;
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;
    struct micros_scheduler_return_plan plan;
    _Alignas(struct micros_ipc_message)
        unsigned char unaligned_storage[
            sizeof(struct micros_ipc_message) + 1
        ];

    EXPECT_TRUE(setup_call_fixture());
    stale_destination =
        endpoints[CALL_PROCESS_SERVER]
        + (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS);
    EXPECT_TRUE(expect_call_failure_unchanged(
        MICROS_IPC_ERROR_UNAUTHORIZED,
        primary_threads[CALL_PROCESS_NO_CALL],
        endpoints[CALL_PROCESS_SERVER],
        &request
    ));
    EXPECT_TRUE(expect_call_failure_unchanged(
        MICROS_IPC_ERROR_UNAUTHORIZED,
        primary_threads[CALL_PROCESS_ISOLATED_CALL],
        endpoints[CALL_PROCESS_SERVER],
        &request
    ));
    EXPECT_TRUE(expect_call_failure_unchanged(
        MICROS_IPC_ERROR_STATE,
        primary_threads[CALL_PROCESS_CLIENT],
        endpoints[CALL_PROCESS_RESERVED],
        &request
    ));
    EXPECT_TRUE(expect_call_failure_unchanged(
        MICROS_IPC_ERROR_DEAD_ENDPOINT,
        primary_threads[CALL_PROCESS_CLIENT],
        stale_destination,
        &request
    ));
    stale_caller = primary_threads[CALL_PROCESS_CLIENT];
    ++stale_caller.generation;
    EXPECT_TRUE(expect_call_failure_unchanged(
        MICROS_IPC_ERROR_DEAD_ENDPOINT,
        stale_caller,
        endpoints[CALL_PROCESS_SERVER],
        &request
    ));
    kernel_request.type |= MICROS_IPC_TYPE_KERNEL_MASK;
    EXPECT_TRUE(expect_call_failure_unchanged(
        MICROS_IPC_ERROR_ARGUMENT,
        primary_threads[CALL_PROCESS_CLIENT],
        endpoints[CALL_PROCESS_SERVER],
        &kernel_request
    ));
    memset(unaligned_storage, UINT8_C(0xa5), sizeof(unaligned_storage));
    memset(unaligned_storage, UINT8_C(0xa5), sizeof(unaligned_storage));
    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_ARGUMENT,
        micros_ipc_call(
            &registry,
            &objects,
            primary_threads[CALL_PROCESS_CLIENT],
            endpoints[CALL_PROCESS_SERVER],
            (struct micros_ipc_message *)(unaligned_storage + 1)
        )
    );
    EXPECT_TRUE(complete_state_is_unchanged(
        &registry_snapshot,
        &objects_snapshot
    ));

    EXPECT_TRUE(
        setup_call_fixture()
        && make_current_held(
            primary_threads[CALL_PROCESS_CLIENT]
        )
    );
    EXPECT_TRUE(expect_call_failure_unchanged(
        MICROS_IPC_ERROR_STATE,
        primary_threads[CALL_PROCESS_CLIENT],
        endpoints[CALL_PROCESS_SERVER],
        &request
    ));

    EXPECT_TRUE(setup_call_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &registry,
            &objects,
            primary_threads[CALL_PROCESS_CLIENT],
            endpoints[CALL_PROCESS_SERVER],
            &request
        )
    );
    EXPECT_TRUE(expect_call_failure_unchanged(
        MICROS_IPC_ERROR_STATE,
        primary_threads[CALL_PROCESS_CLIENT],
        endpoints[CALL_PROCESS_SERVER],
        &request
    ));

    EXPECT_TRUE(setup_call_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            primary_threads[CALL_PROCESS_SERVER],
            endpoints[CALL_PROCESS_CLIENT],
            UINT64_C(0x60002000)
        )
    );
    EXPECT_TRUE(
        micros_thread_install_policy(
            &objects,
            primary_threads[CALL_PROCESS_NO_CALL],
            MICROS_SCHEDULER_PRIORITY_LOWEST,
            100
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_scheduler_accounting_initialize(
            &objects,
            hart,
            100
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_thread_runtime_flags_unset(
            &objects,
            primary_threads[CALL_PROCESS_NO_CALL],
            MICROS_THREAD_RTS_INACTIVE
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_hart_plan_user_return(&objects, hart, &plan)
            == MICROS_KERNEL_OBJECT_OK
        && micros_hart_commit_user_return(&objects, &plan)
            == MICROS_KERNEL_OBJECT_OK
        && micros_scheduler_account_enter_thread(
            &objects,
            hart,
            primary_threads[CALL_PROCESS_NO_CALL],
            120
        ) == MICROS_KERNEL_OBJECT_OK
    );
    EXPECT_TRUE(expect_call_failure_unchanged(
        MICROS_IPC_ERROR_STATE,
        primary_threads[CALL_PROCESS_CLIENT],
        endpoints[CALL_PROCESS_SERVER],
        &request
    ));

    EXPECT_TRUE(setup_call_fixture());
    registry.endpoints[CALL_PROCESS_SERVER].sender_head =
        (struct micros_thread_handle){
            MICROS_THREAD_CAPACITY - 1,
            UINT32_C(0x55aa55aa),
        };
    registry.endpoints[CALL_PROCESS_SERVER].sender_tail =
        registry.endpoints[CALL_PROCESS_SERVER].sender_head;
    EXPECT_TRUE(expect_call_failure_unchanged(
        MICROS_IPC_ERROR_INVARIANT,
        primary_threads[CALL_PROCESS_CLIENT],
        endpoints[CALL_PROCESS_SERVER],
        &request
    ));

    EXPECT_TRUE(setup_call_fixture());
    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_ARGUMENT,
        micros_ipc_call(
            NULL,
            &objects,
            primary_threads[CALL_PROCESS_CLIENT],
            endpoints[CALL_PROCESS_SERVER],
            &request
        )
    );
    EXPECT_TRUE(complete_state_is_unchanged(
        &registry_snapshot,
        &objects_snapshot
    ));
    return true;
}

static bool test_staged_call_token_binding_is_validated(void)
{
    struct micros_ipc_message request = message_pattern(
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

    EXPECT_TRUE(setup_call_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            primary_threads[CALL_PROCESS_SERVER],
            endpoints[CALL_PROCESS_CLIENT],
            UINT64_C(0x60003000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &registry,
            &objects,
            primary_threads[CALL_PROCESS_CLIENT],
            endpoints[CALL_PROCESS_SERVER],
            &request
        )
    );
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    registry_snapshot = registry;
    objects_snapshot = objects;

#define EXPECT_STAGED_TOKEN_CORRUPTION(statement) \
    do { \
        registry = registry_snapshot; \
        objects = objects_snapshot; \
        statement; \
        EXPECT_TRUE( \
            micros_endpoint_registry_validate_objects( \
                &registry, \
                &objects \
            ) == MICROS_ENDPOINT_ERROR_INVARIANT \
        ); \
    } while (false)

    EXPECT_STAGED_TOKEN_CORRUPTION(
        objects.threads[
            primary_threads[CALL_PROCESS_SERVER].slot
        ].ipc_inbound_message.reply_token = 2
    );
    EXPECT_STAGED_TOKEN_CORRUPTION(
        objects.threads[
            primary_threads[CALL_PROCESS_SERVER].slot
        ].ipc_inbound_message.source =
            endpoints[CALL_PROCESS_NO_CALL]
    );
    EXPECT_STAGED_TOKEN_CORRUPTION(
        objects.threads[
            primary_threads[CALL_PROCESS_CLIENT].slot
        ].ipc_reply_callee = endpoints[CALL_PROCESS_NO_CALL]
    );
    registry = registry_snapshot;
    objects = objects_snapshot;
    caller = &objects.threads[
        primary_threads[CALL_PROCESS_CLIENT].slot
    ];
    receiver = &objects.threads[
        primary_threads[CALL_PROCESS_SERVER].slot
    ];
    server = &registry.endpoints[CALL_PROCESS_SERVER];
    caller->runtime_flags |= MICROS_THREAD_RTS_IPC_SEND;
    caller->ipc_queue_kind = MICROS_IPC_QUEUE_SENDER;
    caller->ipc_outbound_message = receiver->ipc_inbound_message;
    caller->ipc_send_destination = endpoints[CALL_PROCESS_SERVER];
    server->sender_head = primary_threads[CALL_PROCESS_CLIENT];
    server->sender_tail = primary_threads[CALL_PROCESS_CLIENT];
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_ERROR_INVARIANT
    );

#undef EXPECT_STAGED_TOKEN_CORRUPTION
    return true;
}

static bool test_allocator_validator_rejects_unissued_token(void)
{
    struct micros_ipc_message request = message_pattern(
        0,
        UINT32_C(0x5001),
        0,
        UINT8_C(0x80)
    );
    struct micros_thread *caller;

    EXPECT_TRUE(setup_call_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &registry,
            &objects,
            primary_threads[CALL_PROCESS_CLIENT],
            endpoints[CALL_PROCESS_SERVER],
            &request
        )
    );
    caller = &objects.threads[
        primary_threads[CALL_PROCESS_CLIENT].slot
    ];
    caller->ipc_reply_token = 2;
    caller->ipc_outbound_message.reply_token = 2;
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_ERROR_INVARIANT
    );
    return true;
}

bool micros_ipc_call_test_run(void)
{
    static const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {
            "call delivers to waiting receiver",
            test_call_delivers_to_waiting_receiver,
        },
        {
            "call preserves independent receiver flags",
            test_call_preserves_independent_receiver_flags,
        },
        {
            "queued calls bind exact threads and deliver FIFO",
            test_queued_calls_bind_exact_threads_and_deliver_fifo,
        },
        {
            "low-level enqueue rejects reply tokens",
            test_low_level_enqueue_rejects_reply_tokens,
        },
        {
            "token exhaustion is preflighted",
            test_token_exhaustion_is_preflighted,
        },
        {
            "call rejections are atomic",
            test_call_rejections_are_atomic,
        },
        {
            "staged call token binding is validated",
            test_staged_call_token_binding_is_validated,
        },
        {
            "allocator validator rejects unissued token",
            test_allocator_validator_rejects_unissued_token,
        },
    };
    size_t index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run()) {
            fprintf(
                stderr,
                "IPC call subtest failed: %s\n",
                tests[index].name
            );
            return false;
        }
    }
    return true;
}
