#include "kernel/endpoint/endpoint_private.h"

#include "kernel/scheduler/scheduler_core_internal.h"

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
        || !endpoint_delivery_state_is_clear(thread)
        || !endpoint_thread_handle_is_zero(thread->ipc_request_receiver)
    ) {
        return false;
    }
    if (!is_call) {
        return (
            thread->ipc_outbound_message.reply_token == 0
            && thread->ipc_receive_buffer == 0
            && thread->ipc_reply_token == 0
            && thread->ipc_reply_callee == 0
            && thread->ipc_reply_phase == MICROS_IPC_REPLY_PHASE_NONE
        );
    }
    return (
        thread->ipc_reply_token != 0
        && thread->ipc_reply_phase == MICROS_IPC_REPLY_PHASE_QUEUED
        && thread->ipc_reply_callee == destination
        && thread->ipc_outbound_message.reply_token
            == thread->ipc_reply_token
        && thread->ipc_receive_buffer != 0
        && thread->ipc_receive_buffer % 8 == 0
    );
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
        || !endpoint_delivery_state_is_clear(thread)
        || thread->ipc_reply_token != 0
        || thread->ipc_reply_callee != 0
        || thread->ipc_reply_phase != MICROS_IPC_REPLY_PHASE_NONE
        || !endpoint_thread_handle_is_zero(thread->ipc_request_receiver)
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

bool micros_endpoint_queue_member_is_valid(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_selection *destination,
    struct micros_thread_handle handle,
    enum micros_ipc_queue_kind kind,
    const struct micros_thread **result
)
{
    const struct micros_thread *thread;
    const struct micros_thread *next;
    struct micros_endpoint_selection source;
    uint16_t token_slot;

    if (
        registry == NULL || destination == NULL || result == NULL
        || destination->record == NULL || destination->process == NULL
        || (destination->record->state != MICROS_ENDPOINT_STATE_ACTIVE
            && destination->record->state != MICROS_ENDPOINT_STATE_SOURCE_ONLY)
        || scheduler_core_preflight_thread(objects, handle, &thread)
            != MICROS_KERNEL_OBJECT_OK
        || (thread->ipc_reply_token != 0
            && (thread->ipc_reply_token > registry->last_reply_token
                || !micros_endpoint_reply_token_slot(thread->ipc_reply_token, &token_slot)
                || token_slot != handle.slot))
        || (!endpoint_thread_handle_is_zero(thread->ipc_next)
            && (endpoint_thread_handles_equal(handle, thread->ipc_next)
                || micros_thread_resolve(objects, thread->ipc_next, &next)
                    != MICROS_KERNEL_OBJECT_OK))
    ) {
        return false;
    }
    if (kind == MICROS_IPC_QUEUE_SENDER) {
        if (
            micros_endpoint_select(
                registry, objects, objects->processes[thread->owner.slot].primary_endpoint,
                &source
            ) != MICROS_ENDPOINT_OK
            || source.record->state != MICROS_ENDPOINT_STATE_ACTIVE
            || !sender_queue_state_is_valid(registry, thread, destination->record->value)
        ) {
            return false;
        }
    } else if (kind == MICROS_IPC_QUEUE_RECEIVER) {
        if (
            destination->record->state != MICROS_ENDPOINT_STATE_ACTIVE
            || (thread->ipc_receive_source != MICROS_ENDPOINT_ANY
                && (micros_endpoint_select(
                    registry, objects, thread->ipc_receive_source, &source
                ) != MICROS_ENDPOINT_OK
                    || (source.record->state != MICROS_ENDPOINT_STATE_ACTIVE
                        && source.record->state != MICROS_ENDPOINT_STATE_SOURCE_ONLY)))
            || !receiver_queue_state_is_valid(registry, thread, destination->record)
        ) {
            return false;
        }
    } else {
        return false;
    }
    *result = thread;
    return true;
}

static bool selected_queue_is_valid(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_selection *selection,
    enum micros_ipc_queue_kind kind
)
{
    struct micros_thread_handle current = kind == MICROS_IPC_QUEUE_SENDER
        ? selection->record->sender_head : selection->record->receiver_head;
    struct micros_thread_handle tail = kind == MICROS_IPC_QUEUE_SENDER
        ? selection->record->sender_tail : selection->record->receiver_tail;

    if (endpoint_thread_handle_is_zero(current)) {
        return endpoint_thread_handle_is_zero(tail);
    }
    for (size_t steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        const struct micros_thread *thread;

        if (!micros_endpoint_queue_member_is_valid(registry, objects, selection, current, kind, &thread)) {
            return false;
        }
        if (endpoint_thread_handles_equal(current, tail)) {
            return endpoint_thread_handle_is_zero(thread->ipc_next);
        }
        current = thread->ipc_next;
    }
    return false;
}

enum micros_endpoint_error endpoint_preflight_record(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_selection *selection
)
{
    if (selection->record->state == MICROS_ENDPOINT_STATE_RESERVED) {
        return endpoint_ipc_state_is_zero(selection->record)
            ? MICROS_ENDPOINT_OK : MICROS_ENDPOINT_ERROR_INVARIANT;
    }
    return selected_queue_is_valid(registry, objects, selection, MICROS_IPC_QUEUE_SENDER)
        && selected_queue_is_valid(registry, objects, selection, MICROS_IPC_QUEUE_RECEIVER)
        && endpoint_pending_notification_state_is_valid(registry, objects, selection->record)
        ? MICROS_ENDPOINT_OK : MICROS_ENDPOINT_ERROR_INVARIANT;
}

bool endpoint_queue_is_valid(
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

bool endpoint_pending_notification_state_is_valid(
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
        struct micros_endpoint_selection selected;

        if (
            (
                endpoint->pending_notification_sources
                & (UINT64_C(1) << index)
            ) == 0
        ) {
            continue;
        }
        source = &registry->endpoints[index];
        if (
            (
                source->state != MICROS_ENDPOINT_STATE_ACTIVE
                && source->state
                    != MICROS_ENDPOINT_STATE_SOURCE_ONLY
            )
            || source->owner.slot != index
            || micros_endpoint_select(registry, objects, source->value, &selected)
                != MICROS_ENDPOINT_OK
            || selected.record != source
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

bool endpoint_has_matchable_work(
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
