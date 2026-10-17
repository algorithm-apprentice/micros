#include "ipc_internal.h"

#include "kernel/endpoint/endpoint_internal.h"

void ipc_copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *output = destination;
    const unsigned char *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
    }
}

void ipc_clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

bool ipc_thread_handle_is_zero(
    struct micros_thread_handle handle
)
{
    return handle.slot == 0 && handle.generation == 0;
}

bool ipc_thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

bool ipc_process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

bool ipc_thread_is_current(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle thread
)
{
    size_t index;

    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        const struct micros_hart *hart = &objects->harts[index];

        if (
            hart->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && ipc_thread_handles_equal(hart->current_thread, thread)
        ) {
            return true;
        }
    }
    return false;
}

enum micros_ipc_error ipc_endpoint_error_to_ipc(
    enum micros_endpoint_error error
)
{
    switch (error) {
    case MICROS_ENDPOINT_OK:
        return MICROS_IPC_OK;
    case MICROS_ENDPOINT_ERROR_ARGUMENT:
    case MICROS_ENDPOINT_ERROR_ENDPOINT:
        return MICROS_IPC_ERROR_ARGUMENT;
    case MICROS_ENDPOINT_ERROR_STALE:
        return MICROS_IPC_ERROR_DEAD_ENDPOINT;
    case MICROS_ENDPOINT_ERROR_STATE:
        return MICROS_IPC_ERROR_STATE;
    case MICROS_ENDPOINT_ERROR_CLOSING:
        return MICROS_IPC_ERROR_ENDPOINT_CLOSING;
    case MICROS_ENDPOINT_ERROR_UNAUTHORIZED:
        return MICROS_IPC_ERROR_UNAUTHORIZED;
    case MICROS_ENDPOINT_ERROR_INVARIANT:
        return MICROS_IPC_ERROR_INVARIANT;
    default:
        return MICROS_IPC_ERROR_INVARIANT;
    }
}

enum micros_ipc_error ipc_scheduler_error_to_ipc(
    enum micros_kernel_object_error error
)
{
    switch (error) {
    case MICROS_KERNEL_OBJECT_OK:
        return MICROS_IPC_OK;
    case MICROS_KERNEL_OBJECT_ERROR_ARGUMENT:
        return MICROS_IPC_ERROR_ARGUMENT;
    case MICROS_KERNEL_OBJECT_ERROR_STALE:
        return MICROS_IPC_ERROR_DEAD_ENDPOINT;
    case MICROS_KERNEL_OBJECT_ERROR_STATE:
        return MICROS_IPC_ERROR_STATE;
    case MICROS_KERNEL_OBJECT_ERROR_INVARIANT:
        return MICROS_IPC_ERROR_INVARIANT;
    default:
        return MICROS_IPC_ERROR_INVARIANT;
    }
}

void ipc_canonicalize_message(
    struct micros_ipc_message *destination,
    const struct micros_ipc_message *source,
    micros_endpoint_t source_endpoint,
    uint64_t reply_token
)
{
    size_t index;

    destination->source = source_endpoint;
    destination->type = source->type;
    destination->reply_token = reply_token;
    for (index = 0; index < sizeof(destination->payload); ++index) {
        destination->payload[index] = source->payload[index];
    }
}

void ipc_clear_message(struct micros_ipc_message *message)
{
    unsigned char *bytes = (unsigned char *)message;
    size_t index;

    for (index = 0; index < sizeof(*message); ++index) {
        bytes[index] = 0;
    }
}

static bool thread_is_held_and_clear(
    const struct micros_thread *thread
)
{
    return (
        thread->scheduler_assigned
        && thread->runtime_flags == MICROS_THREAD_RTS_INACTIVE
        && !thread->ready_linked
        && micros_thread_ipc_state_is_clear(thread)
    );
}

enum micros_ipc_error ipc_resolve_held_thread(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    struct micros_thread **thread
)
{
    const struct micros_thread *resolved;
    enum micros_kernel_object_error error;

    error = micros_thread_resolve(objects, handle, &resolved);
    if (error == MICROS_KERNEL_OBJECT_ERROR_STALE) {
        return MICROS_IPC_ERROR_DEAD_ENDPOINT;
    }
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    if (!thread_is_held_and_clear(resolved)) {
        return MICROS_IPC_ERROR_STATE;
    }
    *thread = &objects->threads[handle.slot];
    return MICROS_IPC_OK;
}

enum micros_ipc_error ipc_resolve_operation_thread(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    micros_endpoint_t *source_endpoint
)
{
    struct micros_thread *thread;
    enum micros_ipc_error error;

    error = ipc_resolve_held_thread(objects, handle, &thread);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    if (ipc_thread_is_current(objects, handle)) {
        return MICROS_IPC_ERROR_STATE;
    }
    *source_endpoint =
        objects->processes[thread->owner.slot].primary_endpoint;
    if (
        *source_endpoint == MICROS_ENDPOINT_NONE
        || *source_endpoint == MICROS_ENDPOINT_ANY
    ) {
        return MICROS_IPC_ERROR_STATE;
    }
    return MICROS_IPC_OK;
}

bool micros_ipc_thread_has_staged_kernel_notification(
    const struct micros_thread *thread
)
{
    return (
        thread != NULL
        && thread->ipc_delivery_pending
        && thread->ipc_staged_result == MICROS_IPC_OK
        && thread->ipc_inbound_message.source
            == MICROS_ENDPOINT_NONE
        && thread->ipc_inbound_message.type
            == MICROS_IPC_TYPE_KERNEL_NOTIFICATION
    );
}
