#include "kernel_object_private.h"

#include "scheduler_core_internal.h"

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

    error = kernel_object_require_initialized(objects);
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

        if (process->generation > MICROS_PROCESS_GENERATION_MAX) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        switch (process->slot_state) {
        case MICROS_KERNEL_OBJECT_SLOT_FREE:
            if (
                process->live_thread_count != 0
                || process->address_space_root != 0
                || process->primary_endpoint
                    != MICROS_PROCESS_ENDPOINT_NONE
                || process->privilege_profile != 0
                || process->endpoint_lifecycle_consumed
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
                || process->primary_endpoint
                    != MICROS_PROCESS_ENDPOINT_NONE
                || process->privilege_profile != 0
                || process->endpoint_lifecycle_consumed
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
                || !kernel_object_user_context_is_zero(&thread->user_context)
                || !kernel_object_thread_scheduler_metadata_is_zero(thread)
                || !micros_thread_ipc_state_is_clear(thread)
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
                || !kernel_object_process_handle_is_valid(thread->owner)
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
                    || !kernel_object_hart_handle_is_null(thread->scheduler_hart)
                    || thread->quantum_counter_ticks != 0
                    || thread->remaining_counter_ticks != 0
                    || thread->ready_linked
                    || !kernel_object_thread_handle_is_null(thread->ready_next)
                )
            ) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            if (thread->context_attached) {
                if (
                    !kernel_object_stack_range_is_valid(
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
                        && kernel_object_ranges_overlap(
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
                            kernel_object_ranges_overlap(
                                thread->kernel_stack_bottom,
                                thread->kernel_stack_top,
                                hart->idle_primary_stack_bottom,
                                hart->idle_primary_stack_top
                            )
                            || kernel_object_ranges_overlap(
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
                || !kernel_object_user_context_is_zero(&thread->user_context)
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
                || !kernel_object_user_context_is_zero(&thread->user_context)
                || !kernel_object_thread_scheduler_metadata_is_zero(thread)
                || !micros_thread_ipc_state_is_clear(thread)
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
                || !kernel_object_trap_anchor_is_zero(&hart->trap)
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
                || !kernel_object_timer_state_is_initial(&hart->timer)
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
                !kernel_object_trap_anchor_stacks_are_valid(&hart->trap)
                || !kernel_object_stack_range_is_valid(
                    hart->idle_primary_stack_bottom,
                    hart->idle_primary_stack_top,
                    MICROS_PRIMARY_TRAP_STACK_MIN_SIZE
                )
                || kernel_object_ranges_overlap(
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
                        kernel_object_ranges_overlap(
                            hart->trap.primary_stack_bottom,
                            hart->trap.primary_stack_top,
                            other->trap.primary_stack_bottom,
                            other->trap.primary_stack_top
                        )
                        || kernel_object_ranges_overlap(
                            hart->trap.primary_stack_bottom,
                            hart->trap.primary_stack_top,
                            other->trap.emergency_stack_bottom,
                            other->trap.emergency_stack_top
                        )
                        || kernel_object_ranges_overlap(
                            hart->trap.emergency_stack_bottom,
                            hart->trap.emergency_stack_top,
                            other->trap.primary_stack_bottom,
                            other->trap.primary_stack_top
                        )
                        || kernel_object_ranges_overlap(
                            hart->trap.emergency_stack_bottom,
                            hart->trap.emergency_stack_top,
                            other->trap.emergency_stack_bottom,
                            other->trap.emergency_stack_top
                        )
                        || kernel_object_ranges_overlap(
                            hart->idle_primary_stack_bottom,
                            hart->idle_primary_stack_top,
                            other->idle_primary_stack_bottom,
                            other->idle_primary_stack_top
                        )
                        || kernel_object_ranges_overlap(
                            hart->idle_primary_stack_bottom,
                            hart->idle_primary_stack_top,
                            other->trap.emergency_stack_bottom,
                            other->trap.emergency_stack_top
                        )
                        || kernel_object_ranges_overlap(
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
            !kernel_object_trap_anchor_is_zero(&hart->trap)
            || hart->idle_primary_stack_bottom != 0
            || hart->idle_primary_stack_top != 0
        ) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        if (!hart->timer.initialized) {
            if (!kernel_object_timer_state_is_initial(&hart->timer)) {
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
                !kernel_object_thread_handle_is_valid(hart->current_thread)
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
