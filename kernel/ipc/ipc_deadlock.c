#include "kernel/ipc/ipc_internal.h"

#include "kernel/scheduler/scheduler_core_internal.h"

_Static_assert(MICROS_THREAD_CAPACITY <= 128, "dependency bitmap must cover every thread slot");

static enum micros_ipc_error resolve_dependency_endpoint(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    struct micros_process_handle *owner
)
{
    struct micros_endpoint_selection selected;

    if (
        endpoint == MICROS_ENDPOINT_NONE
        || endpoint == MICROS_ENDPOINT_ANY
        || micros_endpoint_select(registry, objects, endpoint, &selected)
            != MICROS_ENDPOINT_OK
    ) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    if (
        selected.record->state != MICROS_ENDPOINT_STATE_ACTIVE
        && selected.record->state != MICROS_ENDPOINT_STATE_SOURCE_ONLY
    ) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    *owner = selected.record->owner;
    return MICROS_IPC_OK;
}

static enum micros_ipc_error resolve_sole_live_thread(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle owner,
    struct micros_thread_handle *thread_handle,
    const struct micros_thread **thread
)
{
    const struct micros_process *process;
    const struct micros_thread *matched;
    struct micros_thread_handle matched_handle;

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
    matched_handle = process->sole_thread;
    if (
        scheduler_core_preflight_thread(objects, matched_handle, &matched)
            != MICROS_KERNEL_OBJECT_OK
        || !ipc_process_handles_equal(matched->owner, owner)
        || ((matched->runtime_flags & MICROS_THREAD_RTS_IPC_MASK) != 0
            && !micros_endpoint_thread_ipc_state_is_valid(registry, objects, matched_handle))
    ) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    *thread_handle = matched_handle;
    *thread = matched;
    return MICROS_IPC_OK;
}

static bool root_thread_index_is_valid(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle handle
)
{
    const struct micros_thread *thread;
    const struct micros_process *process;

    if (
        scheduler_core_preflight_registry(objects) != MICROS_KERNEL_OBJECT_OK
        || micros_thread_resolve(objects, handle, &thread) != MICROS_KERNEL_OBJECT_OK
        || micros_process_resolve(objects, thread->owner, &process) != MICROS_KERNEL_OBJECT_OK
        || process->live_thread_count == 0
        || process->live_thread_count > objects->max_threads_per_process
        || process->live_thread_count > objects->live_thread_count
    ) {
        return false;
    }
    return process->live_thread_count == 1
        ? ipc_thread_handles_equal(process->sole_thread, handle)
        : ipc_thread_handle_is_zero(process->sole_thread);
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
    uint64_t visited_low = 0;
    uint64_t visited_high = 0;
    size_t steps;

    if (
        dependency == MICROS_ENDPOINT_ANY
        || dependency == MICROS_ENDPOINT_NONE
    ) {
        return dependency == MICROS_ENDPOINT_ANY
            ? MICROS_IPC_OK
            : MICROS_IPC_ERROR_INVARIANT;
    }
    if (!root_thread_index_is_valid(objects, candidate)) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    if (!ipc_thread_handle_is_zero(cleared_dependency)
        && !root_thread_index_is_valid(objects, cleared_dependency)) {
        return MICROS_IPC_ERROR_INVARIANT;
    }

    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        struct micros_process_handle owner;
        struct micros_thread_handle thread_handle;
        const struct micros_thread *thread;
        micros_endpoint_t next_dependency = MICROS_ENDPOINT_NONE;
        enum micros_ipc_error error;
        bool has_dependency;
        uint64_t *visited;
        uint64_t bit;

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
            registry,
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
        visited = thread_handle.slot < 64 ? &visited_low : &visited_high;
        bit = UINT64_C(1) << (thread_handle.slot % 64);
        if ((*visited & bit) != 0) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
        *visited |= bit;

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
