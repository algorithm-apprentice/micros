#include "micros/ipc_core.h"

#include <stddef.h>

#include "endpoint_internal.h"
#include "scheduler_core_internal.h"

static void copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *output = destination;
    const unsigned char *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
    }
}

static void clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static bool thread_handle_is_zero(
    struct micros_thread_handle handle
)
{
    return handle.slot == 0 && handle.generation == 0;
}

static bool thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

static bool process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

static bool thread_is_current(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle thread
)
{
    size_t index;

    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        const struct micros_hart *hart = &objects->harts[index];

        if (
            hart->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && thread_handles_equal(hart->current_thread, thread)
        ) {
            return true;
        }
    }
    return false;
}

static enum micros_ipc_error endpoint_error_to_ipc(
    enum micros_endpoint_error error
)
{
    switch (error) {
    case MICROS_ENDPOINT_OK:
        return MICROS_IPC_OK;
    case MICROS_ENDPOINT_ERROR_ARGUMENT:
    case MICROS_ENDPOINT_ERROR_ENDPOINT:
        return MICROS_IPC_ERROR_ARGUMENT;
    case MICROS_ENDPOINT_ERROR_STALE:
        return MICROS_IPC_ERROR_DEAD_ENDPOINT;
    case MICROS_ENDPOINT_ERROR_STATE:
        return MICROS_IPC_ERROR_STATE;
    case MICROS_ENDPOINT_ERROR_CLOSING:
        return MICROS_IPC_ERROR_ENDPOINT_CLOSING;
    case MICROS_ENDPOINT_ERROR_UNAUTHORIZED:
        return MICROS_IPC_ERROR_UNAUTHORIZED;
    case MICROS_ENDPOINT_ERROR_INVARIANT:
        return MICROS_IPC_ERROR_INVARIANT;
    default:
        return MICROS_IPC_ERROR_INVARIANT;
    }
}

static enum micros_ipc_error scheduler_error_to_ipc(
    enum micros_kernel_object_error error
)
{
    switch (error) {
    case MICROS_KERNEL_OBJECT_OK:
        return MICROS_IPC_OK;
    case MICROS_KERNEL_OBJECT_ERROR_ARGUMENT:
        return MICROS_IPC_ERROR_ARGUMENT;
    case MICROS_KERNEL_OBJECT_ERROR_STALE:
        return MICROS_IPC_ERROR_DEAD_ENDPOINT;
    case MICROS_KERNEL_OBJECT_ERROR_STATE:
        return MICROS_IPC_ERROR_STATE;
    case MICROS_KERNEL_OBJECT_ERROR_INVARIANT:
        return MICROS_IPC_ERROR_INVARIANT;
    default:
        return MICROS_IPC_ERROR_INVARIANT;
    }
}

static void canonicalize_message(
    struct micros_ipc_message *destination,
    const struct micros_ipc_message *source,
    micros_endpoint_t source_endpoint,
    uint64_t reply_token
)
{
    size_t index;

    destination->source = source_endpoint;
    destination->type = source->type;
    destination->reply_token = reply_token;
    for (index = 0; index < sizeof(destination->payload); ++index) {
        destination->payload[index] = source->payload[index];
    }
}

static void clear_message(struct micros_ipc_message *message)
{
    unsigned char *bytes = (unsigned char *)message;
    size_t index;

    for (index = 0; index < sizeof(*message); ++index) {
        bytes[index] = 0;
    }
}

static void canonicalize_notification(
    struct micros_ipc_message *message,
    micros_endpoint_t source,
    uint64_t event_mask
)
{
    size_t index;

    clear_message(message);
    message->source = source;
    message->type = MICROS_IPC_TYPE_KERNEL_NOTIFICATION;
    for (index = 0; index < sizeof(event_mask); ++index) {
        message->payload[index] =
            (uint8_t)(event_mask >> (index * 8));
    }
}

struct pending_notification_plan {
    bool kernel_origin;
    size_t source_slot;
    micros_endpoint_t source_endpoint;
    uint64_t event_mask;
    struct micros_ipc_message message;
};

