.section .text, "ax", @progbits
.balign 8
.globl micros_mmu_text_probe
.type micros_mmu_text_probe, @function
micros_mmu_text_probe:
    ret
.size micros_mmu_text_probe, . - micros_mmu_text_probe

.balign 4
.globl micros_mmu_store_test_trigger
.type micros_mmu_store_test_trigger, @function
micros_mmu_store_test_trigger:
    la t0, micros_mmu_text_probe
    li t1, 0
.globl micros_mmu_store_fault
micros_mmu_store_fault:
    sd t1, 0(t0)
.globl micros_mmu_store_resume
micros_mmu_store_resume:
    ret
.size micros_mmu_store_test_trigger, . - micros_mmu_store_test_trigger

.globl micros_mmu_execute_test_trigger
.type micros_mmu_execute_test_trigger, @function
micros_mmu_execute_test_trigger:
    addi sp, sp, -16
    sd ra, 0(sp)
    la t0, micros_mmu_writable_probe
    jalr ra, 0(t0)
.globl micros_mmu_execute_resume
micros_mmu_execute_resume:
    ld ra, 0(sp)
    addi sp, sp, 16
    ret
.size micros_mmu_execute_test_trigger, . - micros_mmu_execute_test_trigger

.section .data.mmu_test, "aw", @progbits
.balign 4
.globl micros_mmu_writable_probe
micros_mmu_writable_probe:
    .4byte 0x00008067
