#include "scheduler_runtime_internal.h"

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "micros/timer.h"
#include "micros/user_address_space.h"

#if defined ( MICROS_BUILD_USER_EXECUTION_TEST ) || defined ( MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST ) || defined ( MICROS_BUILD_IPC_ECALL_CORE_TEST ) || defined ( MICROS_BUILD_GRANT_SYSCALL_TEST )
_Noreturn void micros_scheduler_test_enter_without_timer(
    struct micros_thread_handle thread
)
{
    struct scheduler_runtime_context *runtime =
        scheduler_runtime_state();
    struct micros_kernel_objects *objects = scheduler_runtime_authoritative_objects();
    struct micros_hart *hart =
        micros_kernel_object_runtime_boot_hart();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    struct micros_scheduler_return_plan plan;
    struct scheduler_accounting_commit accounting;
    uintptr_t saved_status = riscv_irq_save();

    if (
        runtime->scheduler_initialized
        || objects == NULL
        || hart == NULL
        || micros_scheduler_accounting_initialize(
            objects,
            hart_handle,
            riscv_read_time()
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        MICROS_PANIC(
            hart == NULL ? 0 : hart->hardware_id,
            "scheduler-test-init"
        );
    }
    runtime->scheduler_initialized = true;
    if (
        micros_thread_scheduler_admit(
            objects,
            hart_handle,
            thread,
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            UINT64_MAX,
            true
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_hart_plan_user_return(
            objects,
            hart_handle,
            &plan
        ) != MICROS_KERNEL_OBJECT_OK
        || scheduler_runtime_validate_selected_thread(
            runtime,
            objects,
            plan.selected
        ) != MICROS_SCHEDULER_OK
        || scheduler_runtime_load_selected_context(runtime, plan.selected)
            != MICROS_SCHEDULER_OK
        || scheduler_runtime_preflight_kernel_interval(hart, &accounting)
            != MICROS_SCHEDULER_OK
    ) {
        MICROS_PANIC(hart->hardware_id, "scheduler-test-entry");
    }
    micros_scheduler_apply_return_plan(objects, &plan);
    scheduler_runtime_enter_selected_thread(
        runtime,
        hart,
        plan.selected,
        &accounting,
        NULL
    );
    (void)saved_status;
    micros_riscv_enter_user(&runtime->selected_context);
}
#endif


#if defined ( MICROS_BUILD_USER_EXECUTION_TEST ) || defined ( MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST ) || defined ( MICROS_BUILD_SCHEDULER_TEST ) || defined ( MICROS_BUILD_IPC_ECALL_CORE_TEST ) || defined ( MICROS_BUILD_IPC_SYSCALL_TEST ) || defined ( MICROS_BUILD_GRANT_SYSCALL_TEST ) || defined ( MICROS_BUILD_USER_RUNTIME_TEST )
enum micros_scheduler_error
micros_scheduler_test_prepare_supervisor_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct scheduler_runtime_context *runtime =
        scheduler_runtime_state();
    struct micros_kernel_objects *objects = scheduler_runtime_authoritative_objects();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    struct micros_scheduler_return_plan plan;
    struct micros_thread_handle current;

    if (
        objects == NULL
        || hart == NULL
        || frame == NULL
        || hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_KERNEL
        || micros_hart_current_thread(
            objects,
            hart_handle,
            &current
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return MICROS_SCHEDULER_ERROR_STATE;
    }
    if (
        runtime->scheduler_timer_enabled
        && hart->timer.active
        && !micros_timer_stop(hart)
    ) {
        return MICROS_SCHEDULER_ERROR_TIMER;
    }
    if (
        micros_thread_scheduler_hold(objects, current)
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_plan_user_return(
            objects,
            hart_handle,
            &plan
        ) != MICROS_KERNEL_OBJECT_OK
        || plan.action != MICROS_SCHEDULER_RETURN_ENTER_IDLE
        || micros_hart_commit_user_return(objects, &plan)
            != MICROS_KERNEL_OBJECT_OK
        || micros_user_address_space_activate_kernel()
            != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        return MICROS_SCHEDULER_ERROR_INVARIANT;
    }
    return MICROS_SCHEDULER_OK;
}
#endif