static enum micros_ipc_error find_pending_notification(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_record *destination,
    micros_endpoint_t source_filter,
    struct pending_notification_plan *plan
)
{
    const struct micros_endpoint_record *source;
    const struct micros_endpoint_record *resolved_source;
    size_t source_slot;
    enum micros_endpoint_error endpoint_error;

    if (
        registry == NULL
        || objects == NULL
        || destination == NULL
        || plan == NULL
        || source_filter == MICROS_ENDPOINT_NONE
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    if (source_filter != MICROS_ENDPOINT_ANY) {
        endpoint_error = micros_endpoint_resolve_active(
            registry,
            objects,
            source_filter,
            &resolved_source
        );
        if (endpoint_error != MICROS_ENDPOINT_OK) {
            return endpoint_error_to_ipc(endpoint_error);
        }
        source_slot = resolved_source->owner.slot;
        if (
            (
                destination->pending_notification_sources
                & (UINT64_C(1) << source_slot)
            ) == 0
        ) {
            return MICROS_IPC_ERROR_NOT_READY;
        }
        source = resolved_source;
    } else {
        if (destination->pending_kernel_events != 0) {
            plan->kernel_origin = true;
            plan->source_slot = MICROS_PROCESS_CAPACITY;
            plan->source_endpoint = MICROS_ENDPOINT_NONE;
            plan->event_mask =
                destination->pending_kernel_events;
            canonicalize_notification(
                &plan->message,
                plan->source_endpoint,
                plan->event_mask
            );
            return MICROS_IPC_OK;
        }
        for (
            source_slot = 0;
            source_slot < MICROS_PROCESS_CAPACITY;
            ++source_slot
        ) {
            if (
                (
                    destination->pending_notification_sources
                    & (UINT64_C(1) << source_slot)
                ) != 0
            ) {
                break;
            }
        }
        if (source_slot == MICROS_PROCESS_CAPACITY) {
            return MICROS_IPC_ERROR_NOT_READY;
        }
        source = &registry->endpoints[source_slot];
        endpoint_error = micros_endpoint_resolve_active(
            registry,
            objects,
            source->value,
            &resolved_source
        );
        if (
            endpoint_error != MICROS_ENDPOINT_OK
            || resolved_source != source
            || resolved_source->owner.slot != source_slot
        ) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
    }
    if (
        destination->pending_events[source_slot] == 0
        || source->state != MICROS_ENDPOINT_STATE_ACTIVE
        || source->owner.slot != source_slot
        || (
            source_filter != MICROS_ENDPOINT_ANY
            && source->value != source_filter
        )
    ) {
        return MICROS_IPC_ERROR_INVARIANT;
    }

    plan->kernel_origin = false;
    plan->source_slot = source_slot;
    plan->source_endpoint = source->value;
    plan->event_mask = destination->pending_events[source_slot];
    canonicalize_notification(
        &plan->message,
        plan->source_endpoint,
        plan->event_mask
    );
    return MICROS_IPC_OK;
}

static void consume_pending_notification(
    struct micros_endpoint_record *destination,
    const struct pending_notification_plan *plan
)
{
    if (plan->kernel_origin) {
        destination->pending_kernel_events = 0;
    } else {
        destination->pending_events[plan->source_slot] = 0;
        destination->pending_notification_sources &=
            ~(UINT64_C(1) << plan->source_slot);
    }
}

static bool reply_token_is_available(
    const struct micros_kernel_objects *objects,
    uint64_t reply_token
)
{
    size_t index;

    if (reply_token == 0) {
        return true;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];

        if (
            thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && thread->ipc_reply_token == reply_token
        ) {
            return false;
        }
    }
    return true;
}

static enum micros_ipc_error resolve_reply_waiter(
    struct micros_kernel_objects *objects,
    uint64_t reply_token,
    micros_endpoint_t replying_endpoint,
    struct micros_thread_handle *caller_handle,
    struct micros_thread **caller
)
{
    struct micros_thread *matched = NULL;
    struct micros_thread_handle matched_handle = {0, 0};
    size_t index;

    if (reply_token == 0) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        struct micros_thread *candidate = &objects->threads[index];

        if (
            candidate->slot_state
                != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || candidate->ipc_reply_token != reply_token
        ) {
            continue;
        }
        if (matched != NULL) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
        matched = candidate;
        matched_handle.slot = (uint16_t)index;
        matched_handle.generation = candidate->generation;
    }
    if (
        matched == NULL
        || matched->ipc_queue_kind != MICROS_IPC_QUEUE_NONE
        || !thread_handle_is_zero(matched->ipc_next)
        || (
            matched->runtime_flags
            & MICROS_THREAD_RTS_IPC_MASK
        ) != MICROS_THREAD_RTS_IPC_REPLY
        || matched->ipc_reply_callee != replying_endpoint
    ) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    *caller_handle = matched_handle;
    *caller = matched;
    return MICROS_IPC_OK;
}

static bool reply_request_is_pending(
    const struct micros_kernel_objects *objects,
    uint64_t reply_token
)
{
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];

        if (
            thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && thread->ipc_delivery_pending
            && thread->ipc_staged_result == MICROS_IPC_OK
            && thread->ipc_inbound_message.reply_token
                == reply_token
        ) {
            return true;
        }
    }
    return false;
}

static bool thread_is_held_and_clear(
    const struct micros_thread *thread
)
{
    return (
        thread->scheduler_assigned
        && thread->runtime_flags == MICROS_THREAD_RTS_INACTIVE
        && !thread->ready_linked
        && micros_thread_ipc_state_is_clear(thread)
    );
}

static enum micros_ipc_error resolve_held_thread(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    struct micros_thread **thread
)
{
    const struct micros_thread *resolved;
    enum micros_kernel_object_error error;

    error = micros_thread_resolve(objects, handle, &resolved);
    if (error == MICROS_KERNEL_OBJECT_ERROR_STALE) {
        return MICROS_IPC_ERROR_DEAD_ENDPOINT;
    }
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    if (!thread_is_held_and_clear(resolved)) {
        return MICROS_IPC_ERROR_STATE;
    }
    *thread = &objects->threads[handle.slot];
    return MICROS_IPC_OK;
}

