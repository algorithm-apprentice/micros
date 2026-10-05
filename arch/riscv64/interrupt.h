#ifndef MICROS_ARCH_RISCV64_INTERRUPT_H
#define MICROS_ARCH_RISCV64_INTERRUPT_H

#include <stdbool.h>
#include <stdint.h>

#include "arch/riscv64/trap_context.h"

#define MICROS_RISCV_SIE_STIE (UINT64_C(1) << 5)

static inline uintptr_t riscv_irq_save(void)
{
    uintptr_t status;

    __asm__ volatile(
        "csrrci %0, sstatus, 2"
        : "=r"(status)
        :
        : "memory");
    return status;
}

static inline void riscv_irq_restore(uintptr_t status)
{
    if ((status & MICROS_RISCV_SSTATUS_SIE) != 0) {
        __asm__ volatile("csrsi sstatus, 2" : : : "memory");
    } else {
        __asm__ volatile("csrci sstatus, 2" : : : "memory");
    }
}

static inline bool riscv_irq_is_enabled(void)
{
    uintptr_t status;

    __asm__ volatile("csrr %0, sstatus" : "=r"(status));
    return (status & MICROS_RISCV_SSTATUS_SIE) != 0;
}

static inline void riscv_timer_interrupt_enable(void)
{
    uintptr_t mask = MICROS_RISCV_SIE_STIE;

    __asm__ volatile("csrs sie, %0" : : "r"(mask) : "memory");
}

static inline void riscv_timer_interrupt_disable(void)
{
    uintptr_t mask = MICROS_RISCV_SIE_STIE;

    __asm__ volatile("csrc sie, %0" : : "r"(mask) : "memory");
}

static inline bool riscv_timer_interrupt_is_enabled(void)
{
    uintptr_t enabled;

    __asm__ volatile("csrr %0, sie" : "=r"(enabled));
    return (enabled & MICROS_RISCV_SIE_STIE) != 0;
}

static inline void riscv_wait_for_interrupt_with_irq_window(void)
{
    __asm__ volatile(
        "wfi\n"
        "csrsi sstatus, 2\n"
        "csrci sstatus, 2"
        :
        :
        : "memory");
}

#endif
