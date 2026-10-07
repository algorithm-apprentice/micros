.section .text.ipc_ecall_payload, "ax", @progbits
.balign 4
.globl micros_ipc_ecall_test_payload_start
micros_ipc_ecall_test_payload_start:
    ecall
    bnez a0, 1f
    li t0, 5
    bne a1, t0, 1f
    li t0, 6
    bne a7, t0, 1f
    li t0, 1
    sd t0, 0(s0)

    mv a0, s1
    mv a1, s2
    li a2, 0
    li a3, 0
    li a7, 1
    ecall
    li t0, -3
    bne a0, t0, 2f
    bne a1, s2, 2f
    li t0, 1
    bne a7, t0, 2f
    li t0, 2
    sd t0, 0(s0)

    li a0, 1
    slli a0, a0, 32
    mv a1, s2
    li a2, 0
    li a3, 0
    li a7, 1
    ecall
    li t0, -1
    bne a0, t0, 3f
    bne a1, s2, 3f
    li t0, 1
    bne a7, t0, 3f
    li t0, 3
    sd t0, 0(s0)
    j 4f

1:
    li t0, 0xe1
    sd t0, 0(s0)
    j 4f
2:
    li t0, 0xe2
    sd t0, 0(s0)
    j 4f
3:
    li t0, 0xe3
    sd t0, 0(s0)
4:
    li a7, -1
.globl micros_ipc_ecall_test_payload_done
micros_ipc_ecall_test_payload_done:
    ecall
5:
    wfi
    j 5b
.globl micros_ipc_ecall_test_payload_end
micros_ipc_ecall_test_payload_end:

.section .text, "ax", @progbits
.balign 4
.globl micros_ipc_ecall_test_supervisor_resume
.type micros_ipc_ecall_test_supervisor_resume, @function
micros_ipc_ecall_test_supervisor_resume:
    la t0, micros_ipc_ecall_test_saved_gp
    ld gp, 0(t0)
    la t0, micros_ipc_ecall_test_saved_tp
    ld tp, 0(t0)
    la t0, micros_ipc_ecall_test_saved_sp
    ld sp, 0(t0)
    tail micros_ipc_ecall_core_test_finish
.size micros_ipc_ecall_test_supervisor_resume, . - micros_ipc_ecall_test_supervisor_resume
