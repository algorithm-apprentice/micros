.section .text.runtime_raw_syscall, "ax", @progbits
.option push
.option norvc
.balign 4
.globl micros_runtime_raw_syscall
.type micros_runtime_raw_syscall, @function
micros_runtime_raw_syscall:
    ecall
    ret
.size micros_runtime_raw_syscall, . - micros_runtime_raw_syscall
.option pop

.section .note.GNU-stack, "", @progbits
