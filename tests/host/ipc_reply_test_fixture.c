#include "ipc_reply_test_internal.h"

static struct ipc_reply_test_context reply_context;

struct ipc_reply_test_context *ipc_reply_test_context_get(void)
{
    return &reply_context;
}

bool ipc_reply_test_thread_handle_is_zero(struct micros_thread_handle handle)
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

struct micros_ipc_message ipc_reply_test_message_pattern(
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

struct micros_ipc_message ipc_reply_test_canonical_reply(
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
    struct ipc_reply_test_context *fixture,
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

static bool prepare_reply_processes(
    struct ipc_reply_test_context *fixture,
    const uint8_t profile_ids[REPLY_PROCESS_COUNT]
)
{
    size_t index;

    for (index = 0; index < REPLY_PROCESS_COUNT; ++index) {
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
                index != REPLY_PROCESS_RESERVED_SERVER
                && micros_endpoint_activate(
                    &fixture->registry,
                    &fixture->objects,
                    fixture->endpoints[index]
                ) != MICROS_ENDPOINT_OK
            )
        ) {
            return false;
        }
    }
    return true;
}

static bool prepare_reply_threads(struct ipc_reply_test_context *fixture)
{
    size_t index;

    for (index = 0; index < REPLY_PROCESS_COUNT; ++index) {
        if (
            !prepare_thread(
                fixture,
                fixture->processes[index],
                UINT64_C(0x3000) + index * UINT64_C(0x100),
                UINT64_C(0x12000000)
                    + index * UINT64_C(0x00008000),
                &fixture->primary_threads[index]
            )
        ) {
            return false;
        }
    }
    return (
        prepare_thread(
            fixture,
            fixture->processes[REPLY_PROCESS_CLIENT],
            UINT64_C(0x4000),
            UINT64_C(0x13000000),
            &fixture->client_extra_thread
        )
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
}

bool ipc_reply_test_setup_reply_fixture(struct ipc_reply_test_context *fixture)
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
    memset(&fixture->registry, 0, sizeof(fixture->registry));
    memset(&fixture->objects, 0, sizeof(fixture->objects));
    if (
        micros_endpoint_registry_initialize(
            &fixture->registry,
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) != MICROS_ENDPOINT_OK
        || micros_kernel_objects_initialize(&fixture->objects, 2, 1)
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
    return (
        prepare_reply_processes(fixture, profile_ids)
        && prepare_reply_threads(fixture)
    );
}

bool ipc_reply_test_prepare_server_extra_thread(struct ipc_reply_test_context *fixture)
{
    return prepare_thread(
        fixture,
        fixture->processes[REPLY_PROCESS_SERVER],
        UINT64_C(0x4100),
        UINT64_C(0x13008000),
        &fixture->server_extra_thread
    );
}

bool ipc_reply_test_clear_staged_request(
    struct ipc_reply_test_context *fixture,
    struct micros_thread_handle receiver_handle,
    struct micros_ipc_message *delivered
)
{
    struct micros_thread *receiver =
        &fixture->objects.threads[receiver_handle.slot];

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
        micros_thread_scheduler_hold(&fixture->objects, receiver_handle)
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
        &fixture->registry,
        &fixture->objects
    ) == MICROS_ENDPOINT_OK;
}

bool ipc_reply_test_stage_delivered_call(
    struct ipc_reply_test_context *fixture,
    struct micros_thread_handle caller,
    size_t server_index,
    struct micros_ipc_message *request,
    uintptr_t receive_buffer,
    uint64_t *reply_token
)
{
    struct micros_thread *receiver =
        &fixture->objects.threads[fixture->primary_threads[server_index].slot];

    if (
        reply_token == NULL
        || micros_ipc_receiver_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->primary_threads[server_index],
            fixture->endpoints[REPLY_PROCESS_CLIENT],
            receive_buffer
        ) != MICROS_IPC_OK
        || micros_ipc_call(
            &fixture->registry,
            &fixture->objects,
            caller,
            fixture->endpoints[server_index],
            request,
            (uintptr_t)request
) != MICROS_IPC_OK
        || !receiver->ipc_delivery_pending
        || receiver->ipc_receive_buffer != receive_buffer
        || receiver->ipc_inbound_message.source
            != fixture->endpoints[REPLY_PROCESS_CLIENT]
        || receiver->ipc_inbound_message.reply_token == 0
        || micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) != MICROS_ENDPOINT_OK
    ) {
        return false;
    }
    *reply_token = receiver->ipc_inbound_message.reply_token;
    return true;
}

bool ipc_reply_test_prepare_delivered_call(
    struct ipc_reply_test_context *fixture,
    struct micros_thread_handle caller,
    size_t server_index,
    struct micros_ipc_message *request,
    uintptr_t receive_buffer,
    uint64_t *reply_token
)
{
    struct micros_ipc_message delivered;

    if (
        !ipc_reply_test_stage_delivered_call(
            fixture,
            caller,
            server_index,
            request,
            receive_buffer,
            reply_token
        )
        || !ipc_reply_test_clear_staged_request(
            fixture,
            fixture->primary_threads[server_index],
            &delivered
        )
        || delivered.source != fixture->endpoints[REPLY_PROCESS_CLIENT]
        || delivered.reply_token != *reply_token
    ) {
        return false;
    }
    return true;
}

