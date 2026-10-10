#ifndef MICROS_KERNEL_TRAP_ROUTE_CORE_H
#define MICROS_KERNEL_TRAP_ROUTE_CORE_H

#include <stdbool.h>
#include <stdint.h>

enum micros_trap_interrupt_route {
    MICROS_TRAP_INTERRUPT_NONE = 0,
    MICROS_TRAP_INTERRUPT_USER_TIMER,
    MICROS_TRAP_INTERRUPT_USER_EXTERNAL,
    MICROS_TRAP_INTERRUPT_USER_OTHER,
    MICROS_TRAP_INTERRUPT_SUPERVISOR_TIMER,
    MICROS_TRAP_INTERRUPT_SUPERVISOR_EXTERNAL,
    MICROS_TRAP_INTERRUPT_SUPERVISOR_OTHER,
};

enum micros_trap_interrupt_route micros_trap_interrupt_route_classify(
    bool supervisor_origin,
    uint64_t scause
);

#endif
