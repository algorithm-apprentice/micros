#include "ipc_model_test_internal.h"

static void clear_model_message(struct micros_ipc_message *message)
{
    memset(message, 0, sizeof(*message));
}

void ipc_model_clear_delivered_sender(struct model_thread *sender)
{
    bool completes_send = sender->ipc_reply_token == 0;

    sender->ipc_queue_kind = MICROS_IPC_QUEUE_NONE;
    sender->ipc_next = ipc_model_null_thread_handle();
    clear_model_message(&sender->ipc_outbound_message);
    sender->ipc_send_destination = 0;
    if (completes_send) {
        sender->ipc_receive_buffer = 0;
        sender->ipc_delivery_pending = true;
        clear_model_message(&sender->ipc_inbound_message);
        sender->ipc_staged_result = MICROS_IPC_OK;
    }
}

void ipc_model_clear_ipc_state(struct model_thread *thread)
{
    thread->ipc_queue_kind = MICROS_IPC_QUEUE_NONE;
    thread->ipc_next = ipc_model_null_thread_handle();
    clear_model_message(&thread->ipc_outbound_message);
    thread->ipc_send_destination = 0;
    thread->ipc_receive_source = 0;
    thread->ipc_receive_buffer = 0;
    thread->ipc_delivery_pending = false;
    clear_model_message(&thread->ipc_inbound_message);
    thread->ipc_staged_result = MICROS_IPC_OK;
    thread->ipc_reply_token = 0;
    thread->ipc_reply_callee = 0;
}

void ipc_model_stage_delivery(
    struct model_thread *receiver,
    uintptr_t receive_buffer,
    const struct micros_ipc_message *message
)
{
    receiver->ipc_receive_buffer = receive_buffer;
    receiver->ipc_delivery_pending = true;
    receiver->ipc_inbound_message = *message;
    receiver->ipc_staged_result = MICROS_IPC_OK;
}

enum micros_ipc_error ipc_model_drain_thread(
    struct model_state *model,
    size_t thread_slot
)
{
    struct model_thread *thread;

    if (thread_slot >= MICROS_THREAD_CAPACITY) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    thread = &model->threads[thread_slot];
    if (
        thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || !thread->ipc_delivery_pending
    ) {
        return MICROS_IPC_ERROR_STATE;
    }
    thread->ipc_receive_buffer = 0;
    thread->ipc_delivery_pending = false;
    clear_model_message(&thread->ipc_inbound_message);
    thread->ipc_staged_result = MICROS_IPC_OK;
    return MICROS_IPC_OK;
}

void ipc_model_canonicalize_message(
    struct micros_ipc_message *destination,
    const struct micros_ipc_message *source,
    micros_endpoint_t source_endpoint,
    uint64_t reply_token
)
{
    *destination = *source;
    destination->source = source_endpoint;
    destination->reply_token = reply_token;
}

void ipc_model_canonicalize_notification(
    struct micros_ipc_message *message,
    micros_endpoint_t source,
    uint64_t event_mask
)
{
    size_t index;

    clear_model_message(message);
    message->source = source;
    message->type = MICROS_IPC_TYPE_KERNEL_NOTIFICATION;
    for (index = 0; index < sizeof(event_mask); ++index) {
        message->payload[index] =
            (uint8_t)(event_mask >> (index * 8));
    }
}

void ipc_model_ipc_append(
    struct model_state *model,
    struct micros_thread_handle *head,
    struct micros_thread_handle *tail,
    size_t thread_slot
)
{
    struct micros_thread_handle handle =
        ipc_model_thread_handle(model, thread_slot);

    model->threads[thread_slot].ipc_next = ipc_model_null_thread_handle();
    if (ipc_model_thread_handle_is_zero(*head)) {
        *head = handle;
    } else {
        model->threads[tail->slot].ipc_next = handle;
    }
    *tail = handle;
}

void ipc_model_ipc_unlink(
    struct model_state *model,
    struct micros_thread_handle *head,
    struct micros_thread_handle *tail,
    struct micros_thread_handle previous,
    size_t thread_slot
)
{
    struct micros_thread_handle handle =
        ipc_model_thread_handle(model, thread_slot);
    struct micros_thread_handle next =
        model->threads[thread_slot].ipc_next;

    if (ipc_model_thread_handle_is_zero(previous)) {
        *head = next;
    } else {
        model->threads[previous.slot].ipc_next = next;
    }
    if (ipc_model_thread_handles_equal(*tail, handle)) {
        *tail = previous;
    }
}
