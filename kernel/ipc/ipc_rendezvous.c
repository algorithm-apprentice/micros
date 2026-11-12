#include "kernel/ipc/ipc_internal.h"

#include "kernel/scheduler/scheduler_core_internal.h"

static enum micros_ipc_error resolve_sender_destination(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    micros_endpoint_t destination,
    struct ipc_participant *sender,
    struct micros_endpoint_selection *target
)
{
    enum micros_ipc_error error = ipc_admit_held_thread(registry, objects, handle, sender);

    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = ipc_admit_active_endpoint(registry, objects, destination, target);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    return sender->endpoint.record->state == MICROS_ENDPOINT_STATE_ACTIVE
        ? MICROS_IPC_OK : MICROS_IPC_ERROR_STATE;
}

static enum micros_ipc_error resolve_receive_destination(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    struct ipc_participant *receiver
)
{
    enum micros_ipc_error error = ipc_admit_held_thread(registry, objects, handle, receiver);

    if (error != MICROS_IPC_OK) {
        return error;
    }
    return receiver->endpoint.record->state == MICROS_ENDPOINT_STATE_ACTIVE
        ? MICROS_IPC_OK : MICROS_IPC_ERROR_STATE;
}

void ipc_publish_sender_wait(
    struct micros_endpoint_registry *registry,
    const struct ipc_participant *sender,
    const struct micros_endpoint_selection *destination,
    struct micros_thread *tail,
    const struct micros_ipc_message *message,
    uint64_t reply_token,
    uintptr_t reply_buffer
)
{
    struct micros_endpoint_record *target =
        &registry->endpoints[destination->record->owner.slot];
    struct micros_thread *thread = sender->thread;

    thread->runtime_flags = MICROS_THREAD_RTS_IPC_SEND
        | (reply_token == 0 ? 0 : MICROS_THREAD_RTS_IPC_REPLY);
    thread->ipc_queue_kind = MICROS_IPC_QUEUE_SENDER;
    thread->ipc_next = (struct micros_thread_handle){0, 0};
    thread->ipc_outbound_message = *message;
    thread->ipc_send_destination = destination->record->value;
    thread->ipc_receive_buffer = reply_buffer;
    thread->ipc_reply_token = reply_token;
    thread->ipc_reply_callee = reply_token == 0 ? 0 : destination->record->value;
    thread->ipc_reply_phase = reply_token == 0 ? MICROS_IPC_REPLY_PHASE_NONE : MICROS_IPC_REPLY_PHASE_QUEUED;
    thread->ipc_request_receiver = (struct micros_thread_handle){0, 0};
    ipc_append_thread(sender->handle, tail, &target->sender_head, &target->sender_tail);
}

enum micros_ipc_error ipc_deliver_selected_sender(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    const struct ipc_participant *sender,
    const struct micros_endpoint_selection *destination,
    const struct ipc_queue_selection *selected,
    const struct micros_ipc_message *message,
    uint64_t reply_token,
    uintptr_t reply_buffer
)
{
    struct micros_endpoint_record *target =
        &registry->endpoints[destination->record->owner.slot];
    struct micros_thread *receiver = &objects->threads[selected->handle.slot];
    uintptr_t receive_buffer = receiver->ipc_receive_buffer;
    enum micros_kernel_object_error error = reply_token == 0
        ? micros_scheduler_commit_ipc_wake_pair(
            objects, sender->handle, MICROS_THREAD_RTS_INACTIVE,
            selected->handle, MICROS_THREAD_RTS_IPC_RECEIVE
        )
        : micros_scheduler_commit_ipc_call_delivery(objects, sender->handle, selected->handle);

    if (error != MICROS_KERNEL_OBJECT_OK) {
        return ipc_scheduler_error_to_ipc(error);
    }
    ipc_unlink_thread(
        objects, selected->previous, selected->handle, &target->receiver_head, &target->receiver_tail
    );
    receiver->ipc_queue_kind = MICROS_IPC_QUEUE_NONE;
    receiver->ipc_next = (struct micros_thread_handle){0, 0};
    receiver->ipc_receive_source = 0;
    ipc_stage_delivery(receiver, receive_buffer, message);
    if (reply_token != 0) {
        sender->thread->ipc_receive_buffer = reply_buffer;
        sender->thread->ipc_reply_token = reply_token;
        sender->thread->ipc_reply_callee = destination->record->value;
        sender->thread->ipc_reply_phase = MICROS_IPC_REPLY_PHASE_STAGED;
        sender->thread->ipc_request_receiver = selected->handle;
    }
    return MICROS_IPC_OK;
}

enum micros_ipc_error micros_ipc_sender_enqueue(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    micros_endpoint_t destination,
    const struct micros_ipc_message *message,
    uint64_t reply_token,
    uintptr_t reply_buffer
)
{
    struct ipc_participant sender;
    struct micros_endpoint_selection target;
    struct ipc_queue_selection receiver;
    struct micros_thread *tail;
    struct micros_ipc_message snapshot;
    enum micros_ipc_error error;

    if (registry == NULL || objects == NULL || message == NULL
        || (message->type & MICROS_IPC_TYPE_KERNEL_MASK) != 0
        || (reply_token == 0 ? reply_buffer != 0 : reply_buffer == 0 || reply_buffer % 8 != 0)) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    error = resolve_sender_destination(registry, objects, handle, destination, &sender, &target);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    if (reply_token != 0) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    error = ipc_select_queue(
        registry, objects, &target, MICROS_IPC_QUEUE_RECEIVER, sender.endpoint.record->value, &receiver
    );
    if (error != MICROS_IPC_ERROR_NOT_READY) {
        return error == MICROS_IPC_OK ? MICROS_IPC_ERROR_NOT_READY : error;
    }
    error = ipc_prepare_queue_append(registry, objects, &target, MICROS_IPC_QUEUE_SENDER, &tail);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    ipc_canonicalize_message(&snapshot, message, sender.endpoint.record->value, 0);
    ipc_publish_sender_wait(registry, &sender, &target, tail, &snapshot, 0, 0);
    return MICROS_IPC_OK;
}

enum micros_ipc_error micros_ipc_sender_commit_delivery(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    micros_endpoint_t destination,
    const struct micros_ipc_message *message,
    struct micros_thread_handle *matched_receiver
)
{
    struct ipc_participant sender;
    struct micros_endpoint_selection target;
    struct ipc_queue_selection receiver;
    struct micros_ipc_message snapshot;
    enum micros_ipc_error error;

    if (registry == NULL || objects == NULL || message == NULL || matched_receiver == NULL
        || (message->type & MICROS_IPC_TYPE_KERNEL_MASK) != 0) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    error = resolve_sender_destination(registry, objects, handle, destination, &sender, &target);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = ipc_select_queue(
        registry, objects, &target, MICROS_IPC_QUEUE_RECEIVER, sender.endpoint.record->value, &receiver
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    ipc_canonicalize_message(&snapshot, message, sender.endpoint.record->value, 0);
    error = ipc_deliver_selected_sender(registry, objects, &sender, &target, &receiver, &snapshot, 0, 0);
    if (error == MICROS_IPC_OK) {
        *matched_receiver = receiver.handle;
    }
    return error;
}

enum micros_ipc_error ipc_select_receive(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct ipc_participant *receiver,
    micros_endpoint_t source,
    uintptr_t receive_buffer,
    struct ipc_receive_selection *selection
)
{
    struct ipc_receive_selection candidate;
    enum micros_ipc_error error;

    ipc_clear_bytes(&candidate, sizeof(candidate));
    candidate.receiver = *receiver;
    candidate.source = source;
    candidate.receive_buffer = receive_buffer;
    error = ipc_find_pending_notification(
        registry, objects, receiver->endpoint.record, source, &candidate.notification
    );
    if (error == MICROS_IPC_OK) {
        candidate.choice = IPC_RECEIVE_NOTIFICATION;
    } else if (error == MICROS_IPC_ERROR_NOT_READY) {
        error = ipc_select_queue(
            registry, objects, &receiver->endpoint, MICROS_IPC_QUEUE_SENDER, source, &candidate.sender
        );
        if (error == MICROS_IPC_OK) {
            candidate.choice = IPC_RECEIVE_SENDER;
        } else if (error != MICROS_IPC_ERROR_NOT_READY) {
            return error;
        }
    } else {
        return error;
    }
    ipc_copy_bytes(selection, &candidate, sizeof(*selection));
    return MICROS_IPC_OK;
}

void ipc_publish_receive_wait(
    struct micros_endpoint_registry *registry,
    const struct ipc_receive_selection *selection,
    struct micros_thread *tail
)
{
    struct micros_endpoint_record *endpoint =
        &registry->endpoints[selection->receiver.endpoint.record->owner.slot];
    struct micros_thread *receiver = selection->receiver.thread;

    receiver->runtime_flags = MICROS_THREAD_RTS_IPC_RECEIVE;
    receiver->ipc_queue_kind = MICROS_IPC_QUEUE_RECEIVER;
    receiver->ipc_next = (struct micros_thread_handle){0, 0};
    receiver->ipc_receive_source = selection->source;
    receiver->ipc_receive_buffer = selection->receive_buffer;
    ipc_append_thread(selection->receiver.handle, tail, &endpoint->receiver_head, &endpoint->receiver_tail);
}

enum micros_ipc_error ipc_deliver_selected_receive(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    const struct ipc_receive_selection *selection
)
{
    struct micros_endpoint_record *endpoint =
        &registry->endpoints[selection->receiver.endpoint.record->owner.slot];
    enum micros_kernel_object_error error;

    if (selection->choice == IPC_RECEIVE_NOTIFICATION) {
        error = micros_scheduler_commit_ipc_wake(
            objects, selection->receiver.handle, MICROS_THREAD_RTS_INACTIVE
        );
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return ipc_scheduler_error_to_ipc(error);
        }
        ipc_consume_pending_notification(endpoint, &selection->notification);
        ipc_stage_delivery(
            selection->receiver.thread, selection->receive_buffer, &selection->notification.message
        );
    } else {
        struct micros_thread *sender = &objects->threads[selection->sender.handle.slot];
        struct micros_ipc_message message = sender->ipc_outbound_message;

        error = micros_scheduler_commit_ipc_wake_pair(
            objects, selection->receiver.handle, MICROS_THREAD_RTS_INACTIVE,
            selection->sender.handle, MICROS_THREAD_RTS_IPC_SEND
        );
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return ipc_scheduler_error_to_ipc(error);
        }
        ipc_unlink_thread(
            objects, selection->sender.previous, selection->sender.handle,
            &endpoint->sender_head, &endpoint->sender_tail
        );
        ipc_clear_delivered_sender(sender);
        ipc_stage_delivery(selection->receiver.thread, selection->receive_buffer, &message);
        if (message.reply_token != 0) {
            sender->ipc_reply_phase = MICROS_IPC_REPLY_PHASE_STAGED;
            sender->ipc_request_receiver = selection->receiver.handle;
        }
    }
    return MICROS_IPC_OK;
}

