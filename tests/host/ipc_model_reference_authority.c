#include "ipc_model_test_internal.h"

bool ipc_model_resolve_active_endpoint(
    const struct model_state *model,
    micros_endpoint_t endpoint,
    size_t *slot
)
{
    uint32_t generation;
    size_t decoded_slot;
    const struct model_endpoint *record;
    const struct model_process *process;

    if (
        endpoint == MICROS_ENDPOINT_NONE
        || endpoint == MICROS_ENDPOINT_ANY
    ) {
        return false;
    }
    decoded_slot =
        endpoint & ((UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) - 1);
    generation = endpoint >> MICROS_ENDPOINT_SLOT_BITS;
    if (
        generation == 0
        || decoded_slot >= MICROS_PROCESS_CAPACITY
    ) {
        return false;
    }
    record = &model->endpoints[decoded_slot];
    process = &model->processes[decoded_slot];
    if (
        record->state != MICROS_ENDPOINT_STATE_ACTIVE
        || record->value != endpoint
        || record->owner.slot != decoded_slot
        || record->owner.generation != generation
        || process->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || process->generation != generation
        || process->primary_endpoint != endpoint
    ) {
        return false;
    }
    *slot = decoded_slot;
    return true;
}

enum micros_ipc_error ipc_model_resolve_operation_thread(
    const struct model_state *model,
    size_t thread_slot,
    micros_endpoint_t *source_endpoint
)
{
    const struct model_thread *thread;
    const struct model_process *process;

    if (thread_slot >= MICROS_THREAD_CAPACITY) {
        return MICROS_IPC_ERROR_DEAD_ENDPOINT;
    }
    thread = &model->threads[thread_slot];
    if (!ipc_model_thread_is_held_clear(thread)) {
        return thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            ? MICROS_IPC_ERROR_STATE
            : MICROS_IPC_ERROR_DEAD_ENDPOINT;
    }
    process = &model->processes[thread->owner.slot];
    if (
        process->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || process->generation != thread->owner.generation
    ) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    *source_endpoint = process->primary_endpoint;
    if (
        *source_endpoint == MICROS_ENDPOINT_NONE
        || *source_endpoint == MICROS_ENDPOINT_ANY
    ) {
        return MICROS_IPC_ERROR_STATE;
    }
    return MICROS_IPC_OK;
}

static uint32_t model_profile_operations(uint32_t profile)
{
    if (profile == MODEL_FULL_PROFILE) {
        return MICROS_PRIVILEGE_OPERATION_DEFINED_MASK;
    }
    if (profile == MODEL_DENIED_PROFILE) {
        return MICROS_PRIVILEGE_OPERATION_RECEIVE;
    }
    return 0;
}

enum micros_ipc_error ipc_model_authorize_operation(
    const struct model_state *model,
    micros_endpoint_t source,
    uint32_t operation
)
{
    size_t source_slot;
    uint32_t profile;

    if (!ipc_model_resolve_active_endpoint(model, source, &source_slot)) {
        return MICROS_IPC_ERROR_STATE;
    }
    profile = model->processes[source_slot].privilege_profile;
    return (
        model_profile_operations(profile) & operation
    ) != 0
        ? MICROS_IPC_OK
        : MICROS_IPC_ERROR_UNAUTHORIZED;
}

enum micros_ipc_error ipc_model_authorize_target(
    const struct model_state *model,
    micros_endpoint_t source,
    uint32_t operation,
    micros_endpoint_t destination
)
{
    size_t source_slot;
    size_t destination_slot;
    uint32_t source_profile;
    uint32_t destination_profile;

    if (!ipc_model_resolve_active_endpoint(model, source, &source_slot)) {
        return MICROS_IPC_ERROR_STATE;
    }
    if (!ipc_model_resolve_active_endpoint(
        model,
        destination,
        &destination_slot
    )) {
        return MICROS_IPC_ERROR_DEAD_ENDPOINT;
    }
    source_profile =
        model->processes[source_slot].privilege_profile;
    destination_profile =
        model->processes[destination_slot].privilege_profile;
    if (
        (
            model_profile_operations(source_profile)
            & operation
        ) == 0
    ) {
        return MICROS_IPC_ERROR_UNAUTHORIZED;
    }
    if (
        source_profile != MODEL_FULL_PROFILE
        || destination_profile != MODEL_FULL_PROFILE
    ) {
        return MICROS_IPC_ERROR_UNAUTHORIZED;
    }
    return MICROS_IPC_OK;
}

bool ipc_model_find_receiver(
    const struct model_state *model,
    size_t endpoint_slot,
    micros_endpoint_t source,
    struct micros_thread_handle *previous,
    size_t *receiver_slot
)
{
    struct micros_thread_handle current =
        model->endpoints[endpoint_slot].receiver_head;
    struct micros_thread_handle prior = ipc_model_null_thread_handle();
    size_t steps;

    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        const struct model_thread *receiver;

        if (ipc_model_thread_handle_is_zero(current)) {
            return false;
        }
        receiver = &model->threads[current.slot];
        if (
            receiver->ipc_receive_source == MICROS_ENDPOINT_ANY
            || receiver->ipc_receive_source == source
        ) {
            *previous = prior;
            *receiver_slot = current.slot;
            return true;
        }
        prior = current;
        current = receiver->ipc_next;
    }
    return false;
}

bool ipc_model_find_sender(
    const struct model_state *model,
    size_t endpoint_slot,
    micros_endpoint_t source,
    struct micros_thread_handle *previous,
    size_t *sender_slot
)
{
    struct micros_thread_handle current =
        model->endpoints[endpoint_slot].sender_head;
    struct micros_thread_handle prior = ipc_model_null_thread_handle();
    size_t steps;

    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        const struct model_thread *sender;

        if (ipc_model_thread_handle_is_zero(current)) {
            return false;
        }
        sender = &model->threads[current.slot];
        if (
            source == MICROS_ENDPOINT_ANY
            || sender->ipc_outbound_message.source == source
        ) {
            *previous = prior;
            *sender_slot = current.slot;
            return true;
        }
        prior = current;
        current = sender->ipc_next;
    }
    return false;
}

bool ipc_model_find_pending_notification(
    const struct model_state *model,
    size_t destination_slot,
    micros_endpoint_t source,
    size_t *source_slot,
    uint64_t *event_mask
)
{
    const struct model_endpoint *destination =
        &model->endpoints[destination_slot];
    size_t resolved_slot;
    size_t index;

    if (source != MICROS_ENDPOINT_ANY) {
        if (
            !ipc_model_resolve_active_endpoint(
                model,
                source,
                &resolved_slot
            )
            || (
                destination->pending_notification_sources
                & (UINT64_C(1) << resolved_slot)
            ) == 0
        ) {
            return false;
        }
        *source_slot = resolved_slot;
        *event_mask = destination->pending_events[resolved_slot];
        return true;
    }
    if (destination->pending_kernel_events != 0) {
        *source_slot = MICROS_PROCESS_CAPACITY;
        *event_mask = destination->pending_kernel_events;
        return true;
    }
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        if (
            (
                destination->pending_notification_sources
                & (UINT64_C(1) << index)
            ) != 0
        ) {
            *source_slot = index;
            *event_mask = destination->pending_events[index];
            return true;
        }
    }
    return false;
}

static enum micros_ipc_error model_sole_live_thread(
    const struct model_state *model,
    size_t process_slot,
    size_t *thread_slot
)
{
    const struct model_process *process =
        &model->processes[process_slot];
    size_t matched = MICROS_THREAD_CAPACITY;
    size_t index;

    if (
        process->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || process->live_thread_count != 1
    ) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct model_thread *thread = &model->threads[index];

        if (
            thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && thread->owner.slot == process_slot
            && thread->owner.generation == process->generation
        ) {
            if (matched != MICROS_THREAD_CAPACITY) {
                return MICROS_IPC_ERROR_INVARIANT;
            }
            matched = index;
        }
    }
    if (matched == MICROS_THREAD_CAPACITY) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    *thread_slot = matched;
    return MICROS_IPC_OK;
}

enum micros_ipc_error ipc_model_deadlock_preflight(
    const struct model_state *model,
    size_t candidate_slot,
    micros_endpoint_t dependency,
    size_t cleared_slot
)
{
    struct micros_thread_handle visited[MICROS_THREAD_CAPACITY];
    size_t visited_count = 0;
    size_t steps;

    if (dependency == MICROS_ENDPOINT_ANY) {
        return MICROS_IPC_OK;
    }
    if (dependency == MICROS_ENDPOINT_NONE) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        size_t process_slot;
        size_t thread_slot;
        const struct model_thread *thread;
        micros_endpoint_t next = MICROS_ENDPOINT_NONE;
        bool has_dependency = true;
        size_t index;
        enum micros_ipc_error error;

        if (
            !ipc_model_resolve_active_endpoint(
                model,
                dependency,
                &process_slot
            )
        ) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
        error = model_sole_live_thread(
            model,
            process_slot,
            &thread_slot
        );
        if (error != MICROS_IPC_OK) {
            return error;
        }
        if (thread_slot == candidate_slot) {
            return MICROS_IPC_ERROR_DEADLOCK;
        }
        for (index = 0; index < visited_count; ++index) {
            if (
                visited[index].slot == thread_slot
                && visited[index].generation
                    == model->threads[thread_slot].generation
            ) {
                return MICROS_IPC_ERROR_INVARIANT;
            }
        }
        visited[visited_count] =
            ipc_model_thread_handle(model, thread_slot);
        ++visited_count;
        if (thread_slot == cleared_slot) {
            has_dependency = false;
        } else {
            thread = &model->threads[thread_slot];
            if (
                (
                    thread->runtime_flags
                    & MICROS_THREAD_RTS_IPC_SEND
                ) != 0
            ) {
                next = thread->ipc_send_destination;
            } else if (
                (
                    thread->runtime_flags
                    & MICROS_THREAD_RTS_IPC_REPLY
                ) != 0
            ) {
                next = thread->ipc_reply_callee;
            } else if (
                (
                    thread->runtime_flags
                    & MICROS_THREAD_RTS_IPC_RECEIVE
                ) != 0
            ) {
                if (
                    thread->ipc_receive_source
                    == MICROS_ENDPOINT_ANY
                ) {
                    has_dependency = false;
                } else {
                    next = thread->ipc_receive_source;
                }
            } else {
                has_dependency = false;
            }
        }
        if (!has_dependency) {
            return MICROS_IPC_OK;
        }
        if (
            next == MICROS_ENDPOINT_NONE
            || next == MICROS_ENDPOINT_ANY
        ) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
        dependency = next;
    }
    return MICROS_IPC_ERROR_INVARIANT;
}
