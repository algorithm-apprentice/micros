#ifndef MICROS_KERNEL_PLIC_H
#define MICROS_KERNEL_PLIC_H

#include <stdbool.h>
#include <stdint.h>

#include "kernel/plic_core.h"

bool micros_plic_initialize(void);
bool micros_plic_prepare_tty(void);
bool micros_plic_enable_tty(void);
bool micros_plic_disable_tty(void);
bool micros_plic_claim(uint32_t *source);
bool micros_plic_complete(uint32_t source);
bool micros_plic_validate(enum micros_plic_phase expected_phase);
bool micros_plic_runtime_run_self_test(void);

#endif
