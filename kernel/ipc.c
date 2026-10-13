#include "ipc_internal.h"

#include <stddef.h>

#include "endpoint_internal.h"
#include "scheduler_core_internal.h"

enum ipc_close_thread_action {
    IPC_CLOSE_THREAD_NONE = 0,
    IPC_CLOSE_THREAD_CLEAR,
    IPC_CLOSE_THREAD_DEAD_ENDPOINT,
};

struct ipc_close_thread_plan {
    enum ipc_close_thread_action action;
    bool remove_from_queue;
};

struct ipc_endpoint_close_plan {
    struct micros_process_handle owner;
    micros_endpoint_t endpoint;
    struct ipc_close_thread_plan threads[MICROS_THREAD_CAPACITY];
    struct micros_scheduler_ipc_transition
        transitions[MICROS_THREAD_CAPACITY];
    size_t transition_count;
};

static bool thread_has_staged_reference(
    const struct micros_thread *thread,
    micros_endpoint_t endpoint
)
{
    return (
        thread->ipc_delivery_pending
        && thread->ipc_staged_result == MICROS_IPC_OK
        && thread->ipc_inbound_message.source == endpoint
    );
}

static bool thread_has_endpoint_reference(
    const struct micros_thread *thread,
    micros_endpoint_t endpoint
)
{
    return (
        thread->ipc_send_destination == endpoint
        || thread->ipc_receive_source == endpoint
        || thread->ipc_reply_callee == endpoint
        || thread->ipc_outbound_message.source == endpoint
        || thread_has_staged_reference(thread, endpoint)
    );
}

static enum micros_ipc_error add_close_transition(
    struct ipc_endpoint_close_plan *plan,
    struct micros_thread_handle handle,
    uint32_t clear_flags
)
{
    if (clear_flags == 0) {
        return MICROS_IPC_OK;
    }
    if (plan->transition_count >= MICROS_THREAD_CAPACITY) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    plan->transitions[plan->transition_count].handle = handle;
    plan->transitions[plan->transition_count].clear_flags =
        clear_flags;
    plan->transitions[plan->transition_count].set_flags = 0;
    ++plan->transition_count;
    return MICROS_IPC_OK;
}

static enum micros_ipc_error preflight_close_threads(
    const struct micros_kernel_objects *objects,
    struct ipc_endpoint_close_plan *plan
)
{
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread =
            &objects->threads[index];
        struct micros_thread_handle handle;
        uint32_t clear_flags;
        bool owned;
        bool queued_sender;
        bool queued_receiver;
        bool reply_wait;
        bool staged_reference;
        enum micros_ipc_error error;

        if (
            thread->slot_state
                != MICROS_KERNEL_OBJECT_SLOT_LIVE
        ) {
            continue;
        }
        handle.slot = (uint16_t)index;
        handle.generation = thread->generation;
        owned = ipc_process_handles_equal(thread->owner, plan->owner);
        queued_sender =
            (
                thread->ipc_queue_kind
                == MICROS_IPC_QUEUE_SENDER
            )
            && thread->ipc_send_destination == plan->endpoint;
        queued_receiver =
            (
                thread->ipc_queue_kind
                == MICROS_IPC_QUEUE_RECEIVER
            )
            && thread->ipc_receive_source == plan->endpoint;
        reply_wait =
            thread->ipc_reply_token != 0
            && thread->ipc_reply_callee == plan->endpoint;
        staged_reference =
            thread_has_staged_reference(thread, plan->endpoint);
        clear_flags =
            thread->runtime_flags & MICROS_THREAD_RTS_IPC_MASK;

        if (owned) {
            if (
                ipc_thread_is_current(objects, handle)
                || (
                    thread->ipc_delivery_pending
                    && thread->ipc_staged_result == MICROS_IPC_OK
                )
                || (
                    thread->runtime_flags
                    & MICROS_THREAD_RTS_INACTIVE
                ) == 0
                || (
                    thread->runtime_flags
                    & ~MICROS_THREAD_RTS_IPC_MASK
                ) != MICROS_THREAD_RTS_INACTIVE
            ) {
                return MICROS_IPC_ERROR_STATE;
            }
            if (!micros_thread_ipc_state_is_clear(thread)) {
                plan->threads[index].action =
                    IPC_CLOSE_THREAD_CLEAR;
            }
            plan->threads[index].remove_from_queue =
                thread->ipc_queue_kind != MICROS_IPC_QUEUE_NONE;
        } else if (
            queued_sender
            || queued_receiver
            || reply_wait
        ) {
            plan->threads[index].action =
                IPC_CLOSE_THREAD_DEAD_ENDPOINT;
            plan->threads[index].remove_from_queue =
                queued_sender || queued_receiver;
        } else if (staged_reference) {
            return MICROS_IPC_ERROR_STATE;
        }
        if (
            thread_has_endpoint_reference(thread, plan->endpoint)
            && plan->threads[index].action
                == IPC_CLOSE_THREAD_NONE
        ) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
        if (
            plan->threads[index].action
                == IPC_CLOSE_THREAD_NONE
            || clear_flags == 0
        ) {
            continue;
        }
        error = add_close_transition(plan, handle, clear_flags);
        if (error != MICROS_IPC_OK) {
            return error;
        }
    }
    return MICROS_IPC_OK;
}

