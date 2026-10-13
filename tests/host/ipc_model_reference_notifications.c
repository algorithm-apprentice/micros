#include "ipc_model_test_internal.h"

enum micros_ipc_error ipc_model_notify(
    struct model_state *model,
    size_t notifier_slot,
    micros_endpoint_t destination_endpoint,
    uint64_t event_mask
)
{
    struct model_endpoint *destination;
    struct micros_thread_handle previous = ipc_model_null_thread_handle();
    struct micros_ipc_message notification;
    micros_endpoint_t source_endpoint;
    size_t destination_slot;
    size_t receiver_slot;
    size_t source_slot;
    enum micros_ipc_error error;

    if (event_mask == 0) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    error = ipc_model_resolve_operation_thread(
        model,
        notifier_slot,
        &source_endpoint
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = ipc_model_authorize_target(
        model,
        source_endpoint,
        MICROS_PRIVILEGE_OPERATION_NOTIFY,
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
    source_slot = model->threads[notifier_slot].owner.slot;
    destination = &model->endpoints[destination_slot];
    ipc_model_canonicalize_notification(
        &notification,
        source_endpoint,
        event_mask
    );
    if (
        ipc_model_find_receiver(
            model,
            destination_slot,
            source_endpoint,
            &previous,
            &receiver_slot
        )
        && (
            model->threads[receiver_slot].runtime_flags
            & MICROS_THREAD_RTS_IPC_REPLY
        ) == 0
    ) {
        struct model_thread *receiver =
            &model->threads[receiver_slot];
        const struct model_transition transitions[2] = {
            {
                .thread_slot = notifier_slot,
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
        ipc_model_stage_delivery(
            receiver,
            receive_buffer,
            &notification
        );
        return MICROS_IPC_OK;
    }
    {
        const struct model_transition transition = {
            .thread_slot = notifier_slot,
            .clear_flags = MICROS_THREAD_RTS_INACTIVE,
        };

        error = ipc_model_apply_transitions(model, &transition, 1);
    }
    if (error != MICROS_IPC_OK) {
        return error;
    }
    destination->pending_events[source_slot] |= event_mask;
    destination->pending_notification_sources |=
        UINT64_C(1) << source_slot;
    return MICROS_IPC_OK;
}

enum micros_ipc_error ipc_model_kernel_notify(
    struct model_state *model,
    micros_endpoint_t destination_endpoint,
    uint64_t event_mask
)
{
    struct model_endpoint *destination;
    struct micros_thread_handle previous = ipc_model_null_thread_handle();
    struct micros_ipc_message notification;
    size_t destination_slot;
    size_t receiver_slot;
    enum micros_ipc_error error;

    if (event_mask == 0) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    if (
        !ipc_model_resolve_active_endpoint(
            model,
            destination_endpoint,
            &destination_slot
        )
    ) {
        return MICROS_IPC_ERROR_DEAD_ENDPOINT;
    }
    destination = &model->endpoints[destination_slot];
    ipc_model_canonicalize_notification(
        &notification,
        MICROS_ENDPOINT_NONE,
        event_mask
    );
    if (
        ipc_model_find_receiver(
            model,
            destination_slot,
            MICROS_ENDPOINT_NONE,
            &previous,
            &receiver_slot
        )
        && (
            model->threads[receiver_slot].runtime_flags
            & MICROS_THREAD_RTS_IPC_REPLY
        ) == 0
    ) {
        struct model_thread *receiver =
            &model->threads[receiver_slot];
        const struct model_transition transition = {
            .thread_slot = receiver_slot,
            .clear_flags = MICROS_THREAD_RTS_IPC_RECEIVE,
        };
        uintptr_t receive_buffer = receiver->ipc_receive_buffer;

        error = ipc_model_apply_transitions(model, &transition, 1);
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
        ipc_model_stage_delivery(
            receiver,
            receive_buffer,
            &notification
        );
        return MICROS_IPC_OK;
    }
    destination->pending_kernel_events |= event_mask;
    return MICROS_IPC_OK;
}
