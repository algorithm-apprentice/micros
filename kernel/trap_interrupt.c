#include "kernel/trap_internal.h"

#include "kernel/tty_interrupt.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "micros/scheduler.h"
#include "micros/timer.h"

static void trap_handle_scheduler_timer_error(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_scheduler_error error
)
{
    switch (error) {
    case MICROS_SCHEDULER_OK:
        return;
    case MICROS_SCHEDULER_ERROR_TIMER_INACTIVE:
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "unexpected-timer",
            frame
        );
    case MICROS_SCHEDULER_ERROR_TIMER_TICK_OVERFLOW:
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "timer-tick-overflow",
            frame
        );
    case MICROS_SCHEDULER_ERROR_TIMER_REARM_FAILED:
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "timer-rearm-failed",
            frame
        );
    default:
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "scheduler-timer",
            frame
        );
    }
}

void trap_handle_user_external(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    (void)micros_tty_interrupt_dispatch(hart, frame);
    if (
        micros_scheduler_select_user_return(hart, frame)
            != MICROS_SCHEDULER_OK
    ) {
        trap_record_tty_owner_failure();
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "scheduler-external-return",
            frame
        );
    }
}

void trap_handle_user_timer(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    trap_test_hooks_before_user_timer(hart, frame);
    trap_handle_scheduler_timer_error(
        hart,
        frame,
        micros_scheduler_handle_user_timer(hart)
    );
    trap_test_hooks_after_user_timer(hart, frame);
}

bool trap_handle_supervisor_interrupt(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_trap_interrupt_route interrupt_route
)
{
    if ((frame->scause & MICROS_SCAUSE_INTERRUPT) != 0) {
        if (
            interrupt_route
                == MICROS_TRAP_INTERRUPT_SUPERVISOR_EXTERNAL
        ) {
            if (
                micros_scheduler_enter_supervisor_interrupt(hart)
                    != MICROS_SCHEDULER_OK
            ) {
                trap_record_tty_owner_failure();
                MICROS_TRAP_PANIC(
                    hart->hardware_id,
                    "scheduler-external-entry",
                    frame
                );
            }
            (void)micros_tty_interrupt_dispatch(hart, frame);
            return true;
        }
        if (
            interrupt_route
                == MICROS_TRAP_INTERRUPT_SUPERVISOR_TIMER
        ) {
            if (micros_scheduler_is_initialized()) {
                trap_handle_scheduler_timer_error(
                    hart,
                    frame,
                    micros_scheduler_handle_supervisor_timer(hart)
                );
                return true;
            }
            enum micros_timer_interrupt_result result =
                micros_timer_handle_interrupt(hart);

            switch (result) {
            case MICROS_TIMER_INTERRUPT_HANDLED:
            case MICROS_TIMER_INTERRUPT_HANDLED_SPURIOUS:
                return true;
            case MICROS_TIMER_INTERRUPT_INACTIVE:
                MICROS_TRAP_PANIC(
                    hart->hardware_id,
                    "unexpected-timer",
                    frame
                );
            case MICROS_TIMER_INTERRUPT_TICK_OVERFLOW:
                MICROS_TRAP_PANIC(
                    hart->hardware_id,
                    "timer-tick-overflow",
                    frame
                );
            case MICROS_TIMER_INTERRUPT_REARM_FAILED:
                MICROS_TRAP_PANIC(
                    hart->hardware_id,
                    "timer-rearm-failed",
                    frame
                );
            }
            MICROS_TRAP_PANIC(
                hart->hardware_id,
                "timer-result-invalid",
                frame
            );
        }
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "unexpected-interrupt",
            frame
        );
    }
    return false;
}