static enum micros_ipc_error preflight_close_queue(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle head,
    bool seen[MICROS_THREAD_CAPACITY]
)
{
    struct micros_thread_handle current = head;
    size_t steps;

    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        const struct micros_thread *thread;

        if (ipc_thread_handle_is_zero(current)) {
            return MICROS_IPC_OK;
        }
        if (
            current.slot >= MICROS_THREAD_CAPACITY
            || current.generation == 0
            || seen[current.slot]
            || micros_thread_resolve(objects, current, &thread)
                != MICROS_KERNEL_OBJECT_OK
        ) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
        seen[current.slot] = true;
        current = thread->ipc_next;
    }
    return MICROS_IPC_ERROR_INVARIANT;
}

static enum micros_ipc_error preflight_close_queues(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct ipc_endpoint_close_plan *plan
)
{
    bool seen[MICROS_THREAD_CAPACITY];
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        seen[index] = false;
    }
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        enum micros_ipc_error error;

        error = preflight_close_queue(
            objects,
            registry->endpoints[index].sender_head,
            seen
        );
        if (error != MICROS_IPC_OK) {
            return error;
        }
        error = preflight_close_queue(
            objects,
            registry->endpoints[index].receiver_head,
            seen
        );
        if (error != MICROS_IPC_OK) {
            return error;
        }
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        if (
            plan->threads[index].remove_from_queue
            && !seen[index]
        ) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
    }
    return MICROS_IPC_OK;
}

static enum micros_ipc_error preflight_close_notifications(
    const struct micros_endpoint_registry *registry,
    const struct ipc_endpoint_close_plan *plan
)
{
    uint64_t source_bit =
        UINT64_C(1) << plan->owner.slot;
    size_t index;

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct micros_endpoint_record *destination =
            &registry->endpoints[index];

        if (
            (
                destination->pending_notification_sources
                & source_bit
            ) != 0
            && (
                destination->pending_events[plan->owner.slot] == 0
                || registry->endpoints[plan->owner.slot].value
                    != plan->endpoint
            )
        ) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
    }
    return MICROS_IPC_OK;
}

static enum micros_ipc_error preflight_endpoint_close(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    struct ipc_endpoint_close_plan *plan
)
{
    const struct micros_endpoint_record *record;
    enum micros_endpoint_error endpoint_error;
    enum micros_ipc_error error;
    unsigned char *plan_bytes;
    size_t index;

    endpoint_error = micros_endpoint_resolve_internal(
        registry,
        objects,
        endpoint,
        &record
    );
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return ipc_endpoint_error_to_ipc(endpoint_error);
    }
    plan_bytes = (unsigned char *)plan;
    for (index = 0; index < sizeof(*plan); ++index) {
        plan_bytes[index] = 0;
    }
    plan->owner = record->owner;
    plan->endpoint = endpoint;
    error = preflight_close_threads(objects, plan);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = preflight_close_queues(registry, objects, plan);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    return preflight_close_notifications(registry, plan);
}

