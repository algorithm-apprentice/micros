#include "micros/kernel_objects.h"

#include <stdint.h>

#include "scheduler_core_internal.h"

#define MICROS_KERNEL_OBJECTS_MAGIC UINT64_C(0x4d4943524f534f42)

static bool storage_is_zero(
    const struct micros_kernel_objects *objects
)
{
    const unsigned char *bytes = (const unsigned char *)objects;
    size_t index;

    for (index = 0; index < sizeof(*objects); ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool process_handle_is_valid(
    struct micros_process_handle handle
)
{
    return (
        handle.slot < MICROS_PROCESS_CAPACITY
        && handle.generation != 0
        && handle.generation <= MICROS_PROCESS_GENERATION_MAX
    );
}

static bool thread_handle_is_valid(
    struct micros_thread_handle handle
)
{
    return (
        handle.slot < MICROS_THREAD_CAPACITY
        && handle.generation != 0
    );
}

static bool hart_handle_is_valid(struct micros_hart_handle handle)
{
    return (
        handle.slot < MICROS_HART_CAPACITY
        && handle.generation != 0
    );
}

static bool thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

static bool thread_handle_is_null(
    struct micros_thread_handle handle
)
{
    return handle.generation == 0;
}

static bool hart_handle_is_null(struct micros_hart_handle handle)
{
    return handle.generation == 0;
}

static bool thread_scheduler_metadata_is_zero(
    const struct micros_thread *thread
)
{
    return (
        thread->runtime_flags == 0
        && !thread->scheduler_assigned
        && !thread->scheduler_preemptible
        && thread->scheduler_priority == 0
        && hart_handle_is_null(thread->scheduler_hart)
        && thread->quantum_counter_ticks == 0
        && thread->remaining_counter_ticks == 0
        && !thread->ready_linked
        && thread_handle_is_null(thread->ready_next)
    );
}

static void clear_thread_scheduler_metadata(
    struct micros_thread *thread
)
{
    thread->runtime_flags = 0;
    thread->scheduler_assigned = false;
    thread->scheduler_preemptible = false;
    thread->scheduler_priority = 0;
    thread->scheduler_hart.slot = 0;
    thread->scheduler_hart.generation = 0;
    thread->quantum_counter_ticks = 0;
    thread->remaining_counter_ticks = 0;
    thread->ready_linked = false;
    thread->ready_next.slot = 0;
    thread->ready_next.generation = 0;
}

static bool stack_range_is_valid(
    uintptr_t bottom,
    uintptr_t top,
    uintptr_t minimum_size
)
{
    return (
        bottom != 0
        && top > bottom
        && bottom % MICROS_TRAP_STACK_ALIGNMENT == 0
        && top % MICROS_TRAP_STACK_ALIGNMENT == 0
        && top - bottom >= minimum_size
    );
}

static bool ranges_overlap(
    uintptr_t first_bottom,
    uintptr_t first_top,
    uintptr_t second_bottom,
    uintptr_t second_top
)
{
    return first_bottom < second_top && second_bottom < first_top;
}

static void copy_user_context(
    struct micros_user_context *destination,
    const struct micros_user_context *source
)
{
    unsigned char *destination_bytes =
        (unsigned char *)destination;
    const unsigned char *source_bytes =
        (const unsigned char *)source;
    size_t index;

    for (index = 0; index < sizeof(*destination); ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

static void clear_user_context(struct micros_user_context *context)
{
    unsigned char *bytes = (unsigned char *)context;
    size_t index;

    for (index = 0; index < sizeof(*context); ++index) {
        bytes[index] = 0;
    }
}

static bool user_context_is_zero(
    const struct micros_user_context *context
)
{
    const unsigned char *bytes =
        (const unsigned char *)context;
    size_t index;

    for (index = 0; index < sizeof(*context); ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool trap_anchor_is_zero(
    const struct micros_hart_trap_anchor *trap
)
{
    return (
        trap->primary_stack_top == 0
        && trap->primary_stack_bottom == 0
        && trap->emergency_stack_top == 0
        && trap->emergency_stack_bottom == 0
        && trap->entry_t0 == 0
        && trap->entry_t1 == 0
        && trap->entry_t2 == 0
    );
}

static bool trap_anchor_stacks_are_valid(
    const struct micros_hart_trap_anchor *trap
)
{
    return (
        stack_range_is_valid(
            trap->primary_stack_bottom,
            trap->primary_stack_top,
            MICROS_PRIMARY_TRAP_STACK_MIN_SIZE
        )
        && stack_range_is_valid(
            trap->emergency_stack_bottom,
            trap->emergency_stack_top,
            MICROS_EMERGENCY_TRAP_STACK_MIN_SIZE
        )
        && !ranges_overlap(
            trap->primary_stack_bottom,
            trap->primary_stack_top,
            trap->emergency_stack_bottom,
            trap->emergency_stack_top
        )
    );
}

static enum micros_kernel_object_error require_initialized(
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

static enum micros_kernel_object_error resolve_process_mutable(
    struct micros_kernel_objects *objects,
    struct micros_process_handle handle,
    struct micros_process **process
)
{
    struct micros_process *candidate;

    if (!process_handle_is_valid(handle)) {
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

static enum micros_kernel_object_error resolve_thread_mutable(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    struct micros_thread **thread
)
{
    struct micros_thread *candidate;

    if (!thread_handle_is_valid(handle)) {
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

static enum micros_kernel_object_error resolve_hart_mutable(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle handle,
    struct micros_hart **hart
)
{
    struct micros_hart *candidate;

    if (!hart_handle_is_valid(handle)) {
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

static bool timer_state_is_initial(
    const struct micros_hart_timer_state *timer
)
{
    return (
        !timer->initialized
        && !timer->active
        && timer->interval == 0
        && timer->deadline == 0
        && timer->ticks == 0
        && timer->test_stop_after == 0
    );
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
    if (!storage_is_zero(objects)) {
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
    error = require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (!process_handle_is_valid(handle)) {
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
    error = require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        struct micros_process *process = &objects->processes[index];
        uint32_t generation;

        if (
            process->slot_state
            != MICROS_KERNEL_OBJECT_SLOT_FREE
        ) {
            continue;
        }
        error = micros_process_next_generation(
            (uint16_t)index,
            process->generation,
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

        process->slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE;
        process->generation = generation;
        process->live_thread_count = 0;
        process->address_space_root = 0;
        process->primary_endpoint = MICROS_PROCESS_ENDPOINT_NONE;
        process->privilege_profile = 0;
        ++objects->live_process_count;
        handle->slot = (uint16_t)index;
        handle->generation = generation;
        return MICROS_KERNEL_OBJECT_OK;
    }
    return MICROS_KERNEL_OBJECT_ERROR_EXHAUSTED;
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
    error = require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = resolve_process_mutable(objects, handle, &process);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        process->live_thread_count != 0
        || process->address_space_root != 0
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
    --objects->live_process_count;
    return MICROS_KERNEL_OBJECT_OK;
}

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
    error = require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (!thread_handle_is_valid(handle)) {
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
    size_t index;

    if (objects == NULL || context == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = resolve_thread_mutable(
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
        !stack_range_is_valid(
            kernel_stack_bottom,
            kernel_stack_top,
            MICROS_THREAD_KERNEL_STACK_SIZE
        )
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STACK;
    }
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
            ranges_overlap(
                kernel_stack_bottom,
                kernel_stack_top,
                other->kernel_stack_bottom,
                other->kernel_stack_top
            )
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_STACK;
        }
    }
    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        const struct micros_hart *hart = &objects->harts[index];

        if (
            hart->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || !hart->trap_installed
        ) {
            continue;
        }
        if (
            ranges_overlap(
                kernel_stack_bottom,
                kernel_stack_top,
                hart->idle_primary_stack_bottom,
                hart->idle_primary_stack_top
            )
            || ranges_overlap(
                kernel_stack_bottom,
                kernel_stack_top,
                hart->trap.emergency_stack_bottom,
                hart->trap.emergency_stack_top
            )
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_STACK;
        }
    }

    thread->context_attached = true;
    thread->kernel_stack_bottom = kernel_stack_bottom;
    thread->kernel_stack_top = kernel_stack_top;
    copy_user_context(&thread->user_context, context);
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
    error = require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = resolve_thread_mutable(
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
            && thread_handles_equal(
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
    copy_user_context(&thread->user_context, context);
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
    copy_user_context(context, &thread->user_context);
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
    error = require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = resolve_thread_mutable(
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
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        const struct micros_hart *hart = &objects->harts[index];

        if (
            hart->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && thread_handles_equal(
                hart->current_thread,
                thread_handle
            )
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_STATE;
        }
    }
    thread->context_attached = false;
    thread->kernel_stack_bottom = 0;
    thread->kernel_stack_top = 0;
    clear_user_context(&thread->user_context);
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
    error = require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = resolve_process_mutable(
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
    error = require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = resolve_process_mutable(
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
    error = require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = resolve_process_mutable(objects, owner, &process);
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
        clear_thread_scheduler_metadata(thread);
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
    error = require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = resolve_thread_mutable(objects, handle, &thread);
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
    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        const struct micros_hart *hart = &objects->harts[index];

        if (
            hart->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && thread_handles_equal(hart->current_thread, handle)
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
    }
    error = resolve_process_mutable(objects, thread->owner, &owner);
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
    clear_thread_scheduler_metadata(thread);
    thread->context_attached = false;
    thread->kernel_stack_bottom = 0;
    thread->kernel_stack_top = 0;
    clear_user_context(&thread->user_context);
    --owner->live_thread_count;
    --objects->live_thread_count;
    return MICROS_KERNEL_OBJECT_OK;
}

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
    error = require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (!hart_handle_is_valid(handle)) {
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
    error = require_initialized(objects);
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
    error = require_initialized(objects);
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
    size_t index;

    if (objects == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = resolve_hart_mutable(objects, handle, &hart);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (hart->trap_installed) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    if (
        !stack_range_is_valid(
            primary_stack_bottom,
            primary_stack_top,
            MICROS_PRIMARY_TRAP_STACK_MIN_SIZE
        )
        || !stack_range_is_valid(
            emergency_stack_bottom,
            emergency_stack_top,
            MICROS_EMERGENCY_TRAP_STACK_MIN_SIZE
        )
        || ranges_overlap(
            primary_stack_bottom,
            primary_stack_top,
            emergency_stack_bottom,
            emergency_stack_top
        )
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STACK;
    }
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
            ranges_overlap(
                primary_stack_bottom,
                primary_stack_top,
                other->trap.primary_stack_bottom,
                other->trap.primary_stack_top
            )
            || ranges_overlap(
                primary_stack_bottom,
                primary_stack_top,
                other->idle_primary_stack_bottom,
                other->idle_primary_stack_top
            )
            || ranges_overlap(
                primary_stack_bottom,
                primary_stack_top,
                other->trap.emergency_stack_bottom,
                other->trap.emergency_stack_top
            )
            || ranges_overlap(
                emergency_stack_bottom,
                emergency_stack_top,
                other->trap.primary_stack_bottom,
                other->trap.primary_stack_top
            )
            || ranges_overlap(
                emergency_stack_bottom,
                emergency_stack_top,
                other->idle_primary_stack_bottom,
                other->idle_primary_stack_top
            )
            || ranges_overlap(
                emergency_stack_bottom,
                emergency_stack_top,
                other->trap.emergency_stack_bottom,
                other->trap.emergency_stack_top
            )
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_STACK;
        }
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];

        if (
            thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || !thread->context_attached
        ) {
            continue;
        }
        if (
            ranges_overlap(
                primary_stack_bottom,
                primary_stack_top,
                thread->kernel_stack_bottom,
                thread->kernel_stack_top
            )
            || ranges_overlap(
                emergency_stack_bottom,
                emergency_stack_top,
                thread->kernel_stack_bottom,
                thread->kernel_stack_top
            )
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_STACK;
        }
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
    error = require_initialized(objects);
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

enum micros_kernel_object_error micros_kernel_objects_validate(
    const struct micros_kernel_objects *objects
)
{
    return micros_scheduler_core_validate(objects);
}

enum micros_kernel_object_error micros_kernel_objects_validate_base(
    const struct micros_kernel_objects *objects
)
{
    size_t observed_process_count = 0;
    size_t observed_thread_count = 0;
    size_t observed_hart_count = 0;
    size_t process_thread_counts[MICROS_PROCESS_CAPACITY];
    bool current_threads[MICROS_THREAD_CAPACITY];
    enum micros_kernel_object_error error;
    size_t index;

    error = require_initialized(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        objects->max_threads_per_process == 0
        || objects->max_threads_per_process > MICROS_THREAD_CAPACITY
        || objects->max_harts == 0
        || objects->max_harts > MICROS_HART_CAPACITY
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        process_thread_counts[index] = 0;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        current_threads[index] = false;
    }

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct micros_process *process =
            &objects->processes[index];
        uint32_t unused_generation;

        if (
            process->generation > MICROS_PROCESS_GENERATION_MAX
            || process->primary_endpoint
                != MICROS_PROCESS_ENDPOINT_NONE
            || process->privilege_profile != 0
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        switch (process->slot_state) {
        case MICROS_KERNEL_OBJECT_SLOT_FREE:
            if (
                process->live_thread_count != 0
                || process->address_space_root != 0
                || micros_process_next_generation(
                    (uint16_t)index,
                    process->generation,
                    &unused_generation
                ) != MICROS_KERNEL_OBJECT_OK
            ) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            break;
        case MICROS_KERNEL_OBJECT_SLOT_LIVE:
            if (
                process->generation == 0
                || process->live_thread_count
                    > objects->max_threads_per_process
            ) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            ++observed_process_count;
            break;
        case MICROS_KERNEL_OBJECT_SLOT_QUARANTINED:
            if (
                process->generation == 0
                || process->live_thread_count != 0
                || process->address_space_root != 0
                || micros_process_next_generation(
                    (uint16_t)index,
                    process->generation,
                    &unused_generation
                )
                    != MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED
            ) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            break;
        default:
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
    }

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];
        uint32_t unused_generation;
        size_t other_index;

        switch (thread->slot_state) {
        case MICROS_KERNEL_OBJECT_SLOT_FREE:
            if (
                thread->owner.generation != 0
                || thread->context_attached
                || thread->kernel_stack_bottom != 0
                || thread->kernel_stack_top != 0
                || !user_context_is_zero(&thread->user_context)
                || !thread_scheduler_metadata_is_zero(thread)
                || micros_thread_next_generation(
                    thread->generation,
                    &unused_generation
                ) != MICROS_KERNEL_OBJECT_OK
            ) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            break;
        case MICROS_KERNEL_OBJECT_SLOT_LIVE:
            if (
                thread->generation == 0
                || (
                    thread->runtime_flags
                    & ~MICROS_THREAD_RTS_DEFINED_MASK
                ) != 0
                || !process_handle_is_valid(thread->owner)
                || objects->processes[thread->owner.slot].slot_state
                    != MICROS_KERNEL_OBJECT_SLOT_LIVE
                || objects->processes[thread->owner.slot].generation
                    != thread->owner.generation
            ) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            if (
                !thread->scheduler_assigned
                && (
                    thread->runtime_flags
                        != MICROS_THREAD_RTS_INACTIVE
                    || thread->scheduler_preemptible
                    || thread->scheduler_priority != 0
                    || !hart_handle_is_null(thread->scheduler_hart)
                    || thread->quantum_counter_ticks != 0
                    || thread->remaining_counter_ticks != 0
                    || thread->ready_linked
                    || !thread_handle_is_null(thread->ready_next)
                )
            ) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            if (thread->context_attached) {
                if (
                    !stack_range_is_valid(
                        thread->kernel_stack_bottom,
                        thread->kernel_stack_top,
                        MICROS_THREAD_KERNEL_STACK_SIZE
                    )
                ) {
                    return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
                }
                for (
                    other_index = 0;
                    other_index < index;
                    ++other_index
                ) {
                    const struct micros_thread *other =
                        &objects->threads[other_index];

                    if (
                        other->slot_state
                            == MICROS_KERNEL_OBJECT_SLOT_LIVE
                        && other->context_attached
                        && ranges_overlap(
                            thread->kernel_stack_bottom,
                            thread->kernel_stack_top,
                            other->kernel_stack_bottom,
                            other->kernel_stack_top
                        )
                    ) {
                        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
                    }
                }
                for (
                    other_index = 0;
                    other_index < MICROS_HART_CAPACITY;
                    ++other_index
                ) {
                    const struct micros_hart *hart =
                        &objects->harts[other_index];

                    if (
                        hart->slot_state
                            == MICROS_KERNEL_OBJECT_SLOT_LIVE
                        && hart->trap_installed
                        && (
                            ranges_overlap(
                                thread->kernel_stack_bottom,
                                thread->kernel_stack_top,
                                hart->idle_primary_stack_bottom,
                                hart->idle_primary_stack_top
                            )
                            || ranges_overlap(
                                thread->kernel_stack_bottom,
                                thread->kernel_stack_top,
                                hart->trap.emergency_stack_bottom,
                                hart->trap.emergency_stack_top
                            )
                        )
                    ) {
                        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
                    }
                }
            } else if (
                thread->kernel_stack_bottom != 0
                || thread->kernel_stack_top != 0
                || !user_context_is_zero(&thread->user_context)
            ) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            ++process_thread_counts[thread->owner.slot];
            ++observed_thread_count;
            break;
        case MICROS_KERNEL_OBJECT_SLOT_QUARANTINED:
            if (
                thread->generation == 0
                || thread->owner.generation != 0
                || thread->context_attached
                || thread->kernel_stack_bottom != 0
                || thread->kernel_stack_top != 0
                || !user_context_is_zero(&thread->user_context)
                || !thread_scheduler_metadata_is_zero(thread)
                || micros_thread_next_generation(
                    thread->generation,
                    &unused_generation
                )
                    != MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED
            ) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            break;
        default:
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
    }

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct micros_process *process =
            &objects->processes[index];

        if (
            process->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && (
                process->live_thread_count
                    != process_thread_counts[index]
                || process_thread_counts[index]
                    > objects->max_threads_per_process
            )
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
    }

    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        const struct micros_hart *hart = &objects->harts[index];
        size_t other_index;
        size_t priority;

        if (hart->slot_state == MICROS_KERNEL_OBJECT_SLOT_FREE) {
            if (
                hart->generation != 0
                || hart->hardware_id != 0
                || hart->trap_installed
                || hart->idle_primary_stack_bottom != 0
                || hart->idle_primary_stack_top != 0
                || !trap_anchor_is_zero(&hart->trap)
                || hart->current_thread.generation != 0
                || hart->accounting_owner
                    != MICROS_SCHEDULER_ACCOUNTING_NONE
                || hart->accounting_started_at != 0
                || hart->accounted_thread.generation != 0
                || hart->kernel_counter_ticks != 0
                || hart->idle_counter_ticks != 0
                || hart->interrupt_depth != 0
                || hart->preempt_disable_count != 0
                || hart->reschedule_pending
                || !timer_state_is_initial(&hart->timer)
            ) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            for (
                priority = 0;
                priority < MICROS_SCHEDULER_PRIORITY_COUNT;
                ++priority
            ) {
                if (
                    hart->ready_head[priority].generation != 0
                    || hart->ready_tail[priority].generation != 0
                ) {
                    return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
                }
            }
            continue;
        }
        if (
            hart->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || hart->generation == 0
            || hart->interrupt_depth != 0
            || hart->preempt_disable_count != 0
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        for (other_index = 0; other_index < index; ++other_index) {
            const struct micros_hart *other =
                &objects->harts[other_index];

            if (
                other->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
                && other->hardware_id == hart->hardware_id
            ) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
        }
        if (hart->trap_installed) {
            if (
                !trap_anchor_stacks_are_valid(&hart->trap)
                || !stack_range_is_valid(
                    hart->idle_primary_stack_bottom,
                    hart->idle_primary_stack_top,
                    MICROS_PRIMARY_TRAP_STACK_MIN_SIZE
                )
                || ranges_overlap(
                    hart->idle_primary_stack_bottom,
                    hart->idle_primary_stack_top,
                    hart->trap.emergency_stack_bottom,
                    hart->trap.emergency_stack_top
                )
                || hart->trap.entry_t0 != 0
                || hart->trap.entry_t1 != 0
                || hart->trap.entry_t2 != 0
            ) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            for (other_index = 0; other_index < index; ++other_index) {
                const struct micros_hart *other =
                    &objects->harts[other_index];

                if (
                    other->slot_state
                        == MICROS_KERNEL_OBJECT_SLOT_LIVE
                    && other->trap_installed
                    && (
                        ranges_overlap(
                            hart->trap.primary_stack_bottom,
                            hart->trap.primary_stack_top,
                            other->trap.primary_stack_bottom,
                            other->trap.primary_stack_top
                        )
                        || ranges_overlap(
                            hart->trap.primary_stack_bottom,
                            hart->trap.primary_stack_top,
                            other->trap.emergency_stack_bottom,
                            other->trap.emergency_stack_top
                        )
                        || ranges_overlap(
                            hart->trap.emergency_stack_bottom,
                            hart->trap.emergency_stack_top,
                            other->trap.primary_stack_bottom,
                            other->trap.primary_stack_top
                        )
                        || ranges_overlap(
                            hart->trap.emergency_stack_bottom,
                            hart->trap.emergency_stack_top,
                            other->trap.emergency_stack_bottom,
                            other->trap.emergency_stack_top
                        )
                        || ranges_overlap(
                            hart->idle_primary_stack_bottom,
                            hart->idle_primary_stack_top,
                            other->idle_primary_stack_bottom,
                            other->idle_primary_stack_top
                        )
                        || ranges_overlap(
                            hart->idle_primary_stack_bottom,
                            hart->idle_primary_stack_top,
                            other->trap.emergency_stack_bottom,
                            other->trap.emergency_stack_top
                        )
                        || ranges_overlap(
                            hart->trap.emergency_stack_bottom,
                            hart->trap.emergency_stack_top,
                            other->idle_primary_stack_bottom,
                            other->idle_primary_stack_top
                        )
                    )
                ) {
                    return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
                }
            }
        } else if (
            !trap_anchor_is_zero(&hart->trap)
            || hart->idle_primary_stack_bottom != 0
            || hart->idle_primary_stack_top != 0
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        if (!hart->timer.initialized) {
            if (!timer_state_is_initial(&hart->timer)) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
        } else if (
            hart->timer.active
            && (
                hart->timer.interval == 0
                || hart->timer.deadline == UINT64_MAX
            )
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }

        if (hart->current_thread.generation != 0) {
            const struct micros_thread *thread;
            bool primary_is_idle;
            bool primary_is_thread;

            if (
                !thread_handle_is_valid(hart->current_thread)
                || current_threads[hart->current_thread.slot]
            ) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            thread = &objects->threads[hart->current_thread.slot];
            if (
                thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
                || thread->generation
                    != hart->current_thread.generation
                || !thread->context_attached
            ) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            primary_is_idle = (
                hart->trap.primary_stack_bottom
                    == hart->idle_primary_stack_bottom
                && hart->trap.primary_stack_top
                    == hart->idle_primary_stack_top
            );
            primary_is_thread = (
                thread->context_attached
                && hart->trap.primary_stack_bottom
                    == thread->kernel_stack_bottom
                && hart->trap.primary_stack_top
                    == thread->kernel_stack_top
            );
            if (primary_is_idle || !primary_is_thread) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            current_threads[hart->current_thread.slot] = true;
        } else if (
            hart->trap_installed
            && (
                hart->trap.primary_stack_bottom
                    != hart->idle_primary_stack_bottom
                || hart->trap.primary_stack_top
                    != hart->idle_primary_stack_top
            )
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        ++observed_hart_count;
    }

    if (
        observed_process_count != objects->live_process_count
        || observed_thread_count != objects->live_thread_count
        || observed_hart_count != objects->registered_hart_count
        || observed_hart_count > objects->max_harts
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    return MICROS_KERNEL_OBJECT_OK;
}
