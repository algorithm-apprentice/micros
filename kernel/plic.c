#include "kernel/plic.h"

#include <stdbool.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"

static struct micros_plic_controller plic_controller;

static volatile uint32_t *plic_register(uintptr_t address)
{
    return (volatile uint32_t *)address;
}

bool micros_plic_initialize(void)
{
    const struct micros_plic_registers registers = {
        .priority = plic_register(MICROS_RISCV_PLIC_UART0_PRIORITY),
        .enable = plic_register(
            MICROS_RISCV_PLIC_SUPERVISOR_ENABLE_WORD
        ),
        .threshold = plic_register(
            MICROS_RISCV_PLIC_SUPERVISOR_THRESHOLD
        ),
        .claim_complete = plic_register(
            MICROS_RISCV_PLIC_SUPERVISOR_CLAIM_COMPLETE
        ),
    };

    if (
        micros_plic_controller_initialize(
            &plic_controller,
            &registers
        ) != MICROS_PLIC_OK
    ) {
        return false;
    }
    riscv_mmio_fence();
    return micros_plic_validate(MICROS_PLIC_DISABLED);
}

bool micros_plic_prepare_tty(void)
{
    if (
        micros_plic_controller_prepare_tty(&plic_controller)
        != MICROS_PLIC_OK
    ) {
        return false;
    }
    riscv_mmio_fence();
    return micros_plic_validate(MICROS_PLIC_PREPARED);
}

bool micros_plic_enable_tty(void)
{
    riscv_mmio_fence();
    if (
        micros_plic_controller_enable_tty(&plic_controller)
        != MICROS_PLIC_OK
    ) {
        return false;
    }
    riscv_mmio_fence();
    return micros_plic_validate(MICROS_PLIC_ENABLED);
}

bool micros_plic_disable_tty(void)
{
    if (
        micros_plic_controller_disable_tty(&plic_controller)
        != MICROS_PLIC_OK
    ) {
        return false;
    }
    riscv_mmio_fence();
    return micros_plic_validate(MICROS_PLIC_DISABLED);
}

bool micros_plic_claim(uint32_t *source)
{
    enum micros_plic_error error;

    riscv_mmio_fence();
    error = micros_plic_controller_claim(&plic_controller, source);
    riscv_mmio_fence();
    return error == MICROS_PLIC_OK;
}

bool micros_plic_complete(uint32_t source)
{
    enum micros_plic_error error;

    riscv_mmio_fence();
    error = micros_plic_controller_complete(
        &plic_controller,
        source
    );
    riscv_mmio_fence();
    return error == MICROS_PLIC_OK;
}

bool micros_plic_validate(enum micros_plic_phase expected_phase)
{
    return (
        micros_plic_controller_validate(&plic_controller)
            == MICROS_PLIC_OK
        && plic_controller.phase == expected_phase
    );
}

bool micros_plic_runtime_run_self_test(void)
{
    bool timer_was_enabled;
    bool external_was_enabled;
    bool passed = false;

    if (
        riscv_irq_is_enabled()
        || !micros_plic_validate(MICROS_PLIC_DISABLED)
    ) {
        return false;
    }
    timer_was_enabled = riscv_timer_interrupt_is_enabled();
    external_was_enabled = riscv_external_interrupt_is_enabled();
    riscv_timer_interrupt_disable();
    riscv_external_interrupt_disable();

    if (
        riscv_timer_interrupt_is_enabled()
        || riscv_external_interrupt_is_enabled()
        || !micros_plic_prepare_tty()
        || !micros_plic_enable_tty()
    ) {
        goto cleanup;
    }
    riscv_timer_interrupt_enable();
    if (
        !riscv_timer_interrupt_is_enabled()
        || riscv_external_interrupt_is_enabled()
    ) {
        goto cleanup;
    }
    riscv_external_interrupt_enable();
    if (
        !riscv_timer_interrupt_is_enabled()
        || !riscv_external_interrupt_is_enabled()
    ) {
        goto cleanup;
    }
    riscv_timer_interrupt_disable();
    if (
        riscv_timer_interrupt_is_enabled()
        || !riscv_external_interrupt_is_enabled()
    ) {
        goto cleanup;
    }
    riscv_external_interrupt_disable();
    passed = (
        !riscv_timer_interrupt_is_enabled()
        && !riscv_external_interrupt_is_enabled()
    );

cleanup:
    riscv_external_interrupt_disable();
    riscv_timer_interrupt_disable();
    if (!micros_plic_disable_tty()) {
        passed = false;
    }
    if (timer_was_enabled) {
        riscv_timer_interrupt_enable();
    }
    if (external_was_enabled) {
        riscv_external_interrupt_enable();
    }
    return (
        passed
        && !riscv_irq_is_enabled()
        && riscv_timer_interrupt_is_enabled() == timer_was_enabled
        && riscv_external_interrupt_is_enabled()
            == external_was_enabled
        && micros_plic_validate(MICROS_PLIC_DISABLED)
    );
}
