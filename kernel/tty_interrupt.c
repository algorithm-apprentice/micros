#include "kernel/tty_interrupt.h"

#include <stdint.h>

#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/bootstrap_runtime.h"
#include "kernel/ipc_runtime_internal.h"
#include "kernel/kernel_object_runtime_internal.h"
#include "kernel/plic.h"
#include "kernel/tty_fault.h"
#include "kernel/tty_handoff_runtime.h"
#include "kernel/user_address_space_internal.h"
#include "micros/bootstrap.h"
#include "micros/ipc_core.h"
#include "micros/panic.h"
#ifdef MICROS_BUILD_SCHEDULER_TEST
#include "micros/scheduler.h"
#endif
#include "micros/user_address_space.h"

static _Noreturn void fail_tty_interrupt(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
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
            MICROS_BOOTSTRAP_DIAGNOSTIC_SERVICE_FAULT,
            state->service_id,
            state->endpoint,
            0
        );
    }
    micros_tty_owner_fault_record();
    MICROS_TRAP_PANIC(
        hart == NULL ? 0 : hart->hardware_id,
        "tty-interrupt-invariant",
        frame
    );
}

enum micros_tty_interrupt_result micros_tty_interrupt_dispatch(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct micros_tty_handoff_runtime_state *state;
    const struct micros_bootstrap_control_state *bootstrap;
    struct micros_endpoint_registry *registry;
    struct micros_kernel_objects *objects;
    struct micros_tty_handoff claimed_handoff;
    struct micros_ipc_kernel_notification_plan notification;
    uint32_t source;

    if (hart == NULL || frame == NULL) {
        fail_tty_interrupt(hart, frame, NULL);
    }
#ifdef MICROS_BUILD_SCHEDULER_TEST
    if (micros_scheduler_test_handle_external_interrupt(hart, frame)) {
        return MICROS_TTY_INTERRUPT_HANDLED;
    }
#endif
    if (!micros_plic_claim(&source)) {
        state = micros_tty_handoff_runtime_authoritative_state();
        fail_tty_interrupt(hart, frame, state);
    }
    if (source == 0) {
        return MICROS_TTY_INTERRUPT_SPURIOUS;
    }
    state = micros_tty_handoff_runtime_authoritative_state();
    if (source != MICROS_TTY_UART_IRQ_SOURCE) {
        fail_tty_interrupt(hart, frame, state);
    }
    bootstrap = micros_bootstrap_runtime_state();
    registry = micros_ipc_runtime_authoritative_registry();
    objects =
        micros_kernel_object_runtime_authoritative_registry();
    if (
        state == NULL
        || bootstrap == NULL
        || registry == NULL
        || objects == NULL
        || micros_bootstrap_runtime_validate()
            != MICROS_BOOTSTRAP_OK
        || micros_tty_handoff_runtime_validate(
            bootstrap,
            registry,
            objects
        ) != MICROS_TTY_HANDOFF_OK
        || micros_user_address_space_validate_tty_uart_mapping(
            state->process,
            state->root_physical_address
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || !uart_console_handoff_is_active()
        || !micros_plic_validate(MICROS_PLIC_ENABLED)
        || micros_tty_handoff_runtime_prepare_claim(
            source,
            &claimed_handoff
        ) != MICROS_TTY_HANDOFF_OK
    ) {
        fail_tty_interrupt(hart, frame, state);
    }

    micros_tty_handoff_runtime_commit_claim_prevalidated(
        &claimed_handoff
    );
    if (
        micros_ipc_prepare_kernel_notification(
            registry,
            objects,
            state->endpoint,
            MICROS_KERNEL_EVENT_TTY_IRQ,
            &notification
        ) != MICROS_IPC_OK
    ) {
        fail_tty_interrupt(hart, frame, state);
    }
    micros_ipc_commit_kernel_notification_prevalidated(
        registry,
        objects,
        &notification
    );
    if (
        !micros_plic_validate(MICROS_PLIC_ENABLED)
        || micros_tty_handoff_runtime_validate(
            bootstrap,
            registry,
            objects
        ) != MICROS_TTY_HANDOFF_OK
    ) {
        fail_tty_interrupt(hart, frame, state);
    }
    return MICROS_TTY_INTERRUPT_HANDLED;
}
