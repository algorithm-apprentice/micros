#include "kernel/endpoint/endpoint_private.h"
#include "micros/bootstrap_control.h"

bool endpoint_delivery_state_is_clear(
    const struct micros_thread *thread
)
{
    return (
        !thread->ipc_delivery_pending
        && endpoint_bytes_are_zero(
            &thread->ipc_inbound_message,
            sizeof(thread->ipc_inbound_message)
        )
        && thread->ipc_staged_result == MICROS_IPC_OK
    );
}

bool endpoint_reply_wait_state_is_valid(
    const struct micros_endpoint_registry *registry,
    const struct micros_thread *thread
)
{
    const struct micros_endpoint_record *caller =
        &registry->endpoints[thread->owner.slot];
    const struct micros_endpoint_record *callee;

    if (
        thread->ipc_queue_kind != MICROS_IPC_QUEUE_NONE
        || !endpoint_thread_handle_is_zero(thread->ipc_next)
        || (
            thread->runtime_flags
            & MICROS_THREAD_RTS_IPC_MASK
        ) != MICROS_THREAD_RTS_IPC_REPLY
        || !endpoint_bytes_are_zero(
            &thread->ipc_outbound_message,
            sizeof(thread->ipc_outbound_message)
        )
        || thread->ipc_send_destination != 0
        || thread->ipc_receive_source != 0
        || thread->ipc_receive_buffer == 0
        || thread->ipc_receive_buffer % 8 != 0
        || !endpoint_delivery_state_is_clear(thread)
        || thread->ipc_reply_token == 0
        || (
            caller->state != MICROS_ENDPOINT_STATE_ACTIVE
            && !(
                caller->state
                    == MICROS_ENDPOINT_STATE_SOURCE_ONLY
                && thread->runtime_flags
                    == (
                        MICROS_THREAD_RTS_INACTIVE
                        | MICROS_THREAD_RTS_IPC_REPLY
                    )
            )
        )
        || !endpoint_process_handles_equal(caller->owner, thread->owner)
        || endpoint_record_resolve_validated(
            registry,
            thread->ipc_reply_callee,
            &callee
        ) != MICROS_ENDPOINT_OK
    ) {
        return false;
    }
    return (
        callee->state == MICROS_ENDPOINT_STATE_ACTIVE
        || callee->state == MICROS_ENDPOINT_STATE_SOURCE_ONLY
    );
}

bool endpoint_source_only_process_threads_are_valid(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process
)
{
    size_t index;

    if (endpoint_process_has_current_thread(objects, process)) {
        return false;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];

        if (
            thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || !endpoint_process_handles_equal(thread->owner, process)
        ) {
            continue;
        }
        if (
            (
                thread->runtime_flags == MICROS_THREAD_RTS_INACTIVE
                && micros_thread_ipc_state_is_clear(thread)
            )
            || (
                thread->runtime_flags
                    == (
                        MICROS_THREAD_RTS_INACTIVE
                        | MICROS_THREAD_RTS_IPC_REPLY
                    )
                && endpoint_reply_wait_state_is_valid(registry, thread)
            )
        ) {
            continue;
        }
        return false;
    }
    return true;
}

static bool staged_reply_token_binding_is_valid(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_thread *receiver,
    const struct micros_endpoint_record *receiver_endpoint
)
{
    const struct micros_thread *caller = NULL;
    const struct micros_endpoint_record *caller_endpoint;
    uint64_t reply_token =
        receiver->ipc_inbound_message.reply_token;
    size_t index;

    if (reply_token == 0) {
        return true;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *candidate =
            &objects->threads[index];

        if (
            candidate != receiver
            && candidate->slot_state
                == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && candidate->ipc_delivery_pending
            && candidate->ipc_staged_result == MICROS_IPC_OK
            && candidate->ipc_inbound_message.reply_token
                == reply_token
        ) {
            return false;
        }
        if (
            candidate->slot_state
                != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || candidate->ipc_reply_token != reply_token
        ) {
            continue;
        }
        if (caller != NULL) {
            return false;
        }
        caller = candidate;
    }
    if (
        caller == NULL
        || !endpoint_reply_wait_state_is_valid(registry, caller)
    ) {
        return false;
    }
    caller_endpoint = &registry->endpoints[caller->owner.slot];
    return (
        caller_endpoint->state == MICROS_ENDPOINT_STATE_ACTIVE
        && endpoint_process_handles_equal(
            caller_endpoint->owner,
            caller->owner
        )
        && receiver->ipc_inbound_message.source
            == caller_endpoint->value
        && caller->ipc_reply_callee == receiver_endpoint->value
    );
}

static bool notification_message_is_valid(
    const struct micros_ipc_message *message
)
{
    bool event_mask_is_nonzero = false;
    size_t index;

    if (
        message->type != MICROS_IPC_TYPE_KERNEL_NOTIFICATION
        || message->reply_token != 0
    ) {
        return false;
    }
    for (index = 0; index < sizeof(uint64_t); ++index) {
        if (message->payload[index] != 0) {
            event_mask_is_nonzero = true;
        }
    }
    for (
        index = sizeof(uint64_t);
        index < sizeof(message->payload);
        ++index
    ) {
        if (message->payload[index] != 0) {
            return false;
        }
    }
    return event_mask_is_nonzero;
}

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

