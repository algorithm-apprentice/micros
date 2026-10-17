#include "kernel/tests/ipc/ipc_test_internal.h"

static struct ipc_test_context test_context;
static const struct micros_ipc_message zero_message;

struct ipc_test_context *ipc_test_context_get(void)
{
    return &test_context;
}

static void clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

void ipc_test_copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

bool ipc_test_bytes_equal(const void *left, const void *right, size_t size)
{
    const unsigned char *left_bytes = left;
    const unsigned char *right_bytes = right;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
            return false;
        }
    }
    return true;
}

bool ipc_test_thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

bool ipc_test_thread_handle_is_zero(struct micros_thread_handle handle)
{
    return handle.slot == 0 && handle.generation == 0;
}

struct micros_user_context ipc_test_context_pattern(uint64_t base)
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
    ipc_test_copy_bytes(&context, words, sizeof(context));
    return context;
}

static void fill_stack_pattern(struct ipc_test_context *fixture, size_t stack_index)
{
    size_t index;

    for (index = 0; index < MICROS_THREAD_KERNEL_STACK_SIZE; ++index) {
        fixture->ipc_test_stacks[stack_index][index] =
            (unsigned char)(UINT8_C(0x31) + stack_index + index);
    }
}

static bool stack_pattern_matches(struct ipc_test_context *fixture, size_t stack_index)
{
    size_t index;

    for (index = 0; index < MICROS_THREAD_KERNEL_STACK_SIZE; ++index) {
        if (
            fixture->ipc_test_stacks[stack_index][index]
                != (unsigned char)(
                    UINT8_C(0x31) + stack_index + index
                )
        ) {
            return false;
        }
    }
    return true;
}

bool ipc_test_snapshot_execution_state(
    struct ipc_test_context *fixture,
    const struct micros_kernel_objects *objects,
    const struct micros_process_handle processes[IPC_TEST_PROCESS_COUNT],
    const struct micros_thread_handle threads[IPC_TEST_PROCESS_COUNT]
)
{
    size_t index;

    for (index = 0; index < IPC_TEST_PROCESS_COUNT; ++index) {
        uintptr_t stack_bottom;
        uintptr_t stack_top;

        fixture->root_snapshots[index] =
            objects->processes[processes[index].slot]
                .address_space_root;
        if (
            fixture->root_snapshots[index] == 0
            || micros_user_address_space_validate(processes[index])
                != MICROS_USER_ADDRESS_SPACE_OK
            || micros_thread_inspect_execution_context(
                objects,
                threads[index],
                &fixture->context_snapshots[index],
                &stack_bottom,
                &stack_top
            ) != MICROS_KERNEL_OBJECT_OK
            || stack_bottom
                != (uintptr_t)&fixture->ipc_test_stacks[index][0]
            || stack_top
                != (uintptr_t)&fixture->ipc_test_stacks[index][
                    MICROS_THREAD_KERNEL_STACK_SIZE
                ]
        ) {
            return false;
        }
        fill_stack_pattern(fixture, index);
    }
    return (
        fixture->root_snapshots[0] != fixture->root_snapshots[1]
        && fixture->root_snapshots[0] != fixture->root_snapshots[2]
        && fixture->root_snapshots[1] != fixture->root_snapshots[2]
    );
}

bool ipc_test_execution_state_matches(
    struct ipc_test_context *fixture,
    const struct micros_kernel_objects *objects,
    const struct micros_process_handle processes[IPC_TEST_PROCESS_COUNT],
    const struct micros_thread_handle threads[IPC_TEST_PROCESS_COUNT]
)
{
    size_t index;

    for (index = 0; index < IPC_TEST_PROCESS_COUNT; ++index) {
        struct micros_user_context observed;
        uintptr_t stack_bottom;
        uintptr_t stack_top;

        if (
            objects->processes[processes[index].slot]
                .address_space_root != fixture->root_snapshots[index]
            || micros_user_address_space_validate(processes[index])
                != MICROS_USER_ADDRESS_SPACE_OK
            || micros_thread_inspect_execution_context(
                objects,
                threads[index],
                &observed,
                &stack_bottom,
                &stack_top
            ) != MICROS_KERNEL_OBJECT_OK
            || !ipc_test_bytes_equal(
                &observed,
                &fixture->context_snapshots[index],
                sizeof(observed)
            )
            || stack_bottom
                != (uintptr_t)&fixture->ipc_test_stacks[index][0]
            || stack_top
                != (uintptr_t)&fixture->ipc_test_stacks[index][
                    MICROS_THREAD_KERNEL_STACK_SIZE
                ]
            || !stack_pattern_matches(fixture, index)
        ) {
            return false;
        }
    }
    return true;
}

void ipc_test_fill_message(
    struct micros_ipc_message *message,
    uint32_t type,
    uint8_t seed
)
{
    size_t index;

    clear_bytes(message, sizeof(*message));
    message->source = MICROS_ENDPOINT_ANY;
    message->type = type;
    message->reply_token = UINT64_C(0xa5a5a5a5a5a5a5a5);
    for (index = 0; index < sizeof(message->payload); ++index) {
        message->payload[index] =
            (uint8_t)(seed + (uint8_t)index);
    }
}

