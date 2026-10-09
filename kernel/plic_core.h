#ifndef MICROS_KERNEL_PLIC_CORE_H
#define MICROS_KERNEL_PLIC_CORE_H

#include <stdint.h>

#include "micros/tty.h"

enum micros_plic_error {
    MICROS_PLIC_OK = 0,
    MICROS_PLIC_ERROR_ARGUMENT,
    MICROS_PLIC_ERROR_STATE,
    MICROS_PLIC_ERROR_INVARIANT,
};

enum micros_plic_phase {
    MICROS_PLIC_UNINITIALIZED = 0,
    MICROS_PLIC_DISABLED,
    MICROS_PLIC_PREPARED,
    MICROS_PLIC_ENABLED,
};

struct micros_plic_registers {
    volatile uint32_t *priority;
    volatile uint32_t *enable;
    volatile uint32_t *threshold;
    volatile uint32_t *claim_complete;
};

struct micros_plic_controller {
    uint64_t magic;
    enum micros_plic_phase phase;
    struct micros_plic_registers registers;
};

enum micros_plic_error micros_plic_controller_initialize(
    struct micros_plic_controller *controller,
    const struct micros_plic_registers *registers
);
enum micros_plic_error micros_plic_controller_validate(
    const struct micros_plic_controller *controller
);
enum micros_plic_error micros_plic_controller_prepare_tty(
    struct micros_plic_controller *controller
);
enum micros_plic_error micros_plic_controller_enable_tty(
    struct micros_plic_controller *controller
);
enum micros_plic_error micros_plic_controller_disable_tty(
    struct micros_plic_controller *controller
);
enum micros_plic_error micros_plic_controller_claim(
    const struct micros_plic_controller *controller,
    uint32_t *source
);
enum micros_plic_error micros_plic_controller_complete(
    struct micros_plic_controller *controller,
    uint32_t source
);

#endif