static void commit_close_queue(
    struct micros_kernel_objects *objects,
    const struct ipc_endpoint_close_plan *plan,
    struct micros_thread_handle *head,
    struct micros_thread_handle *tail
)
{
    struct micros_thread_handle current = *head;
    struct micros_thread_handle retained_head = {0, 0};
    struct micros_thread_handle retained_tail = {0, 0};
    size_t steps;

    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        struct micros_thread *thread;
        struct micros_thread_handle next;

        if (ipc_thread_handle_is_zero(current)) {
            break;
        }
        thread = &objects->threads[current.slot];
        next = thread->ipc_next;
        if (!plan->threads[current.slot].remove_from_queue) {
            if (ipc_thread_handle_is_zero(retained_head)) {
                retained_head = current;
            } else {
                objects->threads[
                    retained_tail.slot
                ].ipc_next = current;
            }
            retained_tail = current;
        }
        current = next;
    }
    if (!ipc_thread_handle_is_zero(retained_tail)) {
        objects->threads[retained_tail.slot].ipc_next =
            (struct micros_thread_handle){0, 0};
    }
    *head = retained_head;
    *tail = retained_tail;
}

static void clear_thread_ipc_state_for_close(
    struct micros_thread *thread
)
{
    thread->ipc_queue_kind = MICROS_IPC_QUEUE_NONE;
    thread->ipc_next = (struct micros_thread_handle){0, 0};
    ipc_clear_message(&thread->ipc_outbound_message);
    thread->ipc_send_destination = 0;
    thread->ipc_receive_source = 0;
    thread->ipc_receive_buffer = 0;
    thread->ipc_delivery_pending = false;
    ipc_clear_message(&thread->ipc_inbound_message);
    thread->ipc_staged_result = MICROS_IPC_OK;
    thread->ipc_reply_token = 0;
    thread->ipc_reply_callee = 0;
}

static void stage_dead_endpoint_for_close(
    struct micros_thread *thread
)
{
    clear_thread_ipc_state_for_close(thread);
    thread->ipc_delivery_pending = true;
    thread->ipc_staged_result = MICROS_IPC_ERROR_DEAD_ENDPOINT;
}

static void commit_endpoint_close(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    const struct ipc_endpoint_close_plan *plan
)
{
    uint64_t source_bit = UINT64_C(1) << plan->owner.slot;
    size_t index;

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        struct micros_endpoint_record *record =
            &registry->endpoints[index];

        commit_close_queue(
            objects,
            plan,
            &record->sender_head,
            &record->sender_tail
        );
        commit_close_queue(
            objects,
            plan,
            &record->receiver_head,
            &record->receiver_tail
        );
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        struct micros_thread *thread = &objects->threads[index];

        if (
            plan->threads[index].action
                == IPC_CLOSE_THREAD_CLEAR
        ) {
            clear_thread_ipc_state_for_close(thread);
        } else if (
            plan->threads[index].action
                == IPC_CLOSE_THREAD_DEAD_ENDPOINT
        ) {
            stage_dead_endpoint_for_close(thread);
        }
    }
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        struct micros_endpoint_record *record =
            &registry->endpoints[index];

        record->pending_notification_sources &= ~source_bit;
        record->pending_events[plan->owner.slot] = 0;
    }
    registry->endpoints[
        plan->owner.slot
    ].pending_notification_sources = 0;
    registry->endpoints[
        plan->owner.slot
    ].pending_kernel_events = 0;
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        registry->endpoints[
            plan->owner.slot
        ].pending_events[index] = 0;
    }
    micros_endpoint_close_commit_prevalidated(
        registry,
        objects,
        plan->owner
    );
}

