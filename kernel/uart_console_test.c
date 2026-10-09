#include "kernel/uart_console_test.h"

#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "kernel/plic.h"
#include "kernel/tty_handoff_runtime.h"
#include "micros/panic.h"
#include "micros/tty.h"

#define UART_INTERRUPT_ENABLE_OFFSET UINT64_C(1)
#define UART_INTERRUPT_ENABLE_TEST_VALUE UINT8_C(0x03)
#define UART_INTERRUPT_ENABLE_RX_LINE UINT8_C(0x05)
#define UART_INTERRUPT_ENABLE_TX_EMPTY UINT8_C(0x02)
#define TTY_IRQ_CLAIM_ATTEMPTS UINT32_C(100000)

static const struct micros_bootstrap_binding tty_test_binding = {
    .service_id = MICROS_TTY_SERVICE_ID,
    .process = {
        .slot = MICROS_TTY_PROCESS_SLOT,
        .generation = 7,
    },
    .thread = {
        .slot = 5,
        .generation = 9,
    },
    .root = UINT64_C(0x80200000),
    .endpoint = UINT32_C(0x00007003),
};

static const struct micros_bootstrap_manifest_entry tty_test_entry = {
    .service_id = MICROS_TTY_SERVICE_ID,
    .image_id = 104,
    .process_slot = MICROS_TTY_PROCESS_SLOT,
    .profile_id = MICROS_PRIVILEGE_PROFILE_TTY,
    .role_flags = MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER,
    .device_base = MICROS_TTY_UART_PHYSICAL_BASE,
    .device_length = MICROS_TTY_UART_MAPPED_LENGTH,
    .irq_source = MICROS_TTY_UART_IRQ_SOURCE,
};

static volatile uint8_t *uart_registers(void)
{
    return (volatile uint8_t *)(uintptr_t)
        MICROS_TTY_UART_PHYSICAL_BASE;
}

