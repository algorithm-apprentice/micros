.section .text.ipc_syscall_payload, "ax", @progbits
.balign 4
.globl micros_ipc_syscall_test_payload_start
micros_ipc_syscall_test_payload_start:
.globl micros_ipc_syscall_test_payload_ecall
micros_ipc_syscall_test_payload_ecall:
    ecall
.globl micros_ipc_syscall_test_payload_after_ecall
micros_ipc_syscall_test_payload_after_ecall:
    j micros_ipc_syscall_test_payload_ecall
.globl micros_ipc_syscall_test_payload_spin
micros_ipc_syscall_test_payload_spin:
    j micros_ipc_syscall_test_payload_spin
.globl micros_ipc_syscall_test_payload_end
micros_ipc_syscall_test_payload_end:

.section .text, "ax", @progbits
.balign 4
.globl micros_ipc_syscall_test_supervisor_resume
.type micros_ipc_syscall_test_supervisor_resume, @function
micros_ipc_syscall_test_supervisor_resume:
    la t0, micros_ipc_syscall_test_saved_gp
    ld gp, 0(t0)
    la t0, micros_ipc_syscall_test_saved_tp
    ld tp, 0(t0)
    la t0, micros_ipc_syscall_test_saved_sp
    ld sp, 0(t0)
    tail micros_ipc_syscall_test_finish
.size micros_ipc_syscall_test_supervisor_resume, . - micros_ipc_syscall_test_supervisor_resume
