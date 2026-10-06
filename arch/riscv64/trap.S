#include "arch/riscv64/trap_context.h"

.section .text, "ax", @progbits
.balign 4
.globl micros_trap_entry
.type micros_trap_entry, @function
micros_trap_entry:
    csrrw sp, sscratch, sp
    beqz sp, .Lnested_trap

    sd t0, MICROS_HART_TRAP_ENTRY_T0_OFFSET(sp)
    sd t1, MICROS_HART_TRAP_ENTRY_T1_OFFSET(sp)
    sd t2, MICROS_HART_TRAP_ENTRY_T2_OFFSET(sp)
    mv t2, sp

    ld t1, MICROS_HART_TRAP_PRIMARY_TOP_OFFSET(t2)
    addi sp, t1, -MICROS_TRAP_CONTEXT_SIZE
    ld t0, MICROS_HART_TRAP_ENTRY_T0_OFFSET(t2)
    sd t0, MICROS_TRAP_CONTEXT_T0_OFFSET(sp)
    ld t0, MICROS_HART_TRAP_ENTRY_T1_OFFSET(t2)
    sd t0, MICROS_TRAP_CONTEXT_T1_OFFSET(sp)
    ld t0, MICROS_HART_TRAP_ENTRY_T2_OFFSET(t2)
    sd t0, MICROS_TRAP_CONTEXT_T2_OFFSET(sp)
    csrr t0, sscratch
    sd t0, MICROS_TRAP_CONTEXT_SP_OFFSET(sp)
    sd tp, MICROS_TRAP_CONTEXT_TP_OFFSET(sp)
    sd t2, MICROS_TRAP_CONTEXT_HART_CONTEXT_OFFSET(sp)
    mv tp, t2
    csrr t0, sstatus
    sd t0, MICROS_TRAP_CONTEXT_SSTATUS_OFFSET(sp)
    li t0, MICROS_RISCV_SSTATUS_SUM
    csrc sstatus, t0
    csrw sscratch, zero

#ifdef MICROS_BUILD_NESTED_TRAP_TEST
.globl micros_nested_trap_injected_fault
micros_nested_trap_injected_fault:
    .4byte 0xc0001073
