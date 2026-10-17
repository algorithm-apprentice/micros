#include "kernel/objects/kernel_object_private.h"

bool kernel_object_process_handle_is_valid(
    struct micros_process_handle handle
)
{
    return (
        handle.slot < MICROS_PROCESS_CAPACITY
        && handle.generation != 0
        && handle.generation <= MICROS_PROCESS_GENERATION_MAX
    );
}

bool kernel_object_thread_handle_is_valid(
    struct micros_thread_handle handle
)
{
    return (
        handle.slot < MICROS_THREAD_CAPACITY
        && handle.generation != 0
    );
}

bool kernel_object_hart_handle_is_valid(struct micros_hart_handle handle)
{
    return (
        handle.slot < MICROS_HART_CAPACITY
        && handle.generation != 0
    );
}

bool kernel_object_thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

bool kernel_object_thread_handle_is_null(
    struct micros_thread_handle handle
)
{
    return handle.generation == 0;
}

bool kernel_object_hart_handle_is_null(struct micros_hart_handle handle)
{
    return handle.generation == 0;
}

enum micros_kernel_object_error kernel_object_require_initialized(
    const struct micros_kernel_objects *objects
)
{
    if (objects == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    if (objects->initialization_magic != MICROS_KERNEL_OBJECTS_MAGIC) {
        return MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED;
    }
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error kernel_object_resolve_process_mutable(
    struct micros_kernel_objects *objects,
    struct micros_process_handle handle,
    struct micros_process **process
)
{
    struct micros_process *candidate;

    if (!kernel_object_process_handle_is_valid(handle)) {
        return MICROS_KERNEL_OBJECT_ERROR_STALE;
    }
    candidate = &objects->processes[handle.slot];
    if (
        candidate->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || candidate->generation != handle.generation
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STALE;
    }
    *process = candidate;
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error kernel_object_resolve_thread_mutable(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    struct micros_thread **thread
)
{
    struct micros_thread *candidate;

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

enum micros_kernel_object_error kernel_object_resolve_hart_mutable(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle handle,
    struct micros_hart **hart
)
{
    struct micros_hart *candidate;

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

enum micros_kernel_object_error micros_process_next_generation(
    uint16_t slot,
    uint32_t current_generation,
    uint32_t *next_generation
)
{
    uint32_t candidate;
    uint32_t endpoint;

    if (
        next_generation == NULL
        || slot >= (UINT16_C(1) << MICROS_PROCESS_SLOT_BITS)
        || current_generation > MICROS_PROCESS_GENERATION_MAX
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    if (current_generation == MICROS_PROCESS_GENERATION_MAX) {
        return MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED;
    }

    candidate = current_generation + 1;
    endpoint = (candidate << MICROS_PROCESS_SLOT_BITS) | slot;
    if (
        endpoint == MICROS_PROCESS_ENDPOINT_NONE
        || endpoint == MICROS_PROCESS_ENDPOINT_ANY
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED;
    }

    *next_generation = candidate;
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_thread_next_generation(
    uint32_t current_generation,
    uint32_t *next_generation
)
{
    if (next_generation == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    if (current_generation == UINT32_MAX) {
        return MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED;
    }
    *next_generation = current_generation + 1;
    return MICROS_KERNEL_OBJECT_OK;
}
