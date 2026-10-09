#include "kernel/tty_control_syscall.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/bootstrap_runtime.h"
#include "kernel/ipc_runtime_internal.h"
#include "kernel/plic.h"
#include "kernel/tty_control_core.h"
#include "kernel/tty_control_syscall_core.h"
#include "kernel/tty_fault.h"
#include "kernel/tty_handoff_runtime.h"
#ifdef MICROS_BUILD_TTY_SERVICE_TEST
#include "kernel/tty_service_test.h"
#endif
#include "kernel/user_address_space_internal.h"
#include "micros/ipc_runtime.h"
#include "micros/panic.h"
#include "micros/user_address_space.h"

static _Noreturn void panic_tty_control(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const char *reason
)
{
    MICROS_TRAP_PANIC(
        hart == NULL ? 0 : hart->hardware_id,
        reason,
        frame
    );
}

static enum micros_syscall_return return_result(
    struct micros_trap_frame *frame,
    enum micros_syscall_abi_result result
)
{
    frame->a0 = (uint64_t)(int64_t)result;
    return MICROS_SYSCALL_RETURN_NORMAL;
}

static bool delivery_invariants_hold(
    const struct micros_tty_handoff_runtime_state *state
)
{
    enum micros_plic_phase expected_plic_phase;
    bool expected_external_enable;

    if (state == NULL) {
        return false;
    }
    switch (state->handoff.route_phase) {
    case MICROS_TTY_ROUTE_DISABLED:
    case MICROS_TTY_ROUTE_PANIC:
        expected_plic_phase = MICROS_PLIC_DISABLED;
        expected_external_enable = false;
        break;
    case MICROS_TTY_ROUTE_IDLE:
    case MICROS_TTY_ROUTE_IN_SERVICE:
        expected_plic_phase = MICROS_PLIC_ENABLED;
        expected_external_enable = true;
        break;
    default:
        return false;
    }
    return (
        micros_plic_validate(expected_plic_phase)
        && riscv_external_interrupt_is_enabled()
            == expected_external_enable
    );
}

static _Noreturn void fail_tty_control(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_bootstrap_diagnostic_reason reason,
    const struct micros_tty_handoff_runtime_state *state
)
{
    const struct micros_bootstrap_control_state *bootstrap =
        micros_bootstrap_runtime_state();

    if (
        state != NULL
        && bootstrap != NULL
        && bootstrap->phase == MICROS_BOOTSTRAP_PHASE_RUNNING
    ) {
        micros_bootstrap_runtime_fail(
            reason,
            state->service_id,
            state->endpoint,
            0
        );
    }
    micros_tty_owner_fault_record();
    panic_tty_control(hart, frame, "tty-control-invariant");
}

static bool common_invariants_hold(
    const struct micros_tty_handoff_runtime_state *state,
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
)
{
    return (
        state != NULL
        && bootstrap != NULL
        && registry != NULL
        && objects != NULL
        && micros_bootstrap_runtime_validate()
            == MICROS_BOOTSTRAP_OK
        && micros_tty_handoff_runtime_validate(
            bootstrap,
            registry,
            objects
        ) == MICROS_TTY_HANDOFF_OK
        && micros_user_address_space_validate_tty_uart_mapping(
            state->process,
            state->root_physical_address
        ) == MICROS_USER_ADDRESS_SPACE_OK
        && uart_console_handoff_is_active()
        && delivery_invariants_hold(state)
    );
}

static enum micros_syscall_return handle_commit(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_tty_control_request *request,
    const struct micros_tty_handoff_runtime_state *state,
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_endpoint_registry *registry,
    const struct micros_syscall_context *context
)
{
    struct micros_tty_handoff handoff;
    struct micros_plic_tty_enable_plan plic_plan;
    enum micros_tty_handoff_error error;
    bool timer_was_enabled;
    bool irq_was_enabled;

    error = micros_tty_handoff_runtime_prepare_commit(&handoff);
    if (error == MICROS_TTY_HANDOFF_ERROR_STATE) {
        return return_result(frame, MICROS_SYSCALL_ABI_STATE);
    }
    if (error != MICROS_TTY_HANDOFF_OK) {
        fail_tty_control(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_SERVICE_FAULT,
            state
        );
    }
    if (
        !micros_tty_control_commit_tuple_matches(
            request,
            state->service_id,
            state->endpoint
        )
    ) {
        return return_result(frame, MICROS_SYSCALL_ABI_ARGUMENT);
    }
    if (
        riscv_external_interrupt_is_enabled()
        || !micros_plic_prepare_tty_enable(&plic_plan)
    ) {
        fail_tty_control(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_SERVICE_FAULT,
            state
        );
    }
    timer_was_enabled = riscv_timer_interrupt_is_enabled();
    irq_was_enabled = riscv_irq_is_enabled();

    micros_tty_handoff_runtime_commit_console_prevalidated(
        &handoff
    );
    micros_plic_commit_tty_prepare_prevalidated(&plic_plan);
    micros_plic_commit_tty_enable_prevalidated(&plic_plan);
    riscv_external_interrupt_enable();

    if (
        riscv_timer_interrupt_is_enabled() != timer_was_enabled
        || riscv_irq_is_enabled() != irq_was_enabled
        || !riscv_external_interrupt_is_enabled()
        || !micros_plic_validate(MICROS_PLIC_ENABLED)
        || micros_tty_handoff_runtime_validate(
            bootstrap,
            registry,
            context->objects
        ) != MICROS_TTY_HANDOFF_OK
    ) {
        fail_tty_control(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_SERVICE_FAULT,
            state
        );
    }
    return return_result(frame, MICROS_SYSCALL_ABI_OK);
}

