#include "kernel/scheduler/scheduler_runtime_internal.h"

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "micros/kernel_address_space.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "micros/timer.h"
#include "micros/user_execution.h"

enum micros_scheduler_error micros_scheduler_initialize(
    uint64_t preemption_interval
)
{
    struct scheduler_runtime_context *runtime =
        scheduler_runtime_state();
    struct micros_kernel_objects *objects = scheduler_runtime_authoritative_objects();
    struct micros_hart *hart =
        micros_kernel_object_runtime_boot_hart();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    const struct micros_kernel_address_space_report *kernel_report =
        micros_kernel_address_space_report();
    uintptr_t saved_status = riscv_irq_save();
    enum micros_kernel_object_error error;

    if (
        preemption_interval == 0
        || runtime->scheduler_initialized
        || objects == NULL
        || hart == NULL
        || kernel_report == NULL
        || kernel_report->root_physical_address == 0
        || hart->current_thread.generation != 0
    ) {
        riscv_irq_restore(saved_status);
        return preemption_interval == 0
            ? MICROS_SCHEDULER_ERROR_ARGUMENT
            : MICROS_SCHEDULER_ERROR_STATE;
    }
    if (!micros_timer_initialize(hart)) {
        riscv_irq_restore(saved_status);
        return MICROS_SCHEDULER_ERROR_TIMER;
    }
    error = micros_scheduler_accounting_initialize(
        objects,
        hart_handle,
        riscv_read_time()
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        riscv_irq_restore(saved_status);
        return scheduler_runtime_map_object_error(error);
    }
    runtime->scheduler_preemption_interval = preemption_interval;
    runtime->scheduler_kernel_root = kernel_report->root_physical_address;
    runtime->scheduler_timer_enabled = true;
    runtime->scheduler_initialized = true;
    riscv_irq_restore(saved_status);
    return MICROS_SCHEDULER_OK;
}

bool micros_scheduler_is_initialized(void)
{
    struct scheduler_runtime_context *runtime =
        scheduler_runtime_state();

    return runtime->scheduler_initialized;
}

enum micros_scheduler_error micros_scheduler_admit(
    struct micros_thread_handle thread,
    uint8_t priority,
    uint64_t quantum_counter_ticks
)
{
    struct scheduler_runtime_context *runtime =
        scheduler_runtime_state();
    struct micros_kernel_objects *objects = scheduler_runtime_authoritative_objects();
    struct micros_hart_handle hart =
        micros_kernel_object_runtime_boot_hart_handle();
    uintptr_t saved_status = riscv_irq_save();
    enum micros_kernel_object_error error;

    if (!runtime->scheduler_initialized || objects == NULL) {
        riscv_irq_restore(saved_status);
        return MICROS_SCHEDULER_ERROR_NOT_INITIALIZED;
    }
    if (
        micros_user_execution_inspect(
            thread,
            &runtime->selected_context
        ) != MICROS_USER_EXECUTION_OK
        || micros_user_execution_validate_context(
            thread,
            &runtime->selected_context
        ) != MICROS_USER_EXECUTION_OK
    ) {
        riscv_irq_restore(saved_status);
        return MICROS_SCHEDULER_ERROR_CONTEXT;
    }
    error = micros_thread_scheduler_admit(
        objects,
        hart,
        thread,
        priority,
        quantum_counter_ticks,
        true
    );
    riscv_irq_restore(saved_status);
    return scheduler_runtime_map_object_error(error);
}

enum micros_scheduler_error
micros_scheduler_prepare_start_timer(void)
{
    struct scheduler_runtime_context *runtime =
        scheduler_runtime_state();
    struct micros_hart *hart =
        micros_kernel_object_runtime_boot_hart();
    uintptr_t saved_status = riscv_irq_save();

    if (
        !runtime->scheduler_initialized
        || hart == NULL
        || !runtime->scheduler_timer_enabled
        || runtime->scheduler_start_timer_prepared
        || hart->current_thread.generation != 0
    ) {
        riscv_irq_restore(saved_status);
        return MICROS_SCHEDULER_ERROR_STATE;
    }
    if (
        !micros_timer_start(
            hart,
            runtime->scheduler_preemption_interval
        )
    ) {
        riscv_irq_restore(saved_status);
        return MICROS_SCHEDULER_ERROR_TIMER;
    }
    runtime->scheduler_start_timer_prepared = true;
    riscv_irq_restore(saved_status);
    return MICROS_SCHEDULER_OK;
}

