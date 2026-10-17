#include "kernel/scheduler/scheduler_core_private.h"

bool scheduler_core_thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

bool scheduler_core_hart_handles_equal(
    struct micros_hart_handle left,
    struct micros_hart_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

bool scheduler_core_thread_handle_is_null(
    struct micros_thread_handle handle
)
{
    return handle.generation == 0;
}

bool scheduler_core_hart_handle_is_null(struct micros_hart_handle handle)
{
    return handle.generation == 0;
}

struct micros_thread_handle scheduler_core_null_thread_handle(void)
{
    struct micros_thread_handle handle = {0, 0};

    return handle;
}

enum micros_kernel_object_error scheduler_core_resolve_thread_mutable(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    struct micros_thread **thread
)
{
    const struct micros_thread *resolved;
    enum micros_kernel_object_error error;

    if (objects == NULL || thread == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = micros_thread_resolve(objects, handle, &resolved);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    *thread = &objects->threads[handle.slot];
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error scheduler_core_resolve_hart_mutable(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle handle,
    struct micros_hart **hart
)
{
    const struct micros_hart *resolved;
    enum micros_kernel_object_error error;

    if (objects == NULL || hart == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = micros_hart_resolve(objects, handle, &resolved);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    *hart = &objects->harts[handle.slot];
    return MICROS_KERNEL_OBJECT_OK;
}

bool scheduler_core_thread_is_current(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle thread
)
{
    size_t index;

    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        const struct micros_hart *hart = &objects->harts[index];

        if (
            hart->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && scheduler_core_thread_handles_equal(hart->current_thread, thread)
        ) {
            return true;
        }
    }
    return false;
}

bool scheduler_core_hart_has_assigned_thread(
    const struct micros_kernel_objects *objects,
    struct micros_hart_handle hart_handle
)
{
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];

        if (
            thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && thread->scheduler_assigned
            && scheduler_core_hart_handles_equal(
                thread->scheduler_hart,
                hart_handle
            )
        ) {
            return true;
        }
    }
    return false;
}

bool scheduler_core_add_overflows(uint64_t left, uint64_t right)
{
    return UINT64_MAX - left < right;
}

void scheduler_core_bitmap_clear(uint64_t *bitmap)
{
    size_t index;

    for (
        index = 0;
        index < (MICROS_THREAD_CAPACITY + 63) / 64;
        ++index
    ) {
        bitmap[index] = 0;
    }
}

void scheduler_core_bitmap_set(uint64_t *bitmap, size_t slot)
{
    bitmap[slot / 64] |= UINT64_C(1) << (slot % 64);
}

bool scheduler_core_bitmap_test(const uint64_t *bitmap, size_t slot)
{
    return (
        bitmap[slot / 64] & (UINT64_C(1) << (slot % 64))
    ) != 0;
}
