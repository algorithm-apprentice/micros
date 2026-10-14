#include "scheduler_core_private.h"

enum micros_kernel_object_error micros_scheduler_accounting_initialize(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart_handle,
    uint64_t counter
)
{
    struct micros_hart *hart;
    enum micros_kernel_object_error error;

    error = micros_scheduler_core_validate(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = scheduler_core_resolve_hart_mutable(objects, hart_handle, &hart);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_NONE
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    hart->accounting_owner =
        MICROS_SCHEDULER_ACCOUNTING_KERNEL;
    hart->accounting_started_at = counter;
    hart->accounted_thread = scheduler_core_null_thread_handle();
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_scheduler_account_user_trap(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart_handle,
    uint64_t counter
)
{
    struct micros_hart *hart;
    struct micros_thread *thread;
    uint64_t delta;
    enum micros_kernel_object_error error;

    error = scheduler_core_resolve_hart_mutable(objects, hart_handle, &hart);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_THREAD
        || scheduler_core_thread_handle_is_null(hart->current_thread)
        || !scheduler_core_thread_handles_equal(
            hart->accounted_thread,
            hart->current_thread
        )
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    error = scheduler_core_resolve_thread_mutable(
        objects,
        hart->current_thread,
        &thread
    );
    if (
        error != MICROS_KERNEL_OBJECT_OK
        || !thread->scheduler_assigned
        || !thread->context_attached
        || !scheduler_core_hart_handles_equal(
            thread->scheduler_hart,
            hart_handle
        )
        || thread->runtime_flags != 0
        || thread->scheduler_priority
            >= MICROS_SCHEDULER_PRIORITY_COUNT
        || !thread->ready_linked
        || !scheduler_core_thread_handles_equal(
            hart->ready_head[thread->scheduler_priority],
            hart->current_thread
        )
        || thread->quantum_counter_ticks == 0
        || thread->remaining_counter_ticks
            > thread->quantum_counter_ticks
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    delta = counter - hart->accounting_started_at;
    thread->remaining_counter_ticks =
        delta >= thread->remaining_counter_ticks
            ? 0
            : thread->remaining_counter_ticks - delta;
    hart->accounting_owner =
        MICROS_SCHEDULER_ACCOUNTING_KERNEL;
    hart->accounting_started_at = counter;
    hart->accounted_thread = scheduler_core_null_thread_handle();
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_scheduler_account_idle_trap(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart_handle,
    uint64_t counter
)
{
    struct micros_hart *hart;
    uint64_t delta;
    enum micros_kernel_object_error error;

    error = scheduler_core_resolve_hart_mutable(objects, hart_handle, &hart);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_IDLE
        || !scheduler_core_thread_handle_is_null(hart->current_thread)
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    if (
        !scheduler_core_thread_handle_is_null(hart->accounted_thread)
        || hart->trap.primary_stack_bottom
            != hart->idle_primary_stack_bottom
        || hart->trap.primary_stack_top
            != hart->idle_primary_stack_top
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    delta = counter - hart->accounting_started_at;
    if (scheduler_core_add_overflows(hart->idle_counter_ticks, delta)) {
        return MICROS_KERNEL_OBJECT_ERROR_OVERFLOW;
    }
    hart->idle_counter_ticks += delta;
    hart->accounting_owner =
        MICROS_SCHEDULER_ACCOUNTING_KERNEL;
    hart->accounting_started_at = counter;
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_scheduler_account_enter_thread(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart_handle,
    struct micros_thread_handle thread_handle,
    uint64_t counter
)
{
    struct micros_hart *hart;
    const struct micros_thread *thread;
    uint64_t delta;
    enum micros_kernel_object_error error;

    error = micros_scheduler_core_validate(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = scheduler_core_resolve_hart_mutable(objects, hart_handle, &hart);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_KERNEL
        || !scheduler_core_thread_handles_equal(
            hart->current_thread,
            thread_handle
        )
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    error = micros_thread_resolve(
        objects,
        thread_handle,
        &thread
    );
    if (
        error != MICROS_KERNEL_OBJECT_OK
        || thread->runtime_flags != 0
        || thread->scheduler_priority
            >= MICROS_SCHEDULER_PRIORITY_COUNT
        || !thread->ready_linked
        || !scheduler_core_thread_handles_equal(
            hart->ready_head[thread->scheduler_priority],
            thread_handle
        )
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    delta = counter - hart->accounting_started_at;
    if (scheduler_core_add_overflows(hart->kernel_counter_ticks, delta)) {
        return MICROS_KERNEL_OBJECT_ERROR_OVERFLOW;
    }
    hart->kernel_counter_ticks += delta;
    hart->accounting_owner =
        MICROS_SCHEDULER_ACCOUNTING_THREAD;
    hart->accounting_started_at = counter;
    hart->accounted_thread = thread_handle;
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_scheduler_account_enter_idle(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart_handle,
    uint64_t counter
)
{
    struct micros_hart *hart;
    uint64_t delta;
    enum micros_kernel_object_error error;

    error = micros_scheduler_core_validate(objects);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = scheduler_core_resolve_hart_mutable(objects, hart_handle, &hart);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (
        hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_KERNEL
        || !scheduler_core_thread_handle_is_null(hart->current_thread)
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STATE;
    }
    delta = counter - hart->accounting_started_at;
    if (scheduler_core_add_overflows(hart->kernel_counter_ticks, delta)) {
        return MICROS_KERNEL_OBJECT_ERROR_OVERFLOW;
    }
    hart->kernel_counter_ticks += delta;
    hart->accounting_owner =
        MICROS_SCHEDULER_ACCOUNTING_IDLE;
    hart->accounting_started_at = counter;
    return MICROS_KERNEL_OBJECT_OK;
}
