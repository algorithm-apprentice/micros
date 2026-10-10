#include "micros/ipc_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/scheduler_core.h"

enum {
    NOTIFY_PROCESS_SOURCE_LOW = 0,
    NOTIFY_PROCESS_SOURCE_HIGH,
    NOTIFY_PROCESS_DESTINATION,
    NOTIFY_PROCESS_SENDER,
    NOTIFY_PROCESS_SERVER,
    NOTIFY_PROCESS_NO_NOTIFY,
    NOTIFY_PROCESS_ISOLATED_NOTIFY,
    NOTIFY_PROCESS_RESERVED,
    NOTIFY_PROCESS_COUNT,
    NOTIFY_MODEL_STEPS = 4096,
    NOTIFY_MODEL_TRACE_COUNT = 32,
};

enum notify_model_operation {
    NOTIFY_MODEL_NOTIFY = 0,
    NOTIFY_MODEL_KERNEL_NOTIFY,
    NOTIFY_MODEL_RECEIVE_ANY,
    NOTIFY_MODEL_RECEIVE_SPECIFIC,
    NOTIFY_MODEL_ZERO_REJECTED,
    NOTIFY_MODEL_OPERATION_REJECTED,
    NOTIFY_MODEL_TARGET_REJECTED,
    NOTIFY_MODEL_OPERATION_COUNT,
};

struct notify_model_trace_entry {
    size_t step;
    enum notify_model_operation operation;
    size_t source;
    uint64_t event_mask;
};

struct notify_model_coverage {
    size_t notified;
    size_t coalesced;
    size_t kernel_notified;
    size_t kernel_coalesced;
    size_t kernel_received_any;
    size_t received_any;
    size_t received_specific;
    size_t zero_rejected;
    size_t operation_rejected;
    size_t target_rejected;
};

static const char *const notify_model_operation_names[
    NOTIFY_MODEL_OPERATION_COUNT
] = {
    "notify",
    "kernel-notify",
    "receive-any",
    "receive-specific",
    "zero-rejected",
    "operation-rejected",
    "target-rejected",
};

static struct micros_endpoint_registry registry;
static struct micros_kernel_objects objects;
static struct micros_process_handle processes[NOTIFY_PROCESS_COUNT];
static struct micros_thread_handle primary_threads[NOTIFY_PROCESS_COUNT];
static struct micros_thread_handle destination_extra_threads[2];
static micros_endpoint_t endpoints[NOTIFY_PROCESS_COUNT];
static struct micros_hart_handle hart;
static struct notify_model_trace_entry
    notify_model_trace[NOTIFY_MODEL_TRACE_COUNT];
static size_t notify_model_trace_count;

bool micros_ipc_notify_test_run(void);
bool micros_ipc_notify_model_test_run(void);

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

static bool thread_handle_is_zero(
    struct micros_thread_handle handle
)
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

static bool setup_notify_fixture(void)
{
    static const struct micros_privilege_profile profiles[] = {
        {
            .id = 1,
            .name = "NOTIFIER",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_NOTIFY,
            .notify_targets =
                (UINT32_C(1) << 2)
                | (UINT32_C(1) << 4),
        },
        {
            .id = 2,
            .name = "DESTINATION",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_CALL,
            .call_targets = UINT32_C(1) << 4,
        },
        {
            .id = 3,
            .name = "SENDER",
            .operations = MICROS_PRIVILEGE_OPERATION_SEND,
            .send_targets =
                (UINT32_C(1) << 2)
                | (UINT32_C(1) << 4),
        },
        {
            .id = 4,
            .name = "SERVER",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_REPLY
                | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
        },
        {
            .id = 5,
            .name = "NO_NOTIFY",
            .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
        },
        {
            .id = 6,
            .name = "ISOLATED_NOTIFY",
            .operations = MICROS_PRIVILEGE_OPERATION_NOTIFY,
            .notify_targets = UINT32_C(1) << 6,
        },
    };
    static const uint8_t profile_ids[NOTIFY_PROCESS_COUNT] = {
        1,
        1,
        2,
        3,
        4,
        5,
        6,
        1,
    };
    size_t index;

    memset(&registry, 0, sizeof(registry));
    memset(&objects, 0, sizeof(objects));
    memset(processes, 0, sizeof(processes));
    memset(primary_threads, 0, sizeof(primary_threads));
    memset(destination_extra_threads, 0, sizeof(destination_extra_threads));
    memset(endpoints, 0, sizeof(endpoints));
    if (
        micros_endpoint_registry_initialize(
            &registry,
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) != MICROS_ENDPOINT_OK
        || micros_kernel_objects_initialize(&objects, 3, 1)
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
    for (index = 0; index < NOTIFY_PROCESS_COUNT; ++index) {
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
                index != NOTIFY_PROCESS_RESERVED
                && micros_endpoint_activate(
                    &registry,
                    &objects,
                    endpoints[index]
                ) != MICROS_ENDPOINT_OK
            )
            || !prepare_thread(
                processes[index],
                UINT64_C(0x1000) + index * UINT64_C(0x100),
                UINT64_C(0x10000000)
                    + index * UINT64_C(0x00010000),
                &primary_threads[index]
            )
        ) {
            return false;
        }
    }
    return true;
}

