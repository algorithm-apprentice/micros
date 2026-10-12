#include "vm_handoff_syscall_test_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

bool vm_handoff_syscall_test_successful_mapping(
    struct vm_handoff_syscall_test_context *fixture
)
{
    struct micros_trap_frame frame = {0};

    vm_handoff_syscall_test_reset(fixture);
    return (
        vm_handoff_syscall_test_invoke_mapping(fixture, fixture->vm_process, fixture->vm_thread, &frame)
        && frame.a0 == MICROS_SYSCALL_ABI_OK
        && fixture->operation_stage == 6
        && fixture->mapping_committed
        && fixture->notification_committed
        && !fixture->order_violation
        && fixture->tty_state.handoff.console_phase
            == MICROS_TTY_CONSOLE_MAPPED
    );
}

bool vm_handoff_syscall_test_unauthorized_mapping(
    struct vm_handoff_syscall_test_context *fixture
)
{
    struct micros_trap_frame frame = {0};

    vm_handoff_syscall_test_reset(fixture);
    return (
        vm_handoff_syscall_test_invoke_mapping(
            fixture,
            fixture->foreign_process,
            fixture->foreign_thread,
            &frame
        )
        && frame.a0
            == (uint64_t)(int64_t)MICROS_SYSCALL_ABI_UNAUTHORIZED
        && fixture->operation_stage == 0
        && !fixture->mapping_committed
        && !fixture->notification_committed
        && fixture->tty_state.handoff.console_phase
            == MICROS_TTY_CONSOLE_MAP_REQUESTED
    );
}

bool vm_handoff_syscall_test_wrong_phase(
    struct vm_handoff_syscall_test_context *fixture
)
{
    struct micros_trap_frame frame = {0};

    vm_handoff_syscall_test_reset(fixture);
    if (
        micros_tty_handoff_mark_mapped(&fixture->tty_state.handoff)
            != MICROS_TTY_HANDOFF_OK
    ) {
        return false;
    }
    return (
        vm_handoff_syscall_test_invoke_mapping(fixture, fixture->vm_process, fixture->vm_thread, &frame)
        && frame.a0
            == (uint64_t)(int64_t)MICROS_SYSCALL_ABI_STATE
        && fixture->operation_stage == 0
        && !fixture->mapping_committed
        && !fixture->notification_committed
        && fixture->tty_state.handoff.console_phase
            == MICROS_TTY_CONSOLE_MAPPED
    );
}

bool vm_handoff_syscall_test_notification_atomicity(
    struct vm_handoff_syscall_test_context *fixture
)
{
    struct micros_trap_frame frame = {0};

    vm_handoff_syscall_test_reset(fixture);
    fixture->notification_should_fail = true;
    fixture->panic_armed = true;
    if (setjmp(fixture->panic_target) == 0) {
        (void)vm_handoff_syscall_test_invoke_mapping(fixture, fixture->vm_process, fixture->vm_thread, &frame);
        fixture->panic_armed = false;
        return false;
    }
    fixture->panic_armed = false;
    return (
        fixture->failure_reason
            == MICROS_BOOTSTRAP_DIAGNOSTIC_CONSOLE_MAP_GATE
        && fixture->operation_stage == 3
        && !fixture->mapping_committed
        && !fixture->notification_committed
        && !fixture->order_violation
        && fixture->tty_state.handoff.console_phase
            == MICROS_TTY_CONSOLE_MAP_REQUESTED
    );
}

bool vm_handoff_syscall_test_uncleared_console_event(
    struct vm_handoff_syscall_test_context *fixture,
    int event_state
)
{
    struct micros_trap_frame frame = {0};

    vm_handoff_syscall_test_reset(fixture);
    if (event_state == 1) {
        fixture->vm_thread_record.ipc_delivery_pending = true;
    } else if (event_state == 2) {
        fixture->launcher_endpoint_record.pending_kernel_events =
            MICROS_KERNEL_EVENT_CONSOLE_MAPPED;
    } else if (event_state == 3) {
        fixture->launcher_thread_record.ipc_delivery_pending = true;
        fixture->launcher_thread_record.ipc_staged_result = MICROS_IPC_OK;
        fixture->launcher_thread_record.ipc_inbound_message.source =
            MICROS_ENDPOINT_NONE;
        fixture->launcher_thread_record.ipc_inbound_message.type =
            MICROS_IPC_TYPE_KERNEL_NOTIFICATION;
    } else {
        fixture->vm_endpoint_record.pending_kernel_events =
            MICROS_KERNEL_EVENT_CONSOLE_MAP_REQUEST;
    }
    fixture->panic_armed = true;
    if (setjmp(fixture->panic_target) == 0) {
        (void)vm_handoff_syscall_test_invoke_mapping(fixture, fixture->vm_process, fixture->vm_thread, &frame);
        fixture->panic_armed = false;
        return false;
    }
    fixture->panic_armed = false;
    return (
        fixture->failure_reason
            == MICROS_BOOTSTRAP_DIAGNOSTIC_CONSOLE_MAP_GATE
        && fixture->operation_stage == 0
        && !fixture->mapping_committed
        && !fixture->notification_committed
        && fixture->tty_state.handoff.console_phase
            == MICROS_TTY_CONSOLE_MAP_REQUESTED
    );
}
