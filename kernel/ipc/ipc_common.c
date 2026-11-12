#include "kernel/ipc/ipc_internal.h"

#include "kernel/scheduler/scheduler_core_internal.h"

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
    return scheduler_core_thread_is_current(objects, thread);
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

enum micros_ipc_error ipc_admit_active_endpoint(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    struct micros_endpoint_selection *selection
)
{
    struct micros_endpoint_selection candidate;
    enum micros_endpoint_error error =
        micros_endpoint_select(registry, objects, endpoint, &candidate);

    if (error != MICROS_ENDPOINT_OK) {
        return ipc_endpoint_error_to_ipc(error);
    }
    if (candidate.record->state != MICROS_ENDPOINT_STATE_ACTIVE) {
        return candidate.record->state == MICROS_ENDPOINT_STATE_SOURCE_ONLY
            ? MICROS_IPC_ERROR_ENDPOINT_CLOSING : MICROS_IPC_ERROR_STATE;
    }
    *selection = candidate;
    return MICROS_IPC_OK;
}

enum micros_ipc_error ipc_admit_held_thread(
    const struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    struct ipc_participant *participant
)
{
    struct ipc_participant candidate;
    const struct micros_thread *thread;
    const struct micros_process *process;
    enum micros_endpoint_error endpoint_error = endpoint_preflight_registry(registry);
    enum micros_kernel_object_error object_error;

    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return ipc_endpoint_error_to_ipc(endpoint_error);
    }
    object_error = scheduler_core_preflight_thread(objects, handle, &thread);
    if (object_error != MICROS_KERNEL_OBJECT_OK) {
        return object_error == MICROS_KERNEL_OBJECT_ERROR_STALE
            ? MICROS_IPC_ERROR_DEAD_ENDPOINT : MICROS_IPC_ERROR_INVARIANT;
    }
    if (!thread_is_held_and_clear(thread)) {
        return micros_endpoint_thread_ipc_state_is_valid(registry, objects, handle)
            ? MICROS_IPC_ERROR_STATE : MICROS_IPC_ERROR_INVARIANT;
    }
    process = &objects->processes[thread->owner.slot];
    if (process->primary_endpoint == MICROS_PROCESS_ENDPOINT_NONE) {
        return process->privilege_profile == 0
            ? MICROS_IPC_ERROR_STATE : MICROS_IPC_ERROR_INVARIANT;
    }
    endpoint_error = micros_endpoint_select(
        registry, objects, process->primary_endpoint, &candidate.endpoint
    );
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    if (
        candidate.endpoint.record->state == MICROS_ENDPOINT_STATE_SOURCE_ONLY
        && !micros_endpoint_thread_ipc_state_is_valid(registry, objects, handle)
    ) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    candidate.handle = handle;
    candidate.thread = &objects->threads[handle.slot];
    *participant = candidate;
    return MICROS_IPC_OK;
}

enum micros_ipc_error ipc_admit_operation_thread(
    const struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    struct ipc_participant *participant
)
{
    struct ipc_participant candidate;
    enum micros_ipc_error error = ipc_admit_held_thread(registry, objects, handle, &candidate);

    if (error != MICROS_IPC_OK) {
        return error;
    }
    if (ipc_thread_is_current(objects, handle)) {
        return MICROS_IPC_ERROR_STATE;
    }
    if (candidate.endpoint.record->state != MICROS_ENDPOINT_STATE_ACTIVE) {
        return candidate.endpoint.record->state == MICROS_ENDPOINT_STATE_SOURCE_ONLY
            ? MICROS_IPC_ERROR_ENDPOINT_CLOSING : MICROS_IPC_ERROR_STATE;
    }
    *participant = candidate;
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
