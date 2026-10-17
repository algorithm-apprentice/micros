#include "ipc_internal.h"

#include "kernel/endpoint/endpoint_internal.h"

void ipc_stage_no_message_completion(
    struct micros_thread *thread,
    enum micros_ipc_error result
)
{
    thread->ipc_receive_buffer = 0;
    thread->ipc_delivery_pending = true;
    ipc_clear_message(&thread->ipc_inbound_message);
    thread->ipc_staged_result = result;
}

void ipc_clear_delivered_sender(struct micros_thread *sender)
{
    bool completes_send = sender->ipc_reply_token == 0;

    sender->ipc_queue_kind = MICROS_IPC_QUEUE_NONE;
    sender->ipc_next.slot = 0;
    sender->ipc_next.generation = 0;
    ipc_clear_message(&sender->ipc_outbound_message);
    sender->ipc_send_destination = 0;
    if (completes_send) {
        ipc_stage_no_message_completion(sender, MICROS_IPC_OK);
    }
}

void ipc_stage_delivery(
    struct micros_thread *receiver,
    uintptr_t receive_buffer,
    const struct micros_ipc_message *message
)
{
    receiver->ipc_receive_buffer = receive_buffer;
    receiver->ipc_delivery_pending = true;
    receiver->ipc_inbound_message = *message;
    receiver->ipc_staged_result = MICROS_IPC_OK;
}

enum micros_ipc_error micros_ipc_stage_no_message_completion(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread_handle,
    enum micros_ipc_error result
)
{
    const struct micros_thread *resolved;
    struct micros_thread *thread;
    const struct micros_endpoint_record *owner;
    enum micros_endpoint_error endpoint_error;
    enum micros_kernel_object_error object_error;

    if (
        registry == NULL
        || objects == NULL
        || (
            result != MICROS_IPC_OK
            && result != MICROS_IPC_ERROR_DEAD_ENDPOINT
        )
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    endpoint_error =
        micros_endpoint_registry_validate_objects(registry, objects);
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return ipc_endpoint_error_to_ipc(endpoint_error);
    }
    object_error =
        micros_thread_resolve(objects, thread_handle, &resolved);
    if (object_error == MICROS_KERNEL_OBJECT_ERROR_STALE) {
        return MICROS_IPC_ERROR_DEAD_ENDPOINT;
    }
    if (object_error != MICROS_KERNEL_OBJECT_OK) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    owner = &registry->endpoints[resolved->owner.slot];
    if (
        !resolved->scheduler_assigned
        || !micros_thread_ipc_state_is_clear(resolved)
        || (
            resolved->runtime_flags
            & MICROS_THREAD_RTS_IPC_MASK
        ) != 0
        || owner->state != MICROS_ENDPOINT_STATE_ACTIVE
        || !ipc_process_handles_equal(owner->owner, resolved->owner)
    ) {
        return MICROS_IPC_ERROR_STATE;
    }
    thread = &objects->threads[thread_handle.slot];
    ipc_stage_no_message_completion(thread, result);
    return MICROS_IPC_OK;
}
