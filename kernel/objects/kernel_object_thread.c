#include "kernel/objects/kernel_object_private.h"

enum micros_kernel_object_error micros_thread_resolve(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    const struct micros_thread **thread
)
{
    const struct micros_thread *candidate;
    enum micros_kernel_object_error error;

    if (objects == NULL || thread == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = kernel_object_require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (!kernel_object_thread_handle_is_valid(handle)) {
        return MICROS_KERNEL_OBJECT_ERROR_STALE;
    }
    candidate = &objects->threads[handle.slot];
    if (
        candidate->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || candidate->generation != handle.generation
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STALE;
    }
    *thread = candidate;
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_thread_create(
    struct micros_kernel_objects *objects,
    struct micros_process_handle owner,
    struct micros_thread_handle *handle
)
{
    struct micros_process *process;
    enum micros_kernel_object_error error;
    size_t index;

    if (objects == NULL || handle == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = kernel_object_require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = kernel_object_resolve_process_mutable(objects, owner, &process);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        process->live_thread_count
        >= objects->max_threads_per_process
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_THREAD_LIMIT;
    }

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        struct micros_thread *thread = &objects->threads[index];
        uint32_t generation;

        if (
            thread->slot_state
            != MICROS_KERNEL_OBJECT_SLOT_FREE
        ) {
            continue;
        }
        error = micros_thread_next_generation(
            thread->generation,
            &generation
        );
        if (
            error
            == MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED
        ) {
            continue;
        }
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }

        thread->slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE;
        thread->generation = generation;
        thread->owner = owner;
        kernel_object_clear_thread_scheduler_metadata(thread);
        kernel_object_clear_thread_ipc_state(thread);
        thread->runtime_flags = MICROS_THREAD_RTS_INACTIVE;
        ++process->live_thread_count;
        ++objects->live_thread_count;
        handle->slot = (uint16_t)index;
        handle->generation = generation;
        return MICROS_KERNEL_OBJECT_OK;
    }
    return MICROS_KERNEL_OBJECT_ERROR_EXHAUSTED;
}

enum micros_kernel_object_error micros_thread_release(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle
)
{
    struct micros_thread *thread;
    struct micros_process *owner;
    enum micros_kernel_object_error error;
    uint32_t unused_generation;
    size_t index;

    if (objects == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = kernel_object_require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = kernel_object_resolve_thread_mutable(objects, handle, &thread);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        thread->runtime_flags != MICROS_THREAD_RTS_INACTIVE
        || thread->scheduler_assigned
        || thread->context_attached
        || !micros_thread_ipc_state_is_clear(thread)
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        const struct micros_hart *hart = &objects->harts[index];

        if (
            hart->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && kernel_object_thread_handles_equal(hart->current_thread, handle)
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
    }
    error = kernel_object_resolve_process_mutable(objects, thread->owner, &owner);
    if (
        error != MICROS_KERNEL_OBJECT_OK
        || owner->live_thread_count == 0
        || objects->live_thread_count == 0
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }

    error = micros_thread_next_generation(
        thread->generation,
        &unused_generation
    );
    if (error == MICROS_KERNEL_OBJECT_OK) {
        thread->slot_state = MICROS_KERNEL_OBJECT_SLOT_FREE;
    } else if (
        error == MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED
    ) {
        thread->slot_state =
            MICROS_KERNEL_OBJECT_SLOT_QUARANTINED;
    } else {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    thread->owner.slot = 0;
    thread->owner.generation = 0;
    kernel_object_clear_thread_scheduler_metadata(thread);
    kernel_object_clear_thread_ipc_state(thread);
    thread->context_attached = false;
    thread->kernel_stack_bottom = 0;
    thread->kernel_stack_top = 0;
    kernel_object_clear_user_context(&thread->user_context);
    --owner->live_thread_count;
    --objects->live_thread_count;
    return MICROS_KERNEL_OBJECT_OK;
}
