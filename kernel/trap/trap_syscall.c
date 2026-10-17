#include "kernel/trap/trap_internal.h"

#include <stddef.h>

#include "kernel/boot/bootstrap_runtime.h"
#include "micros/panic.h"
#include "micros/scheduler.h"

static _Noreturn void trap_fail_syscall_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    const struct micros_bootstrap_control_state *state =
        micros_bootstrap_runtime_state();

    if (
        frame->a7
            == MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL
        && state != NULL
        && (
            state->phase == MICROS_BOOTSTRAP_PHASE_RUNNING
            || state->phase
                == MICROS_BOOTSTRAP_PHASE_SEALED
        )
    ) {
        micros_bootstrap_runtime_fail(
            state->phase
                    == MICROS_BOOTSTRAP_PHASE_SEALED
                ? MICROS_BOOTSTRAP_DIAGNOSTIC_COMPLETION
                : MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            0,
            MICROS_ENDPOINT_NONE,
            0
        );
    }
    if (
        frame->a7 == MICROS_SYSCALL_ABI_TTY_CONTROL
    ) {
        trap_record_tty_owner_failure();
    }
    MICROS_TRAP_PANIC(
        hart->hardware_id,
        "ipc-syscall-return",
        frame
    );
}

void trap_handle_user_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    trap_test_hooks_before_ecall(hart, frame);
    enum micros_syscall_return syscall_return =
        micros_syscall_handle_user_ecall(hart, frame);

    trap_test_hooks_after_dispatch(hart, frame, syscall_return);
    enum micros_scheduler_error scheduler_error =
        syscall_return == MICROS_SYSCALL_RETURN_CAPTURED
            ? micros_scheduler_select_captured_user_return(
                hart,
                frame
            )
            : micros_scheduler_select_user_return(
                hart,
                frame
            );

    if (scheduler_error != MICROS_SCHEDULER_OK) {
        trap_fail_syscall_return(hart, frame);
    }
    trap_test_hooks_after_syscall_return(hart, frame, syscall_return);
}
