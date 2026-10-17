#include "kernel/trap/trap_internal.h"

#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "micros/scheduler.h"
#include "micros/user_execution.h"

static void trap_capture_user_context(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    if (
        micros_scheduler_user_trap_enter(hart, frame)
            != MICROS_SCHEDULER_OK
    ) {
        trap_fail_active_bootstrap_service_trap(hart);
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "scheduler-user-entry",
            frame
        );
    }
    trap_test_hooks_pre_capture(hart, frame);
    if (
        micros_user_execution_capture_trap(hart, frame)
            != MICROS_USER_EXECUTION_OK
    ) {
        trap_fail_active_bootstrap_service_trap(hart);
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "user-execution-capture",
            frame
        );
    }
}

static bool trap_route_user_faults(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer,
    bool user_external,
    uint64_t cause_code
)
{
    if (trap_test_hooks_scratch_fault(
        hart, frame, user_timer, user_external, cause_code
    )) {
        return true;
    }
    if (trap_test_hooks_ramfs_fault(
        hart, frame, user_timer, user_external, cause_code
    )) {
        return true;
    }
    trap_test_hooks_vfs_fault(
        hart, frame, user_timer, user_external, cause_code
    );
    if (
        !user_timer
        && !user_external
        && cause_code != MICROS_EXCEPTION_USER_ECALL
    ) {
        (void)trap_handle_vm_self_fault(hart, frame, cause_code);
        trap_fail_active_bootstrap_service_trap(hart);
    }
    return false;
}

static bool trap_route_user_ecalls(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer,
    uint64_t cause_code
)
{
    if (trap_test_hooks_control_ecall(
        hart, frame, user_timer, cause_code
    )) {
        return true;
    }
    if (trap_test_hooks_address_space_handoff(
        hart, frame, user_timer, cause_code
    )) {
        return true;
    }
#if !defined(MICROS_BUILD_USER_EXECUTION_TEST) \
    && !defined(MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST) \
    && !defined(MICROS_BUILD_SCHEDULER_TEST) \
    && !defined(MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST) \
    && !defined(MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST)
    if (
        !user_timer
        && cause_code == MICROS_EXCEPTION_USER_ECALL
    ) {
        trap_handle_user_ecall(hart, frame);
        return true;
    }
#endif
    return false;
}

static bool trap_route_user_fixture_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer
)
{
#if defined(MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST)
    trap_test_hooks_invalid_user_trap(hart, frame, user_timer);
#elif defined(MICROS_BUILD_SCHEDULER_TEST)
    if (trap_test_hooks_scheduler_user_trap(hart, frame, user_timer)) {
        return true;
    }
#elif defined(MICROS_BUILD_USER_EXECUTION_TEST)
#ifdef MICROS_BUILD_USER_EXECUTION_TEST
    trap_test_hooks_user_execution_trap(hart, frame);
    return true;
#endif
#else
    if (!user_timer) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "unexpected-user-trap",
            frame
        );
    }
#endif
    (void)user_timer;
    return false;
}

void trap_handle_user_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_trap_interrupt_route interrupt_route,
    uint64_t cause_code
)
{
    bool user_timer =
        interrupt_route == MICROS_TRAP_INTERRUPT_USER_TIMER;
    bool user_external =
        interrupt_route == MICROS_TRAP_INTERRUPT_USER_EXTERNAL;

    trap_capture_user_context(hart, frame);
    if (trap_route_user_faults(
        hart, frame, user_timer, user_external, cause_code
    )) {
        return;
    }
    if (user_external) {
        trap_handle_user_external(hart, frame);
        return;
    }
    if (user_timer) {
        trap_handle_user_timer(hart, frame);
    }
    if (trap_test_hooks_user_faults(
        hart, frame, user_timer, cause_code
    )) {
        return;
    }
    if (trap_route_user_ecalls(hart, frame, user_timer, cause_code)) {
        return;
    }
    if (trap_route_user_fixture_return(hart, frame, user_timer)) {
        return;
    }
    if (
        micros_scheduler_select_user_return(hart, frame)
            != MICROS_SCHEDULER_OK
    ) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "scheduler-user-return",
            frame
        );
    }
    trap_test_hooks_after_user_return(hart, frame, user_timer);
}