#endif

    sd zero, MICROS_HART_TRAP_ENTRY_T0_OFFSET(t2)
    sd zero, MICROS_HART_TRAP_ENTRY_T1_OFFSET(t2)
    sd zero, MICROS_HART_TRAP_ENTRY_T2_OFFSET(t2)
    sd ra, MICROS_TRAP_CONTEXT_RA_OFFSET(sp)
    sd gp, MICROS_TRAP_CONTEXT_GP_OFFSET(sp)
    sd s0, MICROS_TRAP_CONTEXT_S0_OFFSET(sp)
    sd s1, MICROS_TRAP_CONTEXT_S1_OFFSET(sp)
    sd a0, MICROS_TRAP_CONTEXT_A0_OFFSET(sp)
    sd a1, MICROS_TRAP_CONTEXT_A1_OFFSET(sp)
    sd a2, MICROS_TRAP_CONTEXT_A2_OFFSET(sp)
    sd a3, MICROS_TRAP_CONTEXT_A3_OFFSET(sp)
    sd a4, MICROS_TRAP_CONTEXT_A4_OFFSET(sp)
    sd a5, MICROS_TRAP_CONTEXT_A5_OFFSET(sp)
    sd a6, MICROS_TRAP_CONTEXT_A6_OFFSET(sp)
    sd a7, MICROS_TRAP_CONTEXT_A7_OFFSET(sp)
    sd s2, MICROS_TRAP_CONTEXT_S2_OFFSET(sp)
    sd s3, MICROS_TRAP_CONTEXT_S3_OFFSET(sp)
    sd s4, MICROS_TRAP_CONTEXT_S4_OFFSET(sp)
    sd s5, MICROS_TRAP_CONTEXT_S5_OFFSET(sp)
    sd s6, MICROS_TRAP_CONTEXT_S6_OFFSET(sp)
    sd s7, MICROS_TRAP_CONTEXT_S7_OFFSET(sp)
    sd s8, MICROS_TRAP_CONTEXT_S8_OFFSET(sp)
    sd s9, MICROS_TRAP_CONTEXT_S9_OFFSET(sp)
    sd s10, MICROS_TRAP_CONTEXT_S10_OFFSET(sp)
    sd s11, MICROS_TRAP_CONTEXT_S11_OFFSET(sp)
    sd t3, MICROS_TRAP_CONTEXT_T3_OFFSET(sp)
    sd t4, MICROS_TRAP_CONTEXT_T4_OFFSET(sp)
    sd t5, MICROS_TRAP_CONTEXT_T5_OFFSET(sp)
    sd t6, MICROS_TRAP_CONTEXT_T6_OFFSET(sp)

    csrr t0, sepc
    sd t0, MICROS_TRAP_CONTEXT_SEPC_OFFSET(sp)
    csrr t0, scause
    sd t0, MICROS_TRAP_CONTEXT_SCAUSE_OFFSET(sp)
    csrr t0, stval
    sd t0, MICROS_TRAP_CONTEXT_STVAL_OFFSET(sp)

    mv a0, sp
    call micros_trap_dispatch

    ld t0, MICROS_TRAP_CONTEXT_SSTATUS_OFFSET(sp)
    andi t0, t0, -3
    csrw sstatus, t0
    ld t0, MICROS_TRAP_CONTEXT_SEPC_OFFSET(sp)
    csrw sepc, t0

    ld ra, MICROS_TRAP_CONTEXT_RA_OFFSET(sp)
    ld gp, MICROS_TRAP_CONTEXT_GP_OFFSET(sp)
    ld s0, MICROS_TRAP_CONTEXT_S0_OFFSET(sp)
    ld s1, MICROS_TRAP_CONTEXT_S1_OFFSET(sp)
    ld a0, MICROS_TRAP_CONTEXT_A0_OFFSET(sp)
    ld a1, MICROS_TRAP_CONTEXT_A1_OFFSET(sp)
    ld a2, MICROS_TRAP_CONTEXT_A2_OFFSET(sp)
    ld a3, MICROS_TRAP_CONTEXT_A3_OFFSET(sp)
    ld a4, MICROS_TRAP_CONTEXT_A4_OFFSET(sp)
    ld a5, MICROS_TRAP_CONTEXT_A5_OFFSET(sp)
    ld a6, MICROS_TRAP_CONTEXT_A6_OFFSET(sp)
    ld a7, MICROS_TRAP_CONTEXT_A7_OFFSET(sp)
    ld s2, MICROS_TRAP_CONTEXT_S2_OFFSET(sp)
    ld s3, MICROS_TRAP_CONTEXT_S3_OFFSET(sp)
    ld s4, MICROS_TRAP_CONTEXT_S4_OFFSET(sp)
    ld s5, MICROS_TRAP_CONTEXT_S5_OFFSET(sp)
    ld s6, MICROS_TRAP_CONTEXT_S6_OFFSET(sp)
    ld s7, MICROS_TRAP_CONTEXT_S7_OFFSET(sp)
    ld s8, MICROS_TRAP_CONTEXT_S8_OFFSET(sp)
    ld s9, MICROS_TRAP_CONTEXT_S9_OFFSET(sp)
    ld s10, MICROS_TRAP_CONTEXT_S10_OFFSET(sp)
    ld s11, MICROS_TRAP_CONTEXT_S11_OFFSET(sp)
    ld t3, MICROS_TRAP_CONTEXT_T3_OFFSET(sp)
    ld t4, MICROS_TRAP_CONTEXT_T4_OFFSET(sp)
    ld t5, MICROS_TRAP_CONTEXT_T5_OFFSET(sp)
    ld t6, MICROS_TRAP_CONTEXT_T6_OFFSET(sp)

    ld t0, MICROS_TRAP_CONTEXT_HART_CONTEXT_OFFSET(sp)
    csrw sscratch, t0
    ld t0, MICROS_TRAP_CONTEXT_T0_OFFSET(sp)
    ld t1, MICROS_TRAP_CONTEXT_T1_OFFSET(sp)
    ld t2, MICROS_TRAP_CONTEXT_T2_OFFSET(sp)
    ld tp, MICROS_TRAP_CONTEXT_TP_OFFSET(sp)
    ld sp, MICROS_TRAP_CONTEXT_SP_OFFSET(sp)
    sret

.Lnested_trap:
    csrr a1, sscratch
    csrw sscratch, zero
    mv a0, tp
    ld sp, MICROS_HART_TRAP_EMERGENCY_TOP_OFFSET(tp)
    call micros_trap_nested_panic
.Lnested_halt:
    wfi
    j .Lnested_halt
.size micros_trap_entry, . - micros_trap_entry

.globl micros_riscv_trap_install
.type micros_riscv_trap_install, @function
micros_riscv_trap_install:
    csrci sstatus, MICROS_RISCV_SSTATUS_SIE
    csrw sscratch, a0
    mv tp, a0
    la t0, micros_trap_entry
    csrw stvec, t0
    ret
.size micros_riscv_trap_install, . - micros_riscv_trap_install

.section .trap_stack, "aw", @nobits
.balign 16
.globl __trap_stack_bottom
__trap_stack_bottom:
    .skip 16 * 1024
.globl __trap_stack_top
__trap_stack_top:

.section .trap_emergency_stack, "aw", @nobits
.balign 16
.globl __trap_emergency_stack_bottom
__trap_emergency_stack_bottom:
    .skip 4 * 1024
.globl __trap_emergency_stack_top
__trap_emergency_stack_top:
