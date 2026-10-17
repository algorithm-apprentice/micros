#include "kernel/ipc/ipc_internal.h"

#include "kernel/endpoint/endpoint_internal.h"
#include "kernel/scheduler/scheduler_core_internal.h"

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
