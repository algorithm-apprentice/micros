#include "arch/riscv64/trap_context.h"

.section .text, "ax", @progbits
.balign 4
.globl micros_riscv_enter_user
.type micros_riscv_enter_user, @function
micros_riscv_enter_user:
    mv t5, a0

    ld t0, MICROS_TRAP_CONTEXT_SSTATUS_OFFSET(t5)
    andi t0, t0, -3
    csrw sstatus, t0
    ld t0, MICROS_TRAP_CONTEXT_SEPC_OFFSET(t5)
    csrw sepc, t0

    ld ra, MICROS_TRAP_CONTEXT_RA_OFFSET(t5)
    ld gp, MICROS_TRAP_CONTEXT_GP_OFFSET(t5)
    ld tp, MICROS_TRAP_CONTEXT_TP_OFFSET(t5)
    ld t0, MICROS_TRAP_CONTEXT_T0_OFFSET(t5)
    ld t1, MICROS_TRAP_CONTEXT_T1_OFFSET(t5)
    ld t2, MICROS_TRAP_CONTEXT_T2_OFFSET(t5)
    ld s0, MICROS_TRAP_CONTEXT_S0_OFFSET(t5)
    ld s1, MICROS_TRAP_CONTEXT_S1_OFFSET(t5)
    ld a0, MICROS_TRAP_CONTEXT_A0_OFFSET(t5)
    ld a1, MICROS_TRAP_CONTEXT_A1_OFFSET(t5)
    ld a2, MICROS_TRAP_CONTEXT_A2_OFFSET(t5)
    ld a3, MICROS_TRAP_CONTEXT_A3_OFFSET(t5)
    ld a4, MICROS_TRAP_CONTEXT_A4_OFFSET(t5)
    ld a5, MICROS_TRAP_CONTEXT_A5_OFFSET(t5)
    ld a6, MICROS_TRAP_CONTEXT_A6_OFFSET(t5)
    ld a7, MICROS_TRAP_CONTEXT_A7_OFFSET(t5)
    ld s2, MICROS_TRAP_CONTEXT_S2_OFFSET(t5)
    ld s3, MICROS_TRAP_CONTEXT_S3_OFFSET(t5)
    ld s4, MICROS_TRAP_CONTEXT_S4_OFFSET(t5)
    ld s5, MICROS_TRAP_CONTEXT_S5_OFFSET(t5)
    ld s6, MICROS_TRAP_CONTEXT_S6_OFFSET(t5)
    ld s7, MICROS_TRAP_CONTEXT_S7_OFFSET(t5)
    ld s8, MICROS_TRAP_CONTEXT_S8_OFFSET(t5)
    ld s9, MICROS_TRAP_CONTEXT_S9_OFFSET(t5)
    ld s10, MICROS_TRAP_CONTEXT_S10_OFFSET(t5)
    ld s11, MICROS_TRAP_CONTEXT_S11_OFFSET(t5)
    ld t3, MICROS_TRAP_CONTEXT_T3_OFFSET(t5)
    ld t4, MICROS_TRAP_CONTEXT_T4_OFFSET(t5)
    ld t6, MICROS_TRAP_CONTEXT_T6_OFFSET(t5)
    ld sp, MICROS_TRAP_CONTEXT_SP_OFFSET(t5)
    ld t5, MICROS_TRAP_CONTEXT_T5_OFFSET(t5)
    sret
.size micros_riscv_enter_user, . - micros_riscv_enter_user

.section .thread_kernel_stacks, "aw", @nobits
.balign 4096
.globl micros_thread_kernel_stacks
micros_thread_kernel_stacks:
    .skip 128 * 16 * 1024
.globl micros_thread_kernel_stacks_end
micros_thread_kernel_stacks_end:
