#include "kernel/objects/kernel_object_private.h"
#include "micros/stack_observation.h"
static bool kernel_object_context_stack_avoids_threads(
    const struct micros_kernel_objects *objects,
    const struct micros_thread *thread,
    uintptr_t kernel_stack_bottom,
    uintptr_t kernel_stack_top
)
{
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *other =
            &objects->threads[index];

        if (
            other == thread
            || other->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || !other->context_attached
        ) {
            continue;
        }
        if (
            kernel_object_ranges_overlap(
                kernel_stack_bottom,
                kernel_stack_top,
                other->kernel_stack_bottom,
                other->kernel_stack_top
            )
        ) {
            return false;
        }
    }
    return true;
}

static bool kernel_object_context_stack_avoids_harts(
    const struct micros_kernel_objects *objects,
    uintptr_t kernel_stack_bottom,
    uintptr_t kernel_stack_top
)
{
    size_t index;

    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        const struct micros_hart *hart = &objects->harts[index];

        if (
            hart->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || !hart->trap_installed
        ) {
            continue;
        }
        if (
            kernel_object_ranges_overlap(
                kernel_stack_bottom,
                kernel_stack_top,
                hart->idle_primary_stack_bottom,
                hart->idle_primary_stack_top
            )
            || kernel_object_ranges_overlap(
                kernel_stack_bottom,
                kernel_stack_top,
                hart->trap.emergency_stack_bottom,
                hart->trap.emergency_stack_top
            )
        ) {
            return false;
        }
    }
    return true;
}

enum micros_kernel_object_error
micros_thread_attach_execution_context(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread_handle,
    uintptr_t kernel_stack_bottom,
    uintptr_t kernel_stack_top,
    const struct micros_user_context *context
)
{
    struct micros_thread *thread;
    enum micros_kernel_object_error error;

    if (objects == NULL || context == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = kernel_object_require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = kernel_object_resolve_thread_mutable(
        objects,
        thread_handle,
        &thread
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        thread->runtime_flags != MICROS_THREAD_RTS_INACTIVE
        || thread->scheduler_assigned
        || thread->context_attached
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    if (
        !kernel_object_stack_range_is_valid(
            kernel_stack_bottom,
            kernel_stack_top,
            MICROS_THREAD_KERNEL_STACK_SIZE
        )
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STACK;
    }
    if (
        !kernel_object_context_stack_avoids_threads(
            objects,
            thread,
            kernel_stack_bottom,
            kernel_stack_top
        )
        || !kernel_object_context_stack_avoids_harts(
            objects,
            kernel_stack_bottom,
            kernel_stack_top
        )
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STACK;
    }

    thread->context_attached = true;
    thread->kernel_stack_bottom = kernel_stack_bottom;
    thread->kernel_stack_top = kernel_stack_top;
    kernel_object_copy_user_context(&thread->user_context, context); MICROS_TEST_STACK_ATTACHED(objects, thread_handle);
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error
micros_thread_capture_execution_context(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread_handle,
    const struct micros_user_context *context
)
{
    struct micros_thread *thread;
    enum micros_kernel_object_error error;
    size_t current_count = 0;
    size_t index;

    if (objects == NULL || context == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = kernel_object_require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = kernel_object_resolve_thread_mutable(
        objects,
        thread_handle,
        &thread
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (!thread->context_attached) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        const struct micros_hart *hart = &objects->harts[index];

        if (
            hart->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && kernel_object_thread_handles_equal(
                hart->current_thread,
                thread_handle
            )
        ) {
            ++current_count;
        }
    }
    if (current_count == 0) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    if (current_count > 1) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    kernel_object_copy_user_context(&thread->user_context, context);
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error
micros_thread_inspect_execution_context(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle thread_handle,
    struct micros_user_context *context,
    uintptr_t *kernel_stack_bottom,
    uintptr_t *kernel_stack_top
)
{
    const struct micros_thread *thread;
    enum micros_kernel_object_error error;

    if (
        objects == NULL
        || context == NULL
        || kernel_stack_bottom == NULL
        || kernel_stack_top == NULL
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = micros_thread_resolve(
        objects,
        thread_handle,
        &thread
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (!thread->context_attached) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    kernel_object_copy_user_context(context, &thread->user_context);
    *kernel_stack_bottom = thread->kernel_stack_bottom;
    *kernel_stack_top = thread->kernel_stack_top;
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error
micros_thread_detach_execution_context(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread_handle
)
{
    struct micros_thread *thread;
    enum micros_kernel_object_error error;
    size_t index;

    if (objects == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = kernel_object_require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = kernel_object_resolve_thread_mutable(
        objects,
        thread_handle,
        &thread
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        !thread->context_attached
        || thread->runtime_flags != MICROS_THREAD_RTS_INACTIVE
        || thread->scheduler_assigned
        || !micros_thread_ipc_state_is_clear(thread)
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        const struct micros_hart *hart = &objects->harts[index];

        if (
            hart->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && kernel_object_thread_handles_equal(
                hart->current_thread,
                thread_handle
            )
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_STATE;
        }
    }
    MICROS_TEST_STACK_DETACHING(objects, thread_handle); thread->context_attached = false;
    thread->kernel_stack_bottom = 0;
    thread->kernel_stack_top = 0;
    kernel_object_clear_user_context(&thread->user_context);
    return MICROS_KERNEL_OBJECT_OK;
}