static enum micros_syscall_return handle_complete(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_tty_control_request *request,
    const struct micros_tty_handoff_runtime_state *state,
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_endpoint_registry *registry,
    const struct micros_syscall_context *context
)
{
    struct micros_tty_handoff handoff;
    enum micros_tty_handoff_error error;

    error = micros_tty_handoff_runtime_prepare_complete(
        MICROS_TTY_UART_IRQ_SOURCE,
        &handoff
    );
    if (error == MICROS_TTY_HANDOFF_ERROR_STATE) {
        return return_result(frame, MICROS_SYSCALL_ABI_STATE);
    }
    if (error != MICROS_TTY_HANDOFF_OK) {
        fail_tty_control(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_SERVICE_FAULT,
            state
        );
    }
    if (!micros_tty_control_complete_tuple_matches(request)) {
        return return_result(frame, MICROS_SYSCALL_ABI_ARGUMENT);
    }
    if (
        !riscv_external_interrupt_is_enabled()
        || !micros_plic_validate(MICROS_PLIC_ENABLED)
        || !micros_plic_complete(MICROS_TTY_UART_IRQ_SOURCE)
    ) {
        fail_tty_control(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_SERVICE_FAULT,
            state
        );
    }
    micros_tty_handoff_runtime_commit_complete_prevalidated(
        &handoff
    );
#ifdef MICROS_BUILD_TTY_SERVICE_TEST
    micros_tty_service_test_record_completion();
#endif
    if (
        micros_tty_handoff_runtime_validate(
            bootstrap,
            registry,
            context->objects
        ) != MICROS_TTY_HANDOFF_OK
    ) {
        fail_tty_control(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_SERVICE_FAULT,
            state
        );
    }
    return return_result(frame, MICROS_SYSCALL_ABI_OK);
}

enum micros_syscall_return
micros_tty_control_handle_captured_user_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_syscall_arguments *arguments
)
{
    struct micros_tty_control_request request;
    struct micros_tty_handoff_runtime_state *state;
    struct micros_bootstrap_control_state *bootstrap;
    struct micros_endpoint_registry *registry;
    enum micros_tty_control_authority_result authority;

    if (
        hart == NULL
        || frame == NULL
        || context == NULL
        || arguments == NULL
        || context->objects == NULL
    ) {
        panic_tty_control(hart, frame, "tty-control-argument");
    }
    if (
        micros_tty_control_decode(arguments, &request)
            != MICROS_SYSCALL_ABI_OK
    ) {
        return return_result(frame, MICROS_SYSCALL_ABI_ARGUMENT);
    }
    state = micros_tty_handoff_runtime_authoritative_state();
    bootstrap = micros_bootstrap_runtime_authoritative_state();
    registry = micros_ipc_runtime_authoritative_registry();
    authority = micros_tty_control_syscall_authority_resolve(
        state,
        bootstrap,
        registry,
        context->objects,
        context
    );
    if (authority == MICROS_TTY_CONTROL_AUTHORITY_INVARIANT) {
        fail_tty_control(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    if (authority == MICROS_TTY_CONTROL_AUTHORITY_UNAUTHORIZED) {
        return return_result(
            frame,
            MICROS_SYSCALL_ABI_UNAUTHORIZED
        );
    }
    if (
        authority != MICROS_TTY_CONTROL_AUTHORITY_AUTHORIZED
        || !common_invariants_hold(
            state,
            bootstrap,
            registry,
            context->objects
        )
    ) {
        fail_tty_control(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_SERVICE_FAULT,
            state
        );
    }
    if (request.command == MICROS_TTY_CONTROL_COMMIT) {
        return handle_commit(
            hart,
            frame,
            &request,
            state,
            bootstrap,
            registry,
            context
        );
    }
    if (request.command == MICROS_TTY_CONTROL_IRQ_COMPLETE) {
        return handle_complete(
            hart,
            frame,
            &request,
            state,
            bootstrap,
            registry,
            context
        );
    }
    return return_result(frame, MICROS_SYSCALL_ABI_ARGUMENT);
}