static enum micros_ipc_error resolve_operation_thread(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    micros_endpoint_t *source_endpoint
)
{
    struct micros_thread *thread;
    enum micros_ipc_error error;

    error = resolve_held_thread(objects, handle, &thread);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    if (thread_is_current(objects, handle)) {
        return MICROS_IPC_ERROR_STATE;
    }
    *source_endpoint =
        objects->processes[thread->owner.slot].primary_endpoint;
    if (
        *source_endpoint == MICROS_ENDPOINT_NONE
        || *source_endpoint == MICROS_ENDPOINT_ANY
    ) {
        return MICROS_IPC_ERROR_STATE;
    }
    return MICROS_IPC_OK;
}

static enum micros_ipc_error resolve_dependency_endpoint(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    struct micros_process_handle *owner
)
{
    const struct micros_endpoint_record *record;
    const struct micros_process *process;
    struct micros_process_handle unpacked;

    if (
        endpoint == MICROS_ENDPOINT_NONE
        || endpoint == MICROS_ENDPOINT_ANY
        || micros_endpoint_unpack(endpoint, &unpacked)
            != MICROS_ENDPOINT_OK
        || unpacked.slot >= MICROS_PROCESS_CAPACITY
    ) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    record = &registry->endpoints[unpacked.slot];
    process = &objects->processes[unpacked.slot];
    if (
        record->state != MICROS_ENDPOINT_STATE_ACTIVE
        || record->value != endpoint
        || !process_handles_equal(record->owner, unpacked)
        || process->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || process->generation != unpacked.generation
        || process->primary_endpoint != endpoint
    ) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    *owner = unpacked;
    return MICROS_IPC_OK;
}

static enum micros_ipc_error resolve_sole_live_thread(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle owner,
    struct micros_thread_handle *thread_handle,
    const struct micros_thread **thread
)
{
    const struct micros_process *process;
    const struct micros_thread *matched = NULL;
    struct micros_thread_handle matched_handle = {0, 0};
    size_t index;

    if (
        owner.slot >= MICROS_PROCESS_CAPACITY
        || owner.generation == 0
    ) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    process = &objects->processes[owner.slot];
    if (
        process->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || process->generation != owner.generation
        || process->live_thread_count != 1
    ) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *candidate =
            &objects->threads[index];

        if (
            candidate->slot_state
                != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || !process_handles_equal(candidate->owner, owner)
        ) {
            continue;
        }
        if (matched != NULL) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
        matched = candidate;
        matched_handle.slot = (uint16_t)index;
        matched_handle.generation = candidate->generation;
    }
    if (matched == NULL || matched_handle.generation == 0) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    *thread_handle = matched_handle;
    *thread = matched;
    return MICROS_IPC_OK;
}

static enum micros_ipc_error blocked_dependency(
    const struct micros_thread *thread,
    struct micros_thread_handle thread_handle,
    struct micros_thread_handle cleared_dependency,
    bool *has_dependency,
    micros_endpoint_t *endpoint
)
{
    if (
        !thread_handle_is_zero(cleared_dependency)
        && thread_handles_equal(thread_handle, cleared_dependency)
    ) {
        *has_dependency = false;
        return MICROS_IPC_OK;
    }
    if (
        (
            thread->runtime_flags
            & MICROS_THREAD_RTS_IPC_SEND
        ) != 0
    ) {
        *endpoint = thread->ipc_send_destination;
    } else if (
        (
            thread->runtime_flags
            & MICROS_THREAD_RTS_IPC_REPLY
        ) != 0
    ) {
        *endpoint = thread->ipc_reply_callee;
    } else if (
        (
            thread->runtime_flags
            & MICROS_THREAD_RTS_IPC_RECEIVE
        ) != 0
    ) {
        if (thread->ipc_receive_source == MICROS_ENDPOINT_ANY) {
            *has_dependency = false;
            return MICROS_IPC_OK;
        }
        *endpoint = thread->ipc_receive_source;
    } else {
        *has_dependency = false;
        return MICROS_IPC_OK;
    }
    if (
        *endpoint == MICROS_ENDPOINT_NONE
        || *endpoint == MICROS_ENDPOINT_ANY
    ) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    *has_dependency = true;
    return MICROS_IPC_OK;
}

