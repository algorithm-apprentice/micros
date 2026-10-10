#ifndef MICROS_ARCH_RISCV64_INTERRUPT_H
#define MICROS_ARCH_RISCV64_INTERRUPT_H

#include <stdbool.h>

bool micros_test_irq_is_enabled(void);
bool micros_test_timer_interrupt_is_enabled(void);
void micros_test_external_interrupt_enable(void);
bool micros_test_external_interrupt_is_enabled(void);

static inline bool riscv_irq_is_enabled(void)
{
    return micros_test_irq_is_enabled();
}

static inline bool riscv_timer_interrupt_is_enabled(void)
{
    return micros_test_timer_interrupt_is_enabled();
}

static inline void riscv_external_interrupt_enable(void)
{
    micros_test_external_interrupt_enable();
}

static inline bool riscv_external_interrupt_is_enabled(void)
{
    return micros_test_external_interrupt_is_enabled();
}

#endif