static bool prepare_destination_extra_threads(void)
{
    return (
        prepare_thread(
            processes[NOTIFY_PROCESS_DESTINATION],
            UINT64_C(0x2000),
            UINT64_C(0x11000000),
            &destination_extra_threads[0]
        )
        && prepare_thread(
            processes[NOTIFY_PROCESS_DESTINATION],
            UINT64_C(0x2100),
            UINT64_C(0x11008000),
            &destination_extra_threads[1]
        )
    );
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

static bool expect_notify_failure_unchanged(
    enum micros_ipc_error expected,
    struct micros_thread_handle notifier,
    micros_endpoint_t destination,
    uint64_t event_mask
)
{
    struct micros_endpoint_registry registry_snapshot = registry;
    struct micros_kernel_objects objects_snapshot = objects;

    EXPECT_IPC_ERROR(
        expected,
        micros_ipc_notify(
            &registry,
            &objects,
            notifier,
            destination,
            event_mask
        )
    );
    EXPECT_TRUE(state_is_unchanged(
        &registry_snapshot,
        &objects_snapshot
    ));
    return true;
}

static bool expect_kernel_notify_failure_unchanged(
    enum micros_ipc_error expected,
    micros_endpoint_t destination,
    uint64_t event_mask
)
{
    struct micros_endpoint_registry registry_snapshot = registry;
    struct micros_kernel_objects objects_snapshot = objects;

    EXPECT_IPC_ERROR(
        expected,
        micros_ipc_inject_kernel_notification(
            &registry,
            &objects,
            destination,
            event_mask
        )
    );
    EXPECT_TRUE(state_is_unchanged(
        &registry_snapshot,
        &objects_snapshot
    ));
    return true;
}

static uint64_t notification_event_mask(
    const struct micros_ipc_message *message
)
{
    uint64_t event_mask = 0;
    size_t index;

    for (index = 0; index < sizeof(event_mask); ++index) {
        event_mask |=
            (uint64_t)message->payload[index] << (index * 8);
    }
    return event_mask;
}

static bool notification_message_matches(
    const struct micros_ipc_message *message,
    micros_endpoint_t source,
    uint64_t event_mask
)
{
    size_t index;

    if (
        message->source != source
        || message->type != MICROS_IPC_TYPE_KERNEL_NOTIFICATION
        || message->reply_token != 0
        || notification_event_mask(message) != event_mask
    ) {
        return false;
    }
    for (index = sizeof(event_mask); index < sizeof(message->payload); ++index) {
        if (message->payload[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool clear_staged_delivery(
    struct micros_thread_handle receiver_handle,
    struct micros_ipc_message *delivered
)
{
    struct micros_thread *receiver =
        &objects.threads[receiver_handle.slot];

    if (!receiver->ipc_delivery_pending) {
        return false;
    }
    if (delivered != NULL) {
        *delivered = receiver->ipc_inbound_message;
    }
    receiver->ipc_receive_buffer = 0;
    receiver->ipc_delivery_pending = false;
    memset(
        &receiver->ipc_inbound_message,
        0,
        sizeof(receiver->ipc_inbound_message)
    );
    receiver->ipc_staged_result = MICROS_IPC_OK;
    return true;
}

static bool hold_runnable_thread(struct micros_thread_handle thread_handle)
{
    const struct micros_thread *thread =
        &objects.threads[thread_handle.slot];

    return (
        thread->runtime_flags == 0
        && thread->ready_linked
        && micros_thread_scheduler_hold(&objects, thread_handle)
            == MICROS_KERNEL_OBJECT_OK
    );
}

static bool consume_staged_and_hold(
    struct micros_thread_handle receiver_handle,
    struct micros_ipc_message *delivered
)
{
    return (
        clear_staged_delivery(receiver_handle, delivered)
        && hold_runnable_thread(receiver_handle)
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
}

static bool make_current_held(struct micros_thread_handle thread_handle)
{
    struct micros_scheduler_return_plan plan;

    return (
        micros_scheduler_accounting_initialize(&objects, hart, 100)
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_runtime_flags_unset(
            &objects,
            thread_handle,
            MICROS_THREAD_RTS_INACTIVE
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_hart_plan_user_return(&objects, hart, &plan)
            == MICROS_KERNEL_OBJECT_OK
        && micros_hart_commit_user_return(&objects, &plan)
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_scheduler_hold(&objects, thread_handle)
            == MICROS_KERNEL_OBJECT_OK
    );
}

static bool make_reply_wait_current(
    struct micros_thread_handle caller_handle
)
{
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
        || !make_current_held(caller_handle)
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

static bool make_single_receiver_current_blocked(
    struct micros_thread_handle receiver_handle
)
{
    struct micros_endpoint_record *endpoint =
        &registry.endpoints[processes[NOTIFY_PROCESS_DESTINATION].slot];
    struct micros_thread *receiver =
        &objects.threads[receiver_handle.slot];
    micros_endpoint_t receive_source = receiver->ipc_receive_source;
    uintptr_t receive_buffer = receiver->ipc_receive_buffer;

    if (
        !thread_handles_equal(endpoint->receiver_head, receiver_handle)
        || !thread_handles_equal(endpoint->receiver_tail, receiver_handle)
        || receiver->runtime_flags != MICROS_THREAD_RTS_IPC_RECEIVE
        || receiver->ipc_queue_kind != MICROS_IPC_QUEUE_RECEIVER
        || !thread_handle_is_zero(receiver->ipc_next)
    ) {
        return false;
    }
    endpoint->receiver_head.slot = 0;
    endpoint->receiver_head.generation = 0;
    endpoint->receiver_tail.slot = 0;
    endpoint->receiver_tail.generation = 0;
    receiver->runtime_flags = MICROS_THREAD_RTS_INACTIVE;
    receiver->ipc_queue_kind = MICROS_IPC_QUEUE_NONE;
    receiver->ipc_receive_source = 0;
    receiver->ipc_receive_buffer = 0;
    if (
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) != MICROS_ENDPOINT_OK
        || !make_current_held(receiver_handle)
    ) {
        return false;
    }
    receiver->runtime_flags = MICROS_THREAD_RTS_IPC_RECEIVE;
    receiver->ipc_queue_kind = MICROS_IPC_QUEUE_RECEIVER;
    receiver->ipc_receive_source = receive_source;
    receiver->ipc_receive_buffer = receive_buffer;
    endpoint->receiver_head = receiver_handle;
    endpoint->receiver_tail = receiver_handle;
    return micros_endpoint_registry_validate_objects(
        &registry,
        &objects
    ) == MICROS_ENDPOINT_OK;
}

static bool test_notify_coalesces_and_never_blocks(void)
{
    struct micros_endpoint_record *destination;
    struct micros_thread *source;
    struct micros_thread *receiver;
    uint64_t source_bit;
    const uint64_t first_mask = UINT64_C(0x0000000000000015);
    const uint64_t second_mask = UINT64_C(0x800000000000002a);
    const uint64_t combined_mask = first_mask | second_mask;

    EXPECT_TRUE(setup_notify_fixture());
    destination = &registry.endpoints[
        processes[NOTIFY_PROCESS_DESTINATION].slot
    ];
    source = &objects.threads[
        primary_threads[NOTIFY_PROCESS_SOURCE_LOW].slot
    ];
    receiver = &objects.threads[
        primary_threads[NOTIFY_PROCESS_DESTINATION].slot
    ];
    source_bit = UINT64_C(1)
        << processes[NOTIFY_PROCESS_SOURCE_LOW].slot;

    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
            endpoints[NOTIFY_PROCESS_DESTINATION],
            first_mask
        )
    );
    EXPECT_TRUE(
        source->runtime_flags == 0
        && source->ready_linked
        && source->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && (
            source->runtime_flags
            & MICROS_THREAD_RTS_IPC_MASK
        ) == 0
        && destination->pending_notification_sources == source_bit
        && destination->pending_events[
            processes[NOTIFY_PROCESS_SOURCE_LOW].slot
        ] == first_mask
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
        && hold_runnable_thread(
            primary_threads[NOTIFY_PROCESS_SOURCE_LOW]
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
            endpoints[NOTIFY_PROCESS_DESTINATION],
            second_mask
        )
    );
    EXPECT_TRUE(
        source->runtime_flags == 0
        && source->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && destination->pending_notification_sources == source_bit
        && destination->pending_events[
            processes[NOTIFY_PROCESS_SOURCE_LOW].slot
        ] == combined_mask
        && hold_runnable_thread(
            primary_threads[NOTIFY_PROCESS_SOURCE_LOW]
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60000000)
        )
    );
    EXPECT_TRUE(
        receiver->runtime_flags == 0
        && receiver->ready_linked
        && receiver->ipc_delivery_pending
        && notification_message_matches(
            &receiver->ipc_inbound_message,
            endpoints[NOTIFY_PROCESS_SOURCE_LOW],
            combined_mask
        )
        && destination->pending_notification_sources == 0
        && destination->pending_events[
            processes[NOTIFY_PROCESS_SOURCE_LOW].slot
        ] == 0
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );

    return true;
}

static bool test_kernel_notify_coalesces_and_any_receives(void)
{
    struct micros_endpoint_record *destination;
    struct micros_thread *receiver;
    uint64_t zero_events[MICROS_PROCESS_CAPACITY] = {0};
    const uint64_t first_mask = UINT64_C(0x0000000000000015);
    const uint64_t second_mask = UINT64_C(0x800000000000002a);
    const uint64_t combined_mask = first_mask | second_mask;

    EXPECT_TRUE(setup_notify_fixture());
    destination = &registry.endpoints[
        processes[NOTIFY_PROCESS_DESTINATION].slot
    ];
    receiver = &objects.threads[
        primary_threads[NOTIFY_PROCESS_DESTINATION].slot
    ];

    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_inject_kernel_notification(
            &registry,
            &objects,
            endpoints[NOTIFY_PROCESS_DESTINATION],
            first_mask
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_inject_kernel_notification(
            &registry,
            &objects,
            endpoints[NOTIFY_PROCESS_DESTINATION],
            second_mask
        )
    );
    EXPECT_TRUE(
        destination->pending_kernel_events == combined_mask
        && destination->pending_notification_sources == 0
        && memcmp(
            destination->pending_events,
            zero_events,
            sizeof(destination->pending_events)
        ) == 0
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x6000e000)
        )
    );
    EXPECT_TRUE(
        destination->pending_kernel_events == 0
        && receiver->runtime_flags == 0
        && receiver->ready_linked
        && receiver->ipc_delivery_pending
        && notification_message_matches(
            &receiver->ipc_inbound_message,
            MICROS_ENDPOINT_NONE,
            combined_mask
        )
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );

    return true;
}

static bool test_kernel_notify_wakes_only_any_receiver(void)
{
    struct micros_endpoint_record *destination;
    struct micros_thread *receiver;
    const uint64_t event_mask = UINT64_C(0x0000000000000080);

    EXPECT_TRUE(setup_notify_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x6000f000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_inject_kernel_notification(
            &registry,
            &objects,
            endpoints[NOTIFY_PROCESS_DESTINATION],
            event_mask
        )
    );
    destination = &registry.endpoints[
        processes[NOTIFY_PROCESS_DESTINATION].slot
    ];
    receiver = &objects.threads[
        primary_threads[NOTIFY_PROCESS_DESTINATION].slot
    ];
    EXPECT_TRUE(
        destination->pending_kernel_events == 0
        && thread_handle_is_zero(destination->receiver_head)
        && thread_handle_is_zero(destination->receiver_tail)
        && receiver->runtime_flags == 0
        && receiver->ready_linked
        && receiver->ipc_delivery_pending
        && notification_message_matches(
            &receiver->ipc_inbound_message,
            MICROS_ENDPOINT_NONE,
            event_mask
        )
    );

    EXPECT_TRUE(setup_notify_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_inject_kernel_notification(
            &registry,
            &objects,
            endpoints[NOTIFY_PROCESS_DESTINATION],
            event_mask
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            endpoints[NOTIFY_PROCESS_SOURCE_LOW],
            UINT64_C(0x60010000)
        )
    );
    destination = &registry.endpoints[
        processes[NOTIFY_PROCESS_DESTINATION].slot
    ];
    receiver = &objects.threads[
        primary_threads[NOTIFY_PROCESS_DESTINATION].slot
    ];
    EXPECT_TRUE(
        destination->pending_kernel_events == event_mask
        && thread_handles_equal(
            destination->receiver_head,
            primary_threads[NOTIFY_PROCESS_DESTINATION]
        )
        && thread_handles_equal(
            destination->receiver_tail,
            primary_threads[NOTIFY_PROCESS_DESTINATION]
        )
        && receiver->runtime_flags == MICROS_THREAD_RTS_IPC_RECEIVE
        && !receiver->ipc_delivery_pending
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool test_kernel_notify_prepare_commit_is_retained(void)
{
    struct micros_ipc_kernel_notification_plan plan;
    struct micros_ipc_kernel_notification_plan sentinel;
    struct micros_ipc_kernel_notification_plan zero_plan = {0};
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;
    struct micros_endpoint_record *destination;
    struct micros_thread *receiver;
    const uint64_t event_mask = UINT64_C(0x0000000000000400);

    EXPECT_TRUE(setup_notify_fixture());
    memset(&sentinel, 0xa5, sizeof(sentinel));
    plan = sentinel;
    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_ARGUMENT,
        micros_ipc_prepare_kernel_notification(
            &registry,
            &objects,
            endpoints[NOTIFY_PROCESS_DESTINATION],
            0,
            &plan
        )
    );
    EXPECT_TRUE(
        memcmp(&plan, &sentinel, sizeof(plan)) == 0
        && state_is_unchanged(
            &registry_snapshot,
            &objects_snapshot
        )
    );

    plan = sentinel;
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_prepare_kernel_notification(
            &registry,
            &objects,
            endpoints[NOTIFY_PROCESS_DESTINATION],
            event_mask,
            &plan
        )
    );
    EXPECT_TRUE(
        plan.active
        && !plan.deliver_to_receiver
        && state_is_unchanged(
            &registry_snapshot,
            &objects_snapshot
        )
    );
    micros_ipc_commit_kernel_notification_prevalidated(
        &registry,
        &objects,
        &plan
    );
    destination = &registry.endpoints[
        processes[NOTIFY_PROCESS_DESTINATION].slot
    ];
    EXPECT_TRUE(
        memcmp(&plan, &zero_plan, sizeof(plan)) == 0
        && destination->pending_kernel_events == event_mask
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );

    EXPECT_TRUE(setup_notify_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60010800)
        )
    );
    registry_snapshot = registry;
    objects_snapshot = objects;
    plan = sentinel;
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_prepare_kernel_notification(
            &registry,
            &objects,
            endpoints[NOTIFY_PROCESS_DESTINATION],
            event_mask,
            &plan
        )
    );
    EXPECT_TRUE(
        plan.active
        && plan.deliver_to_receiver
        && state_is_unchanged(
            &registry_snapshot,
            &objects_snapshot
        )
    );
    micros_ipc_commit_kernel_notification_prevalidated(
        &registry,
        &objects,
        &plan
    );
    destination = &registry.endpoints[
        processes[NOTIFY_PROCESS_DESTINATION].slot
    ];
    receiver = &objects.threads[
        primary_threads[NOTIFY_PROCESS_DESTINATION].slot
    ];
    EXPECT_TRUE(
        memcmp(&plan, &zero_plan, sizeof(plan)) == 0
        && destination->pending_kernel_events == 0
        && thread_handle_is_zero(destination->receiver_head)
        && thread_handle_is_zero(destination->receiver_tail)
        && receiver->runtime_flags == 0
        && receiver->ready_linked
        && receiver->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && receiver->ipc_receive_source == 0
        && receiver->ipc_delivery_pending
        && notification_message_matches(
            &receiver->ipc_inbound_message,
            MICROS_ENDPOINT_NONE,
            event_mask
        )
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );

    EXPECT_TRUE(setup_notify_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60011000)
        )
    );
    EXPECT_TRUE(
        make_single_receiver_current_blocked(
            primary_threads[NOTIFY_PROCESS_DESTINATION]
        )
    );
    registry_snapshot = registry;
    objects_snapshot = objects;
    plan = sentinel;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_STATE,
        micros_ipc_prepare_kernel_notification(
            &registry,
            &objects,
            endpoints[NOTIFY_PROCESS_DESTINATION],
            event_mask,
            &plan
        )
    );
    EXPECT_TRUE(
        memcmp(&plan, &sentinel, sizeof(plan)) == 0
        && state_is_unchanged(
            &registry_snapshot,
            &objects_snapshot
        )
    );
    return true;
}

