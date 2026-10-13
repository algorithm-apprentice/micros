#include "ipc_internal.h"

#include "scheduler_core_internal.h"

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

struct reply_receive_selection {
    struct micros_endpoint_record *receiver_endpoint;
    struct ipc_pending_notification_plan notification;
    struct micros_thread_handle previous;
    struct micros_thread_handle sender_handle;
    struct micros_thread *replier;
    struct micros_thread *sender;
    struct micros_thread *previous_tail;
    struct micros_ipc_message incoming_message;
    bool has_notification;
    bool has_sender;
};

static enum micros_ipc_error select_reply_receive_input(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle replier_handle,
    micros_endpoint_t source_endpoint,
    struct reply_receive_selection *selection
)
{
    const struct micros_endpoint_record *resolved_source;
    enum micros_endpoint_error endpoint_error;
    enum micros_ipc_error error;

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
    selection->replier = &objects->threads[replier_handle.slot];
    selection->receiver_endpoint =
        &registry->endpoints[selection->replier->owner.slot];
    error = ipc_find_pending_notification(
        registry,
        objects,
        selection->receiver_endpoint,
        source_endpoint,
        &selection->notification
    );
    selection->has_notification = error == MICROS_IPC_OK;
    if (!selection->has_notification && error != MICROS_IPC_ERROR_NOT_READY) {
        return error;
    }
    selection->has_sender = false;
    if (!selection->has_notification) {
        selection->has_sender = ipc_find_matching_sender(
            selection->receiver_endpoint,
            objects,
            source_endpoint,
            &selection->previous,
            &selection->sender_handle,
            &selection->sender
        );
    }
    return MICROS_IPC_OK;
}

static enum micros_ipc_error commit_reply_receive_scheduler(
    const struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle replier_handle,
    micros_endpoint_t source_endpoint,
    const struct ipc_reply_preflight *reply_plan,
    struct reply_receive_selection *selection
)
{
    enum micros_kernel_object_error scheduler_error;
    enum micros_ipc_error error;

    if (selection->has_notification) {
        scheduler_error = micros_scheduler_commit_ipc_wake_pair(
            objects,
            reply_plan->caller_handle,
            MICROS_THREAD_RTS_IPC_REPLY,
            replier_handle,
            MICROS_THREAD_RTS_INACTIVE
        );
    } else if (selection->has_sender) {
        selection->incoming_message = selection->sender->ipc_outbound_message;
        scheduler_error =
            micros_scheduler_commit_ipc_reply_receive_delivery(
                objects,
                reply_plan->caller_handle,
                replier_handle,
                selection->sender_handle
            );
    } else {
        error = ipc_resolve_queue_tail(
            objects,
            selection->receiver_endpoint->receiver_tail,
            &selection->previous_tail
        );
        if (error != MICROS_IPC_OK) {
            return error;
        }
        error = ipc_preflight_deadlock(
            registry,
            objects,
            replier_handle,
            source_endpoint,
            reply_plan->caller_handle
        );
        if (error != MICROS_IPC_OK) {
            return error;
        }
        scheduler_error =
            micros_scheduler_commit_ipc_reply_receive_wait(
                objects,
                reply_plan->caller_handle,
                replier_handle
            );
    }
    if (scheduler_error != MICROS_KERNEL_OBJECT_OK) {
        return ipc_scheduler_error_to_ipc(scheduler_error);
    }
    return MICROS_IPC_OK;
}

static void publish_reply_receive(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle replier_handle,
    micros_endpoint_t source_endpoint,
    uintptr_t receive_buffer,
    const struct ipc_reply_preflight *reply_plan,
    struct reply_receive_selection *selection
)
{
    ipc_commit_reply_delivery(reply_plan);
    if (selection->has_notification) {
        ipc_consume_pending_notification(
            selection->receiver_endpoint,
            &selection->notification
        );
        ipc_stage_delivery(
            selection->replier,
            receive_buffer,
            &selection->notification.message
        );
    } else if (selection->has_sender) {
        ipc_unlink_thread(
            objects,
            selection->previous,
            selection->sender_handle,
            &selection->receiver_endpoint->sender_head,
            &selection->receiver_endpoint->sender_tail
        );
        ipc_clear_delivered_sender(selection->sender);
        ipc_stage_delivery(
            selection->replier,
            receive_buffer,
            &selection->incoming_message
        );
    } else {
        selection->replier->ipc_queue_kind = MICROS_IPC_QUEUE_RECEIVER;
        selection->replier->ipc_next.slot = 0;
        selection->replier->ipc_next.generation = 0;
        selection->replier->ipc_receive_source = source_endpoint;
        selection->replier->ipc_receive_buffer = receive_buffer;
        ipc_append_thread(
            replier_handle,
            selection->previous_tail,
            &selection->receiver_endpoint->receiver_head,
            &selection->receiver_endpoint->receiver_tail
        );
    }
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
    struct reply_receive_selection selection;
    enum micros_ipc_error error;

    selection.previous = (struct micros_thread_handle){0, 0};
    selection.sender_handle = (struct micros_thread_handle){0, 0};
    selection.sender = NULL;
    selection.previous_tail = NULL;
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
    error = select_reply_receive_input(
        registry,
        objects,
        replier_handle,
        source_endpoint,
        &selection
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = commit_reply_receive_scheduler(
        registry,
        objects,
        replier_handle,
        source_endpoint,
        &reply_plan,
        &selection
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    publish_reply_receive(
        objects,
        replier_handle,
        source_endpoint,
        receive_buffer,
        &reply_plan,
        &selection
    );
    return MICROS_IPC_OK;
}
