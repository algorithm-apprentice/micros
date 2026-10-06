.section .text.user_execution_payload, "ax", @progbits
.balign 4
.globl micros_user_execution_payload_start
micros_user_execution_payload_start:
    addi sp, sp, -16
    sd s0, 0(sp)
    ld s0, 0(sp)
    addi sp, sp, 16
.globl micros_user_execution_payload_kernel_fault
micros_user_execution_payload_kernel_fault:
    ld t1, 0(t0)
.globl micros_user_execution_payload_after_fault
micros_user_execution_payload_after_fault:
    li a7, 1
.globl micros_user_execution_payload_first_ecall
micros_user_execution_payload_first_ecall:
    ecall
.globl micros_user_execution_payload_resume
micros_user_execution_payload_resume:
    addi a0, a0, 1
    li a7, 2
.globl micros_user_execution_payload_second_ecall
micros_user_execution_payload_second_ecall:
    ecall
1:
    wfi
    j 1b
.globl micros_user_execution_payload_end
micros_user_execution_payload_end:

.section .text, "ax", @progbits
.balign 4
.globl micros_user_execution_test_enter
.type micros_user_execution_test_enter, @function
micros_user_execution_test_enter:
    la t0, micros_user_execution_test_saved_state
    sd ra, 0(t0)
    sd sp, 8(t0)
    sd gp, 16(t0)
    sd tp, 24(t0)
    sd s0, 32(t0)
    sd s1, 40(t0)
    sd s2, 48(t0)
    sd s3, 56(t0)
    sd s4, 64(t0)
    sd s5, 72(t0)
    sd s6, 80(t0)
    sd s7, 88(t0)
    sd s8, 96(t0)
    sd s9, 104(t0)
    sd s10, 112(t0)
    sd s11, 120(t0)
    csrr t1, sstatus
    sd t1, 128(t0)
    call micros_user_execution_test_enter_production
1:
    wfi
    j 1b
.size micros_user_execution_test_enter, . - micros_user_execution_test_enter

.globl micros_user_execution_test_supervisor_resume
.type micros_user_execution_test_supervisor_resume, @function
micros_user_execution_test_supervisor_resume:
    csrr t1, sstatus
    la t0, micros_user_execution_test_immediate_status
    sd t1, 0(t0)

    la t0, micros_user_execution_test_saved_state
    ld ra, 0(t0)
    ld gp, 16(t0)
    ld tp, 24(t0)
    ld s0, 32(t0)
    ld s1, 40(t0)
    ld s2, 48(t0)
    ld s3, 56(t0)
    ld s4, 64(t0)
    ld s5, 72(t0)
    ld s6, 80(t0)
    ld s7, 88(t0)
    ld s8, 96(t0)
    ld s9, 104(t0)
    ld s10, 112(t0)
    ld s11, 120(t0)
    ld t1, 128(t0)
    csrw sstatus, t1
    ld sp, 8(t0)

    la t0, micros_user_execution_test_restored_state
    sd ra, 0(t0)
    sd sp, 8(t0)
    sd gp, 16(t0)
    sd tp, 24(t0)
    sd s0, 32(t0)
    sd s1, 40(t0)
    sd s2, 48(t0)
    sd s3, 56(t0)
    sd s4, 64(t0)
    sd s5, 72(t0)
    sd s6, 80(t0)
    sd s7, 88(t0)
    sd s8, 96(t0)
    sd s9, 104(t0)
    sd s10, 112(t0)
    sd s11, 120(t0)
    csrr t1, sstatus
    sd t1, 128(t0)
    ret
.size micros_user_execution_test_supervisor_resume, . - micros_user_execution_test_supervisor_resume

.section .bss.user_execution_test, "aw", @nobits
.balign 16
.globl micros_user_execution_test_saved_state
micros_user_execution_test_saved_state:
    .skip 136

.globl micros_user_execution_test_restored_state
micros_user_execution_test_restored_state:
    .skip 136

.globl micros_user_execution_test_immediate_status
micros_user_execution_test_immediate_status:
    .skip 8
