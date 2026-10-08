.section .text.start, "ax", @progbits
.option push
.option norelax
.option norvc
.balign 4
.globl _start
.type _start, @function
_start:
    mv gp, zero
    mv tp, zero
    call micros_service_main
.globl micros_runtime_service_returned
micros_runtime_service_returned:
    ebreak
1:
    j 1b
.size _start, . - _start
.option pop

.section .note.GNU-stack, "", @progbits
