#include "kernel_objects_test_internal.h"

bool kernel_object_test_lifecycle_model_matches_registry(
    const struct micros_kernel_objects *fixture,
    const struct lifecycle_model *model,
    size_t step
)
{
    size_t index;

    if (
        fixture->live_process_count != model->process_count
        || fixture->live_thread_count != model->thread_count
        || fixture->registered_hart_count != 2
        || fixture->max_threads_per_process != 3
        || fixture->max_harts != 2
    ) {
        goto mismatch;
    }

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct lifecycle_model_process *expected =
            &model->processes[index];
        const struct micros_process *actual = &fixture->processes[index];

        if (
            actual->slot_state
                != (
                    expected->live
                        ? MICROS_KERNEL_OBJECT_SLOT_LIVE
                        : MICROS_KERNEL_OBJECT_SLOT_FREE
                )
            || actual->generation != expected->generation
            || actual->live_thread_count != expected->thread_count
            || actual->address_space_root
                != expected->address_space_root
            || actual->primary_endpoint
                != MICROS_PROCESS_ENDPOINT_NONE
            || actual->privilege_profile != 0
        ) {
            goto mismatch;
        }
    }

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct lifecycle_model_thread *expected =
            &model->threads[index];
        const struct micros_thread *actual = &fixture->threads[index];

        if (
            actual->slot_state
                != (
                    expected->live
                        ? MICROS_KERNEL_OBJECT_SLOT_LIVE
                        : MICROS_KERNEL_OBJECT_SLOT_FREE
                )
            || actual->generation != expected->generation
        ) {
            goto mismatch;
        }
        if (
            expected->live
            && (
                !kernel_object_test_process_handles_equal(actual->owner, expected->owner)
                || actual->runtime_flags
                    != MICROS_THREAD_RTS_INACTIVE
                || actual->scheduler_assigned
                || actual->context_attached
                    != expected->context_attached
                || actual->kernel_stack_bottom
                    != expected->kernel_stack_bottom
                || actual->kernel_stack_top
                    != expected->kernel_stack_top
                || !kernel_object_test_contexts_equal(
                    &actual->user_context,
                    &expected->context
                )
            )
        ) {
            goto mismatch;
        }
        if (
            !expected->live
            && (
                actual->owner.generation != 0
                || actual->runtime_flags != 0
                || actual->scheduler_assigned
                || actual->context_attached
                || actual->kernel_stack_bottom != 0
                || actual->kernel_stack_top != 0
                || !kernel_object_test_contexts_equal(
                    &actual->user_context,
                    &(struct micros_user_context){0}
                )
            )
        ) {
            goto mismatch;
        }
    }

    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        const struct micros_hart *actual = &fixture->harts[index];

        if (index < 2) {
            if (
                actual->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
                || actual->generation != 1
                || actual->hardware_id != index
                || actual->trap_installed
                || actual->idle_primary_stack_bottom != 0
                || actual->idle_primary_stack_top != 0
                || actual->timer.initialized
                || actual->timer.active
                || actual->timer.ticks != 0
                || actual->current_thread.generation != 0
            ) {
                goto mismatch;
            }
        } else if (
            actual->slot_state != MICROS_KERNEL_OBJECT_SLOT_FREE
            || actual->generation != 0
        ) {
            goto mismatch;
        }
    }

    if (
        micros_kernel_objects_validate(fixture)
        != MICROS_KERNEL_OBJECT_OK
    ) {
        goto mismatch;
    }
    return true;

mismatch:
    fprintf(
        stderr,
        "seeded model seed=0x%08x step=%zu registry mismatch\n",
        UINT32_C(0x7a31d4c9),
        step
    );
    return false;
}
