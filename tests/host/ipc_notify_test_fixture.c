#include "ipc_notify_test_internal.h"

static struct ipc_notify_test_context notify_context;

struct ipc_notify_test_context *ipc_notify_test_context_get(void)
{
    return &notify_context;
}

bool ipc_notify_test_thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

bool ipc_notify_test_thread_handle_is_zero(
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

struct micros_ipc_message ipc_notify_test_message_pattern(
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
    struct ipc_notify_test_context *fixture,
    struct micros_process_handle owner,
    uint64_t context_base,
    uintptr_t stack_bottom,
    struct micros_thread_handle *thread
)
{
    struct micros_user_context context =
        context_pattern(context_base);

    return (
        micros_thread_create(&fixture->objects, owner, thread)
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_attach_execution_context(
            &fixture->objects,
            *thread,
            stack_bottom,
            stack_bottom + MICROS_THREAD_KERNEL_STACK_SIZE,
            &context
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_thread_scheduler_admit(
            &fixture->objects,
            fixture->hart,
            *thread,
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            100,
            true
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_thread_scheduler_hold(&fixture->objects, *thread)
            == MICROS_KERNEL_OBJECT_OK
    );
}

static bool prepare_notification_processes(
    struct ipc_notify_test_context *fixture,
    const uint8_t profile_ids[NOTIFY_PROCESS_COUNT]
)
{
    size_t index;

    for (index = 0; index < NOTIFY_PROCESS_COUNT; ++index) {
        if (
            micros_process_create(&fixture->objects, &fixture->processes[index])
                != MICROS_KERNEL_OBJECT_OK
            || micros_endpoint_reserve(
                &fixture->registry,
                &fixture->objects,
                fixture->processes[index],
                &fixture->endpoints[index]
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_install_profile(
                &fixture->registry,
                &fixture->objects,
                fixture->processes[index],
                profile_ids[index]
            ) != MICROS_ENDPOINT_OK
            || (
                index != NOTIFY_PROCESS_RESERVED
                && micros_endpoint_activate(
                    &fixture->registry,
                    &fixture->objects,
                    fixture->endpoints[index]
                ) != MICROS_ENDPOINT_OK
            )
            || !prepare_thread(
                fixture,
                fixture->processes[index],
                UINT64_C(0x1000) + index * UINT64_C(0x100),
                UINT64_C(0x10000000)
                    + index * UINT64_C(0x00010000),
                &fixture->primary_threads[index]
            )
        ) {
            return false;
        }
    }
    return true;
}

bool ipc_notify_test_setup_notify_fixture(struct ipc_notify_test_context *fixture)
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
    memset(&fixture->registry, 0, sizeof(fixture->registry));
    memset(&fixture->objects, 0, sizeof(fixture->objects));
    memset(fixture->processes, 0, sizeof(fixture->processes));
    memset(fixture->primary_threads, 0, sizeof(fixture->primary_threads));
    memset(fixture->destination_extra_threads, 0, sizeof(fixture->destination_extra_threads));
    memset(fixture->endpoints, 0, sizeof(fixture->endpoints));
    if (
        micros_endpoint_registry_initialize(
            &fixture->registry,
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) != MICROS_ENDPOINT_OK
        || micros_kernel_objects_initialize(&fixture->objects, 3, 1)
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_register(&fixture->objects, 0, &fixture->hart)
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_install_trap_stacks(
            &fixture->objects,
            fixture->hart,
            UINT64_C(0x01000000),
            UINT64_C(0x01004000),
            UINT64_C(0x02000000),
            UINT64_C(0x02001000)
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    return prepare_notification_processes(fixture, profile_ids);
}

bool ipc_notify_test_prepare_destination_extra_threads(struct ipc_notify_test_context *fixture)
{
    return (
        prepare_thread(
            fixture,
            fixture->processes[NOTIFY_PROCESS_DESTINATION],
            UINT64_C(0x2000),
            UINT64_C(0x11000000),
            &fixture->destination_extra_threads[0]
        )
        && prepare_thread(
            fixture,
            fixture->processes[NOTIFY_PROCESS_DESTINATION],
            UINT64_C(0x2100),
            UINT64_C(0x11008000),
            &fixture->destination_extra_threads[1]
        )
    );
}

bool ipc_notify_test_state_is_unchanged(
    struct ipc_notify_test_context *fixture,
    const struct micros_endpoint_registry *registry_snapshot,
    const struct micros_kernel_objects *objects_snapshot
)
{
    return (
        memcmp(
            &fixture->registry,
            registry_snapshot,
            sizeof(fixture->registry)
        ) == 0
        && memcmp(
            &fixture->objects,
            objects_snapshot,
            sizeof(fixture->objects)
        ) == 0
    );
}

bool ipc_notify_test_expect_notify_failure_unchanged(
    struct ipc_notify_test_context *fixture,
    enum micros_ipc_error expected,
    struct micros_thread_handle notifier,
    micros_endpoint_t destination,
    uint64_t event_mask
)
{
    struct micros_endpoint_registry registry_snapshot = fixture->registry;
    struct micros_kernel_objects objects_snapshot = fixture->objects;

    EXPECT_IPC_ERROR(
        expected,
        micros_ipc_notify(
            &fixture->registry,
            &fixture->objects,
            notifier,
            destination,
            event_mask
        )
    );
    EXPECT_TRUE(ipc_notify_test_state_is_unchanged(
        fixture,
        &registry_snapshot,
        &objects_snapshot
    ));
    return true;
}

bool ipc_notify_test_expect_kernel_notify_failure_unchanged(
    struct ipc_notify_test_context *fixture,
    enum micros_ipc_error expected,
    micros_endpoint_t destination,
    uint64_t event_mask
)
{
    struct micros_endpoint_registry registry_snapshot = fixture->registry;
    struct micros_kernel_objects objects_snapshot = fixture->objects;

    EXPECT_IPC_ERROR(
        expected,
        micros_ipc_inject_kernel_notification(
            &fixture->registry,
            &fixture->objects,
            destination,
            event_mask
        )
    );
    EXPECT_TRUE(ipc_notify_test_state_is_unchanged(
        fixture,
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

bool ipc_notify_test_notification_message_matches(
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

bool ipc_notify_test_clear_staged_delivery(
    struct ipc_notify_test_context *fixture,
    struct micros_thread_handle receiver_handle,
    struct micros_ipc_message *delivered
)
{
    struct micros_thread *receiver =
        &fixture->objects.threads[receiver_handle.slot];

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

bool ipc_notify_test_hold_runnable_thread(struct ipc_notify_test_context *fixture, struct micros_thread_handle thread_handle)
{
    const struct micros_thread *thread =
        &fixture->objects.threads[thread_handle.slot];

    return (
        thread->runtime_flags == 0
        && thread->ready_linked
        && micros_thread_scheduler_hold(&fixture->objects, thread_handle)
            == MICROS_KERNEL_OBJECT_OK
    );
}

bool ipc_notify_test_consume_staged_and_hold(
    struct ipc_notify_test_context *fixture,
    struct micros_thread_handle receiver_handle,
    struct micros_ipc_message *delivered
)
{
    return (
        ipc_notify_test_clear_staged_delivery(fixture, receiver_handle, delivered)
        && ipc_notify_test_hold_runnable_thread(fixture, receiver_handle)
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
}

bool ipc_notify_test_make_current_held(struct ipc_notify_test_context *fixture, struct micros_thread_handle thread_handle)
{
    struct micros_scheduler_return_plan plan;

    return (
        micros_scheduler_accounting_initialize(&fixture->objects, fixture->hart, 100)
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_runtime_flags_unset(
            &fixture->objects,
            thread_handle,
            MICROS_THREAD_RTS_INACTIVE
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_hart_plan_user_return(&fixture->objects, fixture->hart, &plan)
            == MICROS_KERNEL_OBJECT_OK
        && micros_hart_commit_user_return(&fixture->objects, &plan)
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_scheduler_hold(&fixture->objects, thread_handle)
            == MICROS_KERNEL_OBJECT_OK
    );
}

bool ipc_notify_test_make_reply_wait_current(
    struct ipc_notify_test_context *fixture,
    struct micros_thread_handle caller_handle
)
{
    struct micros_thread *caller =
        &fixture->objects.threads[caller_handle.slot];
    uintptr_t reply_buffer = caller->ipc_receive_buffer;
    uint64_t reply_token = caller->ipc_reply_token;
    micros_endpoint_t reply_callee = caller->ipc_reply_callee;

    caller->runtime_flags = MICROS_THREAD_RTS_INACTIVE;
    caller->ipc_receive_buffer = 0;
    caller->ipc_reply_token = 0;
    caller->ipc_reply_callee = 0;
    if (
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) != MICROS_ENDPOINT_OK
        || !ipc_notify_test_make_current_held(fixture, caller_handle)
    ) {
        return false;
    }
    caller->runtime_flags = MICROS_THREAD_RTS_IPC_REPLY;
    caller->ipc_receive_buffer = reply_buffer;
    caller->ipc_reply_token = reply_token;
    caller->ipc_reply_callee = reply_callee;
    return micros_endpoint_registry_validate_objects(
        &fixture->registry,
        &fixture->objects
    ) == MICROS_ENDPOINT_OK;
}

bool ipc_notify_test_make_single_receiver_current_blocked(
    struct ipc_notify_test_context *fixture,
    struct micros_thread_handle receiver_handle
)
{
    struct micros_endpoint_record *endpoint =
        &fixture->registry.endpoints[fixture->processes[NOTIFY_PROCESS_DESTINATION].slot];
    struct micros_thread *receiver =
        &fixture->objects.threads[receiver_handle.slot];
    micros_endpoint_t receive_source = receiver->ipc_receive_source;
    uintptr_t receive_buffer = receiver->ipc_receive_buffer;

    if (
        !ipc_notify_test_thread_handles_equal(endpoint->receiver_head, receiver_handle)
        || !ipc_notify_test_thread_handles_equal(endpoint->receiver_tail, receiver_handle)
        || receiver->runtime_flags != MICROS_THREAD_RTS_IPC_RECEIVE
        || receiver->ipc_queue_kind != MICROS_IPC_QUEUE_RECEIVER
        || !ipc_notify_test_thread_handle_is_zero(receiver->ipc_next)
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
            &fixture->registry,
            &fixture->objects
        ) != MICROS_ENDPOINT_OK
        || !ipc_notify_test_make_current_held(fixture, receiver_handle)
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
        &fixture->registry,
        &fixture->objects
    ) == MICROS_ENDPOINT_OK;
}
