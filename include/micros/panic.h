#ifndef MICROS_PANIC_H
#define MICROS_PANIC_H

#include <stdint.h>

#include "arch/riscv64/panic_context.h"

struct micros_trap_frame;

void micros_panic_seize(void);

_Noreturn void micros_panic_entry(
    uintptr_t hart_id,
    const char *reason,
    const char *file,
    uint32_t line
);

_Noreturn void micros_trap_panic_entry(
    uintptr_t hart_id,
    const char *reason,
    const char *file,
    uint32_t line,
    const struct micros_trap_frame *trap_frame
);

_Noreturn void micros_panic_report(
    uintptr_t hart_id,
    const char *reason,
    const char *file,
    uint32_t line,
    const struct micros_trap_frame *trap_frame,
    const struct micros_panic_machine_context *context
);

#define MICROS_PANIC(hart_id, reason) \
    micros_panic_entry((hart_id), (reason), __FILE__, (uint32_t)__LINE__)

#define MICROS_TRAP_PANIC(hart_id, reason, trap_frame) \
    micros_trap_panic_entry( \
        (hart_id), \
        (reason), \
        __FILE__, \
        (uint32_t)__LINE__, \
        (trap_frame) \
    )

#endif