static bool test_kernel_notify_rejections_are_atomic(void)
{
    micros_endpoint_t stale_destination;

    EXPECT_TRUE(setup_notify_fixture());
    stale_destination =
        endpoints[NOTIFY_PROCESS_DESTINATION]
        + (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS);
    EXPECT_TRUE(expect_kernel_notify_failure_unchanged(
        MICROS_IPC_ERROR_ARGUMENT,
        endpoints[NOTIFY_PROCESS_DESTINATION],
        0
    ));
    EXPECT_TRUE(expect_kernel_notify_failure_unchanged(
        MICROS_IPC_ERROR_ARGUMENT,
        MICROS_ENDPOINT_NONE,
        1
    ));
    EXPECT_TRUE(expect_kernel_notify_failure_unchanged(
        MICROS_IPC_ERROR_ARGUMENT,
        MICROS_ENDPOINT_ANY,
        1
    ));
    EXPECT_TRUE(expect_kernel_notify_failure_unchanged(
        MICROS_IPC_ERROR_STATE,
        endpoints[NOTIFY_PROCESS_RESERVED],
        1
    ));
    EXPECT_TRUE(expect_kernel_notify_failure_unchanged(
        MICROS_IPC_ERROR_DEAD_ENDPOINT,
        stale_destination,
        1
    ));

    EXPECT_TRUE(setup_notify_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60011000)
        )
    );
    EXPECT_TRUE(
        make_single_receiver_current_blocked(
            primary_threads[NOTIFY_PROCESS_DESTINATION]
        )
        && expect_kernel_notify_failure_unchanged(
            MICROS_IPC_ERROR_STATE,
            endpoints[NOTIFY_PROCESS_DESTINATION],
            UINT64_C(0x100)
        )
    );
    return true;
}