static bool source_only_message_is_valid(
    const struct micros_ipc_message *message
)
{
    size_t index;

    if (
        message->type != MICROS_BOOTSTRAP_MESSAGE_READY_ACK
        || message->reply_token != 0
        || read_u32_le(&message->payload[0])
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || read_u32_le(&message->payload[4]) == 0
        || read_u32_le(&message->payload[8])
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || read_u32_le(&message->payload[12]) != 0
        || read_u32_le(&message->payload[16])
            == MICROS_ENDPOINT_NONE
        || read_u32_le(&message->payload[16])
            == MICROS_ENDPOINT_ANY
    ) {
        return false;
    }
    for (index = 20; index < sizeof(message->payload); ++index) {
        if (message->payload[index] != 0) {
            return false;
        }
    }
    return true;
}

bool endpoint_staged_delivery_state_is_valid(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_thread *thread
)
{
    const struct micros_endpoint_record *owner =
        &registry->endpoints[thread->owner.slot];
    const struct micros_endpoint_record *source;

    if (
        thread->ipc_queue_kind != MICROS_IPC_QUEUE_NONE
        || !endpoint_thread_handle_is_zero(thread->ipc_next)
        || (
            thread->runtime_flags
            & MICROS_THREAD_RTS_IPC_MASK
        ) != 0
        || !endpoint_bytes_are_zero(
            &thread->ipc_outbound_message,
            sizeof(thread->ipc_outbound_message)
        )
        || thread->ipc_send_destination != 0
        || thread->ipc_receive_source != 0
        || thread->ipc_receive_buffer == 0
        || thread->ipc_receive_buffer % 8 != 0
        || !thread->ipc_delivery_pending
        || thread->ipc_staged_result != MICROS_IPC_OK
        || thread->ipc_reply_token != 0
        || thread->ipc_reply_callee != 0
        || owner->state != MICROS_ENDPOINT_STATE_ACTIVE
        || !endpoint_process_handles_equal(owner->owner, thread->owner)
    ) {
        return false;
    }
    if (
        thread->ipc_inbound_message.type
        == MICROS_IPC_TYPE_KERNEL_NOTIFICATION
    ) {
        if (
            !notification_message_is_valid(
                &thread->ipc_inbound_message
            )
        ) {
            return false;
        }
        if (
            thread->ipc_inbound_message.source
                == MICROS_ENDPOINT_NONE
        ) {
            return true;
        }
        return (
            endpoint_record_resolve_validated(
                registry,
                thread->ipc_inbound_message.source,
                &source
            ) == MICROS_ENDPOINT_OK
            && source->state == MICROS_ENDPOINT_STATE_ACTIVE
        );
    }
    if (
        thread->ipc_inbound_message.source
            == MICROS_ENDPOINT_NONE
        || endpoint_record_resolve_validated(
            registry,
            thread->ipc_inbound_message.source,
            &source
        ) != MICROS_ENDPOINT_OK
    ) {
        return false;
    }
    return (
        (
            source->state == MICROS_ENDPOINT_STATE_ACTIVE
            || (
                source->state
                    == MICROS_ENDPOINT_STATE_SOURCE_ONLY
                && source_only_message_is_valid(
                    &thread->ipc_inbound_message
                )
            )
        )
        && (
            thread->ipc_inbound_message.type
            & MICROS_IPC_TYPE_KERNEL_MASK
        ) == 0
        && staged_reply_token_binding_is_valid(
            registry,
            objects,
            thread,
            owner
        )
    );
}

bool endpoint_staged_error_state_is_valid(
    const struct micros_endpoint_registry *registry,
    const struct micros_thread *thread
)
{
    const struct micros_endpoint_record *owner =
        &registry->endpoints[thread->owner.slot];

    return (
        thread->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && endpoint_thread_handle_is_zero(thread->ipc_next)
        && (
            thread->runtime_flags
            & MICROS_THREAD_RTS_IPC_MASK
        ) == 0
        && endpoint_bytes_are_zero(
            &thread->ipc_outbound_message,
            sizeof(thread->ipc_outbound_message)
        )
        && thread->ipc_send_destination == 0
        && thread->ipc_receive_source == 0
        && thread->ipc_receive_buffer == 0
        && thread->ipc_delivery_pending
        && endpoint_bytes_are_zero(
            &thread->ipc_inbound_message,
            sizeof(thread->ipc_inbound_message)
        )
        && thread->ipc_staged_result
            == MICROS_IPC_ERROR_DEAD_ENDPOINT
        && thread->ipc_reply_token == 0
        && thread->ipc_reply_callee == 0
        && owner->state == MICROS_ENDPOINT_STATE_ACTIVE
        && endpoint_process_handles_equal(owner->owner, thread->owner)
    );
}

bool endpoint_staged_no_message_success_state_is_valid(
    const struct micros_endpoint_registry *registry,
    const struct micros_thread *thread
)
{
    const struct micros_endpoint_record *owner =
        &registry->endpoints[thread->owner.slot];

    return (
        thread->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && endpoint_thread_handle_is_zero(thread->ipc_next)
        && (
            thread->runtime_flags
            & MICROS_THREAD_RTS_IPC_MASK
        ) == 0
        && endpoint_bytes_are_zero(
            &thread->ipc_outbound_message,
            sizeof(thread->ipc_outbound_message)
        )
        && thread->ipc_send_destination == 0
        && thread->ipc_receive_source == 0
        && thread->ipc_receive_buffer == 0
        && thread->ipc_delivery_pending
        && endpoint_bytes_are_zero(
            &thread->ipc_inbound_message,
            sizeof(thread->ipc_inbound_message)
        )
        && thread->ipc_staged_result == MICROS_IPC_OK
        && thread->ipc_reply_token == 0
        && thread->ipc_reply_callee == 0
        && owner->state == MICROS_ENDPOINT_STATE_ACTIVE
        && endpoint_process_handles_equal(owner->owner, thread->owner)
    );
}
