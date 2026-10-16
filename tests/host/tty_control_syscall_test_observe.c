#include "tty_control_syscall_test_internal.h"

bool micros_test_irq_is_enabled(void)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    return fixture->irq_enabled;
}

bool micros_test_timer_interrupt_is_enabled(void)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    return fixture->timer_enabled;
}

bool micros_test_external_interrupt_is_enabled(void)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    return fixture->external_enabled;
}

const struct micros_bootstrap_control_state *
micros_bootstrap_runtime_state(void)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    return &fixture->bootstrap;
}

struct micros_bootstrap_control_state *
micros_bootstrap_runtime_authoritative_state(void)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    return &fixture->bootstrap;
}

enum micros_bootstrap_error micros_bootstrap_runtime_validate(void)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    return fixture->runtime_valid
        ? MICROS_BOOTSTRAP_OK
        : MICROS_BOOTSTRAP_ERROR_INVARIANT;
}

const struct micros_endpoint_registry *micros_ipc_runtime_registry(void)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    return &fixture->registry;
}

struct micros_endpoint_registry *
micros_ipc_runtime_authoritative_registry(void)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    return &fixture->registry;
}

const struct micros_kernel_objects *
micros_kernel_object_runtime_registry(void)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    return &fixture->objects;
}

const struct micros_tty_handoff_runtime_state *
micros_tty_handoff_runtime_state(void)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    return &fixture->tty_state;
}

struct micros_tty_handoff_runtime_state *
micros_tty_handoff_runtime_authoritative_state(void)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    return &fixture->tty_state;
}

enum micros_tty_control_authority_result
micros_tty_control_syscall_authority_resolve(
    const struct micros_tty_handoff_runtime_state *state,
    const struct micros_bootstrap_control_state *control,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *kernel_objects,
    const struct micros_syscall_context *context
)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    fixture->authority_calls += 1;
    return (
        state == &fixture->tty_state
        && control == &fixture->bootstrap
        && endpoint_registry == &fixture->registry
        && kernel_objects == &fixture->objects
        && context == &fixture->syscall_context
    ) ? fixture->authority_result
      : MICROS_TTY_CONTROL_AUTHORITY_INVARIANT;
}

enum micros_tty_handoff_error micros_tty_handoff_runtime_validate(
    const struct micros_bootstrap_control_state *control,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *kernel_objects
)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    return (
        fixture->runtime_valid
        && control == &fixture->bootstrap
        && endpoint_registry == &fixture->registry
        && kernel_objects == &fixture->objects
        && micros_tty_handoff_validate(&fixture->tty_state.handoff)
            == MICROS_TTY_HANDOFF_OK
    ) ? MICROS_TTY_HANDOFF_OK
      : MICROS_TTY_HANDOFF_ERROR_INVARIANT;
}

bool micros_plic_validate(enum micros_plic_phase expected_phase)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    return fixture->plic_phase == expected_phase;
}

bool uart_console_handoff_is_active(void)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    return fixture->uart_active;
}

enum micros_user_address_space_error
micros_user_address_space_validate_tty_uart_mapping(
    struct micros_process_handle process,
    uint64_t expected_root_physical_address
)
{
    struct tty_control_syscall_test_fixture *fixture = tty_control_syscall_test_fixture();

    return (
        fixture->mapping_valid
        && process.slot == fixture->tty_state.process.slot
        && process.generation == fixture->tty_state.process.generation
        && expected_root_physical_address
            == fixture->tty_state.root_physical_address
    ) ? MICROS_USER_ADDRESS_SPACE_OK
      : MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
}
