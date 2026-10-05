#ifndef MICROS_TRAP_H
#define MICROS_TRAP_H

#include <stdbool.h>
#include <stdint.h>

#include "arch/riscv64/trap_context.h"

void micros_trap_install(uintptr_t hart_id);
void micros_trap_dispatch(struct micros_trap_frame *frame);
_Noreturn void micros_trap_nested_panic(void);

#ifdef MICROS_BUILD_TRAP_TEST
bool micros_trap_run_self_test(void);
#endif

#ifdef MICROS_BUILD_TRAP_PANIC_TEST
_Noreturn void micros_trap_run_panic_test(void);
#endif

#endif
