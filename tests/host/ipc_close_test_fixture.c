#include "ipc_close_test_internal.h"

static struct ipc_close_test_context close_context;

struct ipc_close_test_context *ipc_close_test_context_get(void)
{
    return &close_context;
}

bool ipc_close_test_thread_handle_is_zero(struct micros_thread_handle handle)
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

struct micros_ipc_message ipc_close_test_message_pattern(
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
    struct ipc_close_test_context *fixture,
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
    memset(&fixture->registry, 0, sizeof(fixture->registry));
    memset(&fixture->objects, 0, sizeof(fixture->objects));
    memset(fixture->processes, 0, sizeof(fixture->processes));
    memset(fixture->primary_threads, 0, sizeof(fixture->primary_threads));
    memset(fixture->endpoints, 0, sizeof(fixture->endpoints));
    if (
        micros_endpoint_registry_initialize(
            &fixture->registry,
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) != MICROS_ENDPOINT_OK
        || micros_kernel_objects_initialize(
            &fixture->objects,
            max_threads_per_process,
            1
        ) != MICROS_KERNEL_OBJECT_OK
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
    for (index = 0; index < process_count; ++index) {
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
                1
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_activate(
                &fixture->registry,
                &fixture->objects,
                fixture->endpoints[index]
            ) != MICROS_ENDPOINT_OK
        ) {
            return false;
        }
    }
    return true;
}

bool ipc_close_test_prepare_thread(
    struct ipc_close_test_context *fixture,
    size_t process_index,
    uint8_t priority,
    struct micros_thread_handle *thread
)
{
    struct micros_user_context context;
    uintptr_t stack_bottom;

    if (
        micros_thread_create(
            &fixture->objects,
            fixture->processes[process_index],
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
            priority,
            100,
            true
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_thread_scheduler_hold(&fixture->objects, *thread)
            == MICROS_KERNEL_OBJECT_OK
    );
}

bool ipc_close_test_setup_fixture(
    struct ipc_close_test_context *fixture,
    size_t process_count,
    size_t max_threads_per_process
)
{
    size_t index;

    if (!setup_endpoints(fixture, process_count, max_threads_per_process)) {
        return false;
    }
    for (index = 0; index < process_count; ++index) {
        if (
            !ipc_close_test_prepare_thread(
                fixture,
                index,
                MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
                &fixture->primary_threads[index]
            )
        ) {
            return false;
        }
    }
    return micros_endpoint_registry_validate_objects(
        &fixture->registry,
        &fixture->objects
    ) == MICROS_ENDPOINT_OK;
}

bool ipc_close_test_hold_thread_for_close(struct ipc_close_test_context *fixture, struct micros_thread_handle thread)
{
    return (
        (
            fixture->objects.threads[thread.slot].runtime_flags
            & MICROS_THREAD_RTS_INACTIVE
        ) != 0
        || micros_thread_scheduler_hold(&fixture->objects, thread)
            == MICROS_KERNEL_OBJECT_OK
    );
}

bool ipc_close_test_thread_has_dead_endpoint_result(
    struct ipc_close_test_context *fixture,
    struct micros_thread_handle handle
)
{
    const struct micros_thread *thread = &fixture->objects.threads[handle.slot];

    return (
        thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
        && thread->generation == handle.generation
        && (
            thread->runtime_flags
            & MICROS_THREAD_RTS_IPC_MASK
        ) == 0
        && thread->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && ipc_close_test_thread_handle_is_zero(thread->ipc_next)
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

bool ipc_close_test_closing_thread_is_detached(
    struct ipc_close_test_context *fixture,
    struct micros_thread_handle handle
)
{
    const struct micros_thread *thread = &fixture->objects.threads[handle.slot];

    return (
        thread->runtime_flags == MICROS_THREAD_RTS_INACTIVE
        && !thread->ready_linked
        && micros_thread_ipc_state_is_clear(thread)
    );
}

bool ipc_close_test_clear_staged_delivery(
    struct ipc_close_test_context *fixture,
    struct micros_thread_handle handle
)
{
    struct micros_thread *thread = &fixture->objects.threads[handle.slot];

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
        &fixture->registry,
        &fixture->objects
    ) == MICROS_ENDPOINT_OK;
}

bool ipc_close_test_endpoint_queue_is_empty(
    const struct micros_endpoint_record *endpoint
)
{
    return (
        ipc_close_test_thread_handle_is_zero(endpoint->sender_head)
        && ipc_close_test_thread_handle_is_zero(endpoint->sender_tail)
        && ipc_close_test_thread_handle_is_zero(endpoint->receiver_head)
        && ipc_close_test_thread_handle_is_zero(endpoint->receiver_tail)
    );
}

bool ipc_close_test_expect_close_failure_unchanged(
    struct ipc_close_test_context *fixture,
    enum micros_ipc_error expected,
    micros_endpoint_t endpoint
)
{
    struct micros_endpoint_registry registry_snapshot = fixture->registry;
    struct micros_kernel_objects objects_snapshot = fixture->objects;

    return (
        micros_ipc_endpoint_close(
            &fixture->registry,
            &fixture->objects,
            endpoint
        ) == expected
        && memcmp(
            &fixture->registry,
            &registry_snapshot,
            sizeof(fixture->registry)
        ) == 0
        && memcmp(
            &fixture->objects,
            &objects_snapshot,
            sizeof(fixture->objects)
        ) == 0
    );
}

static bool make_current_held(struct ipc_close_test_context *fixture, struct micros_thread_handle thread)
{
    struct micros_scheduler_return_plan plan;

    return (
        micros_scheduler_accounting_initialize(&fixture->objects, fixture->hart, 100)
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_runtime_flags_unset(
            &fixture->objects,
            thread,
            MICROS_THREAD_RTS_INACTIVE
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_hart_plan_user_return(&fixture->objects, fixture->hart, &plan)
            == MICROS_KERNEL_OBJECT_OK
        && micros_hart_commit_user_return(&fixture->objects, &plan)
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_scheduler_hold(&fixture->objects, thread)
            == MICROS_KERNEL_OBJECT_OK
    );
}

bool ipc_close_test_make_single_sender_current_blocked(
    struct ipc_close_test_context *fixture,
    struct micros_thread_handle sender,
    micros_endpoint_t destination
)
{
    struct micros_endpoint_record *endpoint;
    struct micros_thread saved;
    struct micros_thread *thread;

    endpoint = &fixture->registry.endpoints[
        fixture->processes[CLOSE_PROCESS_CLOSING].slot
    ];
    thread = &fixture->objects.threads[sender.slot];
    if (
        destination != fixture->endpoints[CLOSE_PROCESS_CLOSING]
        || !thread_handles_equal(endpoint->sender_head, sender)
        || !thread_handles_equal(endpoint->sender_tail, sender)
        || thread->runtime_flags != MICROS_THREAD_RTS_IPC_SEND
        || thread->ipc_queue_kind != MICROS_IPC_QUEUE_SENDER
        || !ipc_close_test_thread_handle_is_zero(thread->ipc_next)
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
            &fixture->registry,
            &fixture->objects
        ) != MICROS_ENDPOINT_OK
        || !make_current_held(fixture, sender)
    ) {
        return false;
    }
    *thread = saved;
    endpoint->sender_head = sender;
    endpoint->sender_tail = sender;
    return micros_endpoint_registry_validate_objects(
        &fixture->registry,
        &fixture->objects
    ) == MICROS_ENDPOINT_OK;
}