static bool test_kernel_notify_validation_and_close_rules(void)
{
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;
    struct micros_thread *receiver;
    const uint64_t event_mask = UINT64_C(0x0000000000000200);

    EXPECT_TRUE(setup_notify_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60012000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_inject_kernel_notification(
            &registry,
            &objects,
            endpoints[NOTIFY_PROCESS_DESTINATION],
            event_mask
        )
    );
    receiver = &objects.threads[
        primary_threads[NOTIFY_PROCESS_DESTINATION].slot
    ];
    registry_snapshot = registry;
    objects_snapshot = objects;
    receiver->ipc_inbound_message.type = UINT32_C(0x2001);
    EXPECT_ENDPOINT_ERROR(
        MICROS_ENDPOINT_ERROR_INVARIANT,
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        )
    );
    registry = registry_snapshot;
    objects = objects_snapshot;
    EXPECT_TRUE(
        hold_runnable_thread(
            primary_threads[NOTIFY_PROCESS_DESTINATION]
        )
    );
    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_STATE,
        micros_ipc_endpoint_close(
            &registry,
            &objects,
            endpoints[NOTIFY_PROCESS_DESTINATION]
        )
    );
    EXPECT_TRUE(state_is_unchanged(
        &registry_snapshot,
        &objects_snapshot
    ));
    EXPECT_TRUE(
        clear_staged_delivery(
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            NULL
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_endpoint_close(
            &registry,
            &objects,
            endpoints[NOTIFY_PROCESS_DESTINATION]
        )
    );

    EXPECT_TRUE(setup_notify_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_inject_kernel_notification(
            &registry,
            &objects,
            endpoints[NOTIFY_PROCESS_DESTINATION],
            event_mask
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_endpoint_close(
            &registry,
            &objects,
            endpoints[NOTIFY_PROCESS_DESTINATION]
        )
    );
    EXPECT_TRUE(
        registry.endpoints[
            processes[NOTIFY_PROCESS_DESTINATION].slot
        ].state == MICROS_ENDPOINT_STATE_FREE
        && registry.endpoints[
            processes[NOTIFY_PROCESS_DESTINATION].slot
        ].pending_kernel_events == 0
    );

    EXPECT_TRUE(setup_notify_fixture());
    registry.endpoints[
        processes[NOTIFY_PROCESS_RESERVED].slot
    ].pending_kernel_events = event_mask;
    EXPECT_ENDPOINT_ERROR(
        MICROS_ENDPOINT_ERROR_INVARIANT,
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        )
    );

    EXPECT_TRUE(setup_notify_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60013000)
        )
    );
    registry.endpoints[
        processes[NOTIFY_PROCESS_DESTINATION].slot
    ].pending_kernel_events = event_mask;
    EXPECT_ENDPOINT_ERROR(
        MICROS_ENDPOINT_ERROR_INVARIANT,
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        )
    );
    return true;
}

static bool test_notify_wakes_first_matching_receiver(void)
{
    struct micros_endpoint_record *destination;
    struct micros_thread *unmatched;
    struct micros_thread *matched;
    struct micros_thread *later;
    const uint64_t event_mask = UINT64_C(0x00000000000000a5);

    EXPECT_TRUE(setup_notify_fixture());
    EXPECT_TRUE(prepare_destination_extra_threads());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            endpoints[NOTIFY_PROCESS_SOURCE_HIGH],
            UINT64_C(0x60001000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            destination_extra_threads[0],
            endpoints[NOTIFY_PROCESS_SOURCE_LOW],
            UINT64_C(0x60002000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            destination_extra_threads[1],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60003000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
            endpoints[NOTIFY_PROCESS_DESTINATION],
            event_mask
        )
    );

    destination = &registry.endpoints[
        processes[NOTIFY_PROCESS_DESTINATION].slot
    ];
    unmatched = &objects.threads[
        primary_threads[NOTIFY_PROCESS_DESTINATION].slot
    ];
    matched = &objects.threads[destination_extra_threads[0].slot];
    later = &objects.threads[destination_extra_threads[1].slot];
    EXPECT_TRUE(
        thread_handles_equal(
            destination->receiver_head,
            primary_threads[NOTIFY_PROCESS_DESTINATION]
        )
        && thread_handles_equal(
            destination->receiver_tail,
            destination_extra_threads[1]
        )
        && thread_handles_equal(
            unmatched->ipc_next,
            destination_extra_threads[1]
        )
        && unmatched->runtime_flags
            == MICROS_THREAD_RTS_IPC_RECEIVE
        && later->runtime_flags == MICROS_THREAD_RTS_IPC_RECEIVE
        && matched->runtime_flags == 0
        && matched->ready_linked
        && matched->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && matched->ipc_receive_source == 0
        && matched->ipc_delivery_pending
        && notification_message_matches(
            &matched->ipc_inbound_message,
            endpoints[NOTIFY_PROCESS_SOURCE_LOW],
            event_mask
        )
        && destination->pending_notification_sources == 0
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool test_call_reply_wait_does_not_consume_notification(void)
{
    struct micros_ipc_message request = message_pattern(
        MICROS_ENDPOINT_ANY,
        UINT32_C(0x4101),
        UINT64_C(0xffffffffffffffff),
        UINT8_C(0x40)
    );
    struct micros_endpoint_record *destination;
    struct micros_thread *caller;
    uint64_t source_bit;
    const uint64_t event_mask = UINT64_C(0x0000000000000040);
    const uint64_t kernel_mask = UINT64_C(0x0000000000000080);

    EXPECT_TRUE(setup_notify_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_SERVER],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60004000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            endpoints[NOTIFY_PROCESS_SERVER],
            &request,
            (uintptr_t)&request
        )
    );
    caller = &objects.threads[
        primary_threads[NOTIFY_PROCESS_DESTINATION].slot
    ];
    EXPECT_TRUE(
        caller->runtime_flags == MICROS_THREAD_RTS_IPC_REPLY
        && caller->ipc_reply_token != 0
        && !caller->ipc_delivery_pending
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
            endpoints[NOTIFY_PROCESS_DESTINATION],
            event_mask
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_inject_kernel_notification(
            &registry,
            &objects,
            endpoints[NOTIFY_PROCESS_DESTINATION],
            kernel_mask
        )
    );
    destination = &registry.endpoints[
        processes[NOTIFY_PROCESS_DESTINATION].slot
    ];
    source_bit = UINT64_C(1)
        << processes[NOTIFY_PROCESS_SOURCE_LOW].slot;
    EXPECT_TRUE(
        caller->runtime_flags == MICROS_THREAD_RTS_IPC_REPLY
        && caller->ipc_reply_token != 0
        && !caller->ipc_delivery_pending
        && destination->pending_notification_sources == source_bit
        && destination->pending_events[
            processes[NOTIFY_PROCESS_SOURCE_LOW].slot
        ] == event_mask
        && destination->pending_kernel_events == kernel_mask
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool test_receive_prefers_lowest_notification_before_sender(void)
{
    struct micros_ipc_message sent = message_pattern(
        MICROS_ENDPOINT_ANY,
        UINT32_C(0x4201),
        UINT64_C(0xffffffffffffffff),
        UINT8_C(0x50)
    );
    struct micros_ipc_message delivered;
    struct micros_ipc_message expected_sent = sent;
    struct micros_endpoint_record *destination;
    struct micros_thread *sender;
    const uint64_t kernel_mask = UINT64_C(0x0000000000000100);
    const uint64_t low_mask = UINT64_C(0x0000000000000001);
    const uint64_t high_mask = UINT64_C(0x8000000000000000);

    EXPECT_TRUE(setup_notify_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_inject_kernel_notification(
            &registry,
            &objects,
            endpoints[NOTIFY_PROCESS_DESTINATION],
            kernel_mask
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_SOURCE_HIGH],
            endpoints[NOTIFY_PROCESS_DESTINATION],
            high_mask
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
            endpoints[NOTIFY_PROCESS_DESTINATION],
            low_mask
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_SENDER],
            endpoints[NOTIFY_PROCESS_DESTINATION],
            &sent
        )
    );
    destination = &registry.endpoints[
        processes[NOTIFY_PROCESS_DESTINATION].slot
    ];
    sender = &objects.threads[
        primary_threads[NOTIFY_PROCESS_SENDER].slot
    ];
    EXPECT_TRUE(
        sender->runtime_flags == MICROS_THREAD_RTS_IPC_SEND
        && sender->ipc_queue_kind == MICROS_IPC_QUEUE_SENDER
        && thread_handles_equal(
            destination->sender_head,
            primary_threads[NOTIFY_PROCESS_SENDER]
        )
    );

    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60005000)
        )
    );
    EXPECT_TRUE(
        consume_staged_and_hold(
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            &delivered
        )
        && notification_message_matches(
            &delivered,
            MICROS_ENDPOINT_NONE,
            kernel_mask
        )
        && destination->pending_kernel_events == 0
        && sender->runtime_flags == MICROS_THREAD_RTS_IPC_SEND
        && destination->pending_events[
            processes[NOTIFY_PROCESS_SOURCE_LOW].slot
        ] == low_mask
        && destination->pending_events[
            processes[NOTIFY_PROCESS_SOURCE_HIGH].slot
        ] == high_mask
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60006000)
        )
    );
    EXPECT_TRUE(
        consume_staged_and_hold(
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            &delivered
        )
        && notification_message_matches(
            &delivered,
            endpoints[NOTIFY_PROCESS_SOURCE_LOW],
            low_mask
        )
        && sender->runtime_flags == MICROS_THREAD_RTS_IPC_SEND
        && destination->pending_events[
            processes[NOTIFY_PROCESS_SOURCE_LOW].slot
        ] == 0
        && destination->pending_events[
            processes[NOTIFY_PROCESS_SOURCE_HIGH].slot
        ] == high_mask
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60007000)
        )
    );
    EXPECT_TRUE(
        consume_staged_and_hold(
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            &delivered
        )
        && notification_message_matches(
            &delivered,
            endpoints[NOTIFY_PROCESS_SOURCE_HIGH],
            high_mask
        )
        && sender->runtime_flags == MICROS_THREAD_RTS_IPC_SEND
        && destination->pending_notification_sources == 0
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60008000)
        )
    );
    expected_sent.source = endpoints[NOTIFY_PROCESS_SENDER];
    expected_sent.reply_token = 0;
    EXPECT_TRUE(
        clear_staged_delivery(
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            &delivered
        )
        && memcmp(&delivered, &expected_sent, sizeof(delivered)) == 0
        && sender->runtime_flags == 0
        && sender->ready_linked
        && sender->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && thread_handle_is_zero(destination->sender_head)
        && thread_handle_is_zero(destination->sender_tail)
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool test_reply_receive_prefers_pending_notification(void)
{
    struct micros_ipc_message request = message_pattern(
        0,
        UINT32_C(0x4301),
        0,
        UINT8_C(0x60)
    );
    struct micros_ipc_message reply = message_pattern(
        MICROS_ENDPOINT_ANY,
        UINT32_C(0x4302),
        UINT64_C(0xffffffffffffffff),
        UINT8_C(0x70)
    );
    struct micros_ipc_message ordinary = message_pattern(
        MICROS_ENDPOINT_ANY,
        UINT32_C(0x4303),
        UINT64_C(0xffffffffffffffff),
        UINT8_C(0x80)
    );
    struct micros_ipc_message delivered_request;
    struct micros_ipc_message expected_reply = reply;
    struct micros_endpoint_record *server_endpoint;
    struct micros_thread *caller;
    struct micros_thread *server;
    struct micros_thread *sender;
    uint64_t reply_token;
    const uint64_t event_mask = UINT64_C(0x00000000a5a55a5a);
    const uint64_t kernel_mask = UINT64_C(0x000000005a5aa5a5);

    EXPECT_TRUE(setup_notify_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_SERVER],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x6000a000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            endpoints[NOTIFY_PROCESS_SERVER],
            &request,
            (uintptr_t)&request
        )
    );
    server = &objects.threads[
        primary_threads[NOTIFY_PROCESS_SERVER].slot
    ];
    reply_token = server->ipc_inbound_message.reply_token;
    EXPECT_TRUE(
        reply_token != 0
        && consume_staged_and_hold(
            primary_threads[NOTIFY_PROCESS_SERVER],
            &delivered_request
        )
        && delivered_request.source
            == endpoints[NOTIFY_PROCESS_DESTINATION]
        && delivered_request.reply_token == reply_token
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
            endpoints[NOTIFY_PROCESS_SERVER],
            event_mask
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_inject_kernel_notification(
            &registry,
            &objects,
            endpoints[NOTIFY_PROCESS_SERVER],
            kernel_mask
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_SENDER],
            endpoints[NOTIFY_PROCESS_SERVER],
            &ordinary
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_reply_receive(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_SERVER],
            reply_token,
            &reply,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x6000b000)
        )
    );

    server_endpoint = &registry.endpoints[
        processes[NOTIFY_PROCESS_SERVER].slot
    ];
    caller = &objects.threads[
        primary_threads[NOTIFY_PROCESS_DESTINATION].slot
    ];
    sender = &objects.threads[
        primary_threads[NOTIFY_PROCESS_SENDER].slot
    ];
    expected_reply.source = endpoints[NOTIFY_PROCESS_SERVER];
    expected_reply.reply_token = 0;
    EXPECT_TRUE(
        caller->runtime_flags == 0
        && caller->ready_linked
        && caller->ipc_reply_token == 0
        && caller->ipc_reply_callee == 0
        && caller->ipc_delivery_pending
        && memcmp(
            &caller->ipc_inbound_message,
            &expected_reply,
            sizeof(expected_reply)
        ) == 0
        && server->runtime_flags == 0
        && server->ready_linked
        && server->ipc_delivery_pending
        && notification_message_matches(
            &server->ipc_inbound_message,
            MICROS_ENDPOINT_NONE,
            kernel_mask
        )
        && server_endpoint->pending_kernel_events == 0
        && server_endpoint->pending_notification_sources
            == (
                UINT64_C(1)
                << processes[NOTIFY_PROCESS_SOURCE_LOW].slot
            )
        && server_endpoint->pending_events[
            processes[NOTIFY_PROCESS_SOURCE_LOW].slot
        ] == event_mask
        && sender->runtime_flags == MICROS_THREAD_RTS_IPC_SEND
        && sender->ipc_queue_kind == MICROS_IPC_QUEUE_SENDER
        && thread_handles_equal(
            server_endpoint->sender_head,
            primary_threads[NOTIFY_PROCESS_SENDER]
        )
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool test_reply_receive_pending_failure_is_atomic(void)
{
    struct micros_ipc_message request = message_pattern(
        0,
        UINT32_C(0x4401),
        0,
        UINT8_C(0x90)
    );
    struct micros_ipc_message reply = message_pattern(
        0,
        UINT32_C(0x4402),
        0,
        UINT8_C(0xa0)
    );
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;
    struct micros_thread *server;
    uint64_t reply_token;
    const uint64_t event_mask = UINT64_C(0x0000000000000400);

    EXPECT_TRUE(setup_notify_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_SERVER],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x6000c000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_call(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            endpoints[NOTIFY_PROCESS_SERVER],
            &request,
            (uintptr_t)&request
        )
    );
    server = &objects.threads[
        primary_threads[NOTIFY_PROCESS_SERVER].slot
    ];
    reply_token = server->ipc_inbound_message.reply_token;
    EXPECT_TRUE(
        reply_token != 0
        && consume_staged_and_hold(
            primary_threads[NOTIFY_PROCESS_SERVER],
            NULL
        )
        && make_reply_wait_current(
            primary_threads[NOTIFY_PROCESS_DESTINATION]
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
            endpoints[NOTIFY_PROCESS_SERVER],
            event_mask
        )
    );
    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_STATE,
        micros_ipc_reply_receive(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_SERVER],
            reply_token,
            &reply,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x6000d000)
        )
    );
    EXPECT_TRUE(
        state_is_unchanged(&registry_snapshot, &objects_snapshot)
        && registry.endpoints[
            processes[NOTIFY_PROCESS_SERVER].slot
        ].pending_events[
            processes[NOTIFY_PROCESS_SOURCE_LOW].slot
        ] == event_mask
        && objects.threads[
            primary_threads[NOTIFY_PROCESS_DESTINATION].slot
        ].ipc_reply_token == reply_token
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool test_notification_validators_and_close_preflight(void)
{
    struct micros_endpoint_registry held_registry;
    struct micros_kernel_objects held_objects;
    struct micros_endpoint_registry pending_registry;
    struct micros_kernel_objects pending_objects;
    struct micros_endpoint_registry staged_registry;
    struct micros_kernel_objects staged_objects;
    struct micros_thread *receiver;
    const uint64_t event_mask = UINT64_C(0x0102030405060708);

    EXPECT_TRUE(setup_notify_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_notify(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
            endpoints[NOTIFY_PROCESS_DESTINATION],
            event_mask
        )
    );
    EXPECT_TRUE(
        hold_runnable_thread(
            primary_threads[NOTIFY_PROCESS_SOURCE_LOW]
        )
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    pending_registry = registry;
    pending_objects = objects;
    EXPECT_ENDPOINT_ERROR(
        MICROS_ENDPOINT_ERROR_STATE,
        micros_endpoint_close(
            &registry,
            &objects,
            endpoints[NOTIFY_PROCESS_SOURCE_LOW]
        )
    );
    EXPECT_TRUE(state_is_unchanged(
        &pending_registry,
        &pending_objects
    ));
    EXPECT_ENDPOINT_ERROR(
        MICROS_ENDPOINT_ERROR_STATE,
        micros_endpoint_close(
            &registry,
            &objects,
            endpoints[NOTIFY_PROCESS_DESTINATION]
        )
    );
    EXPECT_TRUE(state_is_unchanged(
        &pending_registry,
        &pending_objects
    ));

#define EXPECT_PENDING_CORRUPTION(statement) \
    do { \
        registry = pending_registry; \
        objects = pending_objects; \
        statement; \
        EXPECT_ENDPOINT_ERROR( \
            MICROS_ENDPOINT_ERROR_INVARIANT, \
            micros_endpoint_registry_validate_objects( \
                &registry, \
                &objects \
            ) \
        ); \
    } while (false)

    EXPECT_PENDING_CORRUPTION(
        registry.endpoints[
            processes[NOTIFY_PROCESS_DESTINATION].slot
        ].pending_notification_sources = 0
    );
    EXPECT_PENDING_CORRUPTION(
        registry.endpoints[
            processes[NOTIFY_PROCESS_DESTINATION].slot
        ].pending_events[
            processes[NOTIFY_PROCESS_SOURCE_LOW].slot
        ] = 0
    );
    EXPECT_PENDING_CORRUPTION(
        registry.endpoints[
            processes[NOTIFY_PROCESS_SOURCE_LOW].slot
        ].state = MICROS_ENDPOINT_STATE_RESERVED
    );
    EXPECT_PENDING_CORRUPTION(
        registry.endpoints[
            processes[NOTIFY_PROCESS_DESTINATION].slot
        ].pending_notification_sources |= UINT64_C(1) << 63;
        registry.endpoints[
            processes[NOTIFY_PROCESS_DESTINATION].slot
        ].pending_events[63] = 1
    );

#undef EXPECT_PENDING_CORRUPTION

    registry = pending_registry;
    objects = pending_objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60008000)
        )
    );
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    staged_registry = registry;
    staged_objects = objects;
    receiver = &objects.threads[
        primary_threads[NOTIFY_PROCESS_DESTINATION].slot
    ];

