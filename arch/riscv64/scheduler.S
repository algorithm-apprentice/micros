#include "arch/riscv64/scheduler_context.h"
#include "arch/riscv64/trap_context.h"

.section .text, "ax", @progbits
.balign 4

.globl micros_scheduler_idle_pivot
.type micros_scheduler_idle_pivot, @function
micros_scheduler_idle_pivot:
    csrci sstatus, MICROS_RISCV_SSTATUS_SIE_IMMEDIATE
    csrw sscratch, a0
    mv tp, a0
    ld sp, MICROS_HART_TRAP_PRIMARY_TOP_OFFSET(a0)
    j micros_scheduler_idle_wait
.size micros_scheduler_idle_pivot, . - micros_scheduler_idle_pivot

.globl micros_scheduler_idle_wait
.type micros_scheduler_idle_wait, @function
micros_scheduler_idle_wait:
.Lidle_wait:
#ifdef MICROS_BUILD_SCHEDULER_TEST
    la t0, micros_scheduler_test_idle_bypass_once
    ld t1, 0(t0)
    beqz t1, .Lidle_wfi
    sd zero, 0(t0)
    la t0, micros_scheduler_test_idle_bypass_observed
    li t1, 1
    sd t1, 0(t0)
    j .Lidle_enable_window
.Lidle_wfi:
#endif
    wfi
.Lidle_enable_window:
    csrsi sstatus, MICROS_RISCV_SSTATUS_SIE_IMMEDIATE
    csrci sstatus, MICROS_RISCV_SSTATUS_SIE_IMMEDIATE
    lw t0, MICROS_HART_ACCOUNTING_OWNER_OFFSET(tp)
    li t1, MICROS_SCHEDULER_ACCOUNTING_IDLE_VALUE
    beq t0, t1, .Lidle_wait
    li t1, MICROS_SCHEDULER_ACCOUNTING_KERNEL_VALUE
    bne t0, t1, .Lidle_invalid
    mv a0, tp
    call micros_scheduler_idle_select
    j .Lidle_wait
.Lidle_invalid:
    mv a0, tp
    call micros_scheduler_idle_accounting_panic
.Lidle_halt:
    wfi
    j .Lidle_halt
.size micros_scheduler_idle_wait, . - micros_scheduler_idle_wait
