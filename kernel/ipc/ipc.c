#include "kernel/ipc/ipc_internal.h"

static enum micros_ipc_error prepare_authorized_sender(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    micros_endpoint_t destination,
    uint32_t operation,
    struct ipc_participant *sender,
    struct micros_endpoint_selection *target
)
{
    enum micros_ipc_error error = ipc_admit_operation_thread(registry, objects, handle, sender);

    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = ipc_admit_active_endpoint(registry, objects, destination, target);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    return ipc_endpoint_error_to_ipc(micros_privilege_profile_allows_target(
        registry, (uint8_t)sender->endpoint.process->privilege_profile, operation,
        (uint8_t)target->process->privilege_profile
    ));
}

static enum micros_ipc_error send_selected(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    const struct ipc_participant *sender,
    const struct micros_endpoint_selection *destination,
    const struct micros_ipc_message *message,
    uint64_t reply_token,
    uintptr_t reply_buffer
)
{
    struct ipc_queue_selection receiver;
    struct micros_thread *tail;
    enum micros_ipc_error error = ipc_select_queue(
        registry, objects, destination, MICROS_IPC_QUEUE_RECEIVER,
        sender->endpoint.record->value, &receiver
    );

    if (error == MICROS_IPC_OK) {
        return ipc_deliver_selected_sender(
            registry, objects, sender, destination, &receiver, message, reply_token, reply_buffer
        );
    }
    if (error != MICROS_IPC_ERROR_NOT_READY) {
        return error;
    }
    error = ipc_preflight_deadlock(
        registry, objects, sender->handle, destination->record->value,
        (struct micros_thread_handle){0, 0}
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = ipc_prepare_queue_append(registry, objects, destination, MICROS_IPC_QUEUE_SENDER, &tail);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    ipc_publish_sender_wait(registry, sender, destination, tail, message, reply_token, reply_buffer);
    return MICROS_IPC_OK;
}

enum micros_ipc_error micros_ipc_send(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    micros_endpoint_t destination,
    const struct micros_ipc_message *message
)
{
    struct ipc_participant sender;
    struct micros_endpoint_selection target;
    struct micros_ipc_message snapshot;
    enum micros_ipc_error error;

    if (registry == NULL || objects == NULL || message == NULL
        || (uintptr_t)message % _Alignof(struct micros_ipc_message) != 0
        || (message->type & MICROS_IPC_TYPE_KERNEL_MASK) != 0) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    error = prepare_authorized_sender(
        registry, objects, handle, destination, MICROS_PRIVILEGE_OPERATION_SEND, &sender, &target
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    ipc_canonicalize_message(&snapshot, message, sender.endpoint.record->value, 0);
    return send_selected(registry, objects, &sender, &target, &snapshot, 0, 0);
}

enum micros_ipc_error micros_ipc_receive(
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
    error = ipc_admit_operation_thread(registry, objects, handle, &receiver);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = ipc_endpoint_error_to_ipc(micros_privilege_profile_allows_operation(
        registry, (uint8_t)receiver.endpoint.process->privilege_profile, MICROS_PRIVILEGE_OPERATION_RECEIVE
    ));
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = ipc_select_receive(registry, objects, &receiver, source, receive_buffer, &selection);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    if (selection.choice != IPC_RECEIVE_WAIT) {
        return ipc_deliver_selected_receive(registry, objects, &selection);
    }
    error = ipc_preflight_deadlock(registry, objects, handle, source, (struct micros_thread_handle){0, 0});
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = ipc_prepare_queue_append(registry, objects, &receiver.endpoint, MICROS_IPC_QUEUE_RECEIVER, &tail);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    ipc_publish_receive_wait(registry, &selection, tail);
    return MICROS_IPC_OK;
}

enum micros_ipc_error micros_ipc_call(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    micros_endpoint_t destination,
    const struct micros_ipc_message *request,
    uintptr_t reply_buffer
)
{
    struct ipc_participant caller;
    struct micros_endpoint_selection target;
    struct micros_ipc_message snapshot;
    uint64_t reply_token;
    enum micros_ipc_error error;

    if (registry == NULL || objects == NULL || request == NULL
        || (uintptr_t)request % _Alignof(struct micros_ipc_message) != 0
        || reply_buffer == 0 || reply_buffer % _Alignof(struct micros_ipc_message) != 0
        || (request->type & MICROS_IPC_TYPE_KERNEL_MASK) != 0) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    error = prepare_authorized_sender(
        registry, objects, handle, destination, MICROS_PRIVILEGE_OPERATION_CALL, &caller, &target
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    if (!micros_endpoint_next_reply_token(registry->last_reply_token, caller.handle.slot, &reply_token)) {
        return MICROS_IPC_ERROR_REPLY_TOKEN_EXHAUSTED;
    }
    ipc_canonicalize_message(&snapshot, request, caller.endpoint.record->value, reply_token);
    error = send_selected(registry, objects, &caller, &target, &snapshot, reply_token, reply_buffer);
    if (error == MICROS_IPC_OK) {
        registry->last_reply_token = reply_token;
    }
    return error;
}