#define EXPECT_STAGED_CORRUPTION(statement) \
    do { \
        registry = staged_registry; \
        objects = staged_objects; \
        receiver = &objects.threads[ \
            primary_threads[NOTIFY_PROCESS_DESTINATION].slot \
        ]; \
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
        receiver->ipc_inbound_message.type =
            MICROS_IPC_TYPE_KERNEL_MASK | UINT32_C(2)
    );
    EXPECT_STAGED_CORRUPTION(
        receiver->ipc_inbound_message.reply_token = 1
    );
    EXPECT_STAGED_CORRUPTION(
        memset(receiver->ipc_inbound_message.payload, 0, 8)
    );
    EXPECT_STAGED_CORRUPTION(
        receiver->ipc_inbound_message.payload[8] = 1
    );
    EXPECT_STAGED_CORRUPTION(
        receiver->ipc_inbound_message.source +=
            UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS
    );

#undef EXPECT_STAGED_CORRUPTION

    registry = staged_registry;
    objects = staged_objects;
    EXPECT_TRUE(
        hold_runnable_thread(
            primary_threads[NOTIFY_PROCESS_DESTINATION]
        )
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    held_registry = registry;
    held_objects = objects;
    EXPECT_ENDPOINT_ERROR(
        MICROS_ENDPOINT_ERROR_STATE,
        micros_endpoint_close(
            &registry,
            &objects,
            endpoints[NOTIFY_PROCESS_DESTINATION]
        )
    );
    EXPECT_TRUE(state_is_unchanged(
        &held_registry,
        &held_objects
    ));

    receiver = &objects.threads[
        primary_threads[NOTIFY_PROCESS_DESTINATION].slot
    ];
    EXPECT_TRUE(
        clear_staged_delivery(
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            NULL
        )
        && receiver->runtime_flags == MICROS_THREAD_RTS_INACTIVE
        && micros_thread_ipc_state_is_clear(receiver)
    );
    EXPECT_ENDPOINT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_close(
            &registry,
            &objects,
            endpoints[NOTIFY_PROCESS_DESTINATION]
        )
    );
    return true;
}

static bool test_notify_rejections_preserve_state(void)
{
    micros_endpoint_t stale_destination;

    EXPECT_TRUE(setup_notify_fixture());
    stale_destination =
        endpoints[NOTIFY_PROCESS_DESTINATION]
        + (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS);
    EXPECT_TRUE(expect_notify_failure_unchanged(
        MICROS_IPC_ERROR_ARGUMENT,
        primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
        endpoints[NOTIFY_PROCESS_DESTINATION],
        0
    ));
    EXPECT_TRUE(expect_notify_failure_unchanged(
        MICROS_IPC_ERROR_UNAUTHORIZED,
        primary_threads[NOTIFY_PROCESS_NO_NOTIFY],
        endpoints[NOTIFY_PROCESS_DESTINATION],
        1
    ));
    EXPECT_TRUE(expect_notify_failure_unchanged(
        MICROS_IPC_ERROR_UNAUTHORIZED,
        primary_threads[NOTIFY_PROCESS_ISOLATED_NOTIFY],
        endpoints[NOTIFY_PROCESS_DESTINATION],
        1
    ));
    EXPECT_TRUE(expect_notify_failure_unchanged(
        MICROS_IPC_ERROR_STATE,
        primary_threads[NOTIFY_PROCESS_RESERVED],
        endpoints[NOTIFY_PROCESS_DESTINATION],
        1
    ));
    EXPECT_TRUE(expect_notify_failure_unchanged(
        MICROS_IPC_ERROR_STATE,
        primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
        endpoints[NOTIFY_PROCESS_RESERVED],
        1
    ));
    EXPECT_TRUE(expect_notify_failure_unchanged(
        MICROS_IPC_ERROR_DEAD_ENDPOINT,
        primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
        stale_destination,
        1
    ));

    EXPECT_TRUE(
        setup_notify_fixture()
        && make_current_held(
            primary_threads[NOTIFY_PROCESS_SOURCE_LOW]
        )
        && expect_notify_failure_unchanged(
            MICROS_IPC_ERROR_STATE,
            primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
            endpoints[NOTIFY_PROCESS_DESTINATION],
            1
        )
    );

    EXPECT_TRUE(setup_notify_fixture());
    objects.threads[
        primary_threads[NOTIFY_PROCESS_SOURCE_LOW].slot
    ].owner.slot = MICROS_PROCESS_CAPACITY;
    EXPECT_TRUE(expect_notify_failure_unchanged(
        MICROS_IPC_ERROR_INVARIANT,
        primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
        endpoints[NOTIFY_PROCESS_DESTINATION],
        1
    ));
    return true;
}

static bool test_notify_scheduler_failure_is_atomic(void)
{
    EXPECT_TRUE(setup_notify_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60009000)
        )
    );
    EXPECT_TRUE(
        make_single_receiver_current_blocked(
            primary_threads[NOTIFY_PROCESS_DESTINATION]
        )
        && expect_notify_failure_unchanged(
            MICROS_IPC_ERROR_STATE,
            primary_threads[NOTIFY_PROCESS_SOURCE_LOW],
            endpoints[NOTIFY_PROCESS_DESTINATION],
            UINT64_C(0x80)
        )
    );
    return true;
}

static uint32_t notify_model_next_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void notify_model_record(
    size_t step,
    enum notify_model_operation operation,
    size_t source,
    uint64_t event_mask
)
{
    size_t index =
        notify_model_trace_count % NOTIFY_MODEL_TRACE_COUNT;

    notify_model_trace[index].step = step;
    notify_model_trace[index].operation = operation;
    notify_model_trace[index].source = source;
    notify_model_trace[index].event_mask = event_mask;
    ++notify_model_trace_count;
}

static bool notify_model_fail(
    size_t step,
    enum notify_model_operation operation,
    const char *message
)
{
    size_t available =
        notify_model_trace_count < NOTIFY_MODEL_TRACE_COUNT
            ? notify_model_trace_count
            : NOTIFY_MODEL_TRACE_COUNT;
    size_t first = notify_model_trace_count - available;
    size_t offset;

    fprintf(
        stderr,
        "notify model seed=0x30c0a1e5 step=%zu operation=%s: %s\n",
        step,
        notify_model_operation_names[operation],
        message
    );
    for (offset = 0; offset < available; ++offset) {
        const struct notify_model_trace_entry *entry =
            &notify_model_trace[
                (first + offset) % NOTIFY_MODEL_TRACE_COUNT
            ];

        fprintf(
            stderr,
            "  trace step=%zu operation=%s source=%zu "
            "event=0x%016llx\n",
            entry->step,
            notify_model_operation_names[entry->operation],
            entry->source,
            (unsigned long long)entry->event_mask
        );
    }
    return false;
}

static bool notify_model_pending_matches(
    const uint64_t expected[2],
    uint64_t expected_kernel
)
{
    const struct micros_endpoint_record *destination =
        &registry.endpoints[
            processes[NOTIFY_PROCESS_DESTINATION].slot
        ];
    uint64_t expected_bitmap = 0;
    size_t index;

    for (index = 0; index < 2; ++index) {
        size_t process_index =
            index == 0
                ? NOTIFY_PROCESS_SOURCE_LOW
                : NOTIFY_PROCESS_SOURCE_HIGH;
        size_t slot = processes[process_index].slot;

        if (expected[index] != 0) {
            expected_bitmap |= UINT64_C(1) << slot;
        }
        if (destination->pending_events[slot] != expected[index]) {
            return false;
        }
    }
    if (
        destination->pending_notification_sources != expected_bitmap
        || destination->pending_kernel_events != expected_kernel
    ) {
        return false;
    }
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        if (
            index != processes[NOTIFY_PROCESS_SOURCE_LOW].slot
            && index != processes[NOTIFY_PROCESS_SOURCE_HIGH].slot
            && destination->pending_events[index] != 0
        ) {
            return false;
        }
    }
    return true;
}

