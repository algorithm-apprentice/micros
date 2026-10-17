#include "kernel/scheduler/scheduler_runtime_internal.h"

#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/boot/bootstrap_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/timer.h"

enum micros_scheduler_error micros_scheduler_user_trap_enter(
    struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    struct scheduler_runtime_context *runtime =
        scheduler_runtime_state();
    struct micros_kernel_objects *objects = scheduler_runtime_authoritative_objects();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    enum micros_kernel_object_error error;

    if (
        !runtime->scheduler_initialized
        || objects == NULL
        || hart == NULL
        || frame == NULL
    ) {
        return MICROS_SCHEDULER_ERROR_NOT_INITIALIZED;
    }
    if (
        (frame->sstatus & MICROS_RISCV_SSTATUS_SPP) != 0
        || hart != &objects->harts[hart_handle.slot]
    ) {
        return MICROS_SCHEDULER_ERROR_STATE;
    }
    error = micros_scheduler_account_user_trap(
        objects,
        hart_handle,
        riscv_read_time()
    );
    return scheduler_runtime_map_object_error(error);
}

static enum micros_scheduler_error handle_timer_result(
    enum micros_timer_interrupt_result result
)
{
    switch (result) {
    case MICROS_TIMER_INTERRUPT_HANDLED:
    case MICROS_TIMER_INTERRUPT_HANDLED_SPURIOUS:
        return MICROS_SCHEDULER_OK;
    case MICROS_TIMER_INTERRUPT_INACTIVE:
        return MICROS_SCHEDULER_ERROR_TIMER_INACTIVE;
    case MICROS_TIMER_INTERRUPT_TICK_OVERFLOW:
        return MICROS_SCHEDULER_ERROR_TIMER_TICK_OVERFLOW;
    case MICROS_TIMER_INTERRUPT_REARM_FAILED:
        return MICROS_SCHEDULER_ERROR_TIMER_REARM_FAILED;
    }
    return MICROS_SCHEDULER_ERROR_INVARIANT;
}

enum micros_scheduler_error micros_scheduler_handle_user_timer(
    struct micros_hart *hart
)
{
    struct scheduler_runtime_context *runtime =
        scheduler_runtime_state();
    enum micros_scheduler_error error;

    if (!runtime->scheduler_initialized || hart == NULL) {
        return MICROS_SCHEDULER_ERROR_NOT_INITIALIZED;
    }
    error = handle_timer_result(
        micros_timer_handle_interrupt(hart)
    );
    if (error == MICROS_SCHEDULER_OK) {
        micros_bootstrap_runtime_check_deadline(riscv_read_time());
    }
    return error;
}

enum micros_scheduler_error
micros_scheduler_enter_supervisor_interrupt(
    struct micros_hart *hart
)
{
    struct scheduler_runtime_context *runtime =
        scheduler_runtime_state();
    struct micros_kernel_objects *objects = scheduler_runtime_authoritative_objects();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    enum micros_kernel_object_error error;

    if (!runtime->scheduler_initialized || objects == NULL || hart == NULL) {
        return MICROS_SCHEDULER_ERROR_NOT_INITIALIZED;
    }
    if (
        hart->accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_IDLE
    ) {
        error = micros_scheduler_account_idle_trap(
            objects,
            hart_handle,
            riscv_read_time()
        );
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return scheduler_runtime_map_object_error(error);
        }
    } else if (
        hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_KERNEL
    ) {
        return MICROS_SCHEDULER_ERROR_INVARIANT;
    }
    return MICROS_SCHEDULER_OK;
}

enum micros_scheduler_error micros_scheduler_handle_supervisor_timer(
    struct micros_hart *hart
)
{
    enum micros_timer_interrupt_result timer_result;
    enum micros_scheduler_error scheduler_error;

    scheduler_error =
        micros_scheduler_enter_supervisor_interrupt(hart);
    if (scheduler_error != MICROS_SCHEDULER_OK) {
        return scheduler_error;
    }
    timer_result = micros_timer_handle_interrupt(hart);
    if (
        timer_result == MICROS_TIMER_INTERRUPT_HANDLED
        || timer_result == MICROS_TIMER_INTERRUPT_HANDLED_SPURIOUS
    ) {
        micros_bootstrap_runtime_check_deadline(riscv_read_time());
    }
    if (timer_result == MICROS_TIMER_INTERRUPT_HANDLED) {
        hart->reschedule_pending = true;
#ifdef MICROS_BUILD_SCHEDULER_TEST
        if (
            hart->current_thread.generation == 0
            && !micros_scheduler_test_handle_idle_timer(hart)
        ) {
            return MICROS_SCHEDULER_ERROR_INVARIANT;
        }
#endif
    }
    return handle_timer_result(timer_result);
}
