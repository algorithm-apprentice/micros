#include "kernel/scheduler/scheduler_core_private.h"

struct scheduler_ipc_transition_candidate {
    struct micros_thread_handle handle;
    struct micros_thread *thread;
    struct micros_hart *hart;
    uint32_t clear_flags;
    uint32_t set_flags;
    uint32_t resulting_flags;
};

static bool ipc_transition_clear_flags_are_valid(uint32_t flags)
{
    return (
        flags != 0
        && (
            flags
            & ~(
                MICROS_THREAD_RTS_INACTIVE
                | MICROS_THREAD_RTS_IPC_MASK
            )
        ) == 0
    );
}

static bool ipc_transition_set_flags_are_valid(uint32_t flags)
{
    return (
        flags == 0
        || flags == MICROS_THREAD_RTS_IPC_RECEIVE
        || flags == MICROS_THREAD_RTS_IPC_REPLY
    );
}

static enum micros_kernel_object_error preflight_transition_shapes(
    const struct micros_scheduler_ipc_transition *requests,
    size_t request_count
)
{
    size_t index;
    size_t other_index;

    for (index = 0; index < request_count; ++index) {
        if (
            !ipc_transition_clear_flags_are_valid(
                requests[index].clear_flags
            )
            || !ipc_transition_set_flags_are_valid(
                requests[index].set_flags
            )
            || (
                requests[index].clear_flags
                & requests[index].set_flags
            ) != 0
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
        }
        for (other_index = 0; other_index < index; ++other_index) {
            if (
                scheduler_core_thread_handles_equal(
                    requests[index].handle,
                    requests[other_index].handle
                )
            ) {
                return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
            }
        }
    }
    return MICROS_KERNEL_OBJECT_OK;
}

static enum micros_kernel_object_error preflight_transition_candidates(
    struct micros_kernel_objects *objects,
    const struct micros_scheduler_ipc_transition *requests,
    size_t request_count,
    struct scheduler_ipc_transition_candidate *transitions
)
{
    enum micros_kernel_object_error error;
    size_t index;

    for (index = 0; index < request_count; ++index) {
        transitions[index].handle = requests[index].handle;
        transitions[index].clear_flags =
            requests[index].clear_flags;
        transitions[index].set_flags =
            requests[index].set_flags;
        error = scheduler_core_resolve_thread_mutable(
            objects,
            transitions[index].handle,
            &transitions[index].thread
        );
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
        if (
            !transitions[index].thread->scheduler_assigned
            || (
                transitions[index].thread->runtime_flags
                & transitions[index].clear_flags
            ) != transitions[index].clear_flags
            || (
                transitions[index].thread->runtime_flags
                & transitions[index].set_flags
            ) != 0
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_STATE;
        }
        transitions[index].resulting_flags =
            (
                transitions[index].thread->runtime_flags
                & ~transitions[index].clear_flags
            )
            | transitions[index].set_flags;
        transitions[index].hart = NULL;
        if (transitions[index].resulting_flags != 0) {
            continue;
        }
        if (scheduler_core_thread_is_current(objects, transitions[index].handle)) {
            return MICROS_KERNEL_OBJECT_ERROR_STATE;
        }
        error = scheduler_core_resolve_hart_mutable(
            objects,
            transitions[index].thread->scheduler_hart,
            &transitions[index].hart
        );
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        error = scheduler_core_preflight_tail_enqueue(
            objects,
            transitions[index].hart,
            transitions[index].handle,
            transitions[index].thread->scheduler_priority
        );
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
    }
    return MICROS_KERNEL_OBJECT_OK;
}

static void commit_transition_candidates(
    struct micros_kernel_objects *objects,
    struct scheduler_ipc_transition_candidate *transitions,
    size_t request_count
)
{
    size_t index;

    for (index = 0; index < request_count; ++index) {
        transitions[index].thread->runtime_flags =
            transitions[index].resulting_flags;
        if (transitions[index].resulting_flags == 0) {
            scheduler_core_queue_enqueue_tail_prevalidated(
                objects,
                transitions[index].hart,
                transitions[index].handle
            );
        }
    }
}

