.section .text.boot, "ax", @progbits
.globl _start
.type _start, @function
_start:
    la sp, __boot_stack_top

    la t0, __bss_start
    la t1, __bss_end
1:
    bgeu t0, t1, 2f
    sd zero, 0(t0)
    addi t0, t0, 8
    j 1b
2:
    call kernel_main
3:
    wfi
    j 3b
.size _start, . - _start

.section .stack, "aw", @nobits
.balign 16
.globl __boot_stack_bottom
__boot_stack_bottom:
    .skip 16 * 1024
.globl __boot_stack_top
__boot_stack_top:
