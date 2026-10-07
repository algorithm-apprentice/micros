.section .text.ipc_syscall_panic_payload, "ax", @progbits
.balign 4
.globl micros_ipc_syscall_panic_payload_start
micros_ipc_syscall_panic_payload_start:
    ecall
1:
    wfi
    j 1b
.globl micros_ipc_syscall_panic_payload_end
micros_ipc_syscall_panic_payload_end:
