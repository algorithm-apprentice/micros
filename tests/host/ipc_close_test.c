#include "micros/ipc_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/scheduler_core.h"

enum {
    CLOSE_PROCESS_CLOSING = 0,
    CLOSE_PROCESS_FOREIGN_SEND,
    CLOSE_PROCESS_FOREIGN_CALL_QUEUED,
    CLOSE_PROCESS_FOREIGN_CALL_DELIVERED,
    CLOSE_PROCESS_FOREIGN_SPECIFIC,
    CLOSE_PROCESS_FOREIGN_ANY,
    CLOSE_PROCESS_FOREIGN_DESTINATION,
    CLOSE_PROCESS_NOTIFY_DESTINATION,
    CLOSE_PROCESS_NOTIFY_SOURCE,
    CLOSE_PROCESS_STAGED_DESTINATION,
    CLOSE_PROCESS_WAIT_SOURCE,
    CLOSE_PROCESS_COUNT,
    CLOSE_EXTRA_THREAD_COUNT = 4,
    CLOSE_SCENARIO_CASE_COUNT = 512,
    CLOSE_SCENARIO_TRACE_COUNT = 16,
};

enum close_scenario_operation {
    CLOSE_SCENARIO_FOREIGN_SEND = 0,
    CLOSE_SCENARIO_FOREIGN_CALL,
    CLOSE_SCENARIO_SPECIFIC_RECEIVE,
    CLOSE_SCENARIO_ANY_RECEIVE,
    CLOSE_SCENARIO_CLOSING_SEND,
    CLOSE_SCENARIO_STAGED_SEND,
    CLOSE_SCENARIO_NOTIFICATION_FROM_CLOSING,
    CLOSE_SCENARIO_NOTIFICATION_TO_CLOSING,
    CLOSE_SCENARIO_OPERATION_COUNT,
};

struct close_scenario_trace_entry {
    size_t case_index;
    enum close_scenario_operation operation;
};

static const char *const close_scenario_operation_names[
    CLOSE_SCENARIO_OPERATION_COUNT
] = {
    "foreign-send",
    "foreign-call",
    "specific-receive",
    "any-receive",
    "closing-send",
    "staged-send",
    "notification-from-closing",
    "notification-to-closing",
};

static struct micros_endpoint_registry registry;
static struct micros_kernel_objects objects;
static struct micros_process_handle processes[MICROS_PROCESS_CAPACITY];
static struct micros_thread_handle primary_threads[MICROS_PROCESS_CAPACITY];
static micros_endpoint_t endpoints[MICROS_PROCESS_CAPACITY];
static struct micros_hart_handle hart;
static struct close_scenario_trace_entry
    close_scenario_trace[CLOSE_SCENARIO_TRACE_COUNT];
static size_t close_scenario_trace_count;

