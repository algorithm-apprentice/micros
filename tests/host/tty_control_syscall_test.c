#include "tty_control_syscall_test_internal.h"

static bool tty_control_syscall_test_commit_orders_state_before_delivery(void)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    struct micros_syscall_arguments arguments;
    struct micros_trap_frame frame = {0};

    tty_control_syscall_test_reset_fixture(fixture, false);
    arguments = tty_control_syscall_test_commit_arguments(fixture);
    EXPECT_TRUE(
        tty_control_syscall_test_invoke(fixture, &arguments, &frame) == MICROS_SYSCALL_RETURN_NORMAL
        && (int64_t)frame.a0 == MICROS_SYSCALL_ABI_OK
        && fixture->tty_state.handoff.console_phase
            == MICROS_TTY_CONSOLE_OWNED
        && fixture->tty_state.handoff.route_phase == MICROS_TTY_ROUTE_IDLE
        && fixture->plic_phase == MICROS_PLIC_ENABLED
        && fixture->external_enabled
        && fixture->timer_enabled
        && !fixture->irq_enabled
        && fixture->operation_stage == 6
        && !fixture->order_violation
        && fixture->authority_calls == 1
        && fixture->owner_fault_count == 0
    );
    return true;
}

static bool tty_control_syscall_test_completion_orders_hardware_before_idle(void)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    struct micros_syscall_arguments arguments;
    struct micros_trap_frame frame = {0};

    tty_control_syscall_test_reset_fixture(fixture, true);
    arguments = tty_control_syscall_test_complete_arguments();
    EXPECT_TRUE(
        tty_control_syscall_test_invoke(fixture, &arguments, &frame) == MICROS_SYSCALL_RETURN_NORMAL
        && (int64_t)frame.a0 == MICROS_SYSCALL_ABI_OK
        && fixture->tty_state.handoff.console_phase
            == MICROS_TTY_CONSOLE_OWNED
        && fixture->tty_state.handoff.route_phase == MICROS_TTY_ROUTE_IDLE
        && fixture->tty_state.handoff.claimed_source == 0
        && fixture->plic_phase == MICROS_PLIC_ENABLED
        && fixture->external_enabled
        && fixture->operation_stage == 3
        && !fixture->order_violation
        && fixture->authority_calls == 1
        && fixture->owner_fault_count == 0
    );
    return true;
}

static bool tty_control_syscall_test_returned_failures_preserve_state(void)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    struct micros_syscall_arguments arguments;
    struct micros_trap_frame frame = {0};
    struct micros_tty_handoff snapshot;

    tty_control_syscall_test_reset_fixture(fixture, false);
    arguments = tty_control_syscall_test_commit_arguments(fixture);
    snapshot = fixture->tty_state.handoff;
    fixture->authority_result = MICROS_TTY_CONTROL_AUTHORITY_UNAUTHORIZED;
    EXPECT_TRUE(
        tty_control_syscall_test_invoke(fixture, &arguments, &frame) == MICROS_SYSCALL_RETURN_NORMAL
        && (int64_t)frame.a0 == MICROS_SYSCALL_ABI_UNAUTHORIZED
        && memcmp(&fixture->tty_state.handoff, &snapshot, sizeof(snapshot)) == 0
        && fixture->plic_phase == MICROS_PLIC_DISABLED
        && !fixture->external_enabled
        && fixture->operation_stage == 0
        && !fixture->order_violation
    );

    tty_control_syscall_test_reset_fixture(fixture, true);
    if (
        micros_tty_handoff_complete(
            &fixture->tty_state.handoff,
            MICROS_TTY_UART_IRQ_SOURCE
        ) != MICROS_TTY_HANDOFF_OK
    ) {
        return false;
    }
    arguments = tty_control_syscall_test_commit_arguments(fixture);
    snapshot = fixture->tty_state.handoff;
    EXPECT_TRUE(
        tty_control_syscall_test_invoke(fixture, &arguments, &frame) == MICROS_SYSCALL_RETURN_NORMAL
        && (int64_t)frame.a0 == MICROS_SYSCALL_ABI_STATE
        && memcmp(&fixture->tty_state.handoff, &snapshot, sizeof(snapshot)) == 0
        && fixture->plic_phase == MICROS_PLIC_ENABLED
        && fixture->external_enabled
        && fixture->operation_stage == 0
    );

    tty_control_syscall_test_reset_fixture(fixture, false);
    arguments = tty_control_syscall_test_commit_arguments(fixture);
    arguments.a3 += 1;
    snapshot = fixture->tty_state.handoff;
    EXPECT_TRUE(
        tty_control_syscall_test_invoke(fixture, &arguments, &frame) == MICROS_SYSCALL_RETURN_NORMAL
        && (int64_t)frame.a0 == MICROS_SYSCALL_ABI_ARGUMENT
        && memcmp(&fixture->tty_state.handoff, &snapshot, sizeof(snapshot)) == 0
        && fixture->plic_phase == MICROS_PLIC_DISABLED
        && fixture->operation_stage == 1
    );

    tty_control_syscall_test_reset_fixture(fixture, false);
    arguments = tty_control_syscall_test_commit_arguments(fixture);
    arguments.a3 = UINT64_C(1) << 32;
    snapshot = fixture->tty_state.handoff;
    EXPECT_TRUE(
        tty_control_syscall_test_invoke(fixture, &arguments, &frame) == MICROS_SYSCALL_RETURN_NORMAL
        && (int64_t)frame.a0 == MICROS_SYSCALL_ABI_ARGUMENT
        && fixture->authority_calls == 0
        && memcmp(&fixture->tty_state.handoff, &snapshot, sizeof(snapshot)) == 0
        && fixture->operation_stage == 0
    );

    tty_control_syscall_test_reset_fixture(fixture, true);
    if (
        micros_tty_handoff_complete(
            &fixture->tty_state.handoff,
            MICROS_TTY_UART_IRQ_SOURCE
        ) != MICROS_TTY_HANDOFF_OK
    ) {
        return false;
    }
    arguments = tty_control_syscall_test_complete_arguments();
    snapshot = fixture->tty_state.handoff;
    EXPECT_TRUE(
        tty_control_syscall_test_invoke(fixture, &arguments, &frame) == MICROS_SYSCALL_RETURN_NORMAL
        && (int64_t)frame.a0 == MICROS_SYSCALL_ABI_STATE
        && memcmp(&fixture->tty_state.handoff, &snapshot, sizeof(snapshot)) == 0
        && fixture->operation_stage == 0
    );

    tty_control_syscall_test_reset_fixture(fixture, true);
    arguments = tty_control_syscall_test_complete_arguments();
    arguments.a2 = 9;
    snapshot = fixture->tty_state.handoff;
    EXPECT_TRUE(
        tty_control_syscall_test_invoke(fixture, &arguments, &frame) == MICROS_SYSCALL_RETURN_NORMAL
        && (int64_t)frame.a0 == MICROS_SYSCALL_ABI_ARGUMENT
        && memcmp(&fixture->tty_state.handoff, &snapshot, sizeof(snapshot)) == 0
        && fixture->operation_stage == 1
    );
    return true;
}

static bool tty_control_syscall_test_invariant_failure_is_fatal_before_mutation(void)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    struct micros_syscall_arguments arguments;
    struct micros_trap_frame frame = {0};
    struct micros_tty_handoff snapshot;

    tty_control_syscall_test_reset_fixture(fixture, false);
    arguments = tty_control_syscall_test_commit_arguments(fixture);
    snapshot = fixture->tty_state.handoff;
    fixture->plic_prepare_valid = false;
    fixture->panic_armed = true;
    if (setjmp(fixture->panic_target) == 0) {
        (void)tty_control_syscall_test_invoke(fixture, &arguments, &frame);
        fixture->panic_armed = false;
        return false;
    }
    fixture->panic_armed = false;
    return (
        fixture->owner_fault_count == 1
        && memcmp(&fixture->tty_state.handoff, &snapshot, sizeof(snapshot)) == 0
        && fixture->plic_phase == MICROS_PLIC_DISABLED
        && !fixture->external_enabled
        && fixture->operation_stage == 1
        && !fixture->order_violation
    );
}

static bool tty_control_syscall_test_hardware_corruption_precedes_tuple_failure(void)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    struct micros_syscall_arguments arguments;
    struct micros_trap_frame frame = {0};
    struct micros_tty_handoff snapshot;

    tty_control_syscall_test_reset_fixture(fixture, false);
    arguments = tty_control_syscall_test_commit_arguments(fixture);
    arguments.a3 += 1;
    snapshot = fixture->tty_state.handoff;
    fixture->plic_phase = MICROS_PLIC_ENABLED;
    fixture->external_enabled = true;
    fixture->panic_armed = true;
    if (setjmp(fixture->panic_target) == 0) {
        (void)tty_control_syscall_test_invoke(fixture, &arguments, &frame);
        fixture->panic_armed = false;
        return false;
    }
    fixture->panic_armed = false;
    return (
        fixture->owner_fault_count == 1
        && memcmp(&fixture->tty_state.handoff, &snapshot, sizeof(snapshot)) == 0
        && fixture->operation_stage == 0
        && !fixture->order_violation
    );
}

int main(void)
{
    return (
        tty_control_syscall_test_commit_orders_state_before_delivery()
        && tty_control_syscall_test_completion_orders_hardware_before_idle()
        && tty_control_syscall_test_returned_failures_preserve_state()
        && tty_control_syscall_test_invariant_failure_is_fatal_before_mutation()
        && tty_control_syscall_test_hardware_corruption_precedes_tuple_failure()
    ) ? 0 : 1;
}
