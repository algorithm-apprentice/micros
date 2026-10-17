#include "kernel/tests/objects/object_model_test_internal.h"

#include <stdbool.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "micros/scheduler_core.h"

bool micros_kernel_object_runtime_run_self_test(void);

bool micros_kernel_object_runtime_run_self_test(void)
{
    struct kernel_object_model_test_work work;
    work.unchanged_thread = (struct micros_thread_handle){
        UINT16_MAX,
        UINT32_MAX,
    };
    work.resolved_process = NULL;
    uintptr_t saved_status;
    bool passed = false;
    static unsigned char thread_stack[
        MICROS_THREAD_KERNEL_STACK_SIZE
    ] __attribute__((aligned(MICROS_TRAP_STACK_ALIGNMENT)));
    static struct micros_user_context context;

    saved_status = riscv_irq_save();
    work.objects = micros_kernel_object_runtime_test_registry();
    work.hart = micros_kernel_object_runtime_boot_hart();
    work.hart_handle = micros_kernel_object_runtime_boot_hart_handle();
    if (
        work.objects == NULL
        || work.hart == NULL
        || work.hart_handle.generation == 0
        || work.hart->hardware_id != 0
        || !work.hart->trap_installed
        || work.hart->trap.primary_stack_bottom == 0
        || work.hart->trap.primary_stack_top
            <= work.hart->trap.primary_stack_bottom
        || work.hart->trap.emergency_stack_bottom == 0
        || work.hart->trap.emergency_stack_top
            <= work.hart->trap.emergency_stack_bottom
    ) {
        goto done;
    }

    if (!kernel_object_model_test_create_process_and_thread(&work)) {
        goto done;
    }
    if (!kernel_object_model_test_attach_and_select_thread(
        &work,
        (uintptr_t)thread_stack,
        (uintptr_t)thread_stack + sizeof(thread_stack),
        &context
    )) {
        goto done;
    }
    if (
        !kernel_object_model_test_retire_thread_and_process(&work)
        || !kernel_object_model_test_verify_process_reuse(&work)
    ) {
        goto done;
    }
    passed = true;

done:
    riscv_irq_restore(saved_status);
    return passed;
}
