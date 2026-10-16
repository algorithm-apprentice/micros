#include "kernel_object_private.h"

enum micros_kernel_object_error micros_process_resolve(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle handle,
    const struct micros_process **process
)
{
    const struct micros_process *candidate;
    enum micros_kernel_object_error error;

    if (objects == NULL || process == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = kernel_object_require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
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

enum micros_kernel_object_error micros_process_create(
    struct micros_kernel_objects *objects,
    struct micros_process_handle *handle
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

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        enum micros_kernel_object_error create_error;

        if (
            objects->processes[index].slot_state
            != MICROS_KERNEL_OBJECT_SLOT_FREE
        ) {
            continue;
        }
        create_error = micros_process_create_at(
            objects,
            (uint16_t)index,
            handle
        );
        if (
            create_error
            == MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED
        ) {
            continue;
        }
        return create_error;
    }
    return MICROS_KERNEL_OBJECT_ERROR_EXHAUSTED;
}

enum micros_kernel_object_error micros_process_create_at(
    struct micros_kernel_objects *objects,
    uint16_t slot,
    struct micros_process_handle *handle
)
{
    struct micros_process *process;
    uint32_t generation;
    enum micros_kernel_object_error error;

    if (
        objects == NULL
        || handle == NULL
        || slot >= MICROS_PROCESS_CAPACITY
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = kernel_object_require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    process = &objects->processes[slot];
    if (
        process->slot_state
        == MICROS_KERNEL_OBJECT_SLOT_QUARANTINED
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED;
    }
    if (process->slot_state != MICROS_KERNEL_OBJECT_SLOT_FREE) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    error = micros_process_next_generation(
        slot,
        process->generation,
        &generation
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }

    process->slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE;
    process->generation = generation;
    process->live_thread_count = 0;
    process->address_space_root = 0;
    process->primary_endpoint = MICROS_PROCESS_ENDPOINT_NONE;
    process->privilege_profile = 0;
    process->endpoint_lifecycle_consumed = false;
    ++objects->live_process_count;
    handle->slot = slot;
    handle->generation = generation;
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_process_release(
    struct micros_kernel_objects *objects,
    struct micros_process_handle handle
)
{
    struct micros_process *process;
    enum micros_kernel_object_error error;
    uint32_t unused_generation;

    if (objects == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = kernel_object_require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = kernel_object_resolve_process_mutable(objects, handle, &process);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        process->live_thread_count != 0
        || process->address_space_root != 0
        || process->primary_endpoint
            != MICROS_PROCESS_ENDPOINT_NONE
        || process->privilege_profile != 0
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    if (objects->live_process_count == 0) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }

    error = micros_process_next_generation(
        handle.slot,
        process->generation,
        &unused_generation
    );
    if (error == MICROS_KERNEL_OBJECT_OK) {
        process->slot_state = MICROS_KERNEL_OBJECT_SLOT_FREE;
    } else if (
        error == MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED
    ) {
        process->slot_state =
            MICROS_KERNEL_OBJECT_SLOT_QUARANTINED;
    } else {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    process->live_thread_count = 0;
    process->address_space_root = 0;
    process->primary_endpoint = MICROS_PROCESS_ENDPOINT_NONE;
    process->privilege_profile = 0;
    process->endpoint_lifecycle_consumed = false;
    --objects->live_process_count;
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error
micros_process_attach_address_space(
    struct micros_kernel_objects *objects,
    struct micros_process_handle process_handle,
    uintptr_t root
)
{
    struct micros_process *process;
    enum micros_kernel_object_error error;

    if (objects == NULL || root == 0) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = kernel_object_require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = kernel_object_resolve_process_mutable(
        objects,
        process_handle,
        &process
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (process->address_space_root != 0) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    process->address_space_root = root;
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error
micros_process_detach_address_space(
    struct micros_kernel_objects *objects,
    struct micros_process_handle process_handle,
    uintptr_t expected_root
)
{
    struct micros_process *process;
    enum micros_kernel_object_error error;

    if (objects == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = kernel_object_require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = kernel_object_resolve_process_mutable(
        objects,
        process_handle,
        &process
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        expected_root == 0
        || process->live_thread_count != 0
        || process->address_space_root != expected_root
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    process->address_space_root = 0;
    return MICROS_KERNEL_OBJECT_OK;
}
