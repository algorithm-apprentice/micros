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

enum micros_ipc_error micros_ipc_reply(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle replier_handle,
    uint64_t reply_token,
    const struct micros_ipc_message *message
)
{
    return micros_ipc_reply_expected_caller(
        registry,
        objects,
        replier_handle,
        reply_token,
        MICROS_ENDPOINT_NONE,
        message
    );
}

enum micros_ipc_error micros_ipc_reply_expected_caller(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle replier_handle,
    uint64_t reply_token,
    micros_endpoint_t expected_caller,
    const struct micros_ipc_message *message
)
{
    struct ipc_reply_preflight plan;
    enum micros_kernel_object_error scheduler_error;
    enum micros_ipc_error error;

    error = ipc_preflight_reply(
        registry,
        objects,
        replier_handle,
        reply_token,
        expected_caller,
        message,
        MICROS_PRIVILEGE_OPERATION_REPLY,
        &plan
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    scheduler_error = micros_scheduler_commit_ipc_wake_pair(
        objects,
        replier_handle,
        MICROS_THREAD_RTS_INACTIVE,
        plan.caller_handle,
        MICROS_THREAD_RTS_IPC_REPLY
    );
    if (scheduler_error != MICROS_KERNEL_OBJECT_OK) {
        return ipc_scheduler_error_to_ipc(scheduler_error);
    }

    ipc_commit_reply_delivery(&plan);
    return MICROS_IPC_OK;
}

enum micros_ipc_error micros_ipc_reply_receive(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle replier_handle,
    uint64_t reply_token,
    const struct micros_ipc_message *reply_message,
    micros_endpoint_t source_endpoint,
    uintptr_t receive_buffer
)
{
    struct ipc_reply_preflight reply_plan;
    const struct micros_endpoint_record *resolved_source;
    struct micros_endpoint_record *receiver_endpoint;
    struct ipc_pending_notification_plan notification;
    struct micros_thread_handle previous = {0, 0};
    struct micros_thread_handle sender_handle = {0, 0};
    struct micros_thread *replier;
    struct micros_thread *sender = NULL;
    struct micros_thread *previous_tail = NULL;
    struct micros_ipc_message incoming_message;
    enum micros_endpoint_error endpoint_error;
    enum micros_kernel_object_error scheduler_error;
    enum micros_ipc_error error;
    bool has_notification;
    bool has_sender;

    if (
        registry == NULL
        || objects == NULL
        || reply_message == NULL
        || receive_buffer == 0
        || receive_buffer % 8 != 0
        || source_endpoint == MICROS_ENDPOINT_NONE
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    error = ipc_preflight_reply(
        registry,
        objects,
        replier_handle,
        reply_token,
        MICROS_ENDPOINT_NONE,
        reply_message,
        MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
        &reply_plan
    );
    if (error != MICROS_IPC_OK) {
        return error;
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
    replier = &objects->threads[replier_handle.slot];
    receiver_endpoint =
        &registry->endpoints[replier->owner.slot];
    error = ipc_find_pending_notification(
        registry,
        objects,
        receiver_endpoint,
        source_endpoint,
        &notification
    );
    has_notification = error == MICROS_IPC_OK;
    if (!has_notification && error != MICROS_IPC_ERROR_NOT_READY) {
        return error;
    }
    has_sender = false;
    if (!has_notification) {
        has_sender = ipc_find_matching_sender(
            receiver_endpoint,
            objects,
            source_endpoint,
            &previous,
            &sender_handle,
            &sender
        );
    }
    if (has_notification) {
        scheduler_error = micros_scheduler_commit_ipc_wake_pair(
            objects,
            reply_plan.caller_handle,
            MICROS_THREAD_RTS_IPC_REPLY,
            replier_handle,
            MICROS_THREAD_RTS_INACTIVE
        );
    } else if (has_sender) {
        incoming_message = sender->ipc_outbound_message;
        scheduler_error =
            micros_scheduler_commit_ipc_reply_receive_delivery(
                objects,
                reply_plan.caller_handle,
                replier_handle,
                sender_handle
            );
    } else {
        error = ipc_resolve_queue_tail(
            objects,
            receiver_endpoint->receiver_tail,
            &previous_tail
        );
        if (error != MICROS_IPC_OK) {
            return error;
        }
        error = ipc_preflight_deadlock(
            registry,
            objects,
            replier_handle,
            source_endpoint,
            reply_plan.caller_handle
        );
        if (error != MICROS_IPC_OK) {
            return error;
        }
        scheduler_error =
            micros_scheduler_commit_ipc_reply_receive_wait(
                objects,
                reply_plan.caller_handle,
                replier_handle
            );
    }
    if (scheduler_error != MICROS_KERNEL_OBJECT_OK) {
        return ipc_scheduler_error_to_ipc(scheduler_error);
    }

    ipc_commit_reply_delivery(&reply_plan);
    if (has_notification) {
        ipc_consume_pending_notification(
            receiver_endpoint,
            &notification
        );
        ipc_stage_delivery(
            replier,
            receive_buffer,
            &notification.message
        );
    } else if (has_sender) {
        ipc_unlink_thread(
            objects,
            previous,
            sender_handle,
            &receiver_endpoint->sender_head,
            &receiver_endpoint->sender_tail
        );
        ipc_clear_delivered_sender(sender);
        ipc_stage_delivery(
            replier,
            receive_buffer,
            &incoming_message
        );
    } else {
        replier->ipc_queue_kind = MICROS_IPC_QUEUE_RECEIVER;
        replier->ipc_next.slot = 0;
        replier->ipc_next.generation = 0;
        replier->ipc_receive_source = source_endpoint;
        replier->ipc_receive_buffer = receive_buffer;
        ipc_append_thread(
            replier_handle,
            previous_tail,
            &receiver_endpoint->receiver_head,
            &receiver_endpoint->receiver_tail
        );
    }
    return MICROS_IPC_OK;
}