bool micros_ipc_close_test_run(void);
bool micros_ipc_close_model_test_run(void);

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
            .operations = MICROS_PRIVILEGE_OPERATION_DEFINED_MASK,
            .call_targets = UINT32_C(1) << 1,
            .send_targets = UINT32_C(1) << 1,
            .notify_targets = UINT32_C(1) << 1,
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
    uint8_t priority,
    struct micros_thread_handle *thread
)
{
    struct micros_user_context context;
    uintptr_t stack_bottom;

    if (
        micros_thread_create(
            &objects,
            processes[process_index],
            thread
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    context = context_pattern(
        UINT64_C(0x1000)
            + (uint64_t)thread->slot * UINT64_C(0x100)
    );
    stack_bottom =
        UINT64_C(0x10000000)
        + (uintptr_t)thread->slot * UINT64_C(0x00008000);
    return (
        micros_thread_attach_execution_context(
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
            priority,
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
                MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
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

static bool hold_thread_for_close(struct micros_thread_handle thread)
{
    return (
        (
            objects.threads[thread.slot].runtime_flags
            & MICROS_THREAD_RTS_INACTIVE
        ) != 0
        || micros_thread_scheduler_hold(&objects, thread)
            == MICROS_KERNEL_OBJECT_OK
    );
}

static bool thread_has_dead_endpoint_result(
    struct micros_thread_handle handle
)
{
    const struct micros_thread *thread = &objects.threads[handle.slot];

    return (
        thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
        && thread->generation == handle.generation
        && (
            thread->runtime_flags
            & MICROS_THREAD_RTS_IPC_MASK
        ) == 0
        && thread->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && thread_handle_is_zero(thread->ipc_next)
        && bytes_are_zero(
            &thread->ipc_outbound_message,
            sizeof(thread->ipc_outbound_message)
        )
        && thread->ipc_send_destination == 0
        && thread->ipc_receive_source == 0
        && thread->ipc_receive_buffer == 0
        && thread->ipc_delivery_pending
        && bytes_are_zero(
            &thread->ipc_inbound_message,
            sizeof(thread->ipc_inbound_message)
        )
        && thread->ipc_staged_result
            == MICROS_IPC_ERROR_DEAD_ENDPOINT
        && thread->ipc_reply_token == 0
        && thread->ipc_reply_callee == 0
    );
}

static bool closing_thread_is_detached(
    struct micros_thread_handle handle
)
{
    const struct micros_thread *thread = &objects.threads[handle.slot];

    return (
        thread->runtime_flags == MICROS_THREAD_RTS_INACTIVE
        && !thread->ready_linked
        && micros_thread_ipc_state_is_clear(thread)
    );
}

static bool clear_staged_delivery(
    struct micros_thread_handle handle
)
{
    struct micros_thread *thread = &objects.threads[handle.slot];

    if (
        !thread->ipc_delivery_pending
        || thread->ipc_staged_result != MICROS_IPC_OK
    ) {
        return false;
    }
    thread->ipc_receive_buffer = 0;
    thread->ipc_delivery_pending = false;
    memset(
        &thread->ipc_inbound_message,
        0,
        sizeof(thread->ipc_inbound_message)
    );
    thread->ipc_staged_result = MICROS_IPC_OK;
    return micros_endpoint_registry_validate_objects(
        &registry,
        &objects
    ) == MICROS_ENDPOINT_OK;
}

static bool endpoint_queue_is_empty(
    const struct micros_endpoint_record *endpoint
)
{
    return (
        thread_handle_is_zero(endpoint->sender_head)
        && thread_handle_is_zero(endpoint->sender_tail)
        && thread_handle_is_zero(endpoint->receiver_head)
        && thread_handle_is_zero(endpoint->receiver_tail)
    );
}

static bool expect_close_failure_unchanged(
    enum micros_ipc_error expected,
    micros_endpoint_t endpoint
)
{
    struct micros_endpoint_registry registry_snapshot = registry;
    struct micros_kernel_objects objects_snapshot = objects;

    return (
        micros_ipc_endpoint_close(
            &registry,
            &objects,
            endpoint
        ) == expected
        && memcmp(
            &registry,
            &registry_snapshot,
            sizeof(registry)
        ) == 0
        && memcmp(
            &objects,
            &objects_snapshot,
            sizeof(objects)
        ) == 0
    );
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

static bool make_single_sender_current_blocked(
    struct micros_thread_handle sender,
    micros_endpoint_t destination
)
{
    struct micros_endpoint_record *endpoint;
    struct micros_thread saved;
    struct micros_thread *thread;

    endpoint = &registry.endpoints[
        processes[CLOSE_PROCESS_CLOSING].slot
    ];
    thread = &objects.threads[sender.slot];
    if (
        destination != endpoints[CLOSE_PROCESS_CLOSING]
        || !thread_handles_equal(endpoint->sender_head, sender)
        || !thread_handles_equal(endpoint->sender_tail, sender)
        || thread->runtime_flags != MICROS_THREAD_RTS_IPC_SEND
        || thread->ipc_queue_kind != MICROS_IPC_QUEUE_SENDER
        || !thread_handle_is_zero(thread->ipc_next)
    ) {
        return false;
    }
    saved = *thread;
    endpoint->sender_head = (struct micros_thread_handle){0, 0};
    endpoint->sender_tail = (struct micros_thread_handle){0, 0};
    thread->runtime_flags = MICROS_THREAD_RTS_INACTIVE;
    thread->ipc_queue_kind = MICROS_IPC_QUEUE_NONE;
    thread->ipc_next = (struct micros_thread_handle){0, 0};
    memset(
        &thread->ipc_outbound_message,
        0,
        sizeof(thread->ipc_outbound_message)
    );
    thread->ipc_send_destination = 0;
    if (
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) != MICROS_ENDPOINT_OK
        || !make_current_held(sender)
    ) {
        return false;
    }
    *thread = saved;
    endpoint->sender_head = sender;
    endpoint->sender_tail = sender;
    return micros_endpoint_registry_validate_objects(
        &registry,
        &objects
    ) == MICROS_ENDPOINT_OK;
}

static bool test_close_cancels_complete_transaction(void)
{
    struct micros_thread_handle
        closing_extra[CLOSE_EXTRA_THREAD_COUNT];
    struct micros_thread_handle closing_threads[
        CLOSE_EXTRA_THREAD_COUNT + 1
    ];
    struct micros_thread_handle affected_threads[4];
    struct micros_ipc_message messages[5];
    struct micros_thread any_snapshot;
    struct micros_endpoint_record any_endpoint_snapshot;
    uint64_t last_reply_token;
    size_t index;

    EXPECT_TRUE(setup_fixture(CLOSE_PROCESS_COUNT, 6));
    messages[0] = message_pattern(UINT32_C(0x1001), UINT8_C(0x10));
    messages[1] = message_pattern(UINT32_C(0x1002), UINT8_C(0x20));
    messages[2] = message_pattern(UINT32_C(0x1003), UINT8_C(0x30));
    messages[3] = message_pattern(UINT32_C(0x1004), UINT8_C(0x40));
    messages[4] = message_pattern(UINT32_C(0x1005), UINT8_C(0x50));

    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            primary_threads[CLOSE_PROCESS_CLOSING],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60001000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &registry,
            &objects,
            primary_threads[CLOSE_PROCESS_FOREIGN_CALL_DELIVERED],
            endpoints[CLOSE_PROCESS_CLOSING],
            &messages[0]
        )
    );
    EXPECT_TRUE(
        hold_thread_for_close(
            primary_threads[CLOSE_PROCESS_CLOSING]
        )
        && clear_staged_delivery(
            primary_threads[CLOSE_PROCESS_CLOSING]
        )
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );

    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &registry,
            &objects,
            primary_threads[CLOSE_PROCESS_FOREIGN_SEND],
            endpoints[CLOSE_PROCESS_CLOSING],
            &messages[1]
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &registry,
            &objects,
            primary_threads[CLOSE_PROCESS_FOREIGN_CALL_QUEUED],
            endpoints[CLOSE_PROCESS_CLOSING],
            &messages[2]
        )
    );
    for (index = 0; index < CLOSE_EXTRA_THREAD_COUNT; ++index) {
        EXPECT_TRUE(
            prepare_thread(
                CLOSE_PROCESS_CLOSING,
                MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
                &closing_extra[index]
            )
        );
    }
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &registry,
            &objects,
            closing_extra[0],
            endpoints[CLOSE_PROCESS_FOREIGN_DESTINATION],
            &messages[3]
        )
    );
    EXPECT_TRUE(hold_thread_for_close(closing_extra[0]));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &registry,
            &objects,
            closing_extra[1],
            endpoints[CLOSE_PROCESS_FOREIGN_DESTINATION],
            &messages[4]
        )
    );
    EXPECT_TRUE(hold_thread_for_close(closing_extra[1]));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            closing_extra[3],
            endpoints[CLOSE_PROCESS_WAIT_SOURCE],
            UINT64_C(0x60002000)
        )
    );
    EXPECT_TRUE(hold_thread_for_close(closing_extra[3]));

    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            primary_threads[CLOSE_PROCESS_FOREIGN_SPECIFIC],
            endpoints[CLOSE_PROCESS_CLOSING],
            UINT64_C(0x60003000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            primary_threads[CLOSE_PROCESS_FOREIGN_ANY],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60004000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &registry,
            &objects,
            closing_extra[2],
            endpoints[CLOSE_PROCESS_NOTIFY_DESTINATION],
            UINT64_C(0x55)
        )
    );
    EXPECT_TRUE(hold_thread_for_close(closing_extra[2]));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &registry,
            &objects,
            primary_threads[CLOSE_PROCESS_NOTIFY_SOURCE],
            endpoints[CLOSE_PROCESS_CLOSING],
            UINT64_C(0xaa)
        )
    );

    any_snapshot = objects.threads[
        primary_threads[CLOSE_PROCESS_FOREIGN_ANY].slot
    ];
    any_endpoint_snapshot = registry.endpoints[
        processes[CLOSE_PROCESS_FOREIGN_ANY].slot
    ];
    last_reply_token = registry.last_reply_token;
    EXPECT_TRUE(
        last_reply_token != 0
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );

    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_endpoint_close(
            &registry,
            &objects,
            endpoints[CLOSE_PROCESS_CLOSING]
        )
    );

    closing_threads[0] =
        primary_threads[CLOSE_PROCESS_CLOSING];
    for (index = 0; index < CLOSE_EXTRA_THREAD_COUNT; ++index) {
        closing_threads[index + 1] = closing_extra[index];
    }
    for (
        index = 0;
        index < sizeof(closing_threads) / sizeof(closing_threads[0]);
        ++index
    ) {
        EXPECT_TRUE(closing_thread_is_detached(closing_threads[index]));
    }
    affected_threads[0] =
        primary_threads[CLOSE_PROCESS_FOREIGN_SEND];
    affected_threads[1] =
        primary_threads[CLOSE_PROCESS_FOREIGN_CALL_QUEUED];
    affected_threads[2] =
        primary_threads[CLOSE_PROCESS_FOREIGN_CALL_DELIVERED];
    affected_threads[3] =
        primary_threads[CLOSE_PROCESS_FOREIGN_SPECIFIC];
    for (
        index = 0;
        index < sizeof(affected_threads) / sizeof(affected_threads[0]);
        ++index
    ) {
        EXPECT_TRUE(
            thread_has_dead_endpoint_result(affected_threads[index])
        );
    }
    EXPECT_TRUE(
        memcmp(
            &objects.threads[
                primary_threads[CLOSE_PROCESS_FOREIGN_ANY].slot
            ],
            &any_snapshot,
            sizeof(any_snapshot)
        ) == 0
        && memcmp(
            &registry.endpoints[
                processes[CLOSE_PROCESS_FOREIGN_ANY].slot
            ],
            &any_endpoint_snapshot,
            sizeof(any_endpoint_snapshot)
        ) == 0
        && endpoint_queue_is_empty(
            &registry.endpoints[
                processes[CLOSE_PROCESS_FOREIGN_DESTINATION].slot
            ]
        )
        && (
            registry.endpoints[
                processes[CLOSE_PROCESS_NOTIFY_DESTINATION].slot
            ].pending_notification_sources
            & (
                UINT64_C(1)
                << processes[CLOSE_PROCESS_CLOSING].slot
            )
        ) == 0
        && registry.endpoints[
            processes[CLOSE_PROCESS_NOTIFY_DESTINATION].slot
        ].pending_events[processes[CLOSE_PROCESS_CLOSING].slot]
            == 0
        && registry.endpoints[
            processes[CLOSE_PROCESS_CLOSING].slot
        ].state == MICROS_ENDPOINT_STATE_FREE
        && objects.processes[
            processes[CLOSE_PROCESS_CLOSING].slot
        ].primary_endpoint == MICROS_PROCESS_ENDPOINT_NONE
        && objects.processes[
            processes[CLOSE_PROCESS_CLOSING].slot
        ].privilege_profile == 0
        && registry.last_reply_token == last_reply_token
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool test_cascading_close_discards_owned_dead_endpoint_result(void)
{
    struct micros_ipc_message first_message =
        message_pattern(UINT32_C(0x1701), UINT8_C(0x58));
    struct micros_ipc_message second_message =
        message_pattern(UINT32_C(0x1702), UINT8_C(0x60));

    EXPECT_TRUE(setup_fixture(3, 1));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &registry,
            &objects,
            primary_threads[1],
            endpoints[0],
            &first_message
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &registry,
            &objects,
            primary_threads[2],
            endpoints[1],
            &second_message
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_endpoint_close(
            &registry,
            &objects,
            endpoints[0]
        )
    );
    EXPECT_TRUE(
        thread_has_dead_endpoint_result(primary_threads[1])
        && hold_thread_for_close(primary_threads[1])
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_endpoint_close(
            &registry,
            &objects,
            endpoints[1]
        )
    );
    EXPECT_TRUE(
        closing_thread_is_detached(primary_threads[1])
        && thread_has_dead_endpoint_result(primary_threads[2])
        && registry.endpoints[processes[0].slot].state
            == MICROS_ENDPOINT_STATE_FREE
        && registry.endpoints[processes[1].slot].state
            == MICROS_ENDPOINT_STATE_FREE
        && registry.endpoints[processes[2].slot].state
            == MICROS_ENDPOINT_STATE_ACTIVE
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool test_successful_staged_deliveries_block_close_atomically(void)
{
    struct micros_ipc_message message =
        message_pattern(UINT32_C(0x1801), UINT8_C(0x68));

    EXPECT_TRUE(setup_fixture(3, 1));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            primary_threads[CLOSE_PROCESS_CLOSING],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60005500)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &registry,
            &objects,
            primary_threads[CLOSE_PROCESS_FOREIGN_SEND],
            endpoints[CLOSE_PROCESS_CLOSING],
            &message
        )
    );
    EXPECT_TRUE(
        hold_thread_for_close(
            primary_threads[CLOSE_PROCESS_CLOSING]
        )
        && expect_close_failure_unchanged(
            MICROS_IPC_ERROR_STATE,
            endpoints[CLOSE_PROCESS_CLOSING]
        )
        && clear_staged_delivery(
            primary_threads[CLOSE_PROCESS_CLOSING]
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_endpoint_close(
            &registry,
            &objects,
            endpoints[CLOSE_PROCESS_CLOSING]
        )
    );

    EXPECT_TRUE(setup_fixture(3, 1));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            primary_threads[CLOSE_PROCESS_CLOSING],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60005600)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &registry,
            &objects,
            primary_threads[CLOSE_PROCESS_FOREIGN_SEND],
            endpoints[CLOSE_PROCESS_CLOSING],
            UINT64_C(0x40)
        )
    );
    EXPECT_TRUE(
        hold_thread_for_close(
            primary_threads[CLOSE_PROCESS_CLOSING]
        )
        && expect_close_failure_unchanged(
            MICROS_IPC_ERROR_STATE,
            endpoints[CLOSE_PROCESS_CLOSING]
        )
        && clear_staged_delivery(
            primary_threads[CLOSE_PROCESS_CLOSING]
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_endpoint_close(
            &registry,
            &objects,
            endpoints[CLOSE_PROCESS_CLOSING]
        )
    );

    EXPECT_TRUE(setup_fixture(3, 1));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            primary_threads[CLOSE_PROCESS_FOREIGN_SEND],
            endpoints[CLOSE_PROCESS_CLOSING],
            UINT64_C(0x60005700)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &registry,
            &objects,
            primary_threads[CLOSE_PROCESS_CLOSING],
            endpoints[CLOSE_PROCESS_FOREIGN_SEND],
            &message
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_stage_no_message_completion(
            &registry,
            &objects,
            primary_threads[CLOSE_PROCESS_CLOSING],
            MICROS_IPC_OK
        )
    );
    EXPECT_TRUE(
        hold_thread_for_close(
            primary_threads[CLOSE_PROCESS_CLOSING]
        )
        && expect_close_failure_unchanged(
            MICROS_IPC_ERROR_STATE,
            endpoints[CLOSE_PROCESS_CLOSING]
        )
        && clear_staged_delivery(
            primary_threads[CLOSE_PROCESS_FOREIGN_SEND]
        )
        && expect_close_failure_unchanged(
            MICROS_IPC_ERROR_STATE,
            endpoints[CLOSE_PROCESS_CLOSING]
        )
        && clear_staged_delivery(
            primary_threads[CLOSE_PROCESS_CLOSING]
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_endpoint_close(
            &registry,
            &objects,
            endpoints[CLOSE_PROCESS_CLOSING]
        )
    );
    return true;
}

