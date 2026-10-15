#include "micros/endpoint.h"

#include <stdint.h>

#include "endpoint_internal.h"
#include "endpoint_private.h"
#include "micros/bootstrap_control.h"

static bool endpoint_is_pending_notification_source(
    const struct micros_endpoint_registry *registry,
    const struct micros_endpoint_record *source
);

static bool delivery_state_is_clear(
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

static bool sender_queue_state_is_valid(
    const struct micros_endpoint_registry *registry,
    const struct micros_thread *thread,
    micros_endpoint_t destination
)
{
    const struct micros_endpoint_record *source =
        &registry->endpoints[thread->owner.slot];
    bool is_call =
        (
            thread->runtime_flags
            & MICROS_THREAD_RTS_IPC_REPLY
        ) != 0;

    if (
        thread->ipc_queue_kind != MICROS_IPC_QUEUE_SENDER
        || (
            thread->runtime_flags
            & MICROS_THREAD_RTS_IPC_SEND
        ) == 0
        || (
            thread->runtime_flags
            & MICROS_THREAD_RTS_IPC_RECEIVE
        ) != 0
        || source->state != MICROS_ENDPOINT_STATE_ACTIVE
        || !endpoint_process_handles_equal(source->owner, thread->owner)
        || thread->ipc_send_destination != destination
        || thread->ipc_outbound_message.source != source->value
        || (
            thread->ipc_outbound_message.type
            & MICROS_IPC_TYPE_KERNEL_MASK
        ) != 0
        || thread->ipc_receive_source != 0
        || !delivery_state_is_clear(thread)
    ) {
        return false;
    }
    if (!is_call) {
        return (
            thread->ipc_outbound_message.reply_token == 0
            && thread->ipc_receive_buffer == 0
            && thread->ipc_reply_token == 0
            && thread->ipc_reply_callee == 0
        );
    }
    return (
        thread->ipc_reply_token != 0
        && thread->ipc_reply_callee == destination
        && thread->ipc_outbound_message.reply_token
            == thread->ipc_reply_token
        && thread->ipc_receive_buffer != 0
        && thread->ipc_receive_buffer % 8 == 0
    );
}

static bool reply_wait_state_is_valid(
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
        || !delivery_state_is_clear(thread)
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

static bool source_only_process_threads_are_valid(
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
                && reply_wait_state_is_valid(registry, thread)
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
        || !reply_wait_state_is_valid(registry, caller)
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

static bool staged_delivery_state_is_valid(
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

static bool staged_error_state_is_valid(
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

static bool staged_no_message_success_state_is_valid(
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

static bool thread_has_no_ipc_flags(
    const struct micros_thread *thread
)
{
    return (
        thread->runtime_flags
        & MICROS_THREAD_RTS_IPC_MASK
    ) == 0;
}

static bool receiver_queue_state_is_valid(
    const struct micros_endpoint_registry *registry,
    const struct micros_thread *thread,
    const struct micros_endpoint_record *endpoint
)
{
    const struct micros_endpoint_record *source;

    if (
        thread->ipc_queue_kind != MICROS_IPC_QUEUE_RECEIVER
        || (
            thread->runtime_flags
            & MICROS_THREAD_RTS_IPC_MASK
        ) != MICROS_THREAD_RTS_IPC_RECEIVE
        || !endpoint_process_handles_equal(thread->owner, endpoint->owner)
        || !endpoint_bytes_are_zero(
            &thread->ipc_outbound_message,
            sizeof(thread->ipc_outbound_message)
        )
        || thread->ipc_send_destination != 0
        || thread->ipc_receive_buffer == 0
        || thread->ipc_receive_buffer % 8 != 0
        || !delivery_state_is_clear(thread)
        || thread->ipc_reply_token != 0
        || thread->ipc_reply_callee != 0
    ) {
        return false;
    }
    if (thread->ipc_receive_source == MICROS_ENDPOINT_ANY) {
        return true;
    }
    if (
        endpoint_record_resolve_validated(
            registry,
            thread->ipc_receive_source,
            &source
        ) != MICROS_ENDPOINT_OK
    ) {
        return false;
    }
    return (
        source->state == MICROS_ENDPOINT_STATE_ACTIVE
        || source->state == MICROS_ENDPOINT_STATE_SOURCE_ONLY
    );
}

static bool endpoint_queue_is_valid(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_record *endpoint,
    struct micros_thread_handle head,
    struct micros_thread_handle tail,
    enum micros_ipc_queue_kind kind,
    bool seen[MICROS_THREAD_CAPACITY]
)
{
    struct micros_thread_handle current = head;
    size_t steps;

    if (endpoint_thread_handle_is_zero(head)) {
        return endpoint_thread_handle_is_zero(tail);
    }
    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        const struct micros_thread *thread;

        if (
            current.slot >= MICROS_THREAD_CAPACITY
            || current.generation == 0
            || seen[current.slot]
            || micros_thread_resolve(objects, current, &thread)
                != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
        seen[current.slot] = true;
        if (
            kind == MICROS_IPC_QUEUE_SENDER
                ? !sender_queue_state_is_valid(
                    registry,
                    thread,
                    endpoint->value
                )
                : !receiver_queue_state_is_valid(
                    registry,
                    thread,
                    endpoint
                )
        ) {
            return false;
        }
        if (endpoint_thread_handles_equal(current, tail)) {
            return endpoint_thread_handle_is_zero(thread->ipc_next);
        }
        if (endpoint_thread_handle_is_zero(thread->ipc_next)) {
            return false;
        }
        current = thread->ipc_next;
    }
    return false;
}

static bool pending_notification_state_is_valid(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_record *endpoint
)
{
    size_t index;

    if (!endpoint_pending_notification_state_is_shallow_valid(endpoint)) {
        return false;
    }
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct micros_endpoint_record *source;
        const struct micros_process *process;

        if (
            (
                endpoint->pending_notification_sources
                & (UINT64_C(1) << index)
            ) == 0
        ) {
            continue;
        }
        source = &registry->endpoints[index];
        process = &objects->processes[index];
        if (
            (
                source->state != MICROS_ENDPOINT_STATE_ACTIVE
                && source->state
                    != MICROS_ENDPOINT_STATE_SOURCE_ONLY
            )
            || source->owner.slot != index
            || process->slot_state
                != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || process->generation != source->owner.generation
            || process->primary_endpoint != source->value
            || process->privilege_profile == 0
        ) {
            return false;
        }
    }
    return true;
}

static bool receiver_matches_pending_notification(
    const struct micros_endpoint_registry *registry,
    const struct micros_endpoint_record *endpoint,
    const struct micros_thread *receiver
)
{
    struct micros_process_handle source;

    if (
        (
            receiver->runtime_flags
            & MICROS_THREAD_RTS_IPC_REPLY
        ) != 0
    ) {
        return false;
    }
    if (receiver->ipc_receive_source == MICROS_ENDPOINT_ANY) {
        return (
            endpoint->pending_kernel_events != 0
            || endpoint->pending_notification_sources != 0
        );
    }
    if (
        micros_endpoint_unpack(
            receiver->ipc_receive_source,
            &source
        ) != MICROS_ENDPOINT_OK
        || source.slot >= MICROS_PROCESS_CAPACITY
        || (
            endpoint->pending_notification_sources
            & (UINT64_C(1) << source.slot)
        ) == 0
    ) {
        return false;
    }
    return registry->endpoints[source.slot].value
        == receiver->ipc_receive_source;
}

static bool endpoint_has_matchable_work(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_record *endpoint
)
{
    struct micros_thread_handle receiver_handle =
        endpoint->receiver_head;
    size_t receiver_steps;

    if (endpoint_thread_handle_is_zero(endpoint->receiver_head)) {
        return false;
    }
    for (
        receiver_steps = 0;
        receiver_steps < MICROS_THREAD_CAPACITY;
        ++receiver_steps
    ) {
        const struct micros_thread *receiver =
            &objects->threads[receiver_handle.slot];
        struct micros_thread_handle sender_handle =
            endpoint->sender_head;
        size_t sender_steps;

        if (
            receiver_matches_pending_notification(
                registry,
                endpoint,
                receiver
            )
        ) {
            return true;
        }
        if (!endpoint_thread_handle_is_zero(sender_handle)) {
            for (
                sender_steps = 0;
                sender_steps < MICROS_THREAD_CAPACITY;
                ++sender_steps
            ) {
                const struct micros_thread *sender =
                    &objects->threads[sender_handle.slot];

                if (
                    receiver->ipc_receive_source
                        == MICROS_ENDPOINT_ANY
                    || receiver->ipc_receive_source
                        == sender->ipc_outbound_message.source
                ) {
                    return true;
                }
                if (endpoint_thread_handles_equal(
                    sender_handle,
                    endpoint->sender_tail
                )) {
                    break;
                }
                sender_handle = sender->ipc_next;
            }
        }
        if (endpoint_thread_handles_equal(
            receiver_handle,
            endpoint->receiver_tail
        )) {
            break;
        }
        receiver_handle = receiver->ipc_next;
    }
    return false;
}

enum micros_endpoint_error micros_endpoint_registry_validate_objects(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
)
{
    enum micros_endpoint_error endpoint_error;
    bool queued_threads[MICROS_THREAD_CAPACITY];
    size_t index;

    if (registry == NULL || objects == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    endpoint_error = micros_endpoint_registry_validate(registry);
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return endpoint_error;
    }
    if (
        micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return MICROS_ENDPOINT_ERROR_INVARIANT;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        queued_threads[index] = false;
    }

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct micros_endpoint_record *endpoint =
            &registry->endpoints[index];
        const struct micros_process *process =
            &objects->processes[index];
        const struct micros_privilege_profile *unused_profile;

        if (endpoint->state == MICROS_ENDPOINT_STATE_FREE) {
            if (
                process->slot_state
                    == MICROS_KERNEL_OBJECT_SLOT_LIVE
                && (
                    process->primary_endpoint
                        != MICROS_PROCESS_ENDPOINT_NONE
                    || process->privilege_profile != 0
                )
            ) {
                return MICROS_ENDPOINT_ERROR_INVARIANT;
            }
            continue;
        }
        if (
            process->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || process->generation != endpoint->owner.generation
            || process->primary_endpoint != endpoint->value
            || !process->endpoint_lifecycle_consumed
        ) {
            return MICROS_ENDPOINT_ERROR_INVARIANT;
        }
        if (process->privilege_profile != 0) {
            if (
                process->privilege_profile
                    >= MICROS_PRIVILEGE_PROFILE_CAPACITY
                || micros_privilege_profile_resolve(
                    registry,
                    (uint8_t)process->privilege_profile,
                    &unused_profile
                ) != MICROS_ENDPOINT_OK
            ) {
                return MICROS_ENDPOINT_ERROR_INVARIANT;
            }
        } else if (
            endpoint->state == MICROS_ENDPOINT_STATE_ACTIVE
            || endpoint->state
                == MICROS_ENDPOINT_STATE_SOURCE_ONLY
        ) {
            return MICROS_ENDPOINT_ERROR_INVARIANT;
        }
        if (
            endpoint->state == MICROS_ENDPOINT_STATE_SOURCE_ONLY
            && !source_only_process_threads_are_valid(
                registry,
                objects,
                endpoint->owner
            )
        ) {
            return MICROS_ENDPOINT_ERROR_INVARIANT;
        }
        if (
            (
                endpoint->state == MICROS_ENDPOINT_STATE_ACTIVE
                || endpoint->state
                    == MICROS_ENDPOINT_STATE_SOURCE_ONLY
            )
            && (
                !endpoint_queue_is_valid(
                    registry,
                    objects,
                    endpoint,
                    endpoint->sender_head,
                    endpoint->sender_tail,
                    MICROS_IPC_QUEUE_SENDER,
                    queued_threads
                )
                || !endpoint_queue_is_valid(
                    registry,
                    objects,
                    endpoint,
                    endpoint->receiver_head,
                    endpoint->receiver_tail,
                    MICROS_IPC_QUEUE_RECEIVER,
                    queued_threads
                )
                || !pending_notification_state_is_valid(
                    registry,
                    objects,
                    endpoint
                )
                || endpoint_has_matchable_work(
                    registry,
                    objects,
                    endpoint
                )
            )
        ) {
            return MICROS_ENDPOINT_ERROR_INVARIANT;
        }
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];
        size_t other_index;

        if (thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE) {
            continue;
        }
        if (thread->ipc_queue_kind == MICROS_IPC_QUEUE_NONE) {
            if (
                queued_threads[index]
                || (
                    micros_thread_ipc_state_is_clear(thread)
                        ? !thread_has_no_ipc_flags(thread)
                        : (
                            !reply_wait_state_is_valid(
                                registry,
                                thread
                            )
                            && !staged_delivery_state_is_valid(
                                registry,
                                objects,
                                thread
                            )
                            && !staged_error_state_is_valid(
                                registry,
                                thread
                            )
                            && !staged_no_message_success_state_is_valid(
                                registry,
                                thread
                            )
                        )
                )
            ) {
                return MICROS_ENDPOINT_ERROR_INVARIANT;
            }
        } else if (
            (
                thread->ipc_queue_kind != MICROS_IPC_QUEUE_SENDER
                && thread->ipc_queue_kind
                    != MICROS_IPC_QUEUE_RECEIVER
            )
            || !queued_threads[index]
        ) {
            return MICROS_ENDPOINT_ERROR_INVARIANT;
        }
        if (thread->ipc_reply_token == 0) {
            continue;
        }
        if (
            registry->last_reply_token == 0
            || thread->ipc_reply_token
                > registry->last_reply_token
        ) {
            return MICROS_ENDPOINT_ERROR_INVARIANT;
        }
        for (other_index = 0; other_index < index; ++other_index) {
            const struct micros_thread *other =
                &objects->threads[other_index];

            if (
                other->slot_state
                    == MICROS_KERNEL_OBJECT_SLOT_LIVE
                && other->ipc_reply_token
                    == thread->ipc_reply_token
            ) {
                return MICROS_ENDPOINT_ERROR_INVARIANT;
            }
        }
    }
    return MICROS_ENDPOINT_OK;
}

enum micros_endpoint_error micros_endpoint_reserve(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_process_handle process_handle,
    micros_endpoint_t *endpoint
)
{
    const struct micros_process *resolved_process;
    struct micros_process *process;
    struct micros_endpoint_record *record;
    enum micros_kernel_object_error object_error;
    enum micros_endpoint_error error;
    micros_endpoint_t value;

    if (registry == NULL || objects == NULL || endpoint == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_endpoint_registry_validate_objects(registry, objects);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    object_error = micros_process_resolve(
        objects,
        process_handle,
        &resolved_process
    );
    if (object_error != MICROS_KERNEL_OBJECT_OK) {
        return endpoint_process_resolve_error(object_error);
    }
    (void)resolved_process;
    process = &objects->processes[process_handle.slot];
    record = &registry->endpoints[process_handle.slot];
    if (
        process->primary_endpoint != MICROS_PROCESS_ENDPOINT_NONE
        || process->privilege_profile != 0
        || process->endpoint_lifecycle_consumed
        || record->state != MICROS_ENDPOINT_STATE_FREE
    ) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    error = micros_endpoint_pack(process_handle, &value);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }

    record->state = MICROS_ENDPOINT_STATE_RESERVED;
    record->value = value;
    record->owner = process_handle;
    process->primary_endpoint = value;
    process->endpoint_lifecycle_consumed = true;
    *endpoint = value;
    return MICROS_ENDPOINT_OK;
}

enum micros_endpoint_error micros_endpoint_install_profile(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_process_handle process_handle,
    uint8_t profile_id
)
{
    const struct micros_process *resolved_process;
    const struct micros_privilege_profile *unused_profile;
    struct micros_process *process;
    const struct micros_endpoint_record *record;
    enum micros_kernel_object_error object_error;
    enum micros_endpoint_error error;

    if (registry == NULL || objects == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_endpoint_registry_validate_objects(registry, objects);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    object_error = micros_process_resolve(
        objects,
        process_handle,
        &resolved_process
    );
    if (object_error != MICROS_KERNEL_OBJECT_OK) {
        return endpoint_process_resolve_error(object_error);
    }
    (void)resolved_process;
    process = &objects->processes[process_handle.slot];
    if (
        process->primary_endpoint == MICROS_PROCESS_ENDPOINT_NONE
        || process->privilege_profile != 0
    ) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    record = &registry->endpoints[process_handle.slot];
    if (
        record->state != MICROS_ENDPOINT_STATE_RESERVED
        || record->value != process->primary_endpoint
    ) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    error = micros_privilege_profile_resolve(
        registry,
        profile_id,
        &unused_profile
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (!endpoint_process_threads_are_held(objects, process_handle, true)) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }

    process->privilege_profile = profile_id;
    return MICROS_ENDPOINT_OK;
}

enum micros_endpoint_error micros_endpoint_activate(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint
)
{
    const struct micros_endpoint_record *record;
    const struct micros_process *process;
    enum micros_endpoint_error error;

    if (registry == NULL || objects == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_endpoint_registry_validate_objects(registry, objects);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    error = endpoint_record_resolve_validated(
        registry,
        endpoint,
        &record
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (record->state != MICROS_ENDPOINT_STATE_RESERVED) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    process = &objects->processes[record->owner.slot];
    if (process->privilege_profile == 0) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }

    registry->endpoints[record->owner.slot].state =
        MICROS_ENDPOINT_STATE_ACTIVE;
    return MICROS_ENDPOINT_OK;
}

enum micros_endpoint_error micros_endpoint_preflight_publish(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    uint8_t profile_id
)
{
    const struct micros_endpoint_record *record;
    const struct micros_privilege_profile *profile;
    const struct micros_process *process;
    enum micros_endpoint_error error;

    if (registry == NULL || objects == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_endpoint_registry_validate_objects(registry, objects);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    error = endpoint_record_resolve_validated(
        registry,
        endpoint,
        &record
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    error = micros_privilege_profile_resolve(
        registry,
        profile_id,
        &profile
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    process = &objects->processes[record->owner.slot];
    if (
        record->state != MICROS_ENDPOINT_STATE_RESERVED
        || !endpoint_ipc_state_is_zero(record)
        || process->privilege_profile != 0
        || !endpoint_process_threads_are_held(objects, record->owner, true)
    ) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    (void)profile;
    return MICROS_ENDPOINT_OK;
}

void micros_endpoint_commit_publish_prevalidated(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    uint8_t profile_id
)
{
    struct micros_process_handle owner;

    (void)micros_endpoint_unpack(endpoint, &owner);
    objects->processes[owner.slot].privilege_profile = profile_id;
    registry->endpoints[owner.slot].state =
        MICROS_ENDPOINT_STATE_ACTIVE;
}

enum micros_endpoint_error
micros_endpoint_preflight_source_only_for_teardown(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint
)
{
    const struct micros_endpoint_record *record;
    enum micros_endpoint_error error;
    if (registry == NULL || objects == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_endpoint_registry_validate_objects(registry, objects);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    error = endpoint_record_resolve_validated(
        registry,
        endpoint,
        &record
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (
        record->state != MICROS_ENDPOINT_STATE_ACTIVE
    ) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    return MICROS_ENDPOINT_OK;
}

enum micros_endpoint_error
micros_endpoint_preflight_source_only(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint
)
{
    const struct micros_endpoint_record *record;
    enum micros_endpoint_error error;
    size_t index;

    error = micros_endpoint_preflight_source_only_for_teardown(
        registry,
        objects,
        endpoint
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    error = endpoint_record_resolve_validated(
        registry,
        endpoint,
        &record
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (
        !endpoint_ipc_state_is_zero(record)
        || endpoint_is_pending_notification_source(registry, record)
    ) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];

        if (
            thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && (
                (
                    thread->ipc_queue_kind
                        == MICROS_IPC_QUEUE_RECEIVER
                    && thread->ipc_receive_source == endpoint
                )
                || (
                    thread->ipc_reply_token != 0
                    && thread->ipc_reply_callee == endpoint
                )
            )
        ) {
            return MICROS_ENDPOINT_ERROR_STATE;
        }
    }
    return MICROS_ENDPOINT_OK;
}

void micros_endpoint_commit_source_only_prevalidated(
    struct micros_endpoint_registry *registry,
    micros_endpoint_t endpoint
)
{
    struct micros_process_handle owner;

    (void)micros_endpoint_unpack(endpoint, &owner);
    registry->endpoints[owner.slot].state =
        MICROS_ENDPOINT_STATE_SOURCE_ONLY;
}

enum micros_endpoint_error micros_endpoint_seal_source_only(
    struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint
)
{
    const struct micros_endpoint_record *record;
    enum micros_endpoint_error error;

    if (registry == NULL || objects == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_endpoint_preflight_source_only(
        registry,
        objects,
        endpoint
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    error = endpoint_record_resolve_validated(
        registry,
        endpoint,
        &record
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (
        !endpoint_process_threads_are_held(objects, record->owner, false)
        || !endpoint_process_threads_have_clear_ipc_state(objects, record->owner)
    ) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    micros_endpoint_commit_source_only_prevalidated(
        registry,
        endpoint
    );
    return MICROS_ENDPOINT_OK;
}

enum micros_endpoint_error micros_endpoint_resolve_internal(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    const struct micros_endpoint_record **record
)
{
    enum micros_endpoint_error error;

    if (registry == NULL || objects == NULL || record == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_endpoint_registry_validate_objects(registry, objects);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    return endpoint_record_resolve_validated(registry, endpoint, record);
}

enum micros_endpoint_error micros_endpoint_resolve_active(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    const struct micros_endpoint_record **record
)
{
    enum micros_endpoint_error error;
    const struct micros_endpoint_record *resolved_record;

    if (record == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_endpoint_resolve_internal(
        registry,
        objects,
        endpoint,
        &resolved_record
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (resolved_record->state != MICROS_ENDPOINT_STATE_ACTIVE) {
        return resolved_record->state
                == MICROS_ENDPOINT_STATE_SOURCE_ONLY
            ? MICROS_ENDPOINT_ERROR_CLOSING
            : MICROS_ENDPOINT_ERROR_STATE;
    }
    *record = resolved_record;
    return MICROS_ENDPOINT_OK;
}

static bool endpoint_has_foreign_waiters(
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint
)
{
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];

        if (thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE) {
            continue;
        }
        if (
            (
                thread->ipc_queue_kind
                    == MICROS_IPC_QUEUE_RECEIVER
                && thread->ipc_receive_source == endpoint
            )
            || (
                thread->ipc_reply_token != 0
                && thread->ipc_reply_callee == endpoint
            )
            || (
                thread->ipc_delivery_pending
                && thread->ipc_staged_result == MICROS_IPC_OK
                && thread->ipc_inbound_message.source == endpoint
            )
        ) {
            return true;
        }
    }
    return false;
}

static bool endpoint_is_pending_notification_source(
    const struct micros_endpoint_registry *registry,
    const struct micros_endpoint_record *source
)
{
    uint64_t source_bit = UINT64_C(1) << source->owner.slot;
    size_t index;

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct micros_endpoint_record *destination =
            &registry->endpoints[index];

        if (
            (
                destination->state
                    == MICROS_ENDPOINT_STATE_ACTIVE
                || destination->state
                    == MICROS_ENDPOINT_STATE_SOURCE_ONLY
            )
            && (
                destination->pending_notification_sources
                & source_bit
            ) != 0
        ) {
            return true;
        }
    }
    return false;
}

void micros_endpoint_close_commit_prevalidated(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_process_handle owner
)
{
    struct micros_endpoint_record *record =
        &registry->endpoints[owner.slot];
    struct micros_process *process =
        &objects->processes[owner.slot];

    endpoint_clear_endpoint_record(record);
    process->primary_endpoint = MICROS_PROCESS_ENDPOINT_NONE;
    process->privilege_profile = 0;
}

enum micros_endpoint_error micros_endpoint_close(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint
)
{
    const struct micros_endpoint_record *resolved_record;
    struct micros_process_handle owner;
    enum micros_endpoint_error error;

    if (registry == NULL || objects == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_endpoint_registry_validate_objects(registry, objects);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    error = endpoint_record_resolve_validated(
        registry,
        endpoint,
        &resolved_record
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (!endpoint_ipc_state_is_zero(resolved_record)) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    if (
        endpoint_is_pending_notification_source(
            registry,
            resolved_record
        )
    ) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    if (endpoint_has_foreign_waiters(objects, endpoint)) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    owner = resolved_record->owner;
    if (
        !endpoint_process_threads_are_held(objects, owner, false)
        || !endpoint_process_threads_have_clear_ipc_state(objects, owner)
    ) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }

    micros_endpoint_close_commit_prevalidated(
        registry,
        objects,
        owner
    );
    return MICROS_ENDPOINT_OK;
}

enum micros_endpoint_error micros_endpoint_authorize_operation(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t source,
    uint32_t operation
)
{
    const struct micros_endpoint_record *record;
    const struct micros_process *process;
    enum micros_endpoint_error error;

    error = micros_endpoint_resolve_active(
        registry,
        objects,
        source,
        &record
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    process = &objects->processes[record->owner.slot];
    return micros_privilege_profile_allows_operation(
        registry,
        (uint8_t)process->privilege_profile,
        operation
    );
}

enum micros_endpoint_error micros_endpoint_authorize_target(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t source,
    uint32_t operation,
    micros_endpoint_t target
)
{
    const struct micros_endpoint_record *source_record;
    const struct micros_endpoint_record *target_record;
    const struct micros_process *source_process;
    const struct micros_process *target_process;
    enum micros_endpoint_error error;

    if (
        operation != MICROS_PRIVILEGE_OPERATION_CALL
        && operation != MICROS_PRIVILEGE_OPERATION_SEND
        && operation != MICROS_PRIVILEGE_OPERATION_NOTIFY
    ) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_endpoint_resolve_active(
        registry,
        objects,
        source,
        &source_record
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    error = micros_endpoint_resolve_active(
        registry,
        objects,
        target,
        &target_record
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    source_process = &objects->processes[source_record->owner.slot];
    target_process = &objects->processes[target_record->owner.slot];
    return micros_privilege_profile_allows_target(
        registry,
        (uint8_t)source_process->privilege_profile,
        operation,
        (uint8_t)target_process->privilege_profile
    );
}

enum micros_endpoint_error
micros_endpoint_authorize_kernel_operation(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t source,
    uint8_t operation
)
{
    const struct micros_endpoint_record *record;
    const struct micros_process *process;
    enum micros_endpoint_error error;

    error = micros_endpoint_resolve_active(
        registry,
        objects,
        source,
        &record
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    process = &objects->processes[record->owner.slot];
    return micros_privilege_profile_allows_kernel_operation(
        registry,
        (uint8_t)process->privilege_profile,
        operation
    );
}
