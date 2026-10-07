#include "micros/ipc_core.h"

#include <stddef.h>

static bool thread_handle_is_zero(
    struct micros_thread_handle handle
)
{
    return handle.slot == 0 && handle.generation == 0;
}

static enum micros_ipc_error endpoint_error_to_ipc(
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
    case MICROS_ENDPOINT_ERROR_UNAUTHORIZED:
        return MICROS_IPC_ERROR_UNAUTHORIZED;
    case MICROS_ENDPOINT_ERROR_INVARIANT:
        return MICROS_IPC_ERROR_INVARIANT;
    default:
        return MICROS_IPC_ERROR_INVARIANT;
    }
}

static void canonicalize_message(
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

static bool reply_token_is_available(
    const struct micros_kernel_objects *objects,
    uint64_t reply_token
)
{
    size_t index;

    if (reply_token == 0) {
        return true;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];

        if (
            thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && thread->ipc_reply_token == reply_token
        ) {
            return false;
        }
    }
    return true;
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

static enum micros_ipc_error resolve_held_thread(
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

static enum micros_ipc_error resolve_queue_tail(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle tail,
    struct micros_thread **thread
)
{
    const struct micros_thread *resolved;

    if (thread_handle_is_zero(tail)) {
        *thread = NULL;
        return MICROS_IPC_OK;
    }
    if (
        micros_thread_resolve(objects, tail, &resolved)
            != MICROS_KERNEL_OBJECT_OK
        || !thread_handle_is_zero(resolved->ipc_next)
    ) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    *thread = &objects->threads[tail.slot];
    return MICROS_IPC_OK;
}

static bool receiver_is_ready_for(
    const struct micros_endpoint_record *destination,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t source
)
{
    struct micros_thread_handle current = destination->receiver_head;
    size_t steps;

    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        const struct micros_thread *receiver;

        if (thread_handle_is_zero(current)) {
            return false;
        }
        receiver = &objects->threads[current.slot];
        if (
            receiver->ipc_receive_source == MICROS_ENDPOINT_ANY
            || receiver->ipc_receive_source == source
        ) {
            return true;
        }
        current = receiver->ipc_next;
    }
    return false;
}

static bool sender_is_ready_for(
    const struct micros_endpoint_record *receiver_endpoint,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t source
)
{
    struct micros_thread_handle current =
        receiver_endpoint->sender_head;
    size_t steps;

    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        const struct micros_thread *sender;

        if (thread_handle_is_zero(current)) {
            return false;
        }
        sender = &objects->threads[current.slot];
        if (
            source == MICROS_ENDPOINT_ANY
            || sender->ipc_outbound_message.source == source
        ) {
            return true;
        }
        current = sender->ipc_next;
    }
    return false;
}

static void append_thread(
    struct micros_thread_handle thread_handle,
    struct micros_thread *previous_tail,
    struct micros_thread_handle *head,
    struct micros_thread_handle *tail
)
{
    if (previous_tail == NULL) {
        *head = thread_handle;
    } else {
        previous_tail->ipc_next = thread_handle;
    }
    *tail = thread_handle;
}

enum micros_ipc_error micros_ipc_sender_enqueue(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle sender_handle,
    micros_endpoint_t destination_endpoint,
    const struct micros_ipc_message *message,
    uint64_t reply_token,
    uintptr_t reply_buffer
)
{
    const struct micros_endpoint_record *resolved_destination;
    const struct micros_endpoint_record *source;
    struct micros_endpoint_record *destination;
    struct micros_thread *sender;
    struct micros_thread *previous_tail;
    enum micros_endpoint_error endpoint_error;
    enum micros_ipc_error error;

    if (
        registry == NULL
        || objects == NULL
        || message == NULL
        || (
            message->type
            & MICROS_IPC_TYPE_KERNEL_MASK
        ) != 0
        || (
            reply_token == 0
                ? reply_buffer != 0
                : (
                    reply_buffer == 0
                    || reply_buffer % 8 != 0
                )
        )
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    endpoint_error =
        micros_endpoint_registry_validate_objects(registry, objects);
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return endpoint_error_to_ipc(endpoint_error);
    }
    error = resolve_held_thread(objects, sender_handle, &sender);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    endpoint_error = micros_endpoint_resolve_active(
        registry,
        objects,
        destination_endpoint,
        &resolved_destination
    );
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return endpoint_error_to_ipc(endpoint_error);
    }
    source = &registry->endpoints[sender->owner.slot];
    if (
        source->state != MICROS_ENDPOINT_STATE_ACTIVE
        || source->owner.generation != sender->owner.generation
    ) {
        return MICROS_IPC_ERROR_STATE;
    }
    if (!reply_token_is_available(objects, reply_token)) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    if (
        receiver_is_ready_for(
            resolved_destination,
            objects,
            source->value
        )
    ) {
        return MICROS_IPC_ERROR_NOT_READY;
    }
    destination =
        &registry->endpoints[resolved_destination->owner.slot];
    error = resolve_queue_tail(
        objects,
        destination->sender_tail,
        &previous_tail
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }

    sender->runtime_flags = MICROS_THREAD_RTS_IPC_SEND;
    if (reply_token != 0) {
        sender->runtime_flags |= MICROS_THREAD_RTS_IPC_REPLY;
    }
    sender->ipc_queue_kind = MICROS_IPC_QUEUE_SENDER;
    sender->ipc_next.slot = 0;
    sender->ipc_next.generation = 0;
    canonicalize_message(
        &sender->ipc_outbound_message,
        message,
        source->value,
        reply_token
    );
    sender->ipc_send_destination = destination_endpoint;
    sender->ipc_receive_buffer = reply_buffer;
    sender->ipc_reply_token = reply_token;
    sender->ipc_reply_callee =
        reply_token == 0 ? 0 : destination_endpoint;
    append_thread(
        sender_handle,
        previous_tail,
        &destination->sender_head,
        &destination->sender_tail
    );
    return MICROS_IPC_OK;
}

enum micros_ipc_error micros_ipc_receiver_enqueue(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle receiver_handle,
    micros_endpoint_t source_endpoint,
    uintptr_t receive_buffer
)
{
    const struct micros_endpoint_record *resolved_source;
    struct micros_endpoint_record *receiver_endpoint;
    struct micros_thread *receiver;
    struct micros_thread *previous_tail;
    enum micros_endpoint_error endpoint_error;
    enum micros_ipc_error error;

    if (
        registry == NULL
        || objects == NULL
        || receive_buffer == 0
        || receive_buffer % 8 != 0
        || source_endpoint == MICROS_ENDPOINT_NONE
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    endpoint_error =
        micros_endpoint_registry_validate_objects(registry, objects);
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return endpoint_error_to_ipc(endpoint_error);
    }
    error = resolve_held_thread(objects, receiver_handle, &receiver);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    receiver_endpoint =
        &registry->endpoints[receiver->owner.slot];
    if (
        receiver_endpoint->state != MICROS_ENDPOINT_STATE_ACTIVE
        || receiver_endpoint->owner.generation
            != receiver->owner.generation
    ) {
        return MICROS_IPC_ERROR_STATE;
    }
    if (source_endpoint != MICROS_ENDPOINT_ANY) {
        endpoint_error = micros_endpoint_resolve_active(
            registry,
            objects,
            source_endpoint,
            &resolved_source
        );
        if (endpoint_error != MICROS_ENDPOINT_OK) {
            return endpoint_error_to_ipc(endpoint_error);
        }
        (void)resolved_source;
    }
    if (
        sender_is_ready_for(
            receiver_endpoint,
            objects,
            source_endpoint
        )
    ) {
        return MICROS_IPC_ERROR_NOT_READY;
    }
    error = resolve_queue_tail(
        objects,
        receiver_endpoint->receiver_tail,
        &previous_tail
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }

    receiver->runtime_flags = MICROS_THREAD_RTS_IPC_RECEIVE;
    receiver->ipc_queue_kind = MICROS_IPC_QUEUE_RECEIVER;
    receiver->ipc_next.slot = 0;
    receiver->ipc_next.generation = 0;
    receiver->ipc_receive_source = source_endpoint;
    receiver->ipc_receive_buffer = receive_buffer;
    append_thread(
        receiver_handle,
        previous_tail,
        &receiver_endpoint->receiver_head,
        &receiver_endpoint->receiver_tail
    );
    return MICROS_IPC_OK;
}