static enum micros_ipc_error preflight_deadlock(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle candidate,
    micros_endpoint_t dependency,
    struct micros_thread_handle cleared_dependency
)
{
    struct micros_thread_handle visited[MICROS_THREAD_CAPACITY];
    const struct micros_thread *unused_candidate;
    size_t visited_count = 0;
    size_t steps;

    if (
        dependency == MICROS_ENDPOINT_ANY
        || dependency == MICROS_ENDPOINT_NONE
    ) {
        return dependency == MICROS_ENDPOINT_ANY
            ? MICROS_IPC_OK
            : MICROS_IPC_ERROR_INVARIANT;
    }
    if (
        micros_thread_resolve(objects, candidate, &unused_candidate)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    if (!thread_handle_is_zero(cleared_dependency)) {
        const struct micros_thread *unused_cleared;

        if (
            micros_thread_resolve(
                objects,
                cleared_dependency,
                &unused_cleared
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
    }

    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        struct micros_process_handle owner;
        struct micros_thread_handle thread_handle;
        const struct micros_thread *thread;
        micros_endpoint_t next_dependency = MICROS_ENDPOINT_NONE;
        enum micros_ipc_error error;
        bool has_dependency;
        size_t index;

        error = resolve_dependency_endpoint(
            registry,
            objects,
            dependency,
            &owner
        );
        if (error != MICROS_IPC_OK) {
            return error;
        }
        error = resolve_sole_live_thread(
            objects,
            owner,
            &thread_handle,
            &thread
        );
        if (error != MICROS_IPC_OK) {
            return error;
        }
        if (thread_handles_equal(thread_handle, candidate)) {
            return MICROS_IPC_ERROR_DEADLOCK;
        }
        for (index = 0; index < visited_count; ++index) {
            if (thread_handles_equal(visited[index], thread_handle)) {
                return MICROS_IPC_ERROR_INVARIANT;
            }
        }
        visited[visited_count] = thread_handle;
        ++visited_count;

        error = blocked_dependency(
            thread,
            thread_handle,
            cleared_dependency,
            &has_dependency,
            &next_dependency
        );
        if (error != MICROS_IPC_OK) {
            return error;
        }
        if (!has_dependency) {
            return MICROS_IPC_OK;
        }
        dependency = next_dependency;
    }
    return MICROS_IPC_ERROR_INVARIANT;
}

static void stage_delivery(
    struct micros_thread *receiver,
    uintptr_t receive_buffer,
    const struct micros_ipc_message *message
);

struct reply_preflight {
    struct micros_thread_handle caller_handle;
    struct micros_thread *caller;
    struct micros_ipc_message message;
    micros_endpoint_t replying_endpoint;
    uintptr_t reply_buffer;
};

static enum micros_ipc_error preflight_reply(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle replier_handle,
    uint64_t reply_token,
    micros_endpoint_t expected_caller_endpoint,
    const struct micros_ipc_message *message,
    uint32_t required_operation,
    struct reply_preflight *plan
)
{
    struct micros_thread_handle caller_handle = {0, 0};
    struct micros_thread *caller;
    struct micros_ipc_message snapshot;
    micros_endpoint_t replying_endpoint;
    enum micros_endpoint_error endpoint_error;
    enum micros_ipc_error error;

    if (
        registry == NULL
        || objects == NULL
        || message == NULL
        || plan == NULL
        || (
            required_operation
                != MICROS_PRIVILEGE_OPERATION_REPLY
            && required_operation
                != MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE
        )
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
        return endpoint_error_to_ipc(endpoint_error);
    }
    error = resolve_operation_thread(
        objects,
        replier_handle,
        &replying_endpoint
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    endpoint_error = micros_endpoint_authorize_operation(
        registry,
        objects,
        replying_endpoint,
        required_operation
    );
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return endpoint_error_to_ipc(endpoint_error);
    }
    error = resolve_reply_waiter(
        objects,
        reply_token,
        replying_endpoint,
        &caller_handle,
        &caller
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    if (expected_caller_endpoint != MICROS_ENDPOINT_NONE) {
        const struct micros_endpoint_record *caller_endpoint =
            &registry->endpoints[caller->owner.slot];

        if (
            caller_endpoint->state
                != MICROS_ENDPOINT_STATE_ACTIVE
            || caller_endpoint->value
                != expected_caller_endpoint
            || caller_endpoint->owner.slot != caller->owner.slot
            || caller_endpoint->owner.generation
                != caller->owner.generation
        ) {
            return MICROS_IPC_ERROR_REPLY_TOKEN;
        }
    }
    if (reply_request_is_pending(objects, reply_token)) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    canonicalize_message(
        &snapshot,
        message,
        replying_endpoint,
        0
    );

    plan->caller_handle = caller_handle;
    plan->caller = caller;
    plan->message = snapshot;
    plan->replying_endpoint = replying_endpoint;
    plan->reply_buffer = caller->ipc_receive_buffer;
    return MICROS_IPC_OK;
}

static void commit_reply_delivery(
    const struct reply_preflight *plan
)
{
    stage_delivery(
        plan->caller,
        plan->reply_buffer,
        &plan->message
    );
    plan->caller->ipc_reply_token = 0;
    plan->caller->ipc_reply_callee = 0;
}

static enum micros_ipc_error resolve_queue_tail(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle tail,
    struct micros_thread **thread
)
{
    const struct micros_thread *resolved;

    if (thread_handle_is_zero(tail)) {
        *thread = NULL;
        return MICROS_IPC_OK;
    }
    if (
        micros_thread_resolve(objects, tail, &resolved)
            != MICROS_KERNEL_OBJECT_OK
        || !thread_handle_is_zero(resolved->ipc_next)
    ) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    *thread = &objects->threads[tail.slot];
    return MICROS_IPC_OK;
}

static bool receiver_is_ready_for(
    const struct micros_endpoint_record *destination,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t source
)
{
    struct micros_thread_handle current = destination->receiver_head;
    size_t steps;

    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        const struct micros_thread *receiver;

        if (thread_handle_is_zero(current)) {
            return false;
        }
        receiver = &objects->threads[current.slot];
        if (
            receiver->ipc_receive_source == MICROS_ENDPOINT_ANY
            || receiver->ipc_receive_source == source
        ) {
            return true;
        }
        current = receiver->ipc_next;
    }
    return false;
}

static bool sender_is_ready_for(
    const struct micros_endpoint_record *receiver_endpoint,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t source
)
{
    struct micros_thread_handle current =
        receiver_endpoint->sender_head;
    size_t steps;

    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        const struct micros_thread *sender;

        if (thread_handle_is_zero(current)) {
            return false;
        }
        sender = &objects->threads[current.slot];
        if (
            source == MICROS_ENDPOINT_ANY
            || sender->ipc_outbound_message.source == source
        ) {
            return true;
        }
        current = sender->ipc_next;
    }
    return false;
}

static void append_thread(
    struct micros_thread_handle thread_handle,
    struct micros_thread *previous_tail,
    struct micros_thread_handle *head,
    struct micros_thread_handle *tail
)
{
    if (previous_tail == NULL) {
        *head = thread_handle;
    } else {
        previous_tail->ipc_next = thread_handle;
    }
    *tail = thread_handle;
}

static bool find_matching_sender(
    const struct micros_endpoint_record *endpoint,
    struct micros_kernel_objects *objects,
    micros_endpoint_t source,
    struct micros_thread_handle *previous,
    struct micros_thread_handle *matched,
    struct micros_thread **sender
)
{
    struct micros_thread_handle current = endpoint->sender_head;
    struct micros_thread_handle prior = {0, 0};
    size_t steps;

    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        struct micros_thread *candidate;

        if (thread_handle_is_zero(current)) {
            return false;
        }
        candidate = &objects->threads[current.slot];
        if (
            source == MICROS_ENDPOINT_ANY
            || candidate->ipc_outbound_message.source == source
        ) {
            *previous = prior;
            *matched = current;
            *sender = candidate;
            return true;
        }
        prior = current;
        current = candidate->ipc_next;
    }
    return false;
}

