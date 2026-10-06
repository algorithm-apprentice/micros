#ifndef MICROS_TRAP_H
#define MICROS_TRAP_H

#include <stdbool.h>
#include <stdint.h>

#include "arch/riscv64/trap_context.h"

struct micros_hart;

bool micros_trap_install(void);
void micros_trap_dispatch(struct micros_trap_frame *frame);
_Noreturn void micros_trap_nested_panic(
    struct micros_hart *hart,
    const struct micros_trap_frame *outer_frame
);

#ifdef MICROS_BUILD_TRAP_TEST
bool micros_trap_run_self_test(void);
#endif

#ifdef MICROS_BUILD_TRAP_PANIC_TEST
_Noreturn void micros_trap_run_panic_test(void);
#endif

#endif
