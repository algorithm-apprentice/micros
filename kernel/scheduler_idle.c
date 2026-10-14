#include "scheduler_runtime_internal.h"

#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "micros/timer.h"

static void preflight_idle_selection(
    const struct micros_kernel_objects *objects,
    struct micros_hart *hart
)
{
    if (
        objects == NULL
        || hart == NULL
        || hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_KERNEL
    ) {
        micros_scheduler_idle_accounting_panic(hart);
    }
}

static void prepare_idle_timer_accounting(
    struct scheduler_runtime_context *runtime,
    struct micros_hart *hart,
    struct scheduler_accounting_commit *accounting
)
{
    if (
        (
            runtime->scheduler_timer_enabled
            && !micros_timer_prepare_return(hart)
        )
        || scheduler_runtime_preflight_kernel_interval(hart, accounting)
            != MICROS_SCHEDULER_OK
    ) {
        micros_scheduler_idle_accounting_panic(hart);
    }
}

static void commit_idle_accounting(
    struct micros_hart *hart,
    const struct scheduler_accounting_commit *accounting
)
{
    hart->kernel_counter_ticks = accounting->kernel_total;
    hart->accounting_started_at = accounting->counter;
    hart->accounting_owner =
        MICROS_SCHEDULER_ACCOUNTING_IDLE;
    hart->accounted_thread =
        (struct micros_thread_handle){0, 0};
}

void micros_scheduler_idle_select(struct micros_hart *hart)
{
    struct scheduler_runtime_context *runtime =
        scheduler_runtime_state();
    struct micros_kernel_objects *objects = scheduler_runtime_authoritative_objects();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    struct micros_scheduler_return_plan plan;
    struct scheduler_accounting_commit accounting;
    struct scheduler_completion_commit completion;
    enum micros_kernel_object_error error;

#ifdef MICROS_BUILD_SCHEDULER_TEST
    micros_scheduler_test_note_selector_entry();
#endif
    preflight_idle_selection(objects, hart);
    error = micros_hart_plan_user_return(
        objects,
        hart_handle,
        &plan
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        micros_scheduler_idle_accounting_panic(hart);
    }
    if (plan.action == MICROS_SCHEDULER_RETURN_ENTER_IDLE) {
        prepare_idle_timer_accounting(runtime, hart, &accounting);
        micros_scheduler_apply_return_plan(objects, &plan);
        commit_idle_accounting(hart, &accounting);
        return;
    }
    if (
        scheduler_runtime_validate_selected_thread(
            runtime,
            objects,
            plan.selected
        ) != MICROS_SCHEDULER_OK
    ) {
        micros_scheduler_idle_accounting_panic(hart);
    }
    scheduler_runtime_preflight_completion_or_panic(
        objects,
        plan.selected,
        &completion,
        hart,
        NULL
    );
    if (
        scheduler_runtime_load_selected_context(runtime, plan.selected)
            != MICROS_SCHEDULER_OK
    ) {
        micros_scheduler_idle_accounting_panic(hart);
    }
    prepare_idle_timer_accounting(runtime, hart, &accounting);
    scheduler_runtime_commit_completion_or_panic(runtime, &completion, hart, NULL);
    micros_scheduler_apply_return_plan(objects, &plan);
    scheduler_runtime_enter_selected_thread(
        runtime,
        hart,
        plan.selected,
        &accounting,
        NULL
    );
    micros_riscv_enter_user(&runtime->selected_context);
}

_Noreturn void micros_scheduler_idle_accounting_panic(
    struct micros_hart *hart
)
{
    MICROS_PANIC(
        hart == NULL ? 0 : hart->hardware_id,
        "scheduler-idle-accounting"
    );
}