static bool find_matching_receiver_const(
    const struct micros_endpoint_record *endpoint,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t source,
    struct micros_thread_handle *previous,
    struct micros_thread_handle *matched,
    const struct micros_thread **receiver
)
{
    struct micros_thread_handle current = endpoint->receiver_head;
    struct micros_thread_handle prior = {0, 0};
    size_t steps;

    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        const struct micros_thread *candidate;

        if (thread_handle_is_zero(current)) {
            return false;
        }
        candidate = &objects->threads[current.slot];
        if (
            candidate->ipc_receive_source == MICROS_ENDPOINT_ANY
            || candidate->ipc_receive_source == source
        ) {
            *previous = prior;
            *matched = current;
            *receiver = candidate;
            return true;
        }
        prior = current;
        current = candidate->ipc_next;
    }
    return false;
}

static bool find_matching_receiver(
    const struct micros_endpoint_record *endpoint,
    struct micros_kernel_objects *objects,
    micros_endpoint_t source,
    struct micros_thread_handle *previous,
    struct micros_thread_handle *matched,
    struct micros_thread **receiver
)
{
    const struct micros_thread *resolved;

    if (
        !find_matching_receiver_const(
            endpoint,
            objects,
            source,
            previous,
            matched,
            &resolved
        )
    ) {
        return false;
    }
    *receiver = &objects->threads[matched->slot];
    return true;
}

static void unlink_thread(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle previous,
    struct micros_thread_handle matched,
    struct micros_thread_handle *head,
    struct micros_thread_handle *tail
)
{
    struct micros_thread *thread = &objects->threads[matched.slot];

    if (thread_handle_is_zero(previous)) {
        *head = thread->ipc_next;
    } else {
        objects->threads[previous.slot].ipc_next =
            thread->ipc_next;
    }
    if (thread_handles_equal(*tail, matched)) {
        *tail = previous;
    }
}

static void stage_no_message_completion(
    struct micros_thread *thread,
    enum micros_ipc_error result
)
{
    thread->ipc_receive_buffer = 0;
    thread->ipc_delivery_pending = true;
    clear_message(&thread->ipc_inbound_message);
    thread->ipc_staged_result = result;
}

static void clear_delivered_sender(struct micros_thread *sender)
{
    bool completes_send = sender->ipc_reply_token == 0;

    sender->ipc_queue_kind = MICROS_IPC_QUEUE_NONE;
    sender->ipc_next.slot = 0;
    sender->ipc_next.generation = 0;
    clear_message(&sender->ipc_outbound_message);
    sender->ipc_send_destination = 0;
    if (completes_send) {
        stage_no_message_completion(sender, MICROS_IPC_OK);
    }
}

static void stage_delivery(
    struct micros_thread *receiver,
    uintptr_t receive_buffer,
    const struct micros_ipc_message *message
)
{
    receiver->ipc_receive_buffer = receive_buffer;
    receiver->ipc_delivery_pending = true;
    receiver->ipc_inbound_message = *message;
    receiver->ipc_staged_result = MICROS_IPC_OK;
}

enum micros_ipc_error micros_ipc_stage_no_message_completion(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread_handle,
    enum micros_ipc_error result
)
{
    const struct micros_thread *resolved;
    struct micros_thread *thread;
    const struct micros_endpoint_record *owner;
    enum micros_endpoint_error endpoint_error;
    enum micros_kernel_object_error object_error;

    if (
        registry == NULL
        || objects == NULL
        || (
            result != MICROS_IPC_OK
            && result != MICROS_IPC_ERROR_DEAD_ENDPOINT
        )
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    endpoint_error =
        micros_endpoint_registry_validate_objects(registry, objects);
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return endpoint_error_to_ipc(endpoint_error);
    }
    object_error =
        micros_thread_resolve(objects, thread_handle, &resolved);
    if (object_error == MICROS_KERNEL_OBJECT_ERROR_STALE) {
        return MICROS_IPC_ERROR_DEAD_ENDPOINT;
    }
    if (object_error != MICROS_KERNEL_OBJECT_OK) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    owner = &registry->endpoints[resolved->owner.slot];
    if (
        !resolved->scheduler_assigned
        || !micros_thread_ipc_state_is_clear(resolved)
        || (
            resolved->runtime_flags
            & MICROS_THREAD_RTS_IPC_MASK
        ) != 0
        || owner->state != MICROS_ENDPOINT_STATE_ACTIVE
        || !process_handles_equal(owner->owner, resolved->owner)
    ) {
        return MICROS_IPC_ERROR_STATE;
    }
    thread = &objects->threads[thread_handle.slot];
    stage_no_message_completion(thread, result);
    return MICROS_IPC_OK;
}

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
        owned = process_handles_equal(thread->owner, plan->owner);
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
                thread_is_current(objects, handle)
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

        if (thread_handle_is_zero(current)) {
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
        return endpoint_error_to_ipc(endpoint_error);
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

        if (thread_handle_is_zero(current)) {
            break;
        }
        thread = &objects->threads[current.slot];
        next = thread->ipc_next;
        if (!plan->threads[current.slot].remove_from_queue) {
            if (thread_handle_is_zero(retained_head)) {
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
    if (!thread_handle_is_zero(retained_tail)) {
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
    clear_message(&thread->ipc_outbound_message);
    thread->ipc_send_destination = 0;
    thread->ipc_receive_source = 0;
    thread->ipc_receive_buffer = 0;
    thread->ipc_delivery_pending = false;
    clear_message(&thread->ipc_inbound_message);
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
            return scheduler_error_to_ipc(scheduler_error);
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
        return endpoint_error_to_ipc(endpoint_error);
    }
    error = resolve_operation_thread(
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
        return endpoint_error_to_ipc(endpoint_error);
    }
    canonicalize_message(
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
    error = preflight_deadlock(
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

enum micros_ipc_error micros_ipc_notify(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle notifier_handle,
    micros_endpoint_t destination_endpoint,
    uint64_t event_mask
)
{
    const struct micros_endpoint_record *resolved_destination;
    struct micros_endpoint_record *destination;
    struct micros_thread_handle previous = {0, 0};
    struct micros_thread_handle receiver_handle = {0, 0};
    struct micros_thread *receiver = NULL;
    struct micros_ipc_message notification;
    micros_endpoint_t source_endpoint;
    size_t source_slot;
    enum micros_endpoint_error endpoint_error;
    enum micros_kernel_object_error scheduler_error;
    enum micros_ipc_error error;
    bool has_receiver;

    if (
        registry == NULL
        || objects == NULL
        || event_mask == 0
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    endpoint_error =
        micros_endpoint_registry_validate_objects(registry, objects);
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return endpoint_error_to_ipc(endpoint_error);
    }
    error = resolve_operation_thread(
        objects,
        notifier_handle,
        &source_endpoint
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    endpoint_error = micros_endpoint_authorize_target(
        registry,
        objects,
        source_endpoint,
        MICROS_PRIVILEGE_OPERATION_NOTIFY,
        destination_endpoint
    );
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return endpoint_error_to_ipc(endpoint_error);
    }
    endpoint_error = micros_endpoint_resolve_active(
        registry,
        objects,
        destination_endpoint,
        &resolved_destination
    );
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return endpoint_error_to_ipc(endpoint_error);
    }
    destination =
        &registry->endpoints[resolved_destination->owner.slot];
    source_slot =
        objects->threads[notifier_handle.slot].owner.slot;
    canonicalize_notification(
        &notification,
        source_endpoint,
        event_mask
    );
    has_receiver = find_matching_receiver(
        destination,
        objects,
        source_endpoint,
        &previous,
        &receiver_handle,
        &receiver
    );
    if (
        has_receiver
        && (
            receiver->runtime_flags
            & MICROS_THREAD_RTS_IPC_REPLY
        ) == 0
    ) {
        if (
            (
                receiver->runtime_flags
                & MICROS_THREAD_RTS_IPC_MASK
            ) != MICROS_THREAD_RTS_IPC_RECEIVE
        ) {
            return MICROS_IPC_ERROR_STATE;
        }
        scheduler_error = micros_scheduler_commit_ipc_wake_pair(
            objects,
            notifier_handle,
            MICROS_THREAD_RTS_INACTIVE,
            receiver_handle,
            MICROS_THREAD_RTS_IPC_RECEIVE
        );
        if (scheduler_error != MICROS_KERNEL_OBJECT_OK) {
            return scheduler_error_to_ipc(scheduler_error);
        }

        unlink_thread(
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
        stage_delivery(
            receiver,
            receiver->ipc_receive_buffer,
            &notification
        );
        return MICROS_IPC_OK;
    }

    scheduler_error = micros_scheduler_commit_ipc_wake(
        objects,
        notifier_handle,
        MICROS_THREAD_RTS_INACTIVE
    );
    if (scheduler_error != MICROS_KERNEL_OBJECT_OK) {
        return scheduler_error_to_ipc(scheduler_error);
    }
    destination->pending_events[source_slot] |= event_mask;
    destination->pending_notification_sources |=
        UINT64_C(1) << source_slot;
    return MICROS_IPC_OK;
}

enum micros_ipc_error micros_ipc_inject_kernel_notification(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    micros_endpoint_t destination_endpoint,
    uint64_t event_mask
)
{
    struct micros_ipc_kernel_notification_plan plan;
    enum micros_ipc_error error;

    error = micros_ipc_prepare_kernel_notification(
        registry,
        objects,
        destination_endpoint,
        event_mask,
        &plan
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    micros_ipc_commit_kernel_notification_prevalidated(
        registry,
        objects,
        &plan
    );
    return MICROS_IPC_OK;
}

enum micros_ipc_error micros_ipc_prepare_kernel_notification(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t destination_endpoint,
    uint64_t event_mask,
    struct micros_ipc_kernel_notification_plan *plan
)
{
    struct micros_ipc_kernel_notification_plan candidate;
    const struct micros_endpoint_record *resolved_destination;
    const struct micros_endpoint_record *destination;
    struct micros_thread_handle previous = {0, 0};
    struct micros_thread_handle receiver_handle = {0, 0};
    const struct micros_thread *receiver = NULL;
    enum micros_endpoint_error endpoint_error;
    enum micros_kernel_object_error scheduler_error;
    bool has_receiver;

    if (
        registry == NULL
        || objects == NULL
        || plan == NULL
        || destination_endpoint == MICROS_ENDPOINT_NONE
        || destination_endpoint == MICROS_ENDPOINT_ANY
        || event_mask == 0
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    clear_bytes(&candidate, sizeof(candidate));
    endpoint_error =
        micros_endpoint_registry_validate_objects(registry, objects);
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return endpoint_error_to_ipc(endpoint_error);
    }
    endpoint_error = micros_endpoint_resolve_active(
        registry,
        objects,
        destination_endpoint,
        &resolved_destination
    );
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return endpoint_error_to_ipc(endpoint_error);
    }
    destination =
        &registry->endpoints[resolved_destination->owner.slot];
    candidate.destination_slot =
        resolved_destination->owner.slot;
    candidate.event_mask = event_mask;
    canonicalize_notification(
        &candidate.notification,
        MICROS_ENDPOINT_NONE,
        event_mask
    );
    has_receiver = find_matching_receiver_const(
        destination,
        objects,
        MICROS_ENDPOINT_NONE,
        &previous,
        &receiver_handle,
        &receiver
    );
    if (
        has_receiver
        && (
            receiver->runtime_flags
            & MICROS_THREAD_RTS_IPC_REPLY
        ) == 0
    ) {
        if (
            (
                receiver->runtime_flags
                & MICROS_THREAD_RTS_IPC_MASK
            ) != MICROS_THREAD_RTS_IPC_RECEIVE
        ) {
            return MICROS_IPC_ERROR_STATE;
        }
        scheduler_error = micros_scheduler_prepare_ipc_wake(
            objects,
            receiver_handle,
            MICROS_THREAD_RTS_IPC_RECEIVE,
            &candidate.wake
        );
        if (scheduler_error != MICROS_KERNEL_OBJECT_OK) {
            return scheduler_error_to_ipc(scheduler_error);
        }
        candidate.deliver_to_receiver = true;
        candidate.previous_receiver = previous;
        candidate.receiver = receiver_handle;
        candidate.receive_buffer = receiver->ipc_receive_buffer;
    }
    candidate.active = true;
    copy_bytes(plan, &candidate, sizeof(*plan));
    return MICROS_IPC_OK;
}

void micros_ipc_commit_kernel_notification_prevalidated(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_ipc_kernel_notification_plan *plan
)
{
    struct micros_endpoint_record *destination =
        &registry->endpoints[plan->destination_slot];

    if (plan->deliver_to_receiver) {
        struct micros_thread *receiver =
            &objects->threads[plan->receiver.slot];

        micros_scheduler_commit_ipc_wake_prevalidated(
            objects,
            &plan->wake
        );
        unlink_thread(
            objects,
            plan->previous_receiver,
            plan->receiver,
            &destination->receiver_head,
            &destination->receiver_tail
        );
        receiver->ipc_queue_kind = MICROS_IPC_QUEUE_NONE;
        receiver->ipc_next.slot = 0;
        receiver->ipc_next.generation = 0;
        receiver->ipc_receive_source = 0;
        stage_delivery(
            receiver,
            plan->receive_buffer,
            &plan->notification
        );
    } else {
        destination->pending_kernel_events |= plan->event_mask;
    }
    clear_bytes(plan, sizeof(*plan));
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
        return endpoint_error_to_ipc(endpoint_error);
    }
    error = resolve_operation_thread(
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
        return endpoint_error_to_ipc(endpoint_error);
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
    error = preflight_deadlock(
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
        return endpoint_error_to_ipc(endpoint_error);
    }
    error = resolve_held_thread(objects, sender_handle, &sender);
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
        return endpoint_error_to_ipc(endpoint_error);
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
    if (!reply_token_is_available(objects, reply_token)) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    if (
        receiver_is_ready_for(
            resolved_destination,
            objects,
            source->value
        )
    ) {
        return MICROS_IPC_ERROR_NOT_READY;
    }
    destination =
        &registry->endpoints[resolved_destination->owner.slot];
    error = resolve_queue_tail(
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
    canonicalize_message(
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
    append_thread(
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
    struct pending_notification_plan notification;
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
        return endpoint_error_to_ipc(endpoint_error);
    }
    error = resolve_held_thread(objects, receiver_handle, &receiver);
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
            return endpoint_error_to_ipc(endpoint_error);
        }
        (void)resolved_source;
    }
    error = find_pending_notification(
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
            return scheduler_error_to_ipc(scheduler_error);
        }

        consume_pending_notification(
            receiver_endpoint,
            &notification
        );
        stage_delivery(
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
        !find_matching_sender(
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
        return scheduler_error_to_ipc(scheduler_error);
    }

    unlink_thread(
        objects,
        previous,
        sender_handle,
        &receiver_endpoint->sender_head,
        &receiver_endpoint->sender_tail
    );
    clear_delivered_sender(sender);
    stage_delivery(receiver, receive_buffer, &staged_message);
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
        return endpoint_error_to_ipc(endpoint_error);
    }
    error = resolve_held_thread(objects, sender_handle, &sender);
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
        return endpoint_error_to_ipc(endpoint_error);
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
    if (!reply_token_is_available(objects, reply_token)) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    destination =
        &registry->endpoints[resolved_destination->owner.slot];
    if (
        !find_matching_receiver(
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
    canonicalize_message(
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
        return scheduler_error_to_ipc(scheduler_error);
    }

    unlink_thread(
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
    stage_delivery(receiver, receive_buffer, &staged_message);
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
        return endpoint_error_to_ipc(endpoint_error);
    }
    error = resolve_operation_thread(
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
        return endpoint_error_to_ipc(endpoint_error);
    }
    if (registry->last_reply_token == UINT64_MAX) {
        return MICROS_IPC_ERROR_REPLY_TOKEN_EXHAUSTED;
    }
    reply_token = registry->last_reply_token + 1;
    canonicalize_message(
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
    error = preflight_deadlock(
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
    struct reply_preflight plan;
    enum micros_kernel_object_error scheduler_error;
    enum micros_ipc_error error;

    error = preflight_reply(
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
        return scheduler_error_to_ipc(scheduler_error);
    }

    commit_reply_delivery(&plan);
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
    struct reply_preflight reply_plan;
    const struct micros_endpoint_record *resolved_source;
    struct micros_endpoint_record *receiver_endpoint;
    struct pending_notification_plan notification;
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
    error = preflight_reply(
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
            return endpoint_error_to_ipc(endpoint_error);
        }
        (void)resolved_source;
    }
    replier = &objects->threads[replier_handle.slot];
    receiver_endpoint =
        &registry->endpoints[replier->owner.slot];
    error = find_pending_notification(
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
        has_sender = find_matching_sender(
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
        error = resolve_queue_tail(
            objects,
            receiver_endpoint->receiver_tail,
            &previous_tail
        );
        if (error != MICROS_IPC_OK) {
            return error;
        }
        error = preflight_deadlock(
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
        return scheduler_error_to_ipc(scheduler_error);
    }

    commit_reply_delivery(&reply_plan);
    if (has_notification) {
        consume_pending_notification(
            receiver_endpoint,
            &notification
        );
        stage_delivery(
            replier,
            receive_buffer,
            &notification.message
        );
    } else if (has_sender) {
        unlink_thread(
            objects,
            previous,
            sender_handle,
            &receiver_endpoint->sender_head,
            &receiver_endpoint->sender_tail
        );
        clear_delivered_sender(sender);
        stage_delivery(
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
        append_thread(
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
    struct pending_notification_plan notification;
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
        return endpoint_error_to_ipc(endpoint_error);
    }
    error = resolve_held_thread(objects, receiver_handle, &receiver);
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
            return endpoint_error_to_ipc(endpoint_error);
        }
        (void)resolved_source;
    }
    error = find_pending_notification(
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
        sender_is_ready_for(
            receiver_endpoint,
            objects,
            source_endpoint
        )
    ) {
        return MICROS_IPC_ERROR_NOT_READY;
    }
    error = resolve_queue_tail(
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
    append_thread(
        receiver_handle,
        previous_tail,
        &receiver_endpoint->receiver_head,
        &receiver_endpoint->receiver_tail
    );
    return MICROS_IPC_OK;
}
