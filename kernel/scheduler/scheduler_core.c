#include "kernel/scheduler/scheduler_core_private.h"
#include "kernel/objects/kernel_object_private.h"

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
    const struct micros_thread *selected;
    const struct micros_hart *hart;

    return (
        micros_thread_resolve(objects, thread, &selected) == MICROS_KERNEL_OBJECT_OK
        && selected->scheduler_assigned
        && micros_hart_resolve(objects, selected->scheduler_hart, &hart) == MICROS_KERNEL_OBJECT_OK
        && scheduler_core_thread_handles_equal(hart->current_thread, thread)
    );
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

enum micros_kernel_object_error scheduler_core_preflight_registry(
    const struct micros_kernel_objects *objects
)
{
    enum micros_kernel_object_error error = kernel_object_require_initialized(objects);

    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        objects->max_threads_per_process == 0
        || objects->max_threads_per_process > MICROS_THREAD_CAPACITY
        || objects->max_harts == 0
        || objects->max_harts > MICROS_HART_CAPACITY
        || objects->live_process_count > MICROS_PROCESS_CAPACITY
        || objects->live_thread_count > MICROS_THREAD_CAPACITY
        || objects->live_thread_count > objects->live_process_count * objects->max_threads_per_process
        || objects->registered_hart_count > objects->max_harts
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    return MICROS_KERNEL_OBJECT_OK;
}

static bool selected_thread_context_is_valid(const struct micros_thread *thread)
{
    if (thread->context_attached) {
        return kernel_object_stack_range_is_valid(
            thread->kernel_stack_bottom,
            thread->kernel_stack_top,
            MICROS_THREAD_KERNEL_STACK_SIZE
        );
    }
    return (
        thread->kernel_stack_bottom == 0
        && thread->kernel_stack_top == 0
        && kernel_object_user_context_is_zero(&thread->user_context)
    );
}

