.option push
.option norelax
.option norvc

.section .text.start, "ax", @progbits
.balign 4
.globl _start
.type _start, @function
_start:
    mv gp, zero
    mv tp, zero
    mv a0, sp
    call micros_application_start
.globl micros_application_start_returned
.type micros_application_start_returned, @function
micros_application_start_returned:
    ebreak
1:
    j 1b
.size micros_application_start_returned, . - micros_application_start_returned
.size _start, . - _start

.section .text.application_failure, "ax", @progbits
.balign 4
.globl micros_application_startup_failed
.type micros_application_startup_failed, @function
micros_application_startup_failed:
    ebreak
2:
    j 2b
.size micros_application_startup_failed, . - micros_application_startup_failed

.globl micros_application_exit_returned
.type micros_application_exit_returned, @function
micros_application_exit_returned:
    ebreak
3:
    j 3b
.size micros_application_exit_returned, . - micros_application_exit_returned
.option pop

.section .note.GNU-stack, "", @progbits
