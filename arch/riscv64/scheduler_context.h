#ifndef MICROS_ARCH_RISCV64_SCHEDULER_CONTEXT_H
#define MICROS_ARCH_RISCV64_SCHEDULER_CONTEXT_H

#define MICROS_HART_ACCOUNTING_OWNER_OFFSET 360
#define MICROS_SCHEDULER_ACCOUNTING_KERNEL_VALUE 1
#define MICROS_SCHEDULER_ACCOUNTING_IDLE_VALUE 3
#define MICROS_RISCV_SSTATUS_SIE_IMMEDIATE 2

#ifndef __ASSEMBLER__

#include <stddef.h>

#include "micros/kernel_objects.h"

_Static_assert(
    offsetof(struct micros_hart, accounting_owner)
        == MICROS_HART_ACCOUNTING_OWNER_OFFSET,
    "hart accounting-owner offset mismatch"
);
_Static_assert(
    sizeof(enum micros_scheduler_accounting_owner)
        == sizeof(uint32_t),
    "hart accounting-owner size mismatch"
);
_Static_assert(
    MICROS_SCHEDULER_ACCOUNTING_KERNEL
        == MICROS_SCHEDULER_ACCOUNTING_KERNEL_VALUE,
    "kernel accounting-owner value mismatch"
);
_Static_assert(
    MICROS_SCHEDULER_ACCOUNTING_IDLE
        == MICROS_SCHEDULER_ACCOUNTING_IDLE_VALUE,
    "idle accounting-owner value mismatch"
);

#endif

#endif