enum micros_ipc_error micros_ipc_receiver_commit_delivery(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    micros_endpoint_t source,
    uintptr_t receive_buffer,
    struct micros_thread_handle *matched_sender
)
{
    struct ipc_participant receiver;
    struct ipc_receive_selection selection;
    enum micros_ipc_error error;

    if (registry == NULL || objects == NULL || matched_sender == NULL
        || receive_buffer == 0 || receive_buffer % 8 != 0 || source == MICROS_ENDPOINT_NONE) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    error = resolve_receive_destination(registry, objects, handle, &receiver);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = ipc_select_receive(registry, objects, &receiver, source, receive_buffer, &selection);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    if (selection.choice == IPC_RECEIVE_WAIT) {
        return MICROS_IPC_ERROR_NOT_READY;
    }
    error = ipc_deliver_selected_receive(registry, objects, &selection);
    if (error == MICROS_IPC_OK) {
        *matched_sender = selection.choice == IPC_RECEIVE_SENDER
            ? selection.sender.handle : (struct micros_thread_handle){0, 0};
    }
    return error;
}

enum micros_ipc_error micros_ipc_receiver_enqueue(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    micros_endpoint_t source,
    uintptr_t receive_buffer
)
{
    struct ipc_participant receiver;
    struct ipc_receive_selection selection;
    struct micros_thread *tail;
    enum micros_ipc_error error;

    if (registry == NULL || objects == NULL || receive_buffer == 0
        || receive_buffer % 8 != 0 || source == MICROS_ENDPOINT_NONE) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    error = resolve_receive_destination(registry, objects, handle, &receiver);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = ipc_select_receive(registry, objects, &receiver, source, receive_buffer, &selection);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    if (selection.choice != IPC_RECEIVE_WAIT) {
        return MICROS_IPC_ERROR_NOT_READY;
    }
    error = ipc_prepare_queue_append(
        registry, objects, &receiver.endpoint, MICROS_IPC_QUEUE_RECEIVER, &tail
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    ipc_publish_receive_wait(registry, &selection, tail);
    return MICROS_IPC_OK;
}
