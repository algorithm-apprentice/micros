#include "ipc_internal.h"

#include <stddef.h>

#include "endpoint_internal.h"
#include "scheduler_core_internal.h"

enum micros_ipc_error micros_ipc_send(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle sender_handle,
    micros_endpoint_t destination_endpoint,
    const struct micros_ipc_message *message
)
{
    struct micros_thread_handle matched_receiver = {0, 0};
    struct micros_ipc_message snapshot;
    micros_endpoint_t source_endpoint;
    enum micros_endpoint_error endpoint_error;
    enum micros_ipc_error error;

    if (
        registry == NULL
        || objects == NULL
        || message == NULL
        || (uintptr_t)message % _Alignof(struct micros_ipc_message)
            != 0
        || (
            message->type
            & MICROS_IPC_TYPE_KERNEL_MASK
        ) != 0
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    endpoint_error =
        micros_endpoint_registry_validate_objects(registry, objects);
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return ipc_endpoint_error_to_ipc(endpoint_error);
    }
    error = ipc_resolve_operation_thread(
        objects,
        sender_handle,
        &source_endpoint
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    endpoint_error = micros_endpoint_authorize_target(
        registry,
        objects,
        source_endpoint,
        MICROS_PRIVILEGE_OPERATION_SEND,
        destination_endpoint
    );
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return ipc_endpoint_error_to_ipc(endpoint_error);
    }
    ipc_canonicalize_message(
        &snapshot,
        message,
        source_endpoint,
        0
    );
    error = micros_ipc_sender_commit_delivery(
        registry,
        objects,
        sender_handle,
        destination_endpoint,
        &snapshot,
        &matched_receiver
    );
    if (error != MICROS_IPC_ERROR_NOT_READY) {
        return error;
    }
    error = ipc_preflight_deadlock(
        registry,
        objects,
        sender_handle,
        destination_endpoint,
        (struct micros_thread_handle){0, 0}
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = micros_ipc_sender_enqueue(
        registry,
        objects,
        sender_handle,
        destination_endpoint,
        &snapshot,
        0,
        0
    );
    return error == MICROS_IPC_ERROR_NOT_READY
        ? MICROS_IPC_ERROR_INVARIANT
        : error;
}

enum micros_ipc_error micros_ipc_receive(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle receiver_handle,
    micros_endpoint_t source_endpoint,
    uintptr_t receive_buffer
)
{
    struct micros_thread_handle matched_sender = {0, 0};
    micros_endpoint_t receiver_endpoint;
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
        return ipc_endpoint_error_to_ipc(endpoint_error);
    }
    error = ipc_resolve_operation_thread(
        objects,
        receiver_handle,
        &receiver_endpoint
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    endpoint_error = micros_endpoint_authorize_operation(
        registry,
        objects,
        receiver_endpoint,
        MICROS_PRIVILEGE_OPERATION_RECEIVE
    );
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return ipc_endpoint_error_to_ipc(endpoint_error);
    }
    error = micros_ipc_receiver_commit_delivery(
        registry,
        objects,
        receiver_handle,
        source_endpoint,
        receive_buffer,
        &matched_sender
    );
    if (error != MICROS_IPC_ERROR_NOT_READY) {
        return error;
    }
    error = ipc_preflight_deadlock(
        registry,
        objects,
        receiver_handle,
        source_endpoint,
        (struct micros_thread_handle){0, 0}
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = micros_ipc_receiver_enqueue(
        registry,
        objects,
        receiver_handle,
        source_endpoint,
        receive_buffer
    );
    return error == MICROS_IPC_ERROR_NOT_READY
        ? MICROS_IPC_ERROR_INVARIANT
        : error;
}

enum micros_ipc_error micros_ipc_call(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle caller_handle,
    micros_endpoint_t destination_endpoint,
    const struct micros_ipc_message *request,
    uintptr_t reply_buffer
)
{
    struct micros_thread_handle matched_receiver = {0, 0};
    struct micros_ipc_message snapshot;
    micros_endpoint_t source_endpoint;
    uint64_t reply_token;
    enum micros_endpoint_error endpoint_error;
    enum micros_ipc_error error;

    if (
        registry == NULL
        || objects == NULL
        || request == NULL
        || (uintptr_t)request % _Alignof(struct micros_ipc_message)
            != 0
        || reply_buffer == 0
        || reply_buffer % _Alignof(struct micros_ipc_message) != 0
        || (
            request->type
            & MICROS_IPC_TYPE_KERNEL_MASK
        ) != 0
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    endpoint_error =
        micros_endpoint_registry_validate_objects(registry, objects);
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return ipc_endpoint_error_to_ipc(endpoint_error);
    }
    error = ipc_resolve_operation_thread(
        objects,
        caller_handle,
        &source_endpoint
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    endpoint_error = micros_endpoint_authorize_target(
        registry,
        objects,
        source_endpoint,
        MICROS_PRIVILEGE_OPERATION_CALL,
        destination_endpoint
    );
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return ipc_endpoint_error_to_ipc(endpoint_error);
    }
    if (registry->last_reply_token == UINT64_MAX) {
        return MICROS_IPC_ERROR_REPLY_TOKEN_EXHAUSTED;
    }
    reply_token = registry->last_reply_token + 1;
    ipc_canonicalize_message(
        &snapshot,
        request,
        source_endpoint,
        reply_token
    );
    error = ipc_sender_commit_delivery(
        registry,
        objects,
        caller_handle,
        destination_endpoint,
        &snapshot,
        reply_token,
        reply_buffer,
        &matched_receiver
    );
    if (error == MICROS_IPC_OK) {
        registry->last_reply_token = reply_token;
        return MICROS_IPC_OK;
    }
    if (error != MICROS_IPC_ERROR_NOT_READY) {
        return error;
    }
    error = ipc_preflight_deadlock(
        registry,
        objects,
        caller_handle,
        destination_endpoint,
        (struct micros_thread_handle){0, 0}
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = ipc_sender_enqueue(
        registry,
        objects,
        caller_handle,
        destination_endpoint,
        &snapshot,
        reply_token,
        reply_buffer,
        true
    );
    if (error == MICROS_IPC_OK) {
        registry->last_reply_token = reply_token;
        return MICROS_IPC_OK;
    }
    return error == MICROS_IPC_ERROR_NOT_READY
        ? MICROS_IPC_ERROR_INVARIANT
        : error;
}
