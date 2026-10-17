#include "kernel/tests/objects/object_model_test_internal.h"

bool kernel_object_model_test_create_process_and_thread(
    struct kernel_object_model_test_work *work
)
{
    work->error = micros_process_create(work->objects, &work->first_process);
    if (work->error != MICROS_KERNEL_OBJECT_OK) {
        return false;
    }
    work->error = micros_thread_create(
        work->objects,
        work->first_process,
        &work->thread
    );
    if (work->error != MICROS_KERNEL_OBJECT_OK) {
        return false;
    }
    work->error = micros_thread_create(
        work->objects,
        work->first_process,
        &work->unchanged_thread
    );
    if (
        work->error != MICROS_KERNEL_OBJECT_ERROR_THREAD_LIMIT
        || work->unchanged_thread.slot != UINT16_MAX
        || work->unchanged_thread.generation != UINT32_MAX
    ) {
        return false;
    }
    return true;
}

bool kernel_object_model_test_attach_and_select_thread(
    struct kernel_object_model_test_work *work,
    uintptr_t stack_bottom,
    uintptr_t stack_top,
    const struct micros_user_context *context
)
{
    work->error = micros_thread_attach_execution_context(
        work->objects,
        work->thread,
        stack_bottom,
        stack_top,
        context
    );
    if (work->error != MICROS_KERNEL_OBJECT_OK) {
        return false;
    }
    work->error = micros_thread_scheduler_admit(
        work->objects,
        work->hart_handle,
        work->thread,
        MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
        100,
        true
    );
    if (
        work->error != MICROS_KERNEL_OBJECT_OK
        || micros_hart_plan_user_return(
            work->objects,
            work->hart_handle,
            &work->plan
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_hart_commit_user_return(work->objects, &work->plan)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    work->error = micros_hart_current_thread(
        work->objects,
        work->hart_handle,
        &work->current
    );
    if (
        work->error != MICROS_KERNEL_OBJECT_OK
        || work->current.slot != work->thread.slot
        || work->current.generation != work->thread.generation
    ) {
        return false;
    }
    if (
        micros_thread_release(work->objects, work->thread)
        != MICROS_KERNEL_OBJECT_ERROR_STATE
    ) {
        return false;
    }
    return true;
}

bool kernel_object_model_test_retire_thread_and_process(
    struct kernel_object_model_test_work *work
)
{
    if (
        micros_thread_scheduler_hold(work->objects, work->thread)
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_plan_user_return(
            work->objects,
            work->hart_handle,
            &work->plan
        ) != MICROS_KERNEL_OBJECT_OK
        || work->plan.action != MICROS_SCHEDULER_RETURN_ENTER_IDLE
        || micros_hart_commit_user_return(work->objects, &work->plan)
            != MICROS_KERNEL_OBJECT_OK
        || micros_thread_scheduler_remove(work->objects, work->thread)
            != MICROS_KERNEL_OBJECT_OK
        || micros_thread_detach_execution_context(work->objects, work->thread)
            != MICROS_KERNEL_OBJECT_OK
        || micros_thread_release(work->objects, work->thread)
            != MICROS_KERNEL_OBJECT_OK
        || micros_process_release(work->objects, work->first_process)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    return true;
}

bool kernel_object_model_test_verify_process_reuse(
    struct kernel_object_model_test_work *work
)
{
    if (
        micros_process_create(work->objects, &work->replacement_process)
            != MICROS_KERNEL_OBJECT_OK
        || work->replacement_process.slot != work->first_process.slot
        || work->replacement_process.generation
            != work->first_process.generation + 1
        || micros_process_resolve(
            work->objects,
            work->first_process,
            &work->resolved_process
        ) != MICROS_KERNEL_OBJECT_ERROR_STALE
        || work->resolved_process != NULL
        || micros_process_release(work->objects, work->replacement_process)
            != MICROS_KERNEL_OBJECT_OK
        || work->objects->live_process_count != 0
        || work->objects->live_thread_count != 0
        || work->objects->registered_hart_count != 1
        || micros_kernel_objects_validate(work->objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    return true;
}
