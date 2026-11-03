#ifndef MICROS_KERNEL_PANIC_CONTROL_H
#define MICROS_KERNEL_PANIC_CONTROL_H

#include <stdbool.h>

bool micros_panic_claim_first(void);
bool micros_panic_claim_reset(void);
_Noreturn void micros_panic_terminate_no_console(void);
_Noreturn void micros_panic_halt(void);

#endif