static bool test_close_failures_are_atomic(void)
{
    struct micros_ipc_message message =
        message_pattern(UINT32_C(0x2001), UINT8_C(0x70));
    struct micros_process_handle stale_process;
    micros_endpoint_t stale_endpoint;
    struct micros_endpoint_record *closing;
    struct micros_thread *thread;
    uint64_t source_bit;

    EXPECT_TRUE(setup_fixture(3, 1));
    stale_process = processes[CLOSE_PROCESS_CLOSING];
    ++stale_process.generation;
    EXPECT_TRUE(
        micros_endpoint_pack(stale_process, &stale_endpoint)
            == MICROS_ENDPOINT_OK
        && expect_close_failure_unchanged(
            MICROS_IPC_ERROR_DEAD_ENDPOINT,
            stale_endpoint
        )
    );

    EXPECT_TRUE(setup_fixture(3, 1));
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_runtime_flags_unset(
            &objects,
            primary_threads[CLOSE_PROCESS_CLOSING],
            MICROS_THREAD_RTS_INACTIVE
        )
    );
    EXPECT_TRUE(expect_close_failure_unchanged(
        MICROS_IPC_ERROR_STATE,
        endpoints[CLOSE_PROCESS_CLOSING]
    ));

    EXPECT_TRUE(setup_fixture(3, 1));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &registry,
            &objects,
            primary_threads[CLOSE_PROCESS_FOREIGN_SEND],
            endpoints[CLOSE_PROCESS_CLOSING],
            &message
        )
    );
    closing = &registry.endpoints[
        processes[CLOSE_PROCESS_CLOSING].slot
    ];
    ++closing->sender_head.generation;
    EXPECT_TRUE(expect_close_failure_unchanged(
        MICROS_IPC_ERROR_INVARIANT,
        endpoints[CLOSE_PROCESS_CLOSING]
    ));

    EXPECT_TRUE(setup_fixture(3, 1));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            primary_threads[CLOSE_PROCESS_FOREIGN_SEND],
            endpoints[CLOSE_PROCESS_CLOSING],
            UINT64_C(0x60005800)
        )
    );
    ++registry.endpoints[
        processes[CLOSE_PROCESS_FOREIGN_SEND].slot
    ].receiver_head.generation;
    EXPECT_TRUE(expect_close_failure_unchanged(
        MICROS_IPC_ERROR_INVARIANT,
        endpoints[CLOSE_PROCESS_CLOSING]
    ));

    EXPECT_TRUE(setup_fixture(3, 1));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &registry,
            &objects,
            primary_threads[CLOSE_PROCESS_FOREIGN_SEND],
            endpoints[CLOSE_PROCESS_CLOSING],
            &message
        )
    );
    stale_process = processes[CLOSE_PROCESS_CLOSING];
    ++stale_process.generation;
    EXPECT_TRUE(
        micros_endpoint_pack(stale_process, &stale_endpoint)
            == MICROS_ENDPOINT_OK
    );
    objects.threads[
        primary_threads[CLOSE_PROCESS_FOREIGN_SEND].slot
    ].ipc_reply_callee = stale_endpoint;
    EXPECT_TRUE(expect_close_failure_unchanged(
        MICROS_IPC_ERROR_INVARIANT,
        endpoints[CLOSE_PROCESS_CLOSING]
    ));

    EXPECT_TRUE(setup_fixture(3, 1));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &registry,
            &objects,
            primary_threads[CLOSE_PROCESS_FOREIGN_SEND],
            endpoints[CLOSE_PROCESS_CLOSING],
            UINT64_C(0x80)
        )
    );
    closing = &registry.endpoints[
        processes[CLOSE_PROCESS_CLOSING].slot
    ];
    source_bit =
        UINT64_C(1) << processes[CLOSE_PROCESS_FOREIGN_SEND].slot;
    closing->pending_notification_sources &= ~source_bit;
    closing->pending_events[
        processes[CLOSE_PROCESS_FOREIGN_SEND].slot
    ] = 0;
    closing->pending_notification_sources |= UINT64_C(1) << 63;
    closing->pending_events[63] = UINT64_C(1);
    EXPECT_TRUE(expect_close_failure_unchanged(
        MICROS_IPC_ERROR_INVARIANT,
        endpoints[CLOSE_PROCESS_CLOSING]
    ));

    EXPECT_TRUE(setup_fixture(3, 1));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            primary_threads[CLOSE_PROCESS_FOREIGN_SEND],
            endpoints[CLOSE_PROCESS_CLOSING],
            UINT64_C(0x60006000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &registry,
            &objects,
            primary_threads[CLOSE_PROCESS_CLOSING],
            endpoints[CLOSE_PROCESS_FOREIGN_SEND],
            &message
        )
    );
    EXPECT_TRUE(
        hold_thread_for_close(
            primary_threads[CLOSE_PROCESS_CLOSING]
        )
    );
    stale_process = processes[CLOSE_PROCESS_CLOSING];
    ++stale_process.generation;
    EXPECT_TRUE(
        micros_endpoint_pack(stale_process, &stale_endpoint)
            == MICROS_ENDPOINT_OK
    );
    thread = &objects.threads[
        primary_threads[CLOSE_PROCESS_FOREIGN_SEND].slot
    ];
    thread->ipc_inbound_message.source = stale_endpoint;
    EXPECT_TRUE(expect_close_failure_unchanged(
        MICROS_IPC_ERROR_INVARIANT,
        endpoints[CLOSE_PROCESS_CLOSING]
    ));

    EXPECT_TRUE(setup_fixture(3, 1));
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &registry,
            &objects,
            primary_threads[CLOSE_PROCESS_FOREIGN_SEND],
            endpoints[CLOSE_PROCESS_CLOSING],
            &message
        )
    );
    EXPECT_TRUE(
        make_single_sender_current_blocked(
            primary_threads[CLOSE_PROCESS_FOREIGN_SEND],
            endpoints[CLOSE_PROCESS_CLOSING]
        )
        && expect_close_failure_unchanged(
            MICROS_IPC_ERROR_STATE,
            endpoints[CLOSE_PROCESS_CLOSING]
        )
    );
    return true;
}

static uint32_t close_scenario_next_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void close_scenario_record(
    size_t case_index,
    enum close_scenario_operation operation
)
{
    size_t index =
        close_scenario_trace_count % CLOSE_SCENARIO_TRACE_COUNT;

    close_scenario_trace[index].case_index = case_index;
    close_scenario_trace[index].operation = operation;
    ++close_scenario_trace_count;
}

static bool close_scenario_fail(
    uint32_t seed,
    size_t case_index,
    enum close_scenario_operation operation,
    const char *message
)
{
    size_t available =
        close_scenario_trace_count < CLOSE_SCENARIO_TRACE_COUNT
            ? close_scenario_trace_count
            : CLOSE_SCENARIO_TRACE_COUNT;
    size_t first = close_scenario_trace_count - available;
    size_t index;

    fprintf(
        stderr,
        "IPC close scenarios seed=0x%08x case=%zu op=%s: %s\n",
        seed,
        case_index,
        close_scenario_operation_names[operation],
        message
    );
    for (index = first; index < close_scenario_trace_count; ++index) {
        const struct close_scenario_trace_entry *entry =
            &close_scenario_trace[
                index % CLOSE_SCENARIO_TRACE_COUNT
            ];

        fprintf(
            stderr,
            "  case=%zu op=%s\n",
            entry->case_index,
            close_scenario_operation_names[entry->operation]
        );
    }
    return false;
}

