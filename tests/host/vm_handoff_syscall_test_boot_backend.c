#include "vm_handoff_syscall_test_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

struct micros_tty_handoff_runtime_state *
micros_tty_handoff_runtime_authoritative_state(void)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    return &fixture->tty_state;
}

enum micros_tty_handoff_error
micros_tty_handoff_runtime_prepare_mapped(
    struct micros_tty_handoff *candidate
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (
        candidate == NULL
        || fixture->operation_stage != 1
        || fixture->mapping_committed
        || fixture->notification_committed
    ) {
        fixture->order_violation = true;
        return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
    }
    *candidate = fixture->tty_state.handoff;
    if (
        micros_tty_handoff_mark_mapped(candidate)
            != MICROS_TTY_HANDOFF_OK
    ) {
        return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
    }
    fixture->operation_stage = 2;
    return MICROS_TTY_HANDOFF_OK;
}

void micros_tty_handoff_runtime_commit_mapped_prevalidated(
    const struct micros_tty_handoff *candidate
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (
        candidate == NULL
        || fixture->operation_stage != 4
        || !fixture->mapping_committed
        || fixture->notification_committed
    ) {
        fixture->order_violation = true;
        return;
    }
    fixture->tty_state.handoff = *candidate;
    fixture->operation_stage = 5;
}

enum micros_tty_handoff_error micros_tty_handoff_runtime_validate(
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *kernel_objects
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    return (
        state == &fixture->bootstrap
        && endpoint_registry == &fixture->registry
        && kernel_objects == &fixture->objects
        && micros_tty_handoff_validate(&fixture->tty_state.handoff)
            == MICROS_TTY_HANDOFF_OK
    ) ? MICROS_TTY_HANDOFF_OK
      : MICROS_TTY_HANDOFF_ERROR_INVARIANT;
}

bool micros_plic_validate(enum micros_plic_phase expected_phase)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    return expected_phase == MICROS_PLIC_DISABLED;
}

bool uart_console_handoff_is_quiesced(void)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    return true;
}

enum micros_user_address_space_error
micros_user_address_space_complete_wired_handoff(void)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error micros_user_address_space_validate(
    struct micros_process_handle process
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    return (
        vm_handoff_syscall_test_process_handles_equal(process, fixture->tty_process)
        || vm_handoff_syscall_test_process_handles_equal(process, fixture->vm_process)
    )
        ? MICROS_USER_ADDRESS_SPACE_OK
        : MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
}

enum micros_user_address_space_error
micros_user_address_space_prepare_tty_uart_mapping(
    struct micros_process_handle process,
    uint64_t expected_root_physical_address,
    struct micros_user_address_space_tty_uart_plan *plan
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (
        !vm_handoff_syscall_test_process_handles_equal(process, fixture->tty_process)
        || expected_root_physical_address
            != fixture->tty_state.root_physical_address
        || plan == NULL
        || fixture->operation_stage != 0
        || fixture->mapping_committed
        || fixture->notification_committed
    ) {
        fixture->order_violation = true;
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    plan->active = true;
    fixture->operation_stage = 1;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

void micros_user_address_space_commit_tty_uart_mapping(
    struct micros_user_address_space_tty_uart_plan *plan
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (
        plan == NULL
        || !plan->active
        || fixture->operation_stage != 3
        || fixture->mapping_committed
        || fixture->notification_committed
    ) {
        fixture->order_violation = true;
        return;
    }
    fixture->mapping_committed = true;
    fixture->operation_stage = 4;
}

enum micros_ipc_error micros_ipc_prepare_kernel_notification(
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *kernel_objects,
    micros_endpoint_t destination,
    uint64_t event_mask,
    struct micros_ipc_kernel_notification_plan *plan
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (
        endpoint_registry != &fixture->registry
        || kernel_objects != &fixture->objects
        || destination != fixture->launcher_endpoint
        || event_mask != MICROS_KERNEL_EVENT_CONSOLE_MAPPED
        || plan == NULL
        || fixture->operation_stage != 2
        || fixture->mapping_committed
        || fixture->notification_committed
    ) {
        fixture->order_violation = true;
        return MICROS_IPC_ERROR_INVARIANT;
    }
    fixture->operation_stage = 3;
    if (fixture->notification_should_fail) {
        return MICROS_IPC_ERROR_STATE;
    }
    plan->active = true;
    plan->event_mask = event_mask;
    return MICROS_IPC_OK;
}

void micros_ipc_commit_kernel_notification_prevalidated(
    struct micros_endpoint_registry *endpoint_registry,
    struct micros_kernel_objects *kernel_objects,
    struct micros_ipc_kernel_notification_plan *plan
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (
        endpoint_registry != &fixture->registry
        || kernel_objects != &fixture->objects
        || plan == NULL
        || !plan->active
        || fixture->operation_stage != 5
        || !fixture->mapping_committed
        || fixture->notification_committed
    ) {
        fixture->order_violation = true;
        return;
    }
    fixture->notification_committed = true;
    fixture->operation_stage = 6;
}
