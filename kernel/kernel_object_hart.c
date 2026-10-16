#include "kernel_object_private.h"

enum micros_kernel_object_error micros_hart_resolve(
    const struct micros_kernel_objects *objects,
    struct micros_hart_handle handle,
    const struct micros_hart **hart
)
{
    const struct micros_hart *candidate;
    enum micros_kernel_object_error error;

    if (objects == NULL || hart == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = kernel_object_require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (!kernel_object_hart_handle_is_valid(handle)) {
        return MICROS_KERNEL_OBJECT_ERROR_STALE;
    }
    candidate = &objects->harts[handle.slot];
    if (
        candidate->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || candidate->generation != handle.generation
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STALE;
    }
    *hart = candidate;
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_hart_register(
    struct micros_kernel_objects *objects,
    uintptr_t hardware_id,
    struct micros_hart_handle *handle
)
{
    enum micros_kernel_object_error error;
    size_t index;

    if (objects == NULL || handle == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = kernel_object_require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        const struct micros_hart *hart = &objects->harts[index];

        if (
            hart->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && hart->hardware_id == hardware_id
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_DUPLICATE_HART;
        }
    }
    if (objects->registered_hart_count >= objects->max_harts) {
        return MICROS_KERNEL_OBJECT_ERROR_POLICY;
    }

    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        struct micros_hart *hart = &objects->harts[index];
        uint32_t generation;

        if (
            hart->slot_state
            != MICROS_KERNEL_OBJECT_SLOT_FREE
        ) {
            continue;
        }
        error = micros_thread_next_generation(
            hart->generation,
            &generation
        );
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        hart->slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE;
        hart->generation = generation;
        hart->hardware_id = hardware_id;
        ++objects->registered_hart_count;
        handle->slot = (uint16_t)index;
        handle->generation = generation;
        return MICROS_KERNEL_OBJECT_OK;
    }
    return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
}

enum micros_kernel_object_error micros_hart_resolve_context(
    const struct micros_kernel_objects *objects,
    uintptr_t hart_context,
    const struct micros_hart **hart
)
{
    enum micros_kernel_object_error error;
    size_t index;

    if (objects == NULL || hart == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = kernel_object_require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        const struct micros_hart *candidate =
            &objects->harts[index];

        if (
            candidate->slot_state
                == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && (uintptr_t)candidate == hart_context
        ) {
            *hart = candidate;
            return MICROS_KERNEL_OBJECT_OK;
        }
    }
    return MICROS_KERNEL_OBJECT_ERROR_STALE;
}

static bool kernel_object_hart_stacks_avoid_harts(
    const struct micros_kernel_objects *objects,
    const struct micros_hart *hart,
    uintptr_t primary_stack_bottom,
    uintptr_t primary_stack_top,
    uintptr_t emergency_stack_bottom,
    uintptr_t emergency_stack_top
)
{
    size_t index;

    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        const struct micros_hart *other = &objects->harts[index];

        if (
            other == hart
            || other->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || !other->trap_installed
        ) {
            continue;
        }
        if (
            kernel_object_ranges_overlap(
                primary_stack_bottom,
                primary_stack_top,
                other->trap.primary_stack_bottom,
                other->trap.primary_stack_top
            )
            || kernel_object_ranges_overlap(
                primary_stack_bottom,
                primary_stack_top,
                other->idle_primary_stack_bottom,
                other->idle_primary_stack_top
            )
            || kernel_object_ranges_overlap(
                primary_stack_bottom,
                primary_stack_top,
                other->trap.emergency_stack_bottom,
                other->trap.emergency_stack_top
            )
            || kernel_object_ranges_overlap(
                emergency_stack_bottom,
                emergency_stack_top,
                other->trap.primary_stack_bottom,
                other->trap.primary_stack_top
            )
            || kernel_object_ranges_overlap(
                emergency_stack_bottom,
                emergency_stack_top,
                other->idle_primary_stack_bottom,
                other->idle_primary_stack_top
            )
            || kernel_object_ranges_overlap(
                emergency_stack_bottom,
                emergency_stack_top,
                other->trap.emergency_stack_bottom,
                other->trap.emergency_stack_top
            )
        ) {
            return false;
        }
    }
    return true;
}

static bool kernel_object_hart_stacks_avoid_threads(
    const struct micros_kernel_objects *objects,
    uintptr_t primary_stack_bottom,
    uintptr_t primary_stack_top,
    uintptr_t emergency_stack_bottom,
    uintptr_t emergency_stack_top
)
{
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];

        if (
            thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || !thread->context_attached
        ) {
            continue;
        }
        if (
            kernel_object_ranges_overlap(
                primary_stack_bottom,
                primary_stack_top,
                thread->kernel_stack_bottom,
                thread->kernel_stack_top
            )
            || kernel_object_ranges_overlap(
                emergency_stack_bottom,
                emergency_stack_top,
                thread->kernel_stack_bottom,
                thread->kernel_stack_top
            )
        ) {
            return false;
        }
    }
    return true;
}

enum micros_kernel_object_error micros_hart_install_trap_stacks(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle handle,
    uintptr_t primary_stack_bottom,
    uintptr_t primary_stack_top,
    uintptr_t emergency_stack_bottom,
    uintptr_t emergency_stack_top
)
{
    struct micros_hart *hart;
    enum micros_kernel_object_error error;

    if (objects == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = kernel_object_require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = kernel_object_resolve_hart_mutable(objects, handle, &hart);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (hart->trap_installed) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    if (
        !kernel_object_stack_range_is_valid(
            primary_stack_bottom,
            primary_stack_top,
            MICROS_PRIMARY_TRAP_STACK_MIN_SIZE
        )
        || !kernel_object_stack_range_is_valid(
            emergency_stack_bottom,
            emergency_stack_top,
            MICROS_EMERGENCY_TRAP_STACK_MIN_SIZE
        )
        || kernel_object_ranges_overlap(
            primary_stack_bottom,
            primary_stack_top,
            emergency_stack_bottom,
            emergency_stack_top
        )
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STACK;
    }
    if (
        !kernel_object_hart_stacks_avoid_harts(
            objects,
            hart,
            primary_stack_bottom,
            primary_stack_top,
            emergency_stack_bottom,
            emergency_stack_top
        )
        || !kernel_object_hart_stacks_avoid_threads(
            objects,
            primary_stack_bottom,
            primary_stack_top,
            emergency_stack_bottom,
            emergency_stack_top
        )
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STACK;
    }

    hart->trap.primary_stack_top = primary_stack_top;
    hart->trap.primary_stack_bottom = primary_stack_bottom;
    hart->trap.emergency_stack_top = emergency_stack_top;
    hart->trap.emergency_stack_bottom = emergency_stack_bottom;
    hart->trap.entry_t0 = 0;
    hart->trap.entry_t1 = 0;
    hart->trap.entry_t2 = 0;
    hart->idle_primary_stack_bottom = primary_stack_bottom;
    hart->idle_primary_stack_top = primary_stack_top;
    hart->trap_installed = true;
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_hart_current_thread(
    const struct micros_kernel_objects *objects,
    struct micros_hart_handle hart_handle,
    struct micros_thread_handle *thread
)
{
    const struct micros_hart *hart;
    const struct micros_thread *resolved_thread;
    enum micros_kernel_object_error error;

    if (objects == NULL || thread == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = kernel_object_require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = micros_hart_resolve(objects, hart_handle, &hart);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (hart->current_thread.generation == 0) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    error = micros_thread_resolve(
        objects,
        hart->current_thread,
        &resolved_thread
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    *thread = hart->current_thread;
    return MICROS_KERNEL_OBJECT_OK;
}
