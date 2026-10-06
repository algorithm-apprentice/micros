#include "arch/riscv64/trap_context.h"

.section .text, "ax", @progbits
.globl micros_nested_trap_test_trigger
.type micros_nested_trap_test_trigger, @function
micros_nested_trap_test_trigger:
    li t0, MICROS_RISCV_SSTATUS_SUM
    csrs sstatus, t0
    li tp, 0x4e45535445445450
.globl micros_nested_trap_test_outer_fault
micros_nested_trap_test_outer_fault:
    .4byte 0xc0001073
.Lnested_trap_test_returned:
    wfi
    j .Lnested_trap_test_returned
.size micros_nested_trap_test_trigger, . - micros_nested_trap_test_trigger

.section .bss.nested_trap_test, "aw", @nobits
.balign 16
.globl micros_nested_trap_test_emergency_stack_bottom
micros_nested_trap_test_emergency_stack_bottom:
    .skip 4 * 1024
.globl micros_nested_trap_test_emergency_stack_top
micros_nested_trap_test_emergency_stack_top:
