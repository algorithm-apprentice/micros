#include "ipc_internal.h"

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
        (
            record->state != MICROS_ENDPOINT_STATE_ACTIVE
            && record->state
                != MICROS_ENDPOINT_STATE_SOURCE_ONLY
        )
        || record->value != endpoint
        || !ipc_process_handles_equal(record->owner, unpacked)
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
            || !ipc_process_handles_equal(candidate->owner, owner)
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
        !ipc_thread_handle_is_zero(cleared_dependency)
        && ipc_thread_handles_equal(thread_handle, cleared_dependency)
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

enum micros_ipc_error ipc_preflight_deadlock(
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
    if (!ipc_thread_handle_is_zero(cleared_dependency)) {
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
        if (ipc_thread_handles_equal(thread_handle, candidate)) {
            return MICROS_IPC_ERROR_DEADLOCK;
        }
        for (index = 0; index < visited_count; ++index) {
            if (ipc_thread_handles_equal(visited[index], thread_handle)) {
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