enum micros_kernel_object_error micros_scheduler_commit_ipc_transitions(
    struct micros_kernel_objects *objects,
    const struct micros_scheduler_ipc_transition *requests,
    size_t request_count
)
{
    struct scheduler_ipc_transition_candidate transitions[
        MICROS_THREAD_CAPACITY
    ];
    enum micros_kernel_object_error error;

    if (
        objects == NULL
        || requests == NULL
        || request_count < 1
        || request_count > MICROS_THREAD_CAPACITY
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = preflight_transition_shapes(requests, request_count);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = micros_scheduler_core_validate(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = preflight_transition_candidates(
        objects,
        requests,
        request_count,
        transitions
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    commit_transition_candidates(objects, transitions, request_count);
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_scheduler_prepare_ipc_wake(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle thread_handle,
    uint32_t clear_flag,
    struct micros_scheduler_ipc_wake_plan *plan
)
{
    struct micros_scheduler_ipc_wake_plan candidate = {0};
    const struct micros_thread *thread;
    const struct micros_hart *hart;
    enum micros_kernel_object_error error;

    if (
        objects == NULL
        || plan == NULL
        || !ipc_transition_clear_flags_are_valid(clear_flag)
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = micros_scheduler_core_validate(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = micros_thread_resolve(objects, thread_handle, &thread);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        !thread->scheduler_assigned
        || (thread->runtime_flags & clear_flag) != clear_flag
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    candidate.resulting_flags =
        thread->runtime_flags & ~clear_flag;
    if (candidate.resulting_flags == 0) {
        if (scheduler_core_thread_is_current(objects, thread_handle)) {
            return MICROS_KERNEL_OBJECT_ERROR_STATE;
        }
        error = micros_hart_resolve(
            objects,
            thread->scheduler_hart,
            &hart
        );
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        error = scheduler_core_preflight_tail_enqueue(
            objects,
            hart,
            thread_handle,
            thread->scheduler_priority
        );
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
        candidate.hart = thread->scheduler_hart;
    }
    candidate.active = true;
    candidate.thread = thread_handle;
    *plan = candidate;
    return MICROS_KERNEL_OBJECT_OK;
}

void micros_scheduler_commit_ipc_wake_prevalidated(
    struct micros_kernel_objects *objects,
    struct micros_scheduler_ipc_wake_plan *plan
)
{
    struct micros_thread *thread =
        &objects->threads[plan->thread.slot];

    thread->runtime_flags = plan->resulting_flags;
    if (plan->resulting_flags == 0) {
        scheduler_core_queue_enqueue_tail_prevalidated(
            objects,
            &objects->harts[plan->hart.slot],
            plan->thread
        );
    }
    *plan = (struct micros_scheduler_ipc_wake_plan){0};
}

enum micros_kernel_object_error micros_scheduler_commit_ipc_wake(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread,
    uint32_t clear_flag
)
{
    struct micros_scheduler_ipc_wake_plan plan;
    enum micros_kernel_object_error error;

    error = micros_scheduler_prepare_ipc_wake(
        objects,
        thread,
        clear_flag,
        &plan
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    micros_scheduler_commit_ipc_wake_prevalidated(objects, &plan);
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_scheduler_commit_ipc_wake_pair(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle first_thread_handle,
    uint32_t first_clear_flag,
    struct micros_thread_handle second_thread_handle,
    uint32_t second_clear_flag
)
{
    const struct micros_scheduler_ipc_transition requests[2] = {
        {
            .handle = first_thread_handle,
            .clear_flags = first_clear_flag,
            .set_flags = 0,
        },
        {
            .handle = second_thread_handle,
            .clear_flags = second_clear_flag,
            .set_flags = 0,
        },
    };

    return micros_scheduler_commit_ipc_transitions(
        objects,
        requests,
        sizeof(requests) / sizeof(requests[0])
    );
}

enum micros_kernel_object_error micros_scheduler_commit_ipc_call_delivery(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle caller,
    struct micros_thread_handle receiver
)
{
    const struct micros_scheduler_ipc_transition requests[2] = {
        {
            .handle = caller,
            .clear_flags = MICROS_THREAD_RTS_INACTIVE,
            .set_flags = MICROS_THREAD_RTS_IPC_REPLY,
        },
        {
            .handle = receiver,
            .clear_flags = MICROS_THREAD_RTS_IPC_RECEIVE,
            .set_flags = 0,
        },
    };

    return micros_scheduler_commit_ipc_transitions(
        objects,
        requests,
        sizeof(requests) / sizeof(requests[0])
    );
}

enum micros_kernel_object_error
micros_scheduler_commit_ipc_reply_receive_wait(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle caller,
    struct micros_thread_handle replier
)
{
    const struct micros_scheduler_ipc_transition requests[2] = {
        {
            .handle = caller,
            .clear_flags = MICROS_THREAD_RTS_IPC_REPLY,
            .set_flags = 0,
        },
        {
            .handle = replier,
            .clear_flags = MICROS_THREAD_RTS_INACTIVE,
            .set_flags = MICROS_THREAD_RTS_IPC_RECEIVE,
        },
    };

    return micros_scheduler_commit_ipc_transitions(
        objects,
        requests,
        sizeof(requests) / sizeof(requests[0])
    );
}

enum micros_kernel_object_error
micros_scheduler_commit_ipc_reply_receive_delivery(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle caller,
    struct micros_thread_handle replier,
    struct micros_thread_handle sender
)
{
    const struct micros_scheduler_ipc_transition requests[3] = {
        {
            .handle = caller,
            .clear_flags = MICROS_THREAD_RTS_IPC_REPLY,
            .set_flags = 0,
        },
        {
            .handle = replier,
            .clear_flags = MICROS_THREAD_RTS_INACTIVE,
            .set_flags = 0,
        },
        {
            .handle = sender,
            .clear_flags = MICROS_THREAD_RTS_IPC_SEND,
            .set_flags = 0,
        },
    };

    return micros_scheduler_commit_ipc_transitions(
        objects,
        requests,
        sizeof(requests) / sizeof(requests[0])
    );
}
