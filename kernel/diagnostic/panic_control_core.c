#include "kernel/diagnostic/panic_control_core.h"

#include <stddef.h>

enum micros_panic_enter_result micros_panic_control_enter(struct micros_panic_control *control)
{
    if (control == NULL) {
        return MICROS_PANIC_ENTRY_INVARIANT;
    }
    if (control->phase == MICROS_PANIC_IDLE) {
        control->phase = MICROS_PANIC_REPORTING;
        return MICROS_PANIC_FIRST;
    }
    if (control->phase == MICROS_PANIC_REPORTING || control->phase == MICROS_PANIC_RESET_REQUESTED) {
        return MICROS_PANIC_REENTERED;
    }
    return MICROS_PANIC_ENTRY_INVARIANT;
}

enum micros_panic_reset_result micros_panic_control_request_reset(struct micros_panic_control *control)
{
    if (control == NULL) {
        return MICROS_PANIC_RESET_INVARIANT;
    }
    if (control->phase == MICROS_PANIC_IDLE || control->phase == MICROS_PANIC_REPORTING) {
        control->phase = MICROS_PANIC_RESET_REQUESTED;
        return MICROS_PANIC_ISSUE;
    }
    if (control->phase == MICROS_PANIC_RESET_REQUESTED) {
        return MICROS_PANIC_ALREADY_ISSUED;
    }
    return MICROS_PANIC_RESET_INVARIANT;
}
