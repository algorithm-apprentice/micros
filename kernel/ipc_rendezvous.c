#include "ipc_internal.h"

#include "kernel/scheduler/scheduler_core_internal.h"

static enum micros_ipc_error resolve_sender_destination(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle sender_handle,
    micros_endpoint_t destination_endpoint,
    struct micros_thread **sender,
    const struct micros_endpoint_record **source,
    struct micros_endpoint_record **destination
)
{
    const struct micros_endpoint_record *resolved_destination;
    enum micros_endpoint_error endpoint_error;
    enum micros_ipc_error error;

    endpoint_error =
        micros_endpoint_registry_validate_objects(registry, objects);
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return ipc_endpoint_error_to_ipc(endpoint_error);
    }
    error = ipc_resolve_held_thread(objects, sender_handle, sender);
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
        return ipc_endpoint_error_to_ipc(endpoint_error);
    }
    *source = &registry->endpoints[(*sender)->owner.slot];
    if (
        (*source)->state != MICROS_ENDPOINT_STATE_ACTIVE
        || (*source)->owner.generation != (*sender)->owner.generation
    ) {
        return MICROS_IPC_ERROR_STATE;
    }
    *destination =
        &registry->endpoints[resolved_destination->owner.slot];
    return MICROS_IPC_OK;
}

static void commit_sender_enqueue(
    struct micros_thread *sender,
    struct micros_endpoint_record *destination,
    const struct micros_endpoint_record *source,
    struct micros_thread_handle sender_handle,
    struct micros_thread *previous_tail,
    micros_endpoint_t destination_endpoint,
    const struct micros_ipc_message *message,
    uint64_t reply_token,
    uintptr_t reply_buffer
)
{
    sender->runtime_flags = MICROS_THREAD_RTS_IPC_SEND;
    if (reply_token != 0) {
        sender->runtime_flags |= MICROS_THREAD_RTS_IPC_REPLY;
    }
    sender->ipc_queue_kind = MICROS_IPC_QUEUE_SENDER;
    sender->ipc_next.slot = 0;
    sender->ipc_next.generation = 0;
    ipc_canonicalize_message(
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
    ipc_append_thread(
        sender_handle,
        previous_tail,
        &destination->sender_head,
        &destination->sender_tail
    );
}

enum micros_ipc_error ipc_sender_enqueue(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle sender_handle,
    micros_endpoint_t destination_endpoint,
    const struct micros_ipc_message *message,
    uint64_t reply_token,
    uintptr_t reply_buffer,
    bool allocates_reply_token
)
{
    const struct micros_endpoint_record *source;
    struct micros_endpoint_record *destination;
    struct micros_thread *sender;
    struct micros_thread *previous_tail;
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
    error = resolve_sender_destination(
        registry,
        objects,
        sender_handle,
        destination_endpoint,
        &sender,
        &source,
        &destination
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    if (
        reply_token != 0
        && (
            !allocates_reply_token
            || registry->last_reply_token == UINT64_MAX
            || reply_token != registry->last_reply_token + 1
        )
    ) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    if (!ipc_reply_token_is_available(objects, reply_token)) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    if (
        ipc_receiver_is_ready_for(
            destination,
            objects,
            source->value
        )
    ) {
        return MICROS_IPC_ERROR_NOT_READY;
    }
    error = ipc_resolve_queue_tail(
        objects,
        destination->sender_tail,
        &previous_tail
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }

    commit_sender_enqueue(
        sender,
        destination,
        source,
        sender_handle,
        previous_tail,
        destination_endpoint,
        message,
        reply_token,
        reply_buffer
    );
    return MICROS_IPC_OK;
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
    return ipc_sender_enqueue(
        registry,
        objects,
        sender_handle,
        destination_endpoint,
        message,
        reply_token,
        reply_buffer,
        false
    );
}

static enum micros_ipc_error resolve_receive_destination(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle receiver_handle,
    micros_endpoint_t source_endpoint,
    struct micros_thread **receiver,
    struct micros_endpoint_record **receiver_endpoint
)
{
    const struct micros_endpoint_record *resolved_source;
    enum micros_endpoint_error endpoint_error;
    enum micros_ipc_error error;

    endpoint_error =
        micros_endpoint_registry_validate_objects(registry, objects);
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return ipc_endpoint_error_to_ipc(endpoint_error);
    }
    error = ipc_resolve_held_thread(objects, receiver_handle, receiver);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    *receiver_endpoint =
        &registry->endpoints[(*receiver)->owner.slot];
    if (
        (*receiver_endpoint)->state != MICROS_ENDPOINT_STATE_ACTIVE
        || (*receiver_endpoint)->owner.generation
            != (*receiver)->owner.generation
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
            return ipc_endpoint_error_to_ipc(endpoint_error);
        }
        (void)resolved_source;
    }
    return MICROS_IPC_OK;
}

