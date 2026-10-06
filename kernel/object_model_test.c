#include "micros/kernel_object_runtime.h"

#include <stdbool.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "micros/scheduler_core.h"

bool micros_kernel_object_runtime_run_self_test(void);

bool micros_kernel_object_runtime_run_self_test(void)
{
    struct micros_kernel_objects *objects;
    struct micros_hart *hart;
    struct micros_hart_handle hart_handle;
    struct micros_process_handle first_process;
    struct micros_process_handle replacement_process;
    struct micros_thread_handle thread;
    struct micros_thread_handle unchanged_thread = {
        UINT16_MAX,
        UINT32_MAX,
    };
    struct micros_thread_handle current;
    struct micros_scheduler_return_plan plan;
    const struct micros_process *resolved_process = NULL;
    enum micros_kernel_object_error error;
    uintptr_t saved_status;
    bool passed = false;
    static unsigned char thread_stack[
        MICROS_THREAD_KERNEL_STACK_SIZE
    ] __attribute__((aligned(MICROS_TRAP_STACK_ALIGNMENT)));
    static struct micros_user_context context;

    saved_status = riscv_irq_save();
    objects = micros_kernel_object_runtime_test_registry();
    hart = micros_kernel_object_runtime_boot_hart();
    hart_handle = micros_kernel_object_runtime_boot_hart_handle();
    if (
        objects == NULL
        || hart == NULL
        || hart_handle.generation == 0
        || hart->hardware_id != 0
        || !hart->trap_installed
        || hart->trap.primary_stack_bottom == 0
        || hart->trap.primary_stack_top
            <= hart->trap.primary_stack_bottom
        || hart->trap.emergency_stack_bottom == 0
        || hart->trap.emergency_stack_top
            <= hart->trap.emergency_stack_bottom
    ) {
        goto done;
    }

    error = micros_process_create(objects, &first_process);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        goto done;
    }
    error = micros_thread_create(
        objects,
        first_process,
        &thread
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        goto done;
    }
    error = micros_thread_create(
        objects,
        first_process,
        &unchanged_thread
    );
    if (
        error != MICROS_KERNEL_OBJECT_ERROR_THREAD_LIMIT
        || unchanged_thread.slot != UINT16_MAX
        || unchanged_thread.generation != UINT32_MAX
    ) {
        goto done;
    }
    error = micros_thread_attach_execution_context(
        objects,
        thread,
        (uintptr_t)thread_stack,
        (uintptr_t)thread_stack + sizeof(thread_stack),
        &context
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        goto done;
    }
    error = micros_thread_scheduler_admit(
        objects,
        hart_handle,
        thread,
        MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
        100,
        true
    );
    if (
        error != MICROS_KERNEL_OBJECT_OK
        || micros_hart_plan_user_return(
            objects,
            hart_handle,
            &plan
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_hart_commit_user_return(objects, &plan)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        goto done;
    }
    error = micros_hart_current_thread(
        objects,
        hart_handle,
        &current
    );
    if (
        error != MICROS_KERNEL_OBJECT_OK
        || current.slot != thread.slot
        || current.generation != thread.generation
    ) {
        goto done;
    }
    if (
        micros_thread_release(objects, thread)
        != MICROS_KERNEL_OBJECT_ERROR_STATE
    ) {
        goto done;
    }
    if (
        micros_thread_scheduler_hold(objects, thread)
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_plan_user_return(
            objects,
            hart_handle,
            &plan
        ) != MICROS_KERNEL_OBJECT_OK
        || plan.action != MICROS_SCHEDULER_RETURN_ENTER_IDLE
        || micros_hart_commit_user_return(objects, &plan)
            != MICROS_KERNEL_OBJECT_OK
        || micros_thread_scheduler_remove(objects, thread)
            != MICROS_KERNEL_OBJECT_OK
        || micros_thread_detach_execution_context(objects, thread)
            != MICROS_KERNEL_OBJECT_OK
        || micros_thread_release(objects, thread)
            != MICROS_KERNEL_OBJECT_OK
        || micros_process_release(objects, first_process)
            != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(objects, &replacement_process)
            != MICROS_KERNEL_OBJECT_OK
        || replacement_process.slot != first_process.slot
        || replacement_process.generation
            != first_process.generation + 1
        || micros_process_resolve(
            objects,
            first_process,
            &resolved_process
        ) != MICROS_KERNEL_OBJECT_ERROR_STALE
        || resolved_process != NULL
        || micros_process_release(objects, replacement_process)
            != MICROS_KERNEL_OBJECT_OK
        || objects->live_process_count != 0
        || objects->live_thread_count != 0
        || objects->registered_hart_count != 1
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        goto done;
    }
    passed = true;

done:
    riscv_irq_restore(saved_status);
    return passed;
}
