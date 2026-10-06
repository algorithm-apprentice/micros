#include "arch/riscv64/trap_context.h"

.section .text, "ax", @progbits
.balign 4
.globl micros_user_address_space_sum_fault_trigger
.type micros_user_address_space_sum_fault_trigger, @function
micros_user_address_space_sum_fault_trigger:
    li t0, MICROS_RISCV_SSTATUS_SUM
    csrc sstatus, t0
.globl micros_user_address_space_sum_fault
micros_user_address_space_sum_fault:
    ld t1, 0(a0)
.globl micros_user_address_space_sum_resume
micros_user_address_space_sum_resume:
    ret
.size micros_user_address_space_sum_fault_trigger, . - micros_user_address_space_sum_fault_trigger

.balign 4
.globl micros_user_address_space_sum_load
.type micros_user_address_space_sum_load, @function
micros_user_address_space_sum_load:
    li t0, MICROS_RISCV_SSTATUS_SUM
    csrs sstatus, t0
    ld a0, 0(a0)
    csrc sstatus, t0
    ret
.size micros_user_address_space_sum_load, . - micros_user_address_space_sum_load
