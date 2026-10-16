#include "tty_control_syscall_test_internal.h"

static struct tty_control_syscall_test_fixture fixture_state;

struct tty_control_syscall_test_fixture *tty_control_syscall_test_fixture(void)
{
    return &fixture_state;
}

struct micros_syscall_arguments tty_control_syscall_test_commit_arguments(
    struct tty_control_syscall_test_fixture *fixture
)
{
    return (struct micros_syscall_arguments){
        .a0 = MICROS_TTY_CONTROL_COMMIT,
        .a1 = MICROS_TTY_CONTROL_VERSION,
        .a2 = MICROS_TTY_SERVICE_ID,
        .a3 = fixture->tty_state.endpoint,
        .a4 = MICROS_TTY_UART_VIRTUAL_BASE,
        .a5 = MICROS_TTY_UART_PHYSICAL_BASE,
        .a6 = (uint64_t)MICROS_TTY_UART_IRQ_SOURCE << 32
            | MICROS_TTY_UART_MAPPED_LENGTH,
        .a7 = MICROS_SYSCALL_ABI_TTY_CONTROL,
    };
}

struct micros_syscall_arguments tty_control_syscall_test_complete_arguments(void)
{
    return (struct micros_syscall_arguments){
        .a0 = MICROS_TTY_CONTROL_IRQ_COMPLETE,
        .a1 = MICROS_TTY_CONTROL_VERSION,
        .a2 = MICROS_TTY_UART_IRQ_SOURCE,
        .a7 = MICROS_SYSCALL_ABI_TTY_CONTROL,
    };
}

void tty_control_syscall_test_reset_fixture(
    struct tty_control_syscall_test_fixture *fixture,
    bool in_service
)
{
    memset(&fixture->bootstrap, 0, sizeof(fixture->bootstrap));
    memset(&fixture->registry, 0, sizeof(fixture->registry));
    memset(&fixture->objects, 0, sizeof(fixture->objects));
    fixture->tty_state = (struct micros_tty_handoff_runtime_state){
        .service_id = MICROS_TTY_SERVICE_ID,
        .endpoint = UINT32_C(0x00007003),
        .image_id = 104,
        .profile_id = MICROS_PRIVILEGE_PROFILE_TTY,
        .process = {
            .slot = MICROS_TTY_PROCESS_SLOT,
            .generation = 7,
        },
        .thread = {
            .slot = 5,
            .generation = 9,
        },
        .root_physical_address = UINT64_C(0x80200000),
    };
    if (
        micros_tty_handoff_initialize(&fixture->tty_state.handoff)
            != MICROS_TTY_HANDOFF_OK
        || micros_tty_handoff_begin(&fixture->tty_state.handoff, 100, 50)
            != MICROS_TTY_HANDOFF_OK
        || micros_tty_handoff_mark_mapped(&fixture->tty_state.handoff)
            != MICROS_TTY_HANDOFF_OK
        || micros_tty_handoff_release(&fixture->tty_state.handoff)
            != MICROS_TTY_HANDOFF_OK
        || (
            in_service
            && (
                micros_tty_handoff_commit(&fixture->tty_state.handoff)
                    != MICROS_TTY_HANDOFF_OK
                || micros_tty_handoff_claim(
                    &fixture->tty_state.handoff,
                    MICROS_TTY_UART_IRQ_SOURCE
                ) != MICROS_TTY_HANDOFF_OK
            )
        )
    ) {
        abort();
    }
    fixture->syscall_context = (struct micros_syscall_context){
        .objects = &fixture->objects,
        .process = fixture->tty_state.process,
        .current = fixture->tty_state.thread,
        .hart = {
            .slot = 0,
            .generation = 1,
        },
    };
    fixture->authority_result = MICROS_TTY_CONTROL_AUTHORITY_AUTHORIZED;
    fixture->plic_phase = in_service
        ? MICROS_PLIC_ENABLED
        : MICROS_PLIC_DISABLED;
    fixture->runtime_valid = true;
    fixture->mapping_valid = true;
    fixture->uart_active = true;
    fixture->irq_enabled = false;
    fixture->timer_enabled = true;
    fixture->external_enabled = in_service;
    fixture->plic_prepare_valid = true;
    fixture->order_violation = false;
    fixture->authority_calls = 0;
    fixture->owner_fault_count = 0;
    fixture->operation_stage = 0;
    fixture->panic_armed = false;
}

enum micros_syscall_return tty_control_syscall_test_invoke(
    struct tty_control_syscall_test_fixture *fixture,
    struct micros_syscall_arguments *arguments,
    struct micros_trap_frame *frame
)
{
    struct micros_hart hart = {
        .hardware_id = 0,
    };

    return micros_tty_control_handle_captured_user_ecall(
        &hart,
        frame,
        &fixture->syscall_context,
        arguments
    );
}
