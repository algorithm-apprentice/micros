#include "ipc_model_test_internal.h"

static enum micros_ipc_error deliver_model_send_to_receiver(
    struct model_state *model,
    struct model_endpoint *destination,
    size_t sender_slot,
    size_t receiver_slot,
    struct micros_thread_handle previous,
    const struct micros_ipc_message *snapshot
)
{
    enum micros_ipc_error error;

    struct model_thread *receiver =
        &model->threads[receiver_slot];
    const struct model_transition transitions[2] = {
        {
            .thread_slot = sender_slot,
            .clear_flags = MICROS_THREAD_RTS_INACTIVE,
        },
        {
            .thread_slot = receiver_slot,
            .clear_flags = MICROS_THREAD_RTS_IPC_RECEIVE,
        },
    };
    uintptr_t receive_buffer = receiver->ipc_receive_buffer;

    error = ipc_model_apply_transitions(
        model,
        transitions,
        sizeof(transitions) / sizeof(transitions[0])
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    ipc_model_ipc_unlink(
        model,
        &destination->receiver_head,
        &destination->receiver_tail,
        previous,
        receiver_slot
    );
    receiver->ipc_queue_kind = MICROS_IPC_QUEUE_NONE;
    receiver->ipc_next = ipc_model_null_thread_handle();
    receiver->ipc_receive_source = 0;
    ipc_model_stage_delivery(receiver, receive_buffer, snapshot);
    return MICROS_IPC_OK;
}

enum micros_ipc_error ipc_model_send(
    struct model_state *model,
    size_t sender_slot,
    micros_endpoint_t destination_endpoint,
    const struct micros_ipc_message *message
)
{
    struct model_thread *sender;
    struct model_endpoint *destination;
    struct micros_thread_handle previous = ipc_model_null_thread_handle();
    struct micros_ipc_message snapshot;
    micros_endpoint_t source_endpoint;
    size_t destination_slot;
    size_t receiver_slot;
    enum micros_ipc_error error;

    if (
        message == NULL
        || (uintptr_t)message % _Alignof(struct micros_ipc_message)
            != 0
        || (
            message->type
            & MICROS_IPC_TYPE_KERNEL_MASK
        ) != 0
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    error = ipc_model_resolve_operation_thread(
        model,
        sender_slot,
        &source_endpoint
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = ipc_model_authorize_target(
        model,
        source_endpoint,
        MICROS_PRIVILEGE_OPERATION_SEND,
        destination_endpoint
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    (void)ipc_model_resolve_active_endpoint(
        model,
        destination_endpoint,
        &destination_slot
    );
    sender = &model->threads[sender_slot];
    destination = &model->endpoints[destination_slot];
    ipc_model_canonicalize_message(
        &snapshot,
        message,
        source_endpoint,
        0
    );
    if (
        ipc_model_find_receiver(
            model,
            destination_slot,
            source_endpoint,
            &previous,
            &receiver_slot
        )
    ) {
        return deliver_model_send_to_receiver(
            model, destination, sender_slot, receiver_slot, previous, &snapshot
        );
    }
    error = ipc_model_deadlock_preflight(
        model,
        sender_slot,
        destination_endpoint,
        MICROS_THREAD_CAPACITY
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    sender->runtime_flags = MICROS_THREAD_RTS_IPC_SEND;
    sender->ipc_queue_kind = MICROS_IPC_QUEUE_SENDER;
    sender->ipc_outbound_message = snapshot;
    sender->ipc_send_destination = destination_endpoint;
    ipc_model_ipc_append(
        model,
        &destination->sender_head,
        &destination->sender_tail,
        sender_slot
    );
    return MICROS_IPC_OK;
}

static enum micros_ipc_error consume_model_pending_notification(
    struct model_state *model,
    struct model_endpoint *destination,
    struct model_thread *receiver,
    size_t receiver_slot,
    size_t notification_source_slot,
    uint64_t event_mask,
    uintptr_t receive_buffer
)
{
    struct micros_ipc_message incoming;
    enum micros_ipc_error error;

    const struct model_transition transition = {
        .thread_slot = receiver_slot,
        .clear_flags = MICROS_THREAD_RTS_INACTIVE,
    };

    ipc_model_canonicalize_notification(
        &incoming,
        notification_source_slot == MICROS_PROCESS_CAPACITY
            ? MICROS_ENDPOINT_NONE
            : model->endpoints[
                notification_source_slot
            ].value,
        event_mask
    );
    error = ipc_model_apply_transitions(model, &transition, 1);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    if (notification_source_slot == MICROS_PROCESS_CAPACITY) {
        destination->pending_kernel_events = 0;
    } else {
        destination->pending_events[notification_source_slot] = 0;
        destination->pending_notification_sources &=
            ~(UINT64_C(1) << notification_source_slot);
    }
    ipc_model_stage_delivery(receiver, receive_buffer, &incoming);
    return MICROS_IPC_OK;
}

static enum micros_ipc_error consume_model_queued_sender(
    struct model_state *model,
    struct model_endpoint *destination,
    struct model_thread *receiver,
    size_t receiver_slot,
    size_t sender_slot,
    struct micros_thread_handle previous,
    uintptr_t receive_buffer
)
{
    struct model_thread *sender = &model->threads[sender_slot];
    const struct model_transition transitions[2] = {
        {
            .thread_slot = receiver_slot,
            .clear_flags = MICROS_THREAD_RTS_INACTIVE,
        },
        {
            .thread_slot = sender_slot,
            .clear_flags = MICROS_THREAD_RTS_IPC_SEND,
        },
    };
    struct micros_ipc_message incoming;
    enum micros_ipc_error error;

    incoming = sender->ipc_outbound_message;
    error = ipc_model_apply_transitions(
        model,
        transitions,
        sizeof(transitions) / sizeof(transitions[0])
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    ipc_model_ipc_unlink(
        model,
        &destination->sender_head,
        &destination->sender_tail,
        previous,
        sender_slot
    );
    ipc_model_clear_delivered_sender(sender);
    ipc_model_stage_delivery(receiver, receive_buffer, &incoming);
    return MICROS_IPC_OK;
}

enum micros_ipc_error ipc_model_receive(
    struct model_state *model,
    size_t receiver_slot,
    micros_endpoint_t source_endpoint,
    uintptr_t receive_buffer
)
{
    struct model_thread *receiver;
    struct model_endpoint *destination;
    struct micros_thread_handle previous = ipc_model_null_thread_handle();
    micros_endpoint_t receiver_endpoint;
    size_t destination_slot;
    size_t notification_source_slot;
    size_t sender_slot;
    uint64_t event_mask;
    enum micros_ipc_error error;

    if (
        receive_buffer == 0
        || receive_buffer % 8 != 0
        || source_endpoint == MICROS_ENDPOINT_NONE
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    error = ipc_model_resolve_operation_thread(
        model,
        receiver_slot,
        &receiver_endpoint
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = ipc_model_authorize_operation(
        model,
        receiver_endpoint,
        MICROS_PRIVILEGE_OPERATION_RECEIVE
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    if (
        source_endpoint != MICROS_ENDPOINT_ANY
        && !ipc_model_resolve_active_endpoint(
            model,
            source_endpoint,
            &destination_slot
        )
    ) {
        return MICROS_IPC_ERROR_DEAD_ENDPOINT;
    }
    receiver = &model->threads[receiver_slot];
    destination_slot = receiver->owner.slot;
    destination = &model->endpoints[destination_slot];
    if (
        ipc_model_find_pending_notification(
            model,
            destination_slot,
            source_endpoint,
            &notification_source_slot,
            &event_mask
        )
    ) {
        return consume_model_pending_notification(
            model, destination, receiver, receiver_slot,
            notification_source_slot, event_mask, receive_buffer
        );
    }
    if (
        ipc_model_find_sender(
            model,
            destination_slot,
            source_endpoint,
            &previous,
            &sender_slot
        )
    ) {
        return consume_model_queued_sender(
            model,
            destination,
            receiver,
            receiver_slot,
            sender_slot,
            previous,
            receive_buffer
        );
    }
    error = ipc_model_deadlock_preflight(
        model,
        receiver_slot,
        source_endpoint,
        MICROS_THREAD_CAPACITY
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    receiver->runtime_flags = MICROS_THREAD_RTS_IPC_RECEIVE;
    receiver->ipc_queue_kind = MICROS_IPC_QUEUE_RECEIVER;
    receiver->ipc_receive_source = source_endpoint;
    receiver->ipc_receive_buffer = receive_buffer;
    ipc_model_ipc_append(
        model,
        &destination->receiver_head,
        &destination->receiver_tail,
        receiver_slot
    );
    return MICROS_IPC_OK;
}

static enum micros_ipc_error deliver_model_call_and_publish_token(
    struct model_state *model,
    struct model_endpoint *destination,
    struct model_thread *caller,
    size_t caller_slot,
    size_t receiver_slot,
    struct micros_thread_handle previous,
    const struct micros_ipc_message *snapshot,
    uint64_t reply_token,
    micros_endpoint_t destination_endpoint,
    uintptr_t reply_buffer
)
{
    enum micros_ipc_error error;

    struct model_thread *receiver =
        &model->threads[receiver_slot];
    const struct model_transition transitions[2] = {
        {
            .thread_slot = caller_slot,
            .clear_flags = MICROS_THREAD_RTS_INACTIVE,
            .set_flags = MICROS_THREAD_RTS_IPC_REPLY,
        },
        {
            .thread_slot = receiver_slot,
            .clear_flags = MICROS_THREAD_RTS_IPC_RECEIVE,
        },
    };
    uintptr_t receive_buffer = receiver->ipc_receive_buffer;

    error = ipc_model_apply_transitions(
        model,
        transitions,
        sizeof(transitions) / sizeof(transitions[0])
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    ipc_model_ipc_unlink(
        model,
        &destination->receiver_head,
        &destination->receiver_tail,
        previous,
        receiver_slot
    );
    receiver->ipc_queue_kind = MICROS_IPC_QUEUE_NONE;
    receiver->ipc_next = ipc_model_null_thread_handle();
    receiver->ipc_receive_source = 0;
    ipc_model_stage_delivery(receiver, receive_buffer, snapshot);
    caller->ipc_receive_buffer = reply_buffer;
    caller->ipc_reply_token = reply_token;
    caller->ipc_reply_callee = destination_endpoint;
    model->last_reply_token = reply_token;
    return MICROS_IPC_OK;
}

enum micros_ipc_error ipc_model_call(
    struct model_state *model,
    size_t caller_slot,
    micros_endpoint_t destination_endpoint,
    struct micros_ipc_message *message
)
{
    struct model_thread *caller;
    struct model_endpoint *destination;
    struct micros_thread_handle previous = ipc_model_null_thread_handle();
    struct micros_ipc_message snapshot;
    micros_endpoint_t source_endpoint;
    uint64_t reply_token;
    size_t destination_slot;
    size_t receiver_slot;
    enum micros_ipc_error error;

    if (
        message == NULL
        || (uintptr_t)message % _Alignof(struct micros_ipc_message)
            != 0
        || (
            message->type
            & MICROS_IPC_TYPE_KERNEL_MASK
        ) != 0
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    error = ipc_model_resolve_operation_thread(
        model,
        caller_slot,
        &source_endpoint
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = ipc_model_authorize_target(
        model,
        source_endpoint,
        MICROS_PRIVILEGE_OPERATION_CALL,
        destination_endpoint
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    if (model->last_reply_token == UINT64_MAX) {
        return MICROS_IPC_ERROR_REPLY_TOKEN_EXHAUSTED;
    }
    reply_token = model->last_reply_token + 1;
    (void)ipc_model_resolve_active_endpoint(
        model,
        destination_endpoint,
        &destination_slot
    );
    caller = &model->threads[caller_slot];
    destination = &model->endpoints[destination_slot];
    ipc_model_canonicalize_message(
        &snapshot,
        message,
        source_endpoint,
        reply_token
    );
    if (
        ipc_model_find_receiver(
            model,
            destination_slot,
            source_endpoint,
            &previous,
            &receiver_slot
        )
    ) {
        return deliver_model_call_and_publish_token(
            model, destination, caller, caller_slot, receiver_slot, previous,
            &snapshot, reply_token, destination_endpoint, (uintptr_t)message
        );
    }
    error = ipc_model_deadlock_preflight(
        model,
        caller_slot,
        destination_endpoint,
        MICROS_THREAD_CAPACITY
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    caller->runtime_flags =
        MICROS_THREAD_RTS_IPC_SEND | MICROS_THREAD_RTS_IPC_REPLY;
    caller->ipc_queue_kind = MICROS_IPC_QUEUE_SENDER;
    caller->ipc_outbound_message = snapshot;
    caller->ipc_send_destination = destination_endpoint;
    caller->ipc_receive_buffer = (uintptr_t)message;
    caller->ipc_reply_token = reply_token;
    caller->ipc_reply_callee = destination_endpoint;
    ipc_model_ipc_append(
        model,
        &destination->sender_head,
        &destination->sender_tail,
        caller_slot
    );
    model->last_reply_token = reply_token;
    return MICROS_IPC_OK;
}

static enum micros_ipc_error model_resolve_reply_waiter(
    struct model_state *model,
    uint64_t reply_token,
    micros_endpoint_t replying_endpoint,
    size_t *caller_slot
)
{
    size_t matched = MICROS_THREAD_CAPACITY;
    size_t index;

    if (reply_token == 0) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct model_thread *thread = &model->threads[index];

        if (
            thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && thread->ipc_reply_token == reply_token
        ) {
            if (matched != MICROS_THREAD_CAPACITY) {
                return MICROS_IPC_ERROR_INVARIANT;
            }
            matched = index;
        }
    }
    if (matched == MICROS_THREAD_CAPACITY) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    if (
        model->threads[matched].ipc_queue_kind
            != MICROS_IPC_QUEUE_NONE
        || !ipc_model_thread_handle_is_zero(model->threads[matched].ipc_next)
        || (
            model->threads[matched].runtime_flags
            & MICROS_THREAD_RTS_IPC_MASK
        ) != MICROS_THREAD_RTS_IPC_REPLY
        || model->threads[matched].ipc_reply_callee
            != replying_endpoint
    ) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    *caller_slot = matched;
    return MICROS_IPC_OK;
}

static enum micros_ipc_error model_reply_preflight(
    struct model_state *model,
    size_t replier_slot,
    uint64_t reply_token,
    const struct micros_ipc_message *message,
    uint32_t operation,
    micros_endpoint_t *replying_endpoint,
    size_t *caller_slot,
    struct micros_ipc_message *snapshot
)
{
    enum micros_ipc_error error;
    size_t index;

    if (
        message == NULL
        || (uintptr_t)message % _Alignof(struct micros_ipc_message)
            != 0
        || (
            message->type
            & MICROS_IPC_TYPE_KERNEL_MASK
        ) != 0
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    error = ipc_model_resolve_operation_thread(
        model,
        replier_slot,
        replying_endpoint
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = ipc_model_authorize_operation(
        model,
        *replying_endpoint,
        operation
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = model_resolve_reply_waiter(
        model,
        reply_token,
        *replying_endpoint,
        caller_slot
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct model_thread *thread = &model->threads[index];

        if (
            thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && thread->ipc_delivery_pending
            && thread->ipc_staged_result == MICROS_IPC_OK
            && thread->ipc_inbound_message.reply_token
                == reply_token
        ) {
            return MICROS_IPC_ERROR_REPLY_TOKEN;
        }
    }
    ipc_model_canonicalize_message(
        snapshot,
        message,
        *replying_endpoint,
        0
    );
    return MICROS_IPC_OK;
}

enum micros_ipc_error ipc_model_reply(
    struct model_state *model,
    size_t replier_slot,
    uint64_t reply_token,
    const struct micros_ipc_message *message
)
{
    struct micros_ipc_message snapshot;
    micros_endpoint_t replying_endpoint;
    size_t caller_slot;
    uintptr_t reply_buffer;
    enum micros_ipc_error error;
    struct model_transition transitions[2];

    error = model_reply_preflight(
        model,
        replier_slot,
        reply_token,
        message,
        MICROS_PRIVILEGE_OPERATION_REPLY,
        &replying_endpoint,
        &caller_slot,
        &snapshot
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    (void)replying_endpoint;
    transitions[0] = (struct model_transition){
        .thread_slot = replier_slot,
        .clear_flags = MICROS_THREAD_RTS_INACTIVE,
    };
    transitions[1] = (struct model_transition){
        .thread_slot = caller_slot,
        .clear_flags = MICROS_THREAD_RTS_IPC_REPLY,
    };
    error = ipc_model_apply_transitions(
        model,
        transitions,
        sizeof(transitions) / sizeof(transitions[0])
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    reply_buffer = model->threads[caller_slot].ipc_receive_buffer;
    ipc_model_stage_delivery(
        &model->threads[caller_slot],
        reply_buffer,
        &snapshot
    );
    model->threads[caller_slot].ipc_reply_token = 0;
    model->threads[caller_slot].ipc_reply_callee = 0;
    return MICROS_IPC_OK;
}

struct model_reply_receive_plan {
    struct micros_ipc_message reply_snapshot;
    struct micros_ipc_message incoming;
    struct model_endpoint *destination;
    struct micros_thread_handle previous;
    micros_endpoint_t replying_endpoint;
    size_t caller_slot;
    size_t destination_slot;
    size_t notification_source_slot;
    size_t sender_slot;
    uint64_t event_mask;
    bool has_notification;
    bool has_sender;
};

static enum micros_ipc_error preflight_model_combined_reply(
    struct model_state *model,
    size_t replier_slot,
    uint64_t reply_token,
    const struct micros_ipc_message *reply_message,
    micros_endpoint_t source_endpoint,
    struct model_reply_receive_plan *plan
)
{
    enum micros_ipc_error error;

    error = model_reply_preflight(
        model,
        replier_slot,
        reply_token,
        reply_message,
        MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
        &plan->replying_endpoint,
        &plan->caller_slot,
        &plan->reply_snapshot
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    if (
        source_endpoint != MICROS_ENDPOINT_ANY
        && !ipc_model_resolve_active_endpoint(
            model,
            source_endpoint,
            &plan->destination_slot
        )
    ) {
        return MICROS_IPC_ERROR_DEAD_ENDPOINT;
    }
    plan->destination_slot = model->threads[replier_slot].owner.slot;
    plan->destination = &model->endpoints[plan->destination_slot];
    return MICROS_IPC_OK;
}

static enum micros_ipc_error select_model_combined_receive_input(
    struct model_state *model,
    size_t replier_slot,
    micros_endpoint_t source_endpoint,
    struct model_reply_receive_plan *plan
)
{
    enum micros_ipc_error error;

    plan->has_notification = ipc_model_find_pending_notification(
        model,
        plan->destination_slot,
        source_endpoint,
        &plan->notification_source_slot,
        &plan->event_mask
    );
    plan->has_sender = false;
    plan->sender_slot = MICROS_THREAD_CAPACITY;
    if (!plan->has_notification) {
        plan->has_sender = ipc_model_find_sender(
            model,
            plan->destination_slot,
            source_endpoint,
            &plan->previous,
            &plan->sender_slot
        );
    }
    if (!plan->has_notification && !plan->has_sender) {
        error = ipc_model_deadlock_preflight(
            model,
            replier_slot,
            source_endpoint,
            plan->caller_slot
        );
        if (error != MICROS_IPC_OK) {
            return error;
        }
    }
    return MICROS_IPC_OK;
}

static enum micros_ipc_error commit_model_combined_scheduler(
    struct model_state *model,
    size_t replier_slot,
    struct model_reply_receive_plan *plan
)
{
    enum micros_ipc_error error;

    if (plan->has_notification) {
        const struct model_transition transitions[2] = {
            {
                .thread_slot = plan->caller_slot,
                .clear_flags = MICROS_THREAD_RTS_IPC_REPLY,
            },
            {
                .thread_slot = replier_slot,
                .clear_flags = MICROS_THREAD_RTS_INACTIVE,
            },
        };

        error = ipc_model_apply_transitions(
            model,
            transitions,
            sizeof(transitions) / sizeof(transitions[0])
        );
    } else if (plan->has_sender) {
        const struct model_transition transitions[3] = {
            {
                .thread_slot = plan->caller_slot,
                .clear_flags = MICROS_THREAD_RTS_IPC_REPLY,
            },
            {
                .thread_slot = replier_slot,
                .clear_flags = MICROS_THREAD_RTS_INACTIVE,
            },
            {
                .thread_slot = plan->sender_slot,
                .clear_flags = MICROS_THREAD_RTS_IPC_SEND,
            },
        };

        plan->incoming = model->threads[plan->sender_slot].ipc_outbound_message;
        error = ipc_model_apply_transitions(
            model,
            transitions,
            sizeof(transitions) / sizeof(transitions[0])
        );
    } else {
        const struct model_transition transitions[2] = {
            {
                .thread_slot = plan->caller_slot,
                .clear_flags = MICROS_THREAD_RTS_IPC_REPLY,
            },
            {
                .thread_slot = replier_slot,
                .clear_flags = MICROS_THREAD_RTS_INACTIVE,
                .set_flags = MICROS_THREAD_RTS_IPC_RECEIVE,
            },
        };

        error = ipc_model_apply_transitions(
            model,
            transitions,
            sizeof(transitions) / sizeof(transitions[0])
        );
    }
    if (error != MICROS_IPC_OK) {
        return error;
    }
    return MICROS_IPC_OK;
}

static void publish_model_combined_reply_and_receive(
    struct model_state *model,
    size_t replier_slot,
    micros_endpoint_t source_endpoint,
    uintptr_t receive_buffer,
    struct model_reply_receive_plan *plan
)
{
    ipc_model_stage_delivery(
        &model->threads[plan->caller_slot],
        model->threads[plan->caller_slot].ipc_receive_buffer,
        &plan->reply_snapshot
    );
    model->threads[plan->caller_slot].ipc_reply_token = 0;
    model->threads[plan->caller_slot].ipc_reply_callee = 0;
    if (plan->has_notification) {
        ipc_model_canonicalize_notification(
            &plan->incoming,
            plan->notification_source_slot == MICROS_PROCESS_CAPACITY
                ? MICROS_ENDPOINT_NONE
                : model->endpoints[
                    plan->notification_source_slot
                ].value,
            plan->event_mask
        );
        if (plan->notification_source_slot == MICROS_PROCESS_CAPACITY) {
            plan->destination->pending_kernel_events = 0;
        } else {
            plan->destination->pending_events[plan->notification_source_slot] = 0;
            plan->destination->pending_notification_sources &=
                ~(UINT64_C(1) << plan->notification_source_slot);
        }
        ipc_model_stage_delivery(
            &model->threads[replier_slot],
            receive_buffer,
            &plan->incoming
        );
    } else if (plan->has_sender) {
        ipc_model_ipc_unlink(
            model,
            &plan->destination->sender_head,
            &plan->destination->sender_tail,
            plan->previous,
            plan->sender_slot
        );
        ipc_model_clear_delivered_sender(&model->threads[plan->sender_slot]);
        ipc_model_stage_delivery(
            &model->threads[replier_slot],
            receive_buffer,
            &plan->incoming
        );
    } else {
        struct model_thread *replier =
            &model->threads[replier_slot];

        replier->ipc_queue_kind = MICROS_IPC_QUEUE_RECEIVER;
        replier->ipc_receive_source = source_endpoint;
        replier->ipc_receive_buffer = receive_buffer;
        ipc_model_ipc_append(
            model,
            &plan->destination->receiver_head,
            &plan->destination->receiver_tail,
            replier_slot
        );
    }
}

enum micros_ipc_error ipc_model_reply_receive(
    struct model_state *model,
    size_t replier_slot,
    uint64_t reply_token,
    const struct micros_ipc_message *reply_message,
    micros_endpoint_t source_endpoint,
    uintptr_t receive_buffer
)
{
    struct model_reply_receive_plan plan;
    enum micros_ipc_error error;

    plan.previous = ipc_model_null_thread_handle();
    if (
        receive_buffer == 0
        || receive_buffer % 8 != 0
        || source_endpoint == MICROS_ENDPOINT_NONE
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    error = preflight_model_combined_reply(
        model, replier_slot, reply_token, reply_message, source_endpoint, &plan
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = select_model_combined_receive_input(model, replier_slot, source_endpoint, &plan);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = commit_model_combined_scheduler(model, replier_slot, &plan);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    publish_model_combined_reply_and_receive(
        model, replier_slot, source_endpoint, receive_buffer, &plan
    );
    return MICROS_IPC_OK;
}
