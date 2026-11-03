#ifndef MICROS_KERNEL_PANIC_CONTROL_CORE_H
#define MICROS_KERNEL_PANIC_CONTROL_CORE_H

#include <stdint.h>

enum micros_panic_phase {
    MICROS_PANIC_IDLE = 0,
    MICROS_PANIC_REPORTING = 1,
    MICROS_PANIC_RESET_REQUESTED = 2,
};

enum micros_panic_enter_result {
    MICROS_PANIC_FIRST = 0,
    MICROS_PANIC_REENTERED = 1,
    MICROS_PANIC_ENTRY_INVARIANT = 2,
};

enum micros_panic_reset_result {
    MICROS_PANIC_ISSUE = 0,
    MICROS_PANIC_ALREADY_ISSUED = 1,
    MICROS_PANIC_RESET_INVARIANT = 2,
};

struct micros_panic_control {
    uint32_t phase;
};

enum micros_panic_enter_result micros_panic_control_enter(struct micros_panic_control *control);
enum micros_panic_reset_result micros_panic_control_request_reset(struct micros_panic_control *control);

#endif
