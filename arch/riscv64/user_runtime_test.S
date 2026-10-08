.section .text, "ax", @progbits
.option push
.option norelax
.option norvc
.balign 4
.globl micros_user_runtime_test_supervisor_resume
.type micros_user_runtime_test_supervisor_resume, @function
micros_user_runtime_test_supervisor_resume:
    la t0, micros_user_runtime_test_saved_gp
    ld gp, 0(t0)
    la t0, micros_user_runtime_test_saved_tp
    ld tp, 0(t0)
    la t0, micros_user_runtime_test_saved_sp
    ld sp, 0(t0)
    tail micros_user_runtime_test_finish
.size micros_user_runtime_test_supervisor_resume, . - micros_user_runtime_test_supervisor_resume
.option pop

.section .note.GNU-stack, "", @progbits