static enum micros_scheduler_error preflight_start_context_or_restore(
    struct scheduler_runtime_context *runtime,
    struct micros_kernel_objects *objects,
    struct micros_hart *hart,
    uintptr_t saved_status
)
{
    if (!runtime->scheduler_initialized || objects == NULL || hart == NULL) {
        riscv_irq_restore(saved_status);
        return MICROS_SCHEDULER_ERROR_NOT_INITIALIZED;
    }
    if (hart->current_thread.generation != 0) {
        riscv_irq_restore(saved_status);
        return MICROS_SCHEDULER_ERROR_STATE;
    }
    return MICROS_SCHEDULER_OK;
}

static enum micros_scheduler_error prepare_start_timer_or_restore(
    struct scheduler_runtime_context *runtime,
    struct micros_hart *hart,
    uintptr_t saved_status
)
{
    if (runtime->scheduler_timer_enabled) {
        if (
            runtime->scheduler_start_timer_prepared
            && !micros_timer_prepare_return(hart)
        ) {
            riscv_irq_restore(saved_status);
            return MICROS_SCHEDULER_ERROR_TIMER_REARM_FAILED;
        }
        if (
            !runtime->scheduler_start_timer_prepared
            && !micros_timer_start(
                hart,
                runtime->scheduler_preemption_interval
            )
        ) {
            riscv_irq_restore(saved_status);
            return MICROS_SCHEDULER_ERROR_TIMER;
        }
    }
    return MICROS_SCHEDULER_OK;
}

static void preflight_start_accounting_or_panic(
    struct micros_hart *hart,
    struct scheduler_accounting_commit *accounting
)
{
    enum micros_scheduler_error scheduler_error;

    scheduler_error = scheduler_runtime_preflight_kernel_interval(hart, accounting);
    if (scheduler_error != MICROS_SCHEDULER_OK) {
        MICROS_PANIC(
            hart->hardware_id,
            "scheduler-start-accounting"
        );
    }
}

enum micros_scheduler_error micros_scheduler_start(void)
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
    struct scheduler_completion_commit completion;
    uintptr_t saved_status = riscv_irq_save();
    enum micros_kernel_object_error error;
    enum micros_scheduler_error scheduler_error;

    scheduler_error = preflight_start_context_or_restore(
        runtime, objects, hart, saved_status
    );
    if (scheduler_error != MICROS_SCHEDULER_OK) {
        return scheduler_error;
    }
    error = micros_hart_plan_user_return(
        objects,
        hart_handle,
        &plan
    );
    if (
        error != MICROS_KERNEL_OBJECT_OK
        || plan.action == MICROS_SCHEDULER_RETURN_ENTER_IDLE
    ) {
        riscv_irq_restore(saved_status);
        return error == MICROS_KERNEL_OBJECT_OK
            ? MICROS_SCHEDULER_ERROR_EMPTY
            : scheduler_runtime_map_object_error(error);
    }
    scheduler_error = scheduler_runtime_validate_selected_thread(
        runtime,
        objects,
        plan.selected
    );
    if (scheduler_error != MICROS_SCHEDULER_OK) {
        riscv_irq_restore(saved_status);
        return scheduler_error;
    }
    scheduler_runtime_preflight_completion_or_panic(
        objects,
        plan.selected,
        &completion,
        hart,
        NULL
    );
    scheduler_error = scheduler_runtime_load_selected_context(runtime, plan.selected);
    if (scheduler_error != MICROS_SCHEDULER_OK) {
        riscv_irq_restore(saved_status);
        return scheduler_error;
    }
    scheduler_error = scheduler_runtime_preflight_kernel_interval(hart, &accounting);
    if (scheduler_error != MICROS_SCHEDULER_OK) {
        riscv_irq_restore(saved_status);
        return scheduler_error;
    }
    scheduler_error = prepare_start_timer_or_restore(runtime, hart, saved_status);
    if (scheduler_error != MICROS_SCHEDULER_OK) {
        return scheduler_error;
    }
    preflight_start_accounting_or_panic(hart, &accounting);
    scheduler_runtime_commit_completion_or_panic(runtime, &completion, hart, NULL);
    micros_scheduler_apply_return_plan(objects, &plan);
    scheduler_runtime_enter_selected_thread(
        runtime,
        hart,
        plan.selected,
        &accounting,
        NULL
    );
#ifdef MICROS_BUILD_SCHEDULER_TEST
    if (!micros_scheduler_test_after_start(hart)) {
        MICROS_PANIC(
            hart->hardware_id,
            "scheduler-start-test"
        );
    }
#endif
    (void)saved_status;
    micros_riscv_enter_user(&runtime->selected_context);
}
