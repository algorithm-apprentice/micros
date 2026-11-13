#ifndef MICROS_SELECTED_GRANT_IRQ_H
#define MICROS_SELECTED_GRANT_IRQ_H

#include <stdbool.h>
#include <stdint.h>

uintptr_t riscv_irq_save(void);
void riscv_irq_restore(uintptr_t status);
bool riscv_irq_is_enabled(void);

#endif
