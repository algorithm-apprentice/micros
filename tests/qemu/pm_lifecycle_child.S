    .section .rodata.pm_lifecycle_child, "a", @progbits
    .align 3
    .globl micros_pm_lifecycle_child_start
micros_pm_lifecycle_child_start:
    addi sp, sp, -64
    sd zero, 0(sp)
    sd zero, 8(sp)
    sd zero, 16(sp)
    sd zero, 24(sp)
    sd zero, 32(sp)
    sd zero, 40(sp)
    sd zero, 48(sp)
    sd zero, 56(sp)
    li t1, 0x00010001
    sw t1, 4(sp)
1:
    auipc t0, %pcrel_hi(micros_pm_lifecycle_child_pm_endpoint)
    lw a0, %pcrel_lo(1b)(t0)
    mv a1, sp
    li a7, 3
    ecall
2:
    ebreak
    j 2b
    .align 2
    .globl micros_pm_lifecycle_child_pm_endpoint
micros_pm_lifecycle_child_pm_endpoint:
    .word 0
    .globl micros_pm_lifecycle_child_end
micros_pm_lifecycle_child_end:
