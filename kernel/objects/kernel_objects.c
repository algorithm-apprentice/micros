#include "micros/kernel_objects.h"

#include <stdint.h>

#include "kernel/scheduler/scheduler_core_internal.h"

#include "kernel/objects/kernel_object_private.h"

enum micros_kernel_object_error micros_kernel_objects_initialize(
    struct micros_kernel_objects *objects,
    size_t max_threads_per_process,
    size_t max_harts
)
{
    size_t index;

    if (objects == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    if (
        max_threads_per_process == 0
        || max_threads_per_process > MICROS_THREAD_CAPACITY
        || max_harts == 0
        || max_harts > MICROS_HART_CAPACITY
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_POLICY;
    }
    if (objects->initialization_magic == MICROS_KERNEL_OBJECTS_MAGIC) {
        return MICROS_KERNEL_OBJECT_ERROR_ALREADY_INITIALIZED;
    }
    if (!kernel_object_storage_is_zero(objects)) {
        return MICROS_KERNEL_OBJECT_ERROR_STORAGE;
    }

    objects->initialization_magic = MICROS_KERNEL_OBJECTS_MAGIC;
    objects->max_threads_per_process = max_threads_per_process;
    objects->max_harts = max_harts;
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        objects->processes[index].primary_endpoint =
            MICROS_PROCESS_ENDPOINT_NONE;
    }
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_kernel_objects_validate(
    const struct micros_kernel_objects *objects
)
{
    return micros_scheduler_core_validate(objects);
}