enum micros_ipc_error micros_ipc_receiver_commit_delivery(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle receiver_handle,
    micros_endpoint_t source_endpoint,
    uintptr_t receive_buffer,
    struct micros_thread_handle *matched_sender
)
{
    struct micros_endpoint_record *receiver_endpoint;
    struct ipc_pending_notification_plan notification;
    struct micros_thread_handle previous;
    struct micros_thread_handle sender_handle;
    struct micros_thread *receiver;
    struct micros_thread *sender;
    struct micros_ipc_message staged_message;
    enum micros_kernel_object_error scheduler_error;
    enum micros_ipc_error error;

    if (
        registry == NULL
        || objects == NULL
        || matched_sender == NULL
        || receive_buffer == 0
        || receive_buffer % 8 != 0
        || source_endpoint == MICROS_ENDPOINT_NONE
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    error = resolve_receive_destination(
        registry,
        objects,
        receiver_handle,
        source_endpoint,
        &receiver,
        &receiver_endpoint
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = ipc_find_pending_notification(
        registry,
        objects,
        receiver_endpoint,
        source_endpoint,
        &notification
    );
    if (error == MICROS_IPC_OK) {
        scheduler_error = micros_scheduler_commit_ipc_wake(
            objects,
            receiver_handle,
            MICROS_THREAD_RTS_INACTIVE
        );
        if (scheduler_error != MICROS_KERNEL_OBJECT_OK) {
            return ipc_scheduler_error_to_ipc(scheduler_error);
        }

        ipc_consume_pending_notification(
            receiver_endpoint,
            &notification
        );
        ipc_stage_delivery(
            receiver,
            receive_buffer,
            &notification.message
        );
        matched_sender->slot = 0;
        matched_sender->generation = 0;
        return MICROS_IPC_OK;
    }
    if (error != MICROS_IPC_ERROR_NOT_READY) {
        return error;
    }
    if (
        !ipc_find_matching_sender(
            receiver_endpoint,
            objects,
            source_endpoint,
            &previous,
            &sender_handle,
            &sender
        )
    ) {
        return MICROS_IPC_ERROR_NOT_READY;
    }
    staged_message = sender->ipc_outbound_message;
    scheduler_error = micros_scheduler_commit_ipc_wake_pair(
        objects,
        receiver_handle,
        MICROS_THREAD_RTS_INACTIVE,
        sender_handle,
        MICROS_THREAD_RTS_IPC_SEND
    );
    if (scheduler_error != MICROS_KERNEL_OBJECT_OK) {
        return ipc_scheduler_error_to_ipc(scheduler_error);
    }

    ipc_unlink_thread(
        objects,
        previous,
        sender_handle,
        &receiver_endpoint->sender_head,
        &receiver_endpoint->sender_tail
    );
    ipc_clear_delivered_sender(sender);
    ipc_stage_delivery(receiver, receive_buffer, &staged_message);
    *matched_sender = sender_handle;
    return MICROS_IPC_OK;
}

static void publish_sender_delivery(
    struct micros_kernel_objects *objects,
    struct micros_endpoint_record *destination,
    struct micros_thread *sender,
    struct micros_thread *receiver,
    struct micros_thread_handle previous,
    struct micros_thread_handle receiver_handle,
    micros_endpoint_t destination_endpoint,
    const struct micros_ipc_message *staged_message,
    uint64_t reply_token,
    uintptr_t reply_buffer,
    uintptr_t receive_buffer
)
{
    ipc_unlink_thread(
        objects,
        previous,
        receiver_handle,
        &destination->receiver_head,
        &destination->receiver_tail
    );
    receiver->ipc_queue_kind = MICROS_IPC_QUEUE_NONE;
    receiver->ipc_next.slot = 0;
    receiver->ipc_next.generation = 0;
    receiver->ipc_receive_source = 0;
    ipc_stage_delivery(receiver, receive_buffer, staged_message);
    if (reply_token != 0) {
        sender->ipc_receive_buffer = reply_buffer;
        sender->ipc_reply_token = reply_token;
        sender->ipc_reply_callee = destination_endpoint;
    }
}

static enum micros_ipc_error select_delivery_receiver(
    const struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    const struct micros_endpoint_record *destination,
    micros_endpoint_t source_endpoint,
    uint64_t reply_token,
    struct micros_thread_handle *previous,
    struct micros_thread_handle *receiver_handle,
    struct micros_thread **receiver
)
{
    if (
        reply_token != 0
        && (
            registry->last_reply_token == UINT64_MAX
            || reply_token != registry->last_reply_token + 1
        )
    ) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    if (!ipc_reply_token_is_available(objects, reply_token)) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    if (
        !ipc_find_matching_receiver(
            destination,
            objects,
            source_endpoint,
            previous,
            receiver_handle,
            receiver
        )
    ) {
        return MICROS_IPC_ERROR_NOT_READY;
    }
    if (
        (
            (*receiver)->runtime_flags
            & MICROS_THREAD_RTS_IPC_MASK
        ) != MICROS_THREAD_RTS_IPC_RECEIVE
    ) {
        return MICROS_IPC_ERROR_STATE;
    }
    return MICROS_IPC_OK;
}

enum micros_ipc_error ipc_sender_commit_delivery(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle sender_handle,
    micros_endpoint_t destination_endpoint,
    const struct micros_ipc_message *message,
    uint64_t reply_token,
    uintptr_t reply_buffer,
    struct micros_thread_handle *matched_receiver
)
{
    const struct micros_endpoint_record *source;
    struct micros_endpoint_record *destination;
    struct micros_thread_handle previous;
    struct micros_thread_handle receiver_handle;
    struct micros_thread *sender;
    struct micros_thread *receiver;
    struct micros_ipc_message staged_message;
    uintptr_t receive_buffer;
    enum micros_kernel_object_error scheduler_error;
    enum micros_ipc_error error;

    if (
        registry == NULL
        || objects == NULL
        || message == NULL
        || matched_receiver == NULL
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
    error = resolve_sender_destination(
        registry,
        objects,
        sender_handle,
        destination_endpoint,
        &sender,
        &source,
        &destination
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = select_delivery_receiver(
        registry,
        objects,
        destination,
        source->value,
        reply_token,
        &previous,
        &receiver_handle,
        &receiver
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    receive_buffer = receiver->ipc_receive_buffer;
    ipc_canonicalize_message(
        &staged_message,
        message,
        source->value,
        reply_token
    );
    scheduler_error =
        reply_token == 0
            ? micros_scheduler_commit_ipc_wake_pair(
                objects,
                sender_handle,
                MICROS_THREAD_RTS_INACTIVE,
                receiver_handle,
                MICROS_THREAD_RTS_IPC_RECEIVE
            )
            : micros_scheduler_commit_ipc_call_delivery(
                objects,
                sender_handle,
                receiver_handle
            );
    if (scheduler_error != MICROS_KERNEL_OBJECT_OK) {
        return ipc_scheduler_error_to_ipc(scheduler_error);
    }

    publish_sender_delivery(
        objects,
        destination,
        sender,
        receiver,
        previous,
        receiver_handle,
        destination_endpoint,
        &staged_message,
        reply_token,
        reply_buffer,
        receive_buffer
    );
    *matched_receiver = receiver_handle;
    return MICROS_IPC_OK;
}

enum micros_ipc_error micros_ipc_sender_commit_delivery(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle sender_handle,
    micros_endpoint_t destination_endpoint,
    const struct micros_ipc_message *message,
    struct micros_thread_handle *matched_receiver
)
{
    return ipc_sender_commit_delivery(
        registry,
        objects,
        sender_handle,
        destination_endpoint,
        message,
        0,
        0,
        matched_receiver
    );
}

enum micros_ipc_error micros_ipc_receiver_enqueue(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle receiver_handle,
    micros_endpoint_t source_endpoint,
    uintptr_t receive_buffer
)
{
    struct micros_endpoint_record *receiver_endpoint;
    struct ipc_pending_notification_plan notification;
    struct micros_thread *receiver;
    struct micros_thread *previous_tail;
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
    error = resolve_receive_destination(
        registry,
        objects,
        receiver_handle,
        source_endpoint,
        &receiver,
        &receiver_endpoint
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = ipc_find_pending_notification(
        registry,
        objects,
        receiver_endpoint,
        source_endpoint,
        &notification
    );
    if (error == MICROS_IPC_OK) {
        return MICROS_IPC_ERROR_NOT_READY;
    }
    if (error != MICROS_IPC_ERROR_NOT_READY) {
        return error;
    }
    if (
        ipc_sender_is_ready_for(
            receiver_endpoint,
            objects,
            source_endpoint
        )
    ) {
        return MICROS_IPC_ERROR_NOT_READY;
    }
    error = ipc_resolve_queue_tail(
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
    ipc_append_thread(
        receiver_handle,
        previous_tail,
        &receiver_endpoint->receiver_head,
        &receiver_endpoint->receiver_tail
    );
    return MICROS_IPC_OK;
}