_Noreturn void micros_uart_console_runtime_run_self_test(
    uintptr_t hart_id
)
{
    volatile uint8_t *registers = uart_registers();
    const struct micros_tty_handoff_runtime_state *state;
    struct micros_tty_handoff candidate;
    struct micros_plic_tty_enable_plan plic_plan;
    bool timer_was_enabled;
    bool irq_was_enabled;
    uint32_t source = 0;
    uint32_t attempt;

    if (
        micros_tty_handoff_runtime_reset()
            != MICROS_TTY_HANDOFF_OK
        || micros_tty_handoff_runtime_prepare(
            &tty_test_binding,
            &tty_test_entry
        )
            != MICROS_TTY_HANDOFF_OK
        || micros_tty_handoff_runtime_begin(1, 100)
            != MICROS_TTY_HANDOFF_OK
        || micros_tty_handoff_runtime_prepare_mapped(&candidate)
            != MICROS_TTY_HANDOFF_OK
    ) {
        MICROS_PANIC(hart_id, "uart-console-preflight");
    }
    micros_tty_handoff_runtime_commit_mapped_prevalidated(
        &candidate
    );
    if (
        micros_tty_handoff_runtime_prepare_release(&candidate)
            != MICROS_TTY_HANDOFF_OK
        || !uart_console_begin_preflight()
    ) {
        MICROS_PANIC(hart_id, "uart-console-preflight");
    }
    micros_tty_handoff_runtime_commit_release_prevalidated(
        &candidate
    );
    uart_write("MICROS_UART_CONSOLE_TEST begin=early\n");
    uart_flush();
    registers[UART_INTERRUPT_ENABLE_OFFSET] =
        UART_INTERRUPT_ENABLE_TEST_VALUE;
    if (
        registers[UART_INTERRUPT_ENABLE_OFFSET]
            != UART_INTERRUPT_ENABLE_TEST_VALUE
    ) {
        MICROS_PANIC(hart_id, "uart-console-ier-write");
    }

    uart_console_begin_quiesce_prevalidated();
    if (registers[UART_INTERRUPT_ENABLE_OFFSET] != 0) {
        MICROS_PANIC(hart_id, "uart-console-ier-disable");
    }
    uart_console_handoff_commit_prevalidated();
    if (!uart_console_handoff_is_quiesced()) {
        MICROS_PANIC(hart_id, "uart-console-handoff");
    }

    registers[UART_INTERRUPT_ENABLE_OFFSET] =
        UART_INTERRUPT_ENABLE_RX_LINE;
    riscv_mmio_fence();
    if (
        !uart_console_handoff_is_active()
        || uart_console_handoff_is_quiesced()
    ) {
        MICROS_PANIC(hart_id, "uart-console-owned-programming");
    }
    if (
        micros_tty_handoff_runtime_prepare_commit(&candidate)
            != MICROS_TTY_HANDOFF_OK
        || !micros_plic_prepare_tty_enable(&plic_plan)
    ) {
        MICROS_PANIC(hart_id, "uart-console-route-preflight");
    }
    timer_was_enabled = riscv_timer_interrupt_is_enabled();
    irq_was_enabled = riscv_irq_is_enabled();
    micros_tty_handoff_runtime_commit_console_prevalidated(
        &candidate
    );
    micros_plic_commit_tty_prepare_prevalidated(&plic_plan);
    micros_plic_commit_tty_enable_prevalidated(&plic_plan);
    riscv_external_interrupt_enable();
    state = micros_tty_handoff_runtime_state();
    if (
        state == NULL
        || state->handoff.console_phase != MICROS_TTY_CONSOLE_OWNED
        || state->handoff.route_phase != MICROS_TTY_ROUTE_IDLE
        || !micros_plic_validate(MICROS_PLIC_ENABLED)
        || !riscv_external_interrupt_is_enabled()
        || riscv_timer_interrupt_is_enabled() != timer_was_enabled
        || riscv_irq_is_enabled() != irq_was_enabled
    ) {
        MICROS_PANIC(hart_id, "uart-console-route-commit");
    }

    registers[UART_INTERRUPT_ENABLE_OFFSET] =
        UART_INTERRUPT_ENABLE_TX_EMPTY;
    riscv_mmio_fence();
    if (
        micros_tty_handoff_runtime_prepare_claim(
            MICROS_TTY_UART_IRQ_SOURCE,
            &candidate
        ) != MICROS_TTY_HANDOFF_OK
    ) {
        MICROS_PANIC(hart_id, "uart-console-claim-preflight");
    }
    for (attempt = 0; attempt < TTY_IRQ_CLAIM_ATTEMPTS; ++attempt) {
        if (!micros_plic_claim(&source)) {
            MICROS_PANIC(hart_id, "uart-console-claim");
        }
        if (source != 0) {
            break;
        }
    }
    if (
        source != MICROS_TTY_UART_IRQ_SOURCE
    ) {
        MICROS_PANIC(hart_id, "uart-console-claim-source");
    }
    micros_tty_handoff_runtime_commit_claim_prevalidated(
        &candidate
    );
    state = micros_tty_handoff_runtime_state();
    if (
        state == NULL
        || state->handoff.route_phase
            != MICROS_TTY_ROUTE_IN_SERVICE
        || state->handoff.claimed_source
            != MICROS_TTY_UART_IRQ_SOURCE
    ) {
        MICROS_PANIC(hart_id, "uart-console-claim-state");
    }
    source = UINT32_MAX;
    if (
        !micros_plic_claim(&source)
        || source != 0
        || state->handoff.route_phase
            != MICROS_TTY_ROUTE_IN_SERVICE
    ) {
        MICROS_PANIC(hart_id, "uart-console-claim-retained");
    }
    registers[UART_INTERRUPT_ENABLE_OFFSET] = 0;
    riscv_mmio_fence();
    if (
        micros_tty_handoff_runtime_prepare_complete(
            MICROS_TTY_UART_IRQ_SOURCE,
            &candidate
        ) != MICROS_TTY_HANDOFF_OK
        || !micros_plic_complete(MICROS_TTY_UART_IRQ_SOURCE)
    ) {
        MICROS_PANIC(hart_id, "uart-console-complete");
    }
    micros_tty_handoff_runtime_commit_complete_prevalidated(
        &candidate
    );
    state = micros_tty_handoff_runtime_state();
    if (
        state == NULL
        || state->handoff.route_phase != MICROS_TTY_ROUTE_IDLE
        || state->handoff.claimed_source != 0
    ) {
        MICROS_PANIC(hart_id, "uart-console-complete-state");
    }
    micros_panic_seize();
    state = micros_tty_handoff_runtime_state();
    if (
        state == NULL
        || state->handoff.console_phase != MICROS_TTY_CONSOLE_PANIC
        || state->handoff.route_phase != MICROS_TTY_ROUTE_PANIC
        || !micros_plic_validate(MICROS_PLIC_DISABLED)
        || riscv_external_interrupt_is_enabled()
        || !uart_console_panic_is_active()
    ) {
        MICROS_PANIC(hart_id, "uart-console-panic-seize");
    }
    uart_write(
        "MICROS_UART_CONSOLE_TEST_PASS "
        "quiesce=drained ier=disabled ownership=tty panic=seized\n"
    );
    uart_write(
        "MICROS_TTY_IRQ_TEST_PASS "
        "route=state-before-enable claim=source10 "
        "retained=in-service completion=explicit\n"
    );
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_NONE
    );
    MICROS_PANIC(hart_id, "uart-console-reset-returned");
}
