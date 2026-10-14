#include "scheduler_runtime_internal.h"

#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "micros/timer.h"
#include "micros/user_execution.h"

static _Noreturn void panic_invalid_context(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool outgoing
)
{
#if defined(MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST)
    if (!micros_scheduler_invalid_test_report(
        hart,
        frame,
        outgoing
    )) {
        MICROS_TRAP_PANIC(
            hart == NULL ? 0 : hart->hardware_id,
            "scheduler-invalid-context-diagnostic",
            frame
        );
    }
#else
    (void)outgoing;
#endif
    MICROS_TRAP_PANIC(
        hart == NULL ? 0 : hart->hardware_id,
        "invalid-bootstrap-user-context",
        frame
    );
}

static enum micros_scheduler_error capture_outgoing_custody(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart_handle,
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool store_outgoing
)
{
    struct micros_user_context outgoing;
    struct micros_thread_handle current = {0, 0};

    if (store_outgoing) {
        if (
            micros_hart_current_thread(
                objects,
                hart_handle,
                &current
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return MICROS_SCHEDULER_ERROR_STATE;
        }
        scheduler_runtime_copy_user_context(
            &outgoing,
            (const struct micros_user_context *)frame
        );
        if (
            micros_user_execution_validate_context(current, &outgoing)
                != MICROS_USER_EXECUTION_OK
        ) {
            panic_invalid_context(hart, frame, true);
        }
        if (
            micros_user_execution_store_context(current, &outgoing)
                != MICROS_USER_EXECUTION_OK
        ) {
            scheduler_runtime_panic_scheduler_invariant(
                hart,
                "scheduler-outgoing-store",
                frame
            );
        }
    } else if (
        hart->current_thread.slot != 0
        || hart->current_thread.generation != 0
    ) {
        return MICROS_SCHEDULER_ERROR_STATE;
    }
    return MICROS_SCHEDULER_OK;
}

static enum micros_scheduler_error prepare_timer_accounting(
    struct scheduler_runtime_context *runtime,
    struct micros_hart *hart,
    struct scheduler_accounting_commit *accounting
)
{
    enum micros_scheduler_error scheduler_error;

    if (
        runtime->scheduler_timer_enabled
        && !micros_timer_prepare_return(hart)
    ) {
        return MICROS_SCHEDULER_ERROR_TIMER_REARM_FAILED;
    }
    scheduler_error = scheduler_runtime_preflight_kernel_interval(hart, accounting);
    if (scheduler_error != MICROS_SCHEDULER_OK) {
        return scheduler_error;
    }
    return MICROS_SCHEDULER_OK;
}

static _Noreturn void publish_idle_return(
    struct scheduler_runtime_context *runtime,
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct scheduler_accounting_commit *accounting
)
{
    scheduler_runtime_activate_prevalidated_root(
        hart,
        runtime->scheduler_kernel_root,
        frame
    );
    hart->kernel_counter_ticks = accounting->kernel_total;
    hart->accounting_started_at = accounting->counter;
    hart->accounting_owner =
        MICROS_SCHEDULER_ACCOUNTING_IDLE;
    hart->accounted_thread =
        (struct micros_thread_handle){0, 0};
    micros_scheduler_idle_pivot(hart);
}

static enum micros_scheduler_error select_user_return(
    struct scheduler_runtime_context *runtime,
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool store_outgoing
)
{
    struct micros_kernel_objects *objects =
        scheduler_runtime_authoritative_objects();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    struct micros_scheduler_return_plan plan;
    struct scheduler_accounting_commit accounting;
    struct scheduler_completion_commit completion;
    enum micros_kernel_object_error error;
    enum micros_scheduler_error scheduler_error;

    if (
        !runtime->scheduler_initialized
        || objects == NULL
        || hart == NULL
        || frame == NULL
        || hart != &objects->harts[hart_handle.slot]
    ) {
        return MICROS_SCHEDULER_ERROR_STATE;
    }
    scheduler_error = capture_outgoing_custody(
        objects, hart_handle, hart, frame, store_outgoing
    );
    if (scheduler_error != MICROS_SCHEDULER_OK) {
        return scheduler_error;
    }
    error = micros_hart_plan_user_return(
        objects,
        hart_handle,
        &plan
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return scheduler_runtime_map_object_error(error);
    }
    if (plan.action == MICROS_SCHEDULER_RETURN_ENTER_IDLE) {
        scheduler_error = prepare_timer_accounting(runtime, hart, &accounting);
        if (scheduler_error != MICROS_SCHEDULER_OK) {
            return scheduler_error;
        }
        micros_scheduler_apply_return_plan(objects, &plan);
        publish_idle_return(runtime, hart, frame, &accounting);
    }

    scheduler_error = scheduler_runtime_validate_selected_thread(
        runtime,
        objects,
        plan.selected
    );
    if (scheduler_error != MICROS_SCHEDULER_OK) {
        panic_invalid_context(hart, frame, false);
    }
    scheduler_runtime_preflight_completion_or_panic(
        objects,
        plan.selected,
        &completion,
        hart,
        frame
    );
    scheduler_error = scheduler_runtime_load_selected_context(runtime, plan.selected);
    if (scheduler_error != MICROS_SCHEDULER_OK) {
        panic_invalid_context(hart, frame, false);
    }
    scheduler_error = prepare_timer_accounting(runtime, hart, &accounting);
    if (scheduler_error != MICROS_SCHEDULER_OK) {
        return scheduler_error;
    }
    scheduler_runtime_commit_completion_or_panic(runtime, &completion, hart, frame);
    micros_scheduler_apply_return_plan(objects, &plan);
    scheduler_runtime_enter_selected_thread(
        runtime,
        hart,
        plan.selected,
        &accounting,
        frame
    );
    return MICROS_SCHEDULER_OK;
}

enum micros_scheduler_error micros_scheduler_select_user_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct scheduler_runtime_context *runtime =
        scheduler_runtime_state();

    return select_user_return(runtime, hart, frame, true);
}

enum micros_scheduler_error
micros_scheduler_select_captured_user_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct scheduler_runtime_context *runtime =
        scheduler_runtime_state();

    return select_user_return(runtime, hart, frame, false);
}
