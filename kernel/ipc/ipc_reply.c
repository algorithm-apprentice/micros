#include "kernel/ipc/ipc_internal.h"

#include "kernel/scheduler/scheduler_core_internal.h"

enum micros_ipc_error micros_ipc_reply(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle replier,
    uint64_t reply_token,
    const struct micros_ipc_message *message
)
{
    return micros_ipc_reply_expected_caller(
        registry, objects, replier, reply_token, MICROS_ENDPOINT_NONE, message
    );
}

enum micros_ipc_error micros_ipc_reply_expected_caller(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle replier,
    uint64_t reply_token,
    micros_endpoint_t expected_caller,
    const struct micros_ipc_message *message
)
{
    struct ipc_reply_preflight plan;
    enum micros_ipc_error error = ipc_preflight_reply(
        registry, objects, replier, reply_token, expected_caller, message,
        MICROS_PRIVILEGE_OPERATION_REPLY, &plan
    );
    enum micros_kernel_object_error scheduler_error;

    if (error != MICROS_IPC_OK) {
        return error;
    }
    scheduler_error = micros_scheduler_commit_ipc_wake_pair(
        objects, replier, MICROS_THREAD_RTS_INACTIVE,
        plan.caller_handle, MICROS_THREAD_RTS_IPC_REPLY
    );
    if (scheduler_error != MICROS_KERNEL_OBJECT_OK) {
        return ipc_scheduler_error_to_ipc(scheduler_error);
    }
    ipc_commit_reply_delivery(&plan);
    return MICROS_IPC_OK;
}

struct reply_receive_selection {
    struct ipc_receive_selection receive;
    struct micros_thread *previous_tail;
    struct micros_ipc_message incoming_message;
};

_Static_assert(sizeof(struct reply_receive_selection) <= 256, "reply/receive selection is bounded");

static enum micros_ipc_error commit_reply_receive_scheduler(
    const struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    const struct ipc_reply_preflight *reply,
    struct reply_receive_selection *selection
)
{
    const struct ipc_receive_selection *receive = &selection->receive;
    enum micros_kernel_object_error scheduler_error;
    enum micros_ipc_error error;

    if (receive->choice == IPC_RECEIVE_NOTIFICATION) {
        scheduler_error = micros_scheduler_commit_ipc_wake_pair(
            objects, reply->caller_handle, MICROS_THREAD_RTS_IPC_REPLY,
            receive->receiver.handle, MICROS_THREAD_RTS_INACTIVE
        );
    } else if (receive->choice == IPC_RECEIVE_SENDER) {
        selection->incoming_message = receive->sender.thread->ipc_outbound_message;
        scheduler_error = micros_scheduler_commit_ipc_reply_receive_delivery(
            objects, reply->caller_handle, receive->receiver.handle, receive->sender.handle
        );
    } else {
        error = ipc_prepare_queue_append(
            registry, objects, &receive->receiver.endpoint,
            MICROS_IPC_QUEUE_RECEIVER, &selection->previous_tail
        );
        if (error != MICROS_IPC_OK) {
            return error;
        }
        error = ipc_preflight_deadlock(
            registry, objects, receive->receiver.handle, receive->source, reply->caller_handle
        );
        if (error != MICROS_IPC_OK) {
            return error;
        }
        scheduler_error = micros_scheduler_commit_ipc_reply_receive_wait(
            objects, reply->caller_handle, receive->receiver.handle
        );
    }
    return ipc_scheduler_error_to_ipc(scheduler_error);
}

static void publish_reply_receive(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    const struct ipc_reply_preflight *reply,
    const struct reply_receive_selection *selection
)
{
    const struct ipc_receive_selection *receive = &selection->receive;
    struct micros_endpoint_record *endpoint =
        &registry->endpoints[receive->receiver.endpoint.record->owner.slot];
    struct micros_thread *replier = receive->receiver.thread;

    ipc_commit_reply_delivery(reply);
    if (receive->choice == IPC_RECEIVE_NOTIFICATION) {
        ipc_consume_pending_notification(endpoint, &receive->notification);
        ipc_stage_delivery(replier, receive->receive_buffer, &receive->notification.message);
    } else if (receive->choice == IPC_RECEIVE_SENDER) {
        struct micros_thread *sender = &objects->threads[receive->sender.handle.slot];

        ipc_unlink_thread(
            objects, receive->sender.previous, receive->sender.handle,
            &endpoint->sender_head, NULL
        );
        ipc_clear_delivered_sender(sender);
        ipc_stage_delivery(replier, receive->receive_buffer, &selection->incoming_message);
        if (selection->incoming_message.reply_token != 0) {
            sender->ipc_reply_phase = MICROS_IPC_REPLY_PHASE_STAGED;
            sender->ipc_request_receiver = receive->receiver.handle;
        }
    } else {
        replier->ipc_queue_kind = MICROS_IPC_QUEUE_RECEIVER;
        replier->ipc_next = (struct micros_thread_handle){0, 0};
        replier->ipc_receive_source = receive->source;
        replier->ipc_receive_buffer = receive->receive_buffer;
        ipc_append_thread(
            receive->receiver.handle, selection->previous_tail,
            &endpoint->receiver_head, &endpoint->receiver_tail
        );
    }
}

enum micros_ipc_error micros_ipc_reply_receive(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle replier,
    uint64_t reply_token,
    const struct micros_ipc_message *reply_message,
    micros_endpoint_t source,
    uintptr_t receive_buffer
)
{
    struct ipc_reply_preflight reply;
    struct reply_receive_selection selection;
    enum micros_ipc_error error;

    if (registry == NULL || objects == NULL || reply_message == NULL
        || receive_buffer == 0 || receive_buffer % 8 != 0 || source == MICROS_ENDPOINT_NONE) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    error = ipc_preflight_reply(
        registry, objects, replier, reply_token, MICROS_ENDPOINT_NONE, reply_message,
        MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE, &reply
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = ipc_select_receive(
        registry, objects, &reply.replier, source, receive_buffer, &selection.receive
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = commit_reply_receive_scheduler(registry, objects, &reply, &selection);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    publish_reply_receive(registry, objects, &reply, &selection);
    return MICROS_IPC_OK;
}