struct micros_ipc_message ipc_test_canonical_message(
    const struct micros_ipc_message *source,
    micros_endpoint_t source_endpoint,
    uint64_t reply_token
)
{
    struct micros_ipc_message result = *source;

    result.source = source_endpoint;
    result.reply_token = reply_token;
    return result;
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

bool ipc_test_notification_matches(
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

bool ipc_test_prepare_thread(
    struct ipc_test_context *fixture,
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    struct micros_process_handle process,
    size_t stack_index,
    struct micros_thread_handle *thread
)
{
    struct micros_user_context context =
        ipc_test_context_pattern(UINT64_C(0x1000) + stack_index * UINT64_C(0x100));
    uintptr_t stack_bottom =
        (uintptr_t)&fixture->ipc_test_stacks[stack_index][0];
    uintptr_t stack_top =
        (uintptr_t)&fixture->ipc_test_stacks[stack_index][
            MICROS_THREAD_KERNEL_STACK_SIZE
        ];

    return (
        micros_thread_create(objects, process, thread)
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_attach_execution_context(
            objects,
            *thread,
            stack_bottom,
            stack_top,
            &context
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_thread_scheduler_admit(
            objects,
            hart,
            *thread,
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            100,
            true
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_thread_scheduler_hold(objects, *thread)
            == MICROS_KERNEL_OBJECT_OK
    );
}

bool ipc_test_hold_thread(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread
)
{
    const struct micros_thread *resolved =
        &objects->threads[thread.slot];

    return (
        resolved->runtime_flags == 0
        && resolved->ready_linked
        && !resolved->ipc_delivery_pending
        && micros_thread_scheduler_hold(objects, thread)
            == MICROS_KERNEL_OBJECT_OK
    );
}

bool ipc_test_consume_staged(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread,
    enum micros_ipc_error expected_result,
    const struct micros_ipc_message *expected_message
)
{
    struct micros_thread *resolved = &objects->threads[thread.slot];

    if (
        !resolved->ipc_delivery_pending
        || resolved->ipc_staged_result != expected_result
        || (
            expected_message != NULL
            && !ipc_test_bytes_equal(
                &resolved->ipc_inbound_message,
                expected_message,
                sizeof(*expected_message)
            )
        )
        || (
            expected_message == NULL
            && !ipc_test_bytes_equal(
                &resolved->ipc_inbound_message,
                &zero_message,
                sizeof(resolved->ipc_inbound_message)
            )
        )
    ) {
        return false;
    }
    resolved->ipc_receive_buffer = 0;
    resolved->ipc_delivery_pending = false;
    clear_bytes(
        &resolved->ipc_inbound_message,
        sizeof(resolved->ipc_inbound_message)
    );
    resolved->ipc_staged_result = MICROS_IPC_OK;
    return true;
}

void ipc_test_snapshot_state(struct ipc_test_context *fixture, const struct micros_kernel_objects *objects)
{
    ipc_test_copy_bytes(&fixture->registry_snapshot, fixture->registry, sizeof(*fixture->registry));
    ipc_test_copy_bytes(&fixture->objects_snapshot, objects, sizeof(*objects));
}

bool ipc_test_state_matches_snapshot(
    struct ipc_test_context *fixture,
    const struct micros_kernel_objects *objects
)
{
    return (
        ipc_test_bytes_equal(&fixture->registry_snapshot, fixture->registry, sizeof(*fixture->registry))
        && ipc_test_bytes_equal(&fixture->objects_snapshot, objects, sizeof(*objects))
    );
}

bool ipc_test_ready_queue_matches(
    const struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    const struct micros_thread_handle *threads,
    size_t thread_count
)
{
    const struct micros_hart *resolved = &objects->harts[hart.slot];
    struct micros_thread_handle current =
        resolved->ready_head[MICROS_SCHEDULER_PRIORITY_DEFAULT_USER];
    size_t index;

    for (index = 0; index < thread_count; ++index) {
        if (!ipc_test_thread_handles_equal(current, threads[index])) {
            return false;
        }
        current = objects->threads[current.slot].ready_next;
    }
    return (
        ipc_test_thread_handle_is_zero(current)
        && (
            thread_count == 0
                ? ipc_test_thread_handle_is_zero(
                    resolved->ready_tail[
                        MICROS_SCHEDULER_PRIORITY_DEFAULT_USER
                    ]
                )
                : ipc_test_thread_handles_equal(
                    resolved->ready_tail[
                        MICROS_SCHEDULER_PRIORITY_DEFAULT_USER
                    ],
                    threads[thread_count - 1]
                )
        )
    );
}

bool ipc_test_endpoint_records_are_clear(struct ipc_test_context *fixture)
{
    size_t index;
    size_t source;

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct micros_endpoint_record *record =
            &fixture->registry->endpoints[index];

        if (
            record->state != MICROS_ENDPOINT_STATE_FREE
            || record->owner.slot != 0
            || record->owner.generation != 0
            || record->value != 0
            || !ipc_test_thread_handle_is_zero(record->sender_head)
            || !ipc_test_thread_handle_is_zero(record->sender_tail)
            || !ipc_test_thread_handle_is_zero(record->receiver_head)
            || !ipc_test_thread_handle_is_zero(record->receiver_tail)
            || record->pending_notification_sources != 0
            || record->pending_kernel_events != 0
        ) {
            return false;
        }
        for (source = 0; source < MICROS_PROCESS_CAPACITY; ++source) {
            if (record->pending_events[source] != 0) {
                return false;
            }
        }
    }
    return true;
}

bool ipc_test_cleanup_thread(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread
)
{
    return (
        micros_thread_scheduler_remove(objects, thread)
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_detach_execution_context(objects, thread)
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_release(objects, thread)
            == MICROS_KERNEL_OBJECT_OK
    );
}