static bool selected_thread_policy_is_valid(
    const struct micros_kernel_objects *objects,
    const struct micros_thread *thread,
    struct micros_thread_handle handle
)
{
    const struct micros_hart *hart;

    if (!thread->scheduler_assigned) {
        return (
            thread->runtime_flags == MICROS_THREAD_RTS_INACTIVE
            && !thread->scheduler_preemptible
            && thread->scheduler_priority == 0
            && scheduler_core_hart_handle_is_null(thread->scheduler_hart)
            && thread->quantum_counter_ticks == 0
            && thread->remaining_counter_ticks == 0
            && !thread->ready_linked
            && scheduler_core_thread_handle_is_null(thread->ready_next)
        );
    }
    if (
        !thread->context_attached
        || thread->scheduler_priority >= MICROS_SCHEDULER_PRIORITY_COUNT
        || thread->quantum_counter_ticks == 0
        || thread->remaining_counter_ticks > thread->quantum_counter_ticks
        || thread->ready_linked != (thread->runtime_flags == 0)
        || (!thread->ready_linked && !scheduler_core_thread_handle_is_null(thread->ready_next))
        || micros_hart_resolve(objects, thread->scheduler_hart, &hart) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    return (
        (thread->runtime_flags & MICROS_THREAD_RTS_PREEMPTED) == 0
        || scheduler_core_thread_handles_equal(hart->current_thread, handle)
    );
}

enum micros_kernel_object_error scheduler_core_preflight_thread(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    const struct micros_thread **result
)
{
    const struct micros_thread *thread;
    const struct micros_process *process;
    enum micros_kernel_object_error error;

    if (result == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = scheduler_core_preflight_registry(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = micros_thread_resolve(objects, handle, &thread);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        micros_process_resolve(objects, thread->owner, &process) != MICROS_KERNEL_OBJECT_OK
        || process->live_thread_count == 0
        || process->live_thread_count > objects->max_threads_per_process
        || process->live_thread_count > objects->live_thread_count
        || (thread->runtime_flags & ~MICROS_THREAD_RTS_DEFINED_MASK) != 0
        || !selected_thread_context_is_valid(thread)
        || !selected_thread_policy_is_valid(objects, thread, handle)
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    *result = thread;
    return MICROS_KERNEL_OBJECT_OK;
}

static bool selected_hart_stacks_are_valid(const struct micros_hart *hart)
{
    if (!hart->trap_installed) {
        return (
            kernel_object_trap_anchor_is_zero(&hart->trap)
            && hart->idle_primary_stack_bottom == 0
            && hart->idle_primary_stack_top == 0
        );
    }
    return (
        kernel_object_trap_anchor_stacks_are_valid(&hart->trap)
        && kernel_object_stack_range_is_valid(
            hart->idle_primary_stack_bottom,
            hart->idle_primary_stack_top,
            MICROS_PRIMARY_TRAP_STACK_MIN_SIZE
        )
        && !kernel_object_ranges_overlap(
            hart->idle_primary_stack_bottom,
            hart->idle_primary_stack_top,
            hart->trap.emergency_stack_bottom,
            hart->trap.emergency_stack_top
        )
        && hart->trap.entry_t0 == 0
        && hart->trap.entry_t1 == 0
        && hart->trap.entry_t2 == 0
    );
}

static bool selected_hart_accounting_is_valid(
    const struct micros_hart *hart,
    const struct micros_thread *current
)
{
    if (
        hart->accounting_owner > MICROS_SCHEDULER_ACCOUNTING_IDLE
        || (
            hart->accounting_owner == MICROS_SCHEDULER_ACCOUNTING_NONE
            && (hart->accounting_started_at != 0 || hart->kernel_counter_ticks != 0 || hart->idle_counter_ticks != 0)
        )
    ) {
        return false;
    }
    if (hart->accounting_owner == MICROS_SCHEDULER_ACCOUNTING_THREAD) {
        return (
            current != NULL && current->runtime_flags == 0
            && scheduler_core_thread_handles_equal(hart->current_thread, hart->accounted_thread)
        );
    }
    return (
        scheduler_core_thread_handle_is_null(hart->accounted_thread)
        && (hart->accounting_owner != MICROS_SCHEDULER_ACCOUNTING_IDLE || current == NULL)
    );
}

enum micros_kernel_object_error scheduler_core_preflight_hart(
    const struct micros_kernel_objects *objects,
    struct micros_hart_handle handle,
    const struct micros_hart **result
)
{
    const struct micros_hart *hart;
    const struct micros_thread *current = NULL;
    enum micros_kernel_object_error error;

    if (result == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    error = scheduler_core_preflight_registry(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = micros_hart_resolve(objects, handle, &hart);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (!selected_hart_stacks_are_valid(hart)) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    if (!scheduler_core_thread_handle_is_null(hart->current_thread)) {
        error = scheduler_core_preflight_thread(objects, hart->current_thread, &current);
        if (
            error != MICROS_KERNEL_OBJECT_OK || !current->scheduler_assigned
            || !scheduler_core_hart_handles_equal(current->scheduler_hart, handle)
            || hart->trap.primary_stack_bottom != current->kernel_stack_bottom
            || hart->trap.primary_stack_top != current->kernel_stack_top
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        if (current->runtime_flags == 0
            && !scheduler_core_thread_handles_equal(
                hart->ready_head[current->scheduler_priority], hart->current_thread)) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
    } else if (
        hart->trap_installed
        && (hart->trap.primary_stack_bottom != hart->idle_primary_stack_bottom
            || hart->trap.primary_stack_top != hart->idle_primary_stack_top)
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    if (!selected_hart_accounting_is_valid(hart, current)) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    *result = hart;
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error scheduler_core_preflight_thread_operation(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle handle
)
{
    const struct micros_thread *thread;
    const struct micros_hart *hart;
    enum micros_kernel_object_error error = scheduler_core_preflight_thread(objects, handle, &thread);

    if (error != MICROS_KERNEL_OBJECT_OK || !thread->scheduler_assigned) {
        return error;
    }
    return scheduler_core_preflight_hart(objects, thread->scheduler_hart, &hart);
}

enum micros_kernel_object_error scheduler_core_preflight_queue_record(
    const struct micros_kernel_objects *objects,
    const struct micros_hart *hart,
    uint8_t priority,
    struct micros_thread_handle handle,
    const struct micros_thread **result
)
{
    const struct micros_thread *thread;
    enum micros_kernel_object_error error = scheduler_core_preflight_thread(objects, handle, &thread);

    if (
        error != MICROS_KERNEL_OBJECT_OK || !thread->scheduler_assigned
        || !scheduler_core_hart_handles_equal(
            thread->scheduler_hart,
            (struct micros_hart_handle){(uint16_t)(hart - objects->harts), hart->generation}
        )
        || thread->scheduler_priority != priority || thread->runtime_flags != 0 || !thread->ready_linked
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    *result = thread;
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error scheduler_core_preflight_queue_endpoints(
    const struct micros_kernel_objects *objects,
    const struct micros_hart *hart,
    uint8_t priority
)
{
    struct micros_thread_handle head;
    struct micros_thread_handle tail;
    const struct micros_thread *first;
    const struct micros_thread *last;
    const struct micros_thread *successor;
    enum micros_kernel_object_error error;

    if (priority >= MICROS_SCHEDULER_PRIORITY_COUNT) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    head = hart->ready_head[priority];
    tail = hart->ready_tail[priority];
    if (scheduler_core_thread_handle_is_null(head)) {
        return scheduler_core_thread_handles_equal(tail, scheduler_core_null_thread_handle())
            ? MICROS_KERNEL_OBJECT_OK : MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    error = scheduler_core_preflight_queue_record(objects, hart, priority, head, &first);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = scheduler_core_preflight_queue_record(objects, hart, priority, tail, &last);
    if (error != MICROS_KERNEL_OBJECT_OK || !scheduler_core_thread_handle_is_null(last->ready_next)) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    if (scheduler_core_thread_handles_equal(head, tail)) {
        return scheduler_core_thread_handle_is_null(first->ready_next)
            ? MICROS_KERNEL_OBJECT_OK : MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    if (
        scheduler_core_thread_handle_is_null(first->ready_next)
        || scheduler_core_thread_handles_equal(first->ready_next, head)
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    return scheduler_core_preflight_queue_record(objects, hart, priority, first->ready_next, &successor);
}