bool ipc_reply_test_complete_state_is_unchanged(
    struct ipc_reply_test_context *fixture,
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

bool ipc_reply_test_expect_reply_failure_unchanged(
    struct ipc_reply_test_context *fixture,
    enum micros_ipc_error expected,
    struct micros_thread_handle replier,
    uint64_t reply_token,
    const struct micros_ipc_message *message
)
{
    struct micros_endpoint_registry registry_snapshot = fixture->registry;
    struct micros_kernel_objects objects_snapshot = fixture->objects;
    struct micros_ipc_message message_snapshot = *message;

    EXPECT_IPC_ERROR(
        expected,
        micros_ipc_reply(
            &fixture->registry,
            &fixture->objects,
            replier,
            reply_token,
            message
        )
    );
    EXPECT_TRUE(ipc_reply_test_complete_state_is_unchanged(
        fixture,
        &registry_snapshot,
        &objects_snapshot
    ));
    EXPECT_TRUE(
        memcmp(message, &message_snapshot, sizeof(*message)) == 0
    );
    return true;
}

bool ipc_reply_test_expect_reply_receive_failure_unchanged(
    struct ipc_reply_test_context *fixture,
    enum micros_ipc_error expected,
    struct micros_thread_handle replier,
    uint64_t reply_token,
    const struct micros_ipc_message *reply_message,
    micros_endpoint_t source,
    uintptr_t receive_buffer
)
{
    struct micros_endpoint_registry registry_snapshot = fixture->registry;
    struct micros_kernel_objects objects_snapshot = fixture->objects;
    struct micros_ipc_message message_snapshot = *reply_message;

    EXPECT_IPC_ERROR(
        expected,
        micros_ipc_reply_receive(
            &fixture->registry,
            &fixture->objects,
            replier,
            reply_token,
            reply_message,
            source,
            receive_buffer
        )
    );
    EXPECT_TRUE(ipc_reply_test_complete_state_is_unchanged(
        fixture,
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

bool ipc_reply_test_expect_send_failure_unchanged(
    struct ipc_reply_test_context *fixture,
    enum micros_ipc_error expected,
    struct micros_thread_handle sender,
    micros_endpoint_t destination,
    const struct micros_ipc_message *message
)
{
    struct micros_endpoint_registry registry_snapshot = fixture->registry;
    struct micros_kernel_objects objects_snapshot = fixture->objects;
    struct micros_ipc_message message_snapshot = *message;

    EXPECT_IPC_ERROR(
        expected,
        micros_ipc_send(
            &fixture->registry,
            &fixture->objects,
            sender,
            destination,
            message
        )
    );
    EXPECT_TRUE(ipc_reply_test_complete_state_is_unchanged(
        fixture,
        &registry_snapshot,
        &objects_snapshot
    ));
    EXPECT_TRUE(
        memcmp(message, &message_snapshot, sizeof(*message)) == 0
    );
    return true;
}

bool ipc_reply_test_make_reply_wait_current(
    struct ipc_reply_test_context *fixture,
    struct micros_thread_handle caller_handle
)
{
    struct micros_scheduler_return_plan plan;
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
        || micros_scheduler_accounting_initialize(
            &fixture->objects,
            fixture->hart,
            100
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_thread_runtime_flags_unset(
            &fixture->objects,
            caller_handle,
            MICROS_THREAD_RTS_INACTIVE
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_hart_plan_user_return(&fixture->objects, fixture->hart, &plan)
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_commit_user_return(&fixture->objects, &plan)
            != MICROS_KERNEL_OBJECT_OK
        || micros_thread_scheduler_hold(&fixture->objects, caller_handle)
            != MICROS_KERNEL_OBJECT_OK
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

bool ipc_reply_test_make_single_queued_sender_current(
    struct ipc_reply_test_context *fixture,
    struct micros_thread_handle sender_handle,
    size_t destination_index
)
{
    struct micros_scheduler_return_plan plan;
    struct micros_endpoint_record *destination =
        &fixture->registry.endpoints[fixture->processes[destination_index].slot];
    struct micros_thread *sender =
        &fixture->objects.threads[sender_handle.slot];
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
        || !ipc_reply_test_thread_handle_is_zero(sender->ipc_next)
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
            &fixture->registry,
            &fixture->objects
        ) != MICROS_ENDPOINT_OK
        || micros_scheduler_accounting_initialize(
            &fixture->objects,
            fixture->hart,
            100
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_thread_runtime_flags_unset(
            &fixture->objects,
            sender_handle,
            MICROS_THREAD_RTS_INACTIVE
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_hart_plan_user_return(&fixture->objects, fixture->hart, &plan)
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_commit_user_return(&fixture->objects, &plan)
            != MICROS_KERNEL_OBJECT_OK
        || micros_thread_scheduler_hold(&fixture->objects, sender_handle)
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
        &fixture->registry,
        &fixture->objects
    ) == MICROS_ENDPOINT_OK;
}