static bool test_seeded_close_scenarios(void)
{
    const uint32_t seed = UINT32_C(0x30c105e1);
    uint32_t random_state = seed;
    size_t coverage[CLOSE_SCENARIO_OPERATION_COUNT] = {0};
    size_t case_index;

    close_scenario_trace_count = 0;
    for (
        case_index = 0;
        case_index < CLOSE_SCENARIO_CASE_COUNT;
        ++case_index
    ) {
        enum close_scenario_operation operation =
            (enum close_scenario_operation)(
                close_scenario_next_random(&random_state)
                % CLOSE_SCENARIO_OPERATION_COUNT
            );
        struct micros_ipc_message message = message_pattern(
            UINT32_C(0x3000) + (uint32_t)case_index,
            (uint8_t)random_state
        );
        struct micros_thread actor_snapshot;
        struct micros_thread peer_snapshot;
        uint64_t event_mask =
            (
                (uint64_t)close_scenario_next_random(
                    &random_state
                )
                << 32
            )
            | close_scenario_next_random(&random_state)
            | UINT64_C(1);
        bool actor_should_fail = false;
        bool staged_close_blocked = false;

        close_scenario_record(case_index, operation);
        ++coverage[operation];
        if (!setup_fixture(3, 1)) {
            return close_scenario_fail(
                seed,
                case_index,
                operation,
                "fixture setup failed"
            );
        }
        switch (operation) {
        case CLOSE_SCENARIO_FOREIGN_SEND:
            actor_should_fail = true;
            if (
                micros_ipc_send(
                    &registry,
                    &objects,
                    primary_threads[1],
                    endpoints[0],
                    &message
                ) != MICROS_IPC_OK
            ) {
                return close_scenario_fail(
                    seed,
                    case_index,
                    operation,
                    "foreign send setup failed"
                );
            }
            break;
        case CLOSE_SCENARIO_FOREIGN_CALL:
            actor_should_fail = true;
            if (
                micros_ipc_call(
                    &registry,
                    &objects,
                    primary_threads[1],
                    endpoints[0],
                    &message
                ) != MICROS_IPC_OK
            ) {
                return close_scenario_fail(
                    seed,
                    case_index,
                    operation,
                    "foreign call setup failed"
                );
            }
            break;
        case CLOSE_SCENARIO_SPECIFIC_RECEIVE:
            actor_should_fail = true;
            if (
                micros_ipc_receive(
                    &registry,
                    &objects,
                    primary_threads[1],
                    endpoints[0],
                    UINT64_C(0x61000000)
                ) != MICROS_IPC_OK
            ) {
                return close_scenario_fail(
                    seed,
                    case_index,
                    operation,
                    "specific receive setup failed"
                );
            }
            break;
        case CLOSE_SCENARIO_ANY_RECEIVE:
            if (
                micros_ipc_receive(
                    &registry,
                    &objects,
                    primary_threads[1],
                    MICROS_ENDPOINT_ANY,
                    UINT64_C(0x61000000)
                ) != MICROS_IPC_OK
            ) {
                return close_scenario_fail(
                    seed,
                    case_index,
                    operation,
                    "ANY receive setup failed"
                );
            }
            break;
        case CLOSE_SCENARIO_CLOSING_SEND:
            if (
                micros_ipc_send(
                    &registry,
                    &objects,
                    primary_threads[0],
                    endpoints[1],
                    &message
                ) != MICROS_IPC_OK
                || !hold_thread_for_close(primary_threads[0])
            ) {
                return close_scenario_fail(
                    seed,
                    case_index,
                    operation,
                    "closing send setup failed"
                );
            }
            break;
        case CLOSE_SCENARIO_STAGED_SEND:
            staged_close_blocked = true;
            if (
                micros_ipc_receive(
                    &registry,
                    &objects,
                    primary_threads[1],
                    endpoints[0],
                    UINT64_C(0x61000000)
                ) != MICROS_IPC_OK
                || micros_ipc_send(
                    &registry,
                    &objects,
                    primary_threads[0],
                    endpoints[1],
                    &message
                ) != MICROS_IPC_OK
                || !hold_thread_for_close(primary_threads[0])
            ) {
                return close_scenario_fail(
                    seed,
                    case_index,
                    operation,
                    "staged send setup failed"
                );
            }
            break;
        case CLOSE_SCENARIO_NOTIFICATION_FROM_CLOSING:
            if (
                micros_ipc_notify(
                    &registry,
                    &objects,
                    primary_threads[0],
                    endpoints[1],
                    event_mask
                ) != MICROS_IPC_OK
                || !hold_thread_for_close(primary_threads[0])
            ) {
                return close_scenario_fail(
                    seed,
                    case_index,
                    operation,
                    "closing notification setup failed"
                );
            }
            break;
        case CLOSE_SCENARIO_NOTIFICATION_TO_CLOSING:
            if (
                micros_ipc_notify(
                    &registry,
                    &objects,
                    primary_threads[1],
                    endpoints[0],
                    event_mask
                ) != MICROS_IPC_OK
            ) {
                return close_scenario_fail(
                    seed,
                    case_index,
                    operation,
                    "foreign notification setup failed"
                );
            }
            break;
        default:
            return close_scenario_fail(
                seed,
                case_index,
                operation,
                "unknown operation"
            );
        }
        actor_snapshot = objects.threads[primary_threads[1].slot];
        peer_snapshot = objects.threads[primary_threads[2].slot];
        if (staged_close_blocked) {
            if (
                !expect_close_failure_unchanged(
                    MICROS_IPC_ERROR_STATE,
                    endpoints[0]
                )
                || !clear_staged_delivery(primary_threads[1])
            ) {
                return close_scenario_fail(
                    seed,
                    case_index,
                    operation,
                    "staged close preflight mismatch"
                );
            }
            actor_snapshot =
                objects.threads[primary_threads[1].slot];
        }
        if (
            micros_ipc_endpoint_close(
                &registry,
                &objects,
                endpoints[0]
            ) != MICROS_IPC_OK
        ) {
            return close_scenario_fail(
                seed,
                case_index,
                operation,
                "close failed"
            );
        }
        if (
            !closing_thread_is_detached(primary_threads[0])
            || registry.endpoints[processes[0].slot].state
                != MICROS_ENDPOINT_STATE_FREE
            || micros_endpoint_registry_validate_objects(
                &registry,
                &objects
            ) != MICROS_ENDPOINT_OK
        ) {
            return close_scenario_fail(
                seed,
                case_index,
                operation,
                "closed state is invalid"
            );
        }
        if (
            actor_should_fail
                ? !thread_has_dead_endpoint_result(
                    primary_threads[1]
                )
                : memcmp(
                    &objects.threads[primary_threads[1].slot],
                    &actor_snapshot,
                    sizeof(actor_snapshot)
                ) != 0
        ) {
            return close_scenario_fail(
                seed,
                case_index,
                operation,
                "actor outcome mismatch"
            );
        }
        if (
            memcmp(
                &objects.threads[primary_threads[2].slot],
                &peer_snapshot,
                sizeof(peer_snapshot)
            ) != 0
        ) {
            return close_scenario_fail(
                seed,
                case_index,
                operation,
                "unrelated peer changed"
            );
        }
        if (
            operation == CLOSE_SCENARIO_CLOSING_SEND
            && !thread_handle_is_zero(
                registry.endpoints[processes[1].slot].sender_head
            )
        ) {
            return close_scenario_fail(
                seed,
                case_index,
                operation,
                "closing sender remained queued"
            );
        }
        if (
            operation
                == CLOSE_SCENARIO_NOTIFICATION_FROM_CLOSING
            && (
                registry.endpoints[processes[1].slot]
                    .pending_notification_sources != 0
                || registry.endpoints[processes[1].slot]
                    .pending_events[processes[0].slot] != 0
            )
        ) {
            return close_scenario_fail(
                seed,
                case_index,
                operation,
                "closing notification remained pending"
            );
        }
    }
    for (
        case_index = 0;
        case_index < CLOSE_SCENARIO_OPERATION_COUNT;
        ++case_index
    ) {
        if (coverage[case_index] == 0) {
            return close_scenario_fail(
                seed,
                CLOSE_SCENARIO_CASE_COUNT,
                (enum close_scenario_operation)case_index,
                "operation received no coverage"
            );
        }
    }
    return true;
}

bool micros_ipc_close_test_run(void)
{
    static const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {
            "complete cancellation transaction",
            test_close_cancels_complete_transaction,
        },
        {
            "cascading close discards owned dead-endpoint result",
            test_cascading_close_discards_owned_dead_endpoint_result,
        },
        {
            "successful staged deliveries block close atomically",
            test_successful_staged_deliveries_block_close_atomically,
        },
        {
            "stale corrupt and late failures are atomic",
            test_close_failures_are_atomic,
        },
    };
    size_t index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run()) {
            fprintf(
                stderr,
                "IPC close subtest failed: %s\n",
                tests[index].name
            );
            return false;
        }
    }
    return true;
}

bool micros_ipc_close_model_test_run(void)
{
    return test_seeded_close_scenarios();
}
