#include "kernel/endpoint/endpoint_private.h"

bool endpoint_storage_is_zero(
    const struct micros_endpoint_registry *registry
)
{
    const unsigned char *bytes = (const unsigned char *)registry;
    size_t index;

    for (index = 0; index < sizeof(*registry); ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

void endpoint_clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

void endpoint_copy_bytes(
    void *destination,
    const void *source,
    size_t size
)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

bool endpoint_bytes_equal(
    const void *left,
    const void *right,
    size_t size
)
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

bool endpoint_bytes_are_zero(const void *storage, size_t size)
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

bool endpoint_names_equal(
    const char *left,
    const char *right
)
{
    size_t index;

    for (index = 0; index < MICROS_PRIVILEGE_PROFILE_NAME_SIZE; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
        if (left[index] == '\0') {
            return true;
        }
    }
    return false;
}

bool endpoint_profile_is_zero(
    const struct micros_privilege_profile *profile
)
{
    const unsigned char *bytes = (const unsigned char *)profile;
    size_t index;

    for (index = 0; index < sizeof(*profile); ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

bool endpoint_record_is_zero(
    const struct micros_endpoint_record *endpoint
)
{
    size_t index;

    if (
        endpoint->state != MICROS_ENDPOINT_STATE_FREE
        || endpoint->owner.slot != 0
        || endpoint->owner.generation != 0
        || endpoint->value != 0
        || endpoint->sender_head.slot != 0
        || endpoint->sender_head.generation != 0
        || endpoint->sender_tail.slot != 0
        || endpoint->sender_tail.generation != 0
        || endpoint->receiver_head.slot != 0
        || endpoint->receiver_head.generation != 0
        || endpoint->receiver_tail.slot != 0
        || endpoint->receiver_tail.generation != 0
        || endpoint->pending_notification_sources != 0
        || endpoint->pending_kernel_events != 0
    ) {
        return false;
    }
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        if (endpoint->pending_events[index] != 0) {
            return false;
        }
    }
    return true;
}

bool endpoint_ipc_state_is_zero(
    const struct micros_endpoint_record *endpoint
)
{
    size_t index;

    if (
        endpoint->sender_head.slot != 0
        || endpoint->sender_head.generation != 0
        || endpoint->sender_tail.slot != 0
        || endpoint->sender_tail.generation != 0
        || endpoint->receiver_head.slot != 0
        || endpoint->receiver_head.generation != 0
        || endpoint->receiver_tail.slot != 0
        || endpoint->receiver_tail.generation != 0
        || endpoint->pending_notification_sources != 0
        || endpoint->pending_kernel_events != 0
    ) {
        return false;
    }
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        if (endpoint->pending_events[index] != 0) {
            return false;
        }
    }
    return true;
}

bool endpoint_thread_handle_is_zero(
    struct micros_thread_handle handle
)
{
    return handle.slot == 0 && handle.generation == 0;
}

static bool thread_handle_is_shallow_valid(
    struct micros_thread_handle handle
)
{
    return (
        handle.slot < MICROS_THREAD_CAPACITY
        && handle.generation != 0
    );
}

bool endpoint_queue_pair_is_shallow_valid(
    struct micros_thread_handle head,
    struct micros_thread_handle tail
)
{
    if (endpoint_thread_handle_is_zero(head)) {
        return endpoint_thread_handle_is_zero(tail);
    }
    return (
        thread_handle_is_shallow_valid(head)
        && thread_handle_is_shallow_valid(tail)
    );
}

bool endpoint_pending_notification_state_is_shallow_valid(
    const struct micros_endpoint_record *endpoint
)
{
    size_t index;

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        bool source_pending =
            (
                endpoint->pending_notification_sources
                & (UINT64_C(1) << index)
            ) != 0;

        if (source_pending != (endpoint->pending_events[index] != 0)) {
            return false;
        }
    }
    return true;
}

void endpoint_clear_endpoint_record(
    struct micros_endpoint_record *endpoint
)
{
    size_t index;

    endpoint->state = MICROS_ENDPOINT_STATE_FREE;
    endpoint->owner.slot = 0;
    endpoint->owner.generation = 0;
    endpoint->value = 0;
    endpoint->sender_head.slot = 0;
    endpoint->sender_head.generation = 0;
    endpoint->sender_tail.slot = 0;
    endpoint->sender_tail.generation = 0;
    endpoint->receiver_head.slot = 0;
    endpoint->receiver_head.generation = 0;
    endpoint->receiver_tail.slot = 0;
    endpoint->receiver_tail.generation = 0;
    endpoint->pending_notification_sources = 0;
    endpoint->pending_kernel_events = 0;
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        endpoint->pending_events[index] = 0;
    }
}

enum micros_endpoint_error endpoint_canonicalize_profile(
    const struct micros_privilege_profile *source,
    struct micros_privilege_profile *destination
)
{
    size_t index;
    bool terminated = false;

    if (
        source == NULL
        || destination == NULL
        || source->id == 0
        || source->id >= MICROS_PRIVILEGE_PROFILE_CAPACITY
        || source->operations == 0
        || (
            source->operations
            & ~MICROS_PRIVILEGE_OPERATION_DEFINED_MASK
        ) != 0
    ) {
        return MICROS_ENDPOINT_ERROR_PROFILE;
    }
    endpoint_clear_bytes(destination, sizeof(*destination));
    destination->id = source->id;
    for (
        index = 0;
        index < MICROS_PRIVILEGE_PROFILE_NAME_SIZE;
        ++index
    ) {
        unsigned char character = (unsigned char)source->name[index];

        if (character == '\0') {
            terminated = true;
            break;
        }
        if (character < 0x20 || character > 0x7e) {
            return MICROS_ENDPOINT_ERROR_PROFILE;
        }
        destination->name[index] = (char)character;
    }
    if (!terminated || index == 0) {
        return MICROS_ENDPOINT_ERROR_PROFILE;
    }
    destination->operations = source->operations;
    destination->call_targets = source->call_targets;
    destination->send_targets = source->send_targets;
    destination->notify_targets = source->notify_targets;
    destination->kernel_operations = source->kernel_operations;

    if (
        (
            destination->call_targets != 0
            && (
                destination->operations
                & MICROS_PRIVILEGE_OPERATION_CALL
            ) == 0
        )
        || (
            destination->send_targets != 0
            && (
                destination->operations
                & MICROS_PRIVILEGE_OPERATION_SEND
            ) == 0
        )
        || (
            destination->notify_targets != 0
            && (
                destination->operations
                & MICROS_PRIVILEGE_OPERATION_NOTIFY
            ) == 0
        )
    ) {
        return MICROS_ENDPOINT_ERROR_PROFILE;
    }
    return MICROS_ENDPOINT_OK;
}
