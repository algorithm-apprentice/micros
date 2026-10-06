#include "micros/kernel_object_runtime.h"

#include <stdbool.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"

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
    const struct micros_process *resolved_process = NULL;
    enum micros_kernel_object_error error;
    uintptr_t saved_status;
    bool passed = false;

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
    error = micros_hart_bind_thread(
        objects,
        hart_handle,
        thread
    );
    if (
        error != MICROS_KERNEL_OBJECT_OK
        || objects->threads[thread.slot].state
            != MICROS_THREAD_STATE_RUNNING
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
        micros_hart_clear_thread(objects, hart_handle, thread)
            != MICROS_KERNEL_OBJECT_OK
        || objects->threads[thread.slot].state
            != MICROS_THREAD_STATE_INACTIVE
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