static bool test_seeded_notify_model(void)
{
    static const enum notify_model_operation scripted_operations[] = {
        NOTIFY_MODEL_NOTIFY,
        NOTIFY_MODEL_NOTIFY,
        NOTIFY_MODEL_KERNEL_NOTIFY,
        NOTIFY_MODEL_KERNEL_NOTIFY,
        NOTIFY_MODEL_RECEIVE_ANY,
        NOTIFY_MODEL_NOTIFY,
        NOTIFY_MODEL_RECEIVE_ANY,
        NOTIFY_MODEL_RECEIVE_SPECIFIC,
        NOTIFY_MODEL_ZERO_REJECTED,
        NOTIFY_MODEL_OPERATION_REJECTED,
        NOTIFY_MODEL_TARGET_REJECTED,
    };
    static const size_t scripted_sources[] = {
        0,
        0,
        0,
        0,
        0,
        1,
        0,
        1,
        0,
        0,
        0,
    };
    const uint32_t seed = UINT32_C(0x30c0a1e5);
    uint64_t expected[2] = {0, 0};
    uint64_t expected_kernel = 0;
    struct notify_model_coverage coverage;
    uint32_t random_state = seed;
    size_t step;

    EXPECT_TRUE(setup_notify_fixture());
    memset(&coverage, 0, sizeof(coverage));
    memset(notify_model_trace, 0, sizeof(notify_model_trace));
    notify_model_trace_count = 0;
    for (step = 0; step < NOTIFY_MODEL_STEPS; ++step) {
        uint32_t random = notify_model_next_random(&random_state);
        enum notify_model_operation operation =
            (enum notify_model_operation)(
                (random >> 24) % NOTIFY_MODEL_OPERATION_COUNT
            );
        size_t source = (random >> 16) & 1U;
        size_t process_index =
            source == 0
                ? NOTIFY_PROCESS_SOURCE_LOW
                : NOTIFY_PROCESS_SOURCE_HIGH;
        uint64_t event_mask =
            (
                (uint64_t)random << 32
            )
            | notify_model_next_random(&random_state);

        if (event_mask == 0) {
            event_mask = 1;
        }
        if (
            step
            < sizeof(scripted_operations)
                / sizeof(scripted_operations[0])
        ) {
            operation = scripted_operations[step];
            source = scripted_sources[step];
            process_index =
                source == 0
                    ? NOTIFY_PROCESS_SOURCE_LOW
                    : NOTIFY_PROCESS_SOURCE_HIGH;
            event_mask = UINT64_C(1) << step;
        }
        if (
            operation == NOTIFY_MODEL_RECEIVE_ANY
            && expected_kernel == 0
            && expected[0] == 0
            && expected[1] == 0
        ) {
            operation = NOTIFY_MODEL_NOTIFY;
        }
        if (
            operation == NOTIFY_MODEL_RECEIVE_SPECIFIC
            && expected[source] == 0
        ) {
            operation = NOTIFY_MODEL_NOTIFY;
        }
        notify_model_record(step, operation, source, event_mask);

        switch (operation) {
        case NOTIFY_MODEL_NOTIFY: {
            bool was_pending = expected[source] != 0;

            if (
                micros_ipc_notify(
                    &registry,
                    &objects,
                    primary_threads[process_index],
                    endpoints[NOTIFY_PROCESS_DESTINATION],
                    event_mask
                ) != MICROS_IPC_OK
            ) {
                return notify_model_fail(
                    step,
                    operation,
                    "authorized notify failed"
                );
            }
            expected[source] |= event_mask;
            ++coverage.notified;
            if (was_pending) {
                ++coverage.coalesced;
            }
            if (!hold_runnable_thread(primary_threads[process_index])) {
                return notify_model_fail(
                    step,
                    operation,
                    "notifier did not remain nonblocking"
                );
            }
            break;
        }
        case NOTIFY_MODEL_KERNEL_NOTIFY: {
            bool was_pending = expected_kernel != 0;

            if (
                micros_ipc_inject_kernel_notification(
                    &registry,
                    &objects,
                    endpoints[NOTIFY_PROCESS_DESTINATION],
                    event_mask
                ) != MICROS_IPC_OK
            ) {
                return notify_model_fail(
                    step,
                    operation,
                    "kernel notify failed"
                );
            }
            expected_kernel |= event_mask;
            ++coverage.kernel_notified;
            if (was_pending) {
                ++coverage.kernel_coalesced;
            }
            break;
        }
        case NOTIFY_MODEL_RECEIVE_ANY:
        case NOTIFY_MODEL_RECEIVE_SPECIFIC: {
            struct micros_ipc_message delivered;
            size_t selected = source;
            micros_endpoint_t receive_source =
                endpoints[process_index];
            micros_endpoint_t expected_source;
            uint64_t expected_mask;

            if (operation == NOTIFY_MODEL_RECEIVE_ANY) {
                receive_source = MICROS_ENDPOINT_ANY;
                if (expected_kernel != 0) {
                    expected_source = MICROS_ENDPOINT_NONE;
                    expected_mask = expected_kernel;
                } else {
                    selected = expected[0] != 0 ? 0 : 1;
                    expected_source = endpoints[
                        selected == 0
                            ? NOTIFY_PROCESS_SOURCE_LOW
                            : NOTIFY_PROCESS_SOURCE_HIGH
                    ];
                    expected_mask = expected[selected];
                }
            } else {
                expected_source = endpoints[process_index];
                expected_mask = expected[selected];
            }
            if (
                micros_ipc_receive(
                    &registry,
                    &objects,
                    primary_threads[NOTIFY_PROCESS_DESTINATION],
                    receive_source,
                    UINT64_C(0x61000000)
                        + step * UINT64_C(0x100)
                ) != MICROS_IPC_OK
                || !consume_staged_and_hold(
                    primary_threads[NOTIFY_PROCESS_DESTINATION],
                    &delivered
                )
                || !notification_message_matches(
                    &delivered,
                    expected_source,
                    expected_mask
                )
            ) {
                return notify_model_fail(
                    step,
                    operation,
                    "receive selection diverged"
                );
            }
            if (operation == NOTIFY_MODEL_RECEIVE_ANY) {
                if (expected_source == MICROS_ENDPOINT_NONE) {
                    expected_kernel = 0;
                    ++coverage.kernel_received_any;
                } else {
                    expected[selected] = 0;
                }
                ++coverage.received_any;
            } else {
                expected[selected] = 0;
                ++coverage.received_specific;
            }
            break;
        }
        case NOTIFY_MODEL_ZERO_REJECTED: {
            struct micros_endpoint_registry registry_snapshot = registry;
            struct micros_kernel_objects objects_snapshot = objects;

            if (
                micros_ipc_notify(
                    &registry,
                    &objects,
                    primary_threads[process_index],
                    endpoints[NOTIFY_PROCESS_DESTINATION],
                    0
                ) != MICROS_IPC_ERROR_ARGUMENT
                || !state_is_unchanged(
                    &registry_snapshot,
                    &objects_snapshot
                )
            ) {
                return notify_model_fail(
                    step,
                    operation,
                    "zero event mask changed state"
                );
            }
            ++coverage.zero_rejected;
            break;
        }
        case NOTIFY_MODEL_OPERATION_REJECTED: {
            struct micros_endpoint_registry registry_snapshot = registry;
            struct micros_kernel_objects objects_snapshot = objects;

            if (
                micros_ipc_notify(
                    &registry,
                    &objects,
                    primary_threads[NOTIFY_PROCESS_NO_NOTIFY],
                    endpoints[NOTIFY_PROCESS_DESTINATION],
                    event_mask
                ) != MICROS_IPC_ERROR_UNAUTHORIZED
                || !state_is_unchanged(
                    &registry_snapshot,
                    &objects_snapshot
                )
            ) {
                return notify_model_fail(
                    step,
                    operation,
                    "operation denial changed state"
                );
            }
            ++coverage.operation_rejected;
            break;
        }
        case NOTIFY_MODEL_TARGET_REJECTED: {
            struct micros_endpoint_registry registry_snapshot = registry;
            struct micros_kernel_objects objects_snapshot = objects;

            if (
                micros_ipc_notify(
                    &registry,
                    &objects,
                    primary_threads[
                        NOTIFY_PROCESS_ISOLATED_NOTIFY
                    ],
                    endpoints[NOTIFY_PROCESS_DESTINATION],
                    event_mask
                ) != MICROS_IPC_ERROR_UNAUTHORIZED
                || !state_is_unchanged(
                    &registry_snapshot,
                    &objects_snapshot
                )
            ) {
                return notify_model_fail(
                    step,
                    operation,
                    "target denial changed state"
                );
            }
            ++coverage.target_rejected;
            break;
        }
        default:
            return notify_model_fail(
                step,
                operation,
                "unknown operation"
            );
        }
        if (
            !notify_model_pending_matches(
                expected,
                expected_kernel
            )
            || micros_endpoint_registry_validate_objects(
                &registry,
                &objects
            ) != MICROS_ENDPOINT_OK
        ) {
            return notify_model_fail(
                step,
                operation,
                "state or validator diverged"
            );
        }
    }
    EXPECT_TRUE(
        coverage.notified != 0
        && coverage.coalesced != 0
        && coverage.kernel_notified != 0
        && coverage.kernel_coalesced != 0
        && coverage.kernel_received_any != 0
        && coverage.received_any != 0
        && coverage.received_specific != 0
        && coverage.zero_rejected != 0
        && coverage.operation_rejected != 0
        && coverage.target_rejected != 0
    );
    return true;
}

static bool test_staged_kernel_notification_is_detected(void)
{
    const struct micros_thread *destination;

    EXPECT_TRUE(
        setup_notify_fixture()
        && micros_thread_resolve(
            &objects,
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            &destination
        ) == MICROS_KERNEL_OBJECT_OK
        && !micros_ipc_thread_has_staged_kernel_notification(
            destination
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x6100a000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_inject_kernel_notification(
            &registry,
            &objects,
            endpoints[NOTIFY_PROCESS_DESTINATION],
            UINT64_C(0x0000000000000400)
        )
    );
    EXPECT_TRUE(
        micros_thread_resolve(
            &objects,
            primary_threads[NOTIFY_PROCESS_DESTINATION],
            &destination
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_ipc_thread_has_staged_kernel_notification(
            destination
        )
    );
    return true;
}

bool micros_ipc_notify_test_run(void)
{
    static const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {
            "notify coalesces and never blocks",
            test_notify_coalesces_and_never_blocks,
        },
        {
            "kernel notify coalesces and any receives",
            test_kernel_notify_coalesces_and_any_receives,
        },
        {
            "kernel notify wakes only any receiver",
            test_kernel_notify_wakes_only_any_receiver,
        },
        {
            "kernel notify prepare commit is retained",
            test_kernel_notify_prepare_commit_is_retained,
        },
        {
            "staged kernel notification is detected",
            test_staged_kernel_notification_is_detected,
        },
        {
            "kernel notify rejections are atomic",
            test_kernel_notify_rejections_are_atomic,
        },
        {
            "kernel notify validation and close rules",
            test_kernel_notify_validation_and_close_rules,
        },
        {
            "notify wakes first matching receiver",
            test_notify_wakes_first_matching_receiver,
        },
        {
            "call reply wait excludes notification",
            test_call_reply_wait_does_not_consume_notification,
        },
        {
            "receive prefers lowest notification before sender",
            test_receive_prefers_lowest_notification_before_sender,
        },
        {
            "reply receive prefers pending notification",
            test_reply_receive_prefers_pending_notification,
        },
        {
            "reply receive pending failure is atomic",
            test_reply_receive_pending_failure_is_atomic,
        },
        {
            "notification validators and close preflight",
            test_notification_validators_and_close_preflight,
        },
        {
            "notify rejections preserve state",
            test_notify_rejections_preserve_state,
        },
        {
            "notify scheduler failure is atomic",
            test_notify_scheduler_failure_is_atomic,
        },
    };
    size_t index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run()) {
            fprintf(
                stderr,
                "notify subtest %zu failed - %s\n",
                index + 1,
                tests[index].name
            );
            return false;
        }
    }
    return true;
}

bool micros_ipc_notify_model_test_run(void)
{
    return test_seeded_notify_model();
}
