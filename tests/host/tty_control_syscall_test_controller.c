#include "tty_control_syscall_test_internal.h"

void micros_test_external_interrupt_enable(void)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    if (fixture->operation_stage != 5) {
        fixture->order_violation = true;
        return;
    }
    fixture->external_enabled = true;
    fixture->operation_stage = 6;
}

_Noreturn void micros_bootstrap_runtime_fail(
    enum micros_bootstrap_diagnostic_reason reason,
    uint32_t service_id,
    micros_endpoint_t endpoint,
    uint64_t detail
)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    (void)reason;
    (void)service_id;
    (void)endpoint;
    (void)detail;
    if (fixture->panic_armed) {
        longjmp(fixture->panic_target, 1);
    }
    abort();
}

enum micros_tty_handoff_error
micros_tty_handoff_runtime_prepare_commit(
    struct micros_tty_handoff *candidate
)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    if (candidate == NULL || fixture->operation_stage != 0) {
        fixture->order_violation = true;
        return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
    }
    *candidate = fixture->tty_state.handoff;
    if (
        micros_tty_handoff_commit(candidate)
            != MICROS_TTY_HANDOFF_OK
    ) {
        return MICROS_TTY_HANDOFF_ERROR_STATE;
    }
    fixture->operation_stage = 1;
    return MICROS_TTY_HANDOFF_OK;
}

void micros_tty_handoff_runtime_commit_console_prevalidated(
    const struct micros_tty_handoff *candidate
)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    if (candidate == NULL || fixture->operation_stage != 2) {
        fixture->order_violation = true;
        return;
    }
    fixture->tty_state.handoff = *candidate;
    fixture->operation_stage = 3;
}

enum micros_tty_handoff_error
micros_tty_handoff_runtime_prepare_complete(
    uint32_t source,
    struct micros_tty_handoff *candidate
)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    if (candidate == NULL || fixture->operation_stage != 0) {
        fixture->order_violation = true;
        return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
    }
    *candidate = fixture->tty_state.handoff;
    if (
        micros_tty_handoff_complete(candidate, source)
            != MICROS_TTY_HANDOFF_OK
    ) {
        return MICROS_TTY_HANDOFF_ERROR_STATE;
    }
    fixture->operation_stage = 1;
    return MICROS_TTY_HANDOFF_OK;
}

void micros_tty_handoff_runtime_commit_complete_prevalidated(
    const struct micros_tty_handoff *candidate
)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    if (candidate == NULL || fixture->operation_stage != 2) {
        fixture->order_violation = true;
        return;
    }
    fixture->tty_state.handoff = *candidate;
    fixture->operation_stage = 3;
}

bool micros_plic_prepare_tty_enable(
    struct micros_plic_tty_enable_plan *plan
)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    if (
        plan == NULL
        || fixture->operation_stage != 1
        || !fixture->plic_prepare_valid
    ) {
        return false;
    }
    plan->validation_magic = UINT64_C(1);
    fixture->operation_stage = 2;
    return true;
}

void micros_plic_commit_tty_prepare_prevalidated(
    const struct micros_plic_tty_enable_plan *plan
)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    if (
        plan == NULL
        || plan->validation_magic != UINT64_C(1)
        || fixture->operation_stage != 3
    ) {
        fixture->order_violation = true;
        return;
    }
    fixture->plic_phase = MICROS_PLIC_PREPARED;
    fixture->operation_stage = 4;
}

void micros_plic_commit_tty_enable_prevalidated(
    struct micros_plic_tty_enable_plan *plan
)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    if (
        plan == NULL
        || plan->validation_magic != UINT64_C(1)
        || fixture->operation_stage != 4
    ) {
        fixture->order_violation = true;
        return;
    }
    plan->validation_magic = 0;
    fixture->plic_phase = MICROS_PLIC_ENABLED;
    fixture->operation_stage = 5;
}

bool micros_plic_complete(uint32_t source)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    if (
        source != MICROS_TTY_UART_IRQ_SOURCE
        || fixture->plic_phase != MICROS_PLIC_ENABLED
        || fixture->operation_stage != 1
    ) {
        fixture->order_violation = true;
        return false;
    }
    fixture->operation_stage = 2;
    return true;
}

void micros_tty_owner_fault_record(void)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    fixture->owner_fault_count += 1;
}

_Noreturn void micros_trap_panic_entry(
    uintptr_t hart_id,
    const char *reason,
    const char *file,
    uint32_t line,
    const struct micros_trap_frame *trap_frame
)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    (void)hart_id;
    (void)reason;
    (void)file;
    (void)line;
    (void)trap_frame;
    if (fixture->panic_armed) {
        longjmp(fixture->panic_target, 1);
    }
    abort();
}
