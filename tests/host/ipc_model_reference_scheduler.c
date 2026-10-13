#include "ipc_model_test_internal.h"

static void model_ready_append(
    struct model_state *model,
    size_t thread_slot
)
{
    struct model_thread *thread = &model->threads[thread_slot];
    uint8_t priority = thread->scheduler_priority;
    struct micros_thread_handle handle =
        ipc_model_thread_handle(model, thread_slot);
    struct micros_thread_handle tail =
        model->hart.ready_tail[priority];

    thread->ready_linked = true;
    thread->ready_next = ipc_model_null_thread_handle();
    if (ipc_model_thread_handle_is_zero(tail)) {
        model->hart.ready_head[priority] = handle;
    } else {
        model->threads[tail.slot].ready_next = handle;
    }
    model->hart.ready_tail[priority] = handle;
}

static bool model_ready_remove(
    struct model_state *model,
    size_t thread_slot
)
{
    struct model_thread *thread = &model->threads[thread_slot];
    uint8_t priority = thread->scheduler_priority;
    struct micros_thread_handle current =
        model->hart.ready_head[priority];
    struct micros_thread_handle previous = ipc_model_null_thread_handle();
    struct micros_thread_handle target =
        ipc_model_thread_handle(model, thread_slot);
    size_t steps;

    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        struct micros_thread_handle next;

        if (ipc_model_thread_handle_is_zero(current)) {
            return false;
        }
        next = model->threads[current.slot].ready_next;
        if (ipc_model_thread_handles_equal(current, target)) {
            if (ipc_model_thread_handle_is_zero(previous)) {
                model->hart.ready_head[priority] = next;
            } else {
                model->threads[previous.slot].ready_next = next;
            }
            if (
                ipc_model_thread_handles_equal(
                    model->hart.ready_tail[priority],
                    target
                )
            ) {
                model->hart.ready_tail[priority] = previous;
            }
            thread->ready_linked = false;
            thread->ready_next = ipc_model_null_thread_handle();
            return true;
        }
        previous = current;
        current = next;
    }
    return false;
}

enum micros_ipc_error ipc_model_apply_transitions(
    struct model_state *model,
    const struct model_transition *transitions,
    size_t transition_count
)
{
    size_t index;
    size_t other;

    for (index = 0; index < transition_count; ++index) {
        const struct model_transition *transition =
            &transitions[index];
        const struct model_thread *thread;
        uint32_t resulting_flags;

        if (
            transition->thread_slot >= MICROS_THREAD_CAPACITY
            || transition->clear_flags == 0
        ) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
        for (other = 0; other < index; ++other) {
            if (
                transitions[other].thread_slot
                    == transition->thread_slot
            ) {
                return MICROS_IPC_ERROR_INVARIANT;
            }
        }
        thread = &model->threads[transition->thread_slot];
        if (
            thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || !thread->scheduler_assigned
            || (
                thread->runtime_flags
                & transition->clear_flags
            ) != transition->clear_flags
            || (
                thread->runtime_flags
                & transition->set_flags
            ) != 0
        ) {
            return MICROS_IPC_ERROR_STATE;
        }
        resulting_flags =
            (
                thread->runtime_flags
                & ~transition->clear_flags
            )
            | transition->set_flags;
        if (
            resulting_flags == 0
            && (
                thread->ready_linked
                || !ipc_model_thread_handle_is_zero(thread->ready_next)
                || ipc_model_thread_handles_equal(
                    model->hart.current_thread,
                    ipc_model_thread_handle(
                        model,
                        transition->thread_slot
                    )
                )
            )
        ) {
            return MICROS_IPC_ERROR_STATE;
        }
    }
    for (index = 0; index < transition_count; ++index) {
        const struct model_transition *transition =
            &transitions[index];
        struct model_thread *thread =
            &model->threads[transition->thread_slot];

        thread->runtime_flags =
            (
                thread->runtime_flags
                & ~transition->clear_flags
            )
            | transition->set_flags;
        if (thread->runtime_flags == 0) {
            model_ready_append(model, transition->thread_slot);
        }
    }
    return MICROS_IPC_OK;
}

enum micros_ipc_error ipc_model_hold_thread(
    struct model_state *model,
    size_t thread_slot
)
{
    struct model_thread *thread;

    if (thread_slot >= MICROS_THREAD_CAPACITY) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    thread = &model->threads[thread_slot];
    if (
        thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || !thread->scheduler_assigned
        || thread->runtime_flags != 0
        || !thread->ready_linked
    ) {
        return MICROS_IPC_ERROR_STATE;
    }
    if (!model_ready_remove(model, thread_slot)) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    thread->runtime_flags = MICROS_THREAD_RTS_INACTIVE;
    return MICROS_IPC_OK;
}
