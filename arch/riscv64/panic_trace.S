.section .text, "ax", @progbits
.option push
.option norvc
.option norelax
.balign 4

.globl micros_panic_trace_frame_pointer
.type micros_panic_trace_frame_pointer, @function
micros_panic_trace_frame_pointer:
    mv a0, s0
    ret
.size micros_panic_trace_frame_pointer, . - micros_panic_trace_frame_pointer

.globl micros_panic_trace_load_word
.type micros_panic_trace_load_word, @function
micros_panic_trace_load_word:
#ifdef MICROS_BUILD_PANIC_TRACE_TEST
    la t0, micros_panic_trace_test_scenario
    lw t0, 0(t0)
    li t1, 15
    beq t0, t1, .Ltrace_fault
#endif
    ld a0, 0(a0)
    ret
#ifdef MICROS_BUILD_PANIC_TRACE_TEST
.Ltrace_fault:
    li a0, 0
.globl micros_panic_trace_test_load_fault
micros_panic_trace_test_load_fault:
    ld a0, 0(a0)
    ret
#endif
.size micros_panic_trace_load_word, . - micros_panic_trace_load_word
.option pop