enum micros_ipc_error micros_ipc_endpoint_close(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint
)
{
    struct ipc_endpoint_close_plan plan;
    enum micros_kernel_object_error scheduler_error;
    enum micros_ipc_error error;

    if (registry == NULL || objects == NULL) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    error = preflight_endpoint_close(
        registry,
        objects,
        endpoint,
        &plan
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    if (plan.transition_count != 0) {
        scheduler_error = micros_scheduler_commit_ipc_transitions(
            objects,
            plan.transitions,
            plan.transition_count
        );
        if (scheduler_error != MICROS_KERNEL_OBJECT_OK) {
            return ipc_scheduler_error_to_ipc(scheduler_error);
        }
    }
    commit_endpoint_close(registry, objects, &plan);
    return MICROS_IPC_OK;
}

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

static enum micros_ipc_error sender_commit_delivery(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle sender_handle,
    micros_endpoint_t destination_endpoint,
    const struct micros_ipc_message *message,
    uint64_t reply_token,
    uintptr_t reply_buffer,
    struct micros_thread_handle *matched_receiver
);

static enum micros_ipc_error sender_enqueue(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle sender_handle,
    micros_endpoint_t destination_endpoint,
    const struct micros_ipc_message *message,
    uint64_t reply_token,
    uintptr_t reply_buffer,
    bool allocates_reply_token
)
{
    const struct micros_endpoint_record *resolved_destination;
    const struct micros_endpoint_record *source;
    struct micros_endpoint_record *destination;
    struct micros_thread *sender;
    struct micros_thread *previous_tail;
    enum micros_endpoint_error endpoint_error;
    enum micros_ipc_error error;

    if (
        registry == NULL
        || objects == NULL
        || message == NULL
        || (
            message->type
            & MICROS_IPC_TYPE_KERNEL_MASK
        ) != 0
        || (
            reply_token == 0
                ? reply_buffer != 0
                : (
                    reply_buffer == 0
                    || reply_buffer % 8 != 0
                )
        )
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    endpoint_error =
        micros_endpoint_registry_validate_objects(registry, objects);
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return ipc_endpoint_error_to_ipc(endpoint_error);
    }
    error = ipc_resolve_held_thread(objects, sender_handle, &sender);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    endpoint_error = micros_endpoint_resolve_active(
        registry,
        objects,
        destination_endpoint,
        &resolved_destination
    );
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return ipc_endpoint_error_to_ipc(endpoint_error);
    }
    source = &registry->endpoints[sender->owner.slot];
    if (
        source->state != MICROS_ENDPOINT_STATE_ACTIVE
        || source->owner.generation != sender->owner.generation
    ) {
        return MICROS_IPC_ERROR_STATE;
    }
    if (
        reply_token != 0
        && (
            !allocates_reply_token
            || registry->last_reply_token == UINT64_MAX
            || reply_token != registry->last_reply_token + 1
        )
    ) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    if (!ipc_reply_token_is_available(objects, reply_token)) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    if (
        ipc_receiver_is_ready_for(
            resolved_destination,
            objects,
            source->value
        )
    ) {
        return MICROS_IPC_ERROR_NOT_READY;
    }
    destination =
        &registry->endpoints[resolved_destination->owner.slot];
    error = ipc_resolve_queue_tail(
        objects,
        destination->sender_tail,
        &previous_tail
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }

    sender->runtime_flags = MICROS_THREAD_RTS_IPC_SEND;
    if (reply_token != 0) {
        sender->runtime_flags |= MICROS_THREAD_RTS_IPC_REPLY;
    }
    sender->ipc_queue_kind = MICROS_IPC_QUEUE_SENDER;
    sender->ipc_next.slot = 0;
    sender->ipc_next.generation = 0;
    ipc_canonicalize_message(
        &sender->ipc_outbound_message,
        message,
        source->value,
        reply_token
    );
    sender->ipc_send_destination = destination_endpoint;
    sender->ipc_receive_buffer = reply_buffer;
    sender->ipc_reply_token = reply_token;
    sender->ipc_reply_callee =
        reply_token == 0 ? 0 : destination_endpoint;
    ipc_append_thread(
        sender_handle,
        previous_tail,
        &destination->sender_head,
        &destination->sender_tail
    );
    return MICROS_IPC_OK;
}

enum micros_ipc_error micros_ipc_sender_enqueue(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle sender_handle,
    micros_endpoint_t destination_endpoint,
    const struct micros_ipc_message *message,
    uint64_t reply_token,
    uintptr_t reply_buffer
)
{
    return sender_enqueue(
        registry,
        objects,
        sender_handle,
        destination_endpoint,
        message,
        reply_token,
        reply_buffer,
        false
    );
}

enum micros_ipc_error micros_ipc_receiver_commit_delivery(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle receiver_handle,
    micros_endpoint_t source_endpoint,
    uintptr_t receive_buffer,
    struct micros_thread_handle *matched_sender
)
{
    const struct micros_endpoint_record *resolved_source;
    struct micros_endpoint_record *receiver_endpoint;
    struct ipc_pending_notification_plan notification;
    struct micros_thread_handle previous;
    struct micros_thread_handle sender_handle;
    struct micros_thread *receiver;
    struct micros_thread *sender;
    struct micros_ipc_message staged_message;
    enum micros_endpoint_error endpoint_error;
    enum micros_kernel_object_error scheduler_error;
    enum micros_ipc_error error;

    if (
        registry == NULL
        || objects == NULL
        || matched_sender == NULL
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
    error = ipc_resolve_held_thread(objects, receiver_handle, &receiver);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    receiver_endpoint =
        &registry->endpoints[receiver->owner.slot];
    if (
        receiver_endpoint->state != MICROS_ENDPOINT_STATE_ACTIVE
        || receiver_endpoint->owner.generation
            != receiver->owner.generation
    ) {
        return MICROS_IPC_ERROR_STATE;
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
    error = ipc_find_pending_notification(
        registry,
        objects,
        receiver_endpoint,
        source_endpoint,
        &notification
    );
    if (error == MICROS_IPC_OK) {
        scheduler_error = micros_scheduler_commit_ipc_wake(
            objects,
            receiver_handle,
            MICROS_THREAD_RTS_INACTIVE
        );
        if (scheduler_error != MICROS_KERNEL_OBJECT_OK) {
            return ipc_scheduler_error_to_ipc(scheduler_error);
        }

        ipc_consume_pending_notification(
            receiver_endpoint,
            &notification
        );
        ipc_stage_delivery(
            receiver,
            receive_buffer,
            &notification.message
        );
        matched_sender->slot = 0;
        matched_sender->generation = 0;
        return MICROS_IPC_OK;
    }
    if (error != MICROS_IPC_ERROR_NOT_READY) {
        return error;
    }
    if (
        !ipc_find_matching_sender(
            receiver_endpoint,
            objects,
            source_endpoint,
            &previous,
            &sender_handle,
            &sender
        )
    ) {
        return MICROS_IPC_ERROR_NOT_READY;
    }
    staged_message = sender->ipc_outbound_message;
    scheduler_error = micros_scheduler_commit_ipc_wake_pair(
        objects,
        receiver_handle,
        MICROS_THREAD_RTS_INACTIVE,
        sender_handle,
        MICROS_THREAD_RTS_IPC_SEND
    );
    if (scheduler_error != MICROS_KERNEL_OBJECT_OK) {
        return ipc_scheduler_error_to_ipc(scheduler_error);
    }

    ipc_unlink_thread(
        objects,
        previous,
        sender_handle,
        &receiver_endpoint->sender_head,
        &receiver_endpoint->sender_tail
    );
    ipc_clear_delivered_sender(sender);
    ipc_stage_delivery(receiver, receive_buffer, &staged_message);
    *matched_sender = sender_handle;
    return MICROS_IPC_OK;
}

static enum micros_ipc_error sender_commit_delivery(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle sender_handle,
    micros_endpoint_t destination_endpoint,
    const struct micros_ipc_message *message,
    uint64_t reply_token,
    uintptr_t reply_buffer,
    struct micros_thread_handle *matched_receiver
)
{
    const struct micros_endpoint_record *resolved_destination;
    const struct micros_endpoint_record *source;
    struct micros_endpoint_record *destination;
    struct micros_thread_handle previous;
    struct micros_thread_handle receiver_handle;
    struct micros_thread *sender;
    struct micros_thread *receiver;
    struct micros_ipc_message staged_message;
    uintptr_t receive_buffer;
    enum micros_endpoint_error endpoint_error;
    enum micros_kernel_object_error scheduler_error;
    enum micros_ipc_error error;

    if (
        registry == NULL
        || objects == NULL
        || message == NULL
        || matched_receiver == NULL
        || (
            message->type
            & MICROS_IPC_TYPE_KERNEL_MASK
        ) != 0
        || (
            reply_token == 0
                ? reply_buffer != 0
                : (
                    reply_buffer == 0
                    || reply_buffer % 8 != 0
                )
        )
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    endpoint_error =
        micros_endpoint_registry_validate_objects(registry, objects);
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return ipc_endpoint_error_to_ipc(endpoint_error);
    }
    error = ipc_resolve_held_thread(objects, sender_handle, &sender);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    endpoint_error = micros_endpoint_resolve_active(
        registry,
        objects,
        destination_endpoint,
        &resolved_destination
    );
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return ipc_endpoint_error_to_ipc(endpoint_error);
    }
    source = &registry->endpoints[sender->owner.slot];
    if (
        source->state != MICROS_ENDPOINT_STATE_ACTIVE
        || source->owner.generation != sender->owner.generation
    ) {
        return MICROS_IPC_ERROR_STATE;
    }
    if (
        reply_token != 0
        && (
            registry->last_reply_token == UINT64_MAX
            || reply_token != registry->last_reply_token + 1
        )
    ) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    if (!ipc_reply_token_is_available(objects, reply_token)) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    destination =
        &registry->endpoints[resolved_destination->owner.slot];
    if (
        !ipc_find_matching_receiver(
            destination,
            objects,
            source->value,
            &previous,
            &receiver_handle,
            &receiver
        )
    ) {
        return MICROS_IPC_ERROR_NOT_READY;
    }
    if (
        (
            receiver->runtime_flags
            & MICROS_THREAD_RTS_IPC_MASK
        ) != MICROS_THREAD_RTS_IPC_RECEIVE
    ) {
        return MICROS_IPC_ERROR_STATE;
    }
    receive_buffer = receiver->ipc_receive_buffer;
    ipc_canonicalize_message(
        &staged_message,
        message,
        source->value,
        reply_token
    );
    scheduler_error =
        reply_token == 0
            ? micros_scheduler_commit_ipc_wake_pair(
                objects,
                sender_handle,
                MICROS_THREAD_RTS_INACTIVE,
                receiver_handle,
                MICROS_THREAD_RTS_IPC_RECEIVE
            )
            : micros_scheduler_commit_ipc_call_delivery(
                objects,
                sender_handle,
                receiver_handle
            );
    if (scheduler_error != MICROS_KERNEL_OBJECT_OK) {
        return ipc_scheduler_error_to_ipc(scheduler_error);
    }

    ipc_unlink_thread(
        objects,
        previous,
        receiver_handle,
        &destination->receiver_head,
        &destination->receiver_tail
    );
    receiver->ipc_queue_kind = MICROS_IPC_QUEUE_NONE;
    receiver->ipc_next.slot = 0;
    receiver->ipc_next.generation = 0;
    receiver->ipc_receive_source = 0;
    ipc_stage_delivery(receiver, receive_buffer, &staged_message);
    if (reply_token != 0) {
        sender->ipc_receive_buffer = reply_buffer;
        sender->ipc_reply_token = reply_token;
        sender->ipc_reply_callee = destination_endpoint;
    }
    *matched_receiver = receiver_handle;
    return MICROS_IPC_OK;
}

enum micros_ipc_error micros_ipc_sender_commit_delivery(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle sender_handle,
    micros_endpoint_t destination_endpoint,
    const struct micros_ipc_message *message,
    struct micros_thread_handle *matched_receiver
)
{
    return sender_commit_delivery(
        registry,
        objects,
        sender_handle,
        destination_endpoint,
        message,
        0,
        0,
        matched_receiver
    );
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
    error = sender_commit_delivery(
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
    error = sender_enqueue(
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

enum micros_ipc_error micros_ipc_receiver_enqueue(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle receiver_handle,
    micros_endpoint_t source_endpoint,
    uintptr_t receive_buffer
)
{
    const struct micros_endpoint_record *resolved_source;
    struct micros_endpoint_record *receiver_endpoint;
    struct ipc_pending_notification_plan notification;
    struct micros_thread *receiver;
    struct micros_thread *previous_tail;
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
    error = ipc_resolve_held_thread(objects, receiver_handle, &receiver);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    receiver_endpoint =
        &registry->endpoints[receiver->owner.slot];
    if (
        receiver_endpoint->state != MICROS_ENDPOINT_STATE_ACTIVE
        || receiver_endpoint->owner.generation
            != receiver->owner.generation
    ) {
        return MICROS_IPC_ERROR_STATE;
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
    error = ipc_find_pending_notification(
        registry,
        objects,
        receiver_endpoint,
        source_endpoint,
        &notification
    );
    if (error == MICROS_IPC_OK) {
        return MICROS_IPC_ERROR_NOT_READY;
    }
    if (error != MICROS_IPC_ERROR_NOT_READY) {
        return error;
    }
    if (
        ipc_sender_is_ready_for(
            receiver_endpoint,
            objects,
            source_endpoint
        )
    ) {
        return MICROS_IPC_ERROR_NOT_READY;
    }
    error = ipc_resolve_queue_tail(
        objects,
        receiver_endpoint->receiver_tail,
        &previous_tail
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }

    receiver->runtime_flags = MICROS_THREAD_RTS_IPC_RECEIVE;
    receiver->ipc_queue_kind = MICROS_IPC_QUEUE_RECEIVER;
    receiver->ipc_next.slot = 0;
    receiver->ipc_next.generation = 0;
    receiver->ipc_receive_source = source_endpoint;
    receiver->ipc_receive_buffer = receive_buffer;
    ipc_append_thread(
        receiver_handle,
        previous_tail,
        &receiver_endpoint->receiver_head,
        &receiver_endpoint->receiver_tail
    );
    return MICROS_IPC_OK;
}
