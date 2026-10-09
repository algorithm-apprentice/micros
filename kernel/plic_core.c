#include "kernel/plic_core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MICROS_PLIC_CONTROLLER_MAGIC UINT64_C(0x504c49434354524c)

static bool registers_are_valid(
    const struct micros_plic_registers *registers
)
{
    uintptr_t priority;
    uintptr_t enable;
    uintptr_t threshold;
    uintptr_t claim_complete;

    if (
        registers == NULL
        || registers->priority == NULL
        || registers->enable == NULL
        || registers->threshold == NULL
        || registers->claim_complete == NULL
    ) {
        return false;
    }
    priority = (uintptr_t)registers->priority;
    enable = (uintptr_t)registers->enable;
    threshold = (uintptr_t)registers->threshold;
    claim_complete = (uintptr_t)registers->claim_complete;
    return (
        ((priority | enable | threshold | claim_complete)
            & (sizeof(uint32_t) - 1))
            == 0
        && priority != enable
        && priority != threshold
        && priority != claim_complete
        && enable != threshold
        && enable != claim_complete
        && threshold != claim_complete
    );
}

static bool controller_shape_is_valid(
    const struct micros_plic_controller *controller
)
{
    return (
        controller != NULL
        && controller->magic == MICROS_PLIC_CONTROLLER_MAGIC
        && registers_are_valid(&controller->registers)
    );
}

enum micros_plic_error micros_plic_controller_initialize(
    struct micros_plic_controller *controller,
    const struct micros_plic_registers *registers
)
{
    struct micros_plic_controller initialized;

    if (controller == NULL || !registers_are_valid(registers)) {
        return MICROS_PLIC_ERROR_ARGUMENT;
    }
    initialized.magic = MICROS_PLIC_CONTROLLER_MAGIC;
    initialized.phase = MICROS_PLIC_DISABLED;
    initialized.registers = *registers;
    *initialized.registers.enable = 0;
    *initialized.registers.priority = 0;
    *initialized.registers.threshold = 0;
    *controller = initialized;
    return MICROS_PLIC_OK;
}

enum micros_plic_error micros_plic_controller_validate(
    const struct micros_plic_controller *controller
)
{
    uint32_t expected_priority;
    uint32_t expected_enable;

    if (!controller_shape_is_valid(controller)) {
        return MICROS_PLIC_ERROR_ARGUMENT;
    }
    switch (controller->phase) {
    case MICROS_PLIC_DISABLED:
        expected_priority = 0;
        expected_enable = 0;
        break;
    case MICROS_PLIC_PREPARED:
        expected_priority = 1;
        expected_enable = 0;
        break;
    case MICROS_PLIC_ENABLED:
        expected_priority = 1;
        expected_enable =
            UINT32_C(1) << MICROS_TTY_UART_IRQ_SOURCE;
        break;
    case MICROS_PLIC_UNINITIALIZED:
    default:
        return MICROS_PLIC_ERROR_INVARIANT;
    }
    if (
        *controller->registers.priority != expected_priority
        || *controller->registers.enable != expected_enable
        || *controller->registers.threshold != 0
    ) {
        return MICROS_PLIC_ERROR_INVARIANT;
    }
    return MICROS_PLIC_OK;
}

enum micros_plic_error micros_plic_controller_prepare_tty(
    struct micros_plic_controller *controller
)
{
    if (
        micros_plic_controller_validate(controller) != MICROS_PLIC_OK
    ) {
        return controller_shape_is_valid(controller)
            ? MICROS_PLIC_ERROR_INVARIANT
            : MICROS_PLIC_ERROR_ARGUMENT;
    }
    if (controller->phase != MICROS_PLIC_DISABLED) {
        return MICROS_PLIC_ERROR_STATE;
    }

    *controller->registers.priority = 1;
    *controller->registers.threshold = 0;
    controller->phase = MICROS_PLIC_PREPARED;
    return MICROS_PLIC_OK;
}

enum micros_plic_error micros_plic_controller_enable_tty(
    struct micros_plic_controller *controller
)
{
    if (
        micros_plic_controller_validate(controller) != MICROS_PLIC_OK
    ) {
        return controller_shape_is_valid(controller)
            ? MICROS_PLIC_ERROR_INVARIANT
            : MICROS_PLIC_ERROR_ARGUMENT;
    }
    if (controller->phase != MICROS_PLIC_PREPARED) {
        return MICROS_PLIC_ERROR_STATE;
    }

    *controller->registers.enable =
        UINT32_C(1) << MICROS_TTY_UART_IRQ_SOURCE;
    controller->phase = MICROS_PLIC_ENABLED;
    return MICROS_PLIC_OK;
}

enum micros_plic_error micros_plic_controller_disable_tty(
    struct micros_plic_controller *controller
)
{
    if (!controller_shape_is_valid(controller)) {
        return MICROS_PLIC_ERROR_ARGUMENT;
    }

    *controller->registers.enable = 0;
    *controller->registers.priority = 0;
    *controller->registers.threshold = 0;
    controller->phase = MICROS_PLIC_DISABLED;
    return MICROS_PLIC_OK;
}

enum micros_plic_error micros_plic_controller_claim(
    const struct micros_plic_controller *controller,
    uint32_t *source
)
{
    if (source == NULL) {
        return MICROS_PLIC_ERROR_ARGUMENT;
    }
    if (
        micros_plic_controller_validate(controller) != MICROS_PLIC_OK
    ) {
        return controller_shape_is_valid(controller)
            ? MICROS_PLIC_ERROR_INVARIANT
            : MICROS_PLIC_ERROR_ARGUMENT;
    }
    if (controller->phase != MICROS_PLIC_ENABLED) {
        return MICROS_PLIC_ERROR_STATE;
    }

    *source = *controller->registers.claim_complete;
    return MICROS_PLIC_OK;
}

enum micros_plic_error micros_plic_controller_complete(
    struct micros_plic_controller *controller,
    uint32_t source
)
{
    if (source != MICROS_TTY_UART_IRQ_SOURCE) {
        return MICROS_PLIC_ERROR_ARGUMENT;
    }
    if (
        micros_plic_controller_validate(controller) != MICROS_PLIC_OK
    ) {
        return controller_shape_is_valid(controller)
            ? MICROS_PLIC_ERROR_INVARIANT
            : MICROS_PLIC_ERROR_ARGUMENT;
    }
    if (controller->phase != MICROS_PLIC_ENABLED) {
        return MICROS_PLIC_ERROR_STATE;
    }

    *controller->registers.claim_complete = source;
    return MICROS_PLIC_OK;
}
