#include "kernel/trap_route_core.h"

#include <stdint.h>

#define MICROS_SCAUSE_INTERRUPT (UINT64_C(1) << 63)
#define MICROS_SCAUSE_CODE_MASK (MICROS_SCAUSE_INTERRUPT - 1)

enum {
    MICROS_INTERRUPT_SUPERVISOR_TIMER = 5,
    MICROS_INTERRUPT_SUPERVISOR_EXTERNAL = 9,
};

enum micros_trap_interrupt_route micros_trap_interrupt_route_classify(
    bool supervisor_origin,
    uint64_t scause
)
{
    uint64_t cause_code;

    if ((scause & MICROS_SCAUSE_INTERRUPT) == 0) {
        return MICROS_TRAP_INTERRUPT_NONE;
    }
    cause_code = scause & MICROS_SCAUSE_CODE_MASK;
    if (cause_code == MICROS_INTERRUPT_SUPERVISOR_TIMER) {
        return supervisor_origin
            ? MICROS_TRAP_INTERRUPT_SUPERVISOR_TIMER
            : MICROS_TRAP_INTERRUPT_USER_TIMER;
    }
    if (cause_code == MICROS_INTERRUPT_SUPERVISOR_EXTERNAL) {
        return supervisor_origin
            ? MICROS_TRAP_INTERRUPT_SUPERVISOR_EXTERNAL
            : MICROS_TRAP_INTERRUPT_USER_EXTERNAL;
    }
    return supervisor_origin
        ? MICROS_TRAP_INTERRUPT_SUPERVISOR_OTHER
        : MICROS_TRAP_INTERRUPT_USER_OTHER;
}
