.section .text, "ax", @progbits
.balign 4

.globl micros_panic_trace_test_armed
.type micros_panic_trace_test_armed, @function
micros_panic_trace_test_armed:
    nop
    ret
.size micros_panic_trace_test_armed, . - micros_panic_trace_test_armed

.globl micros_panic_trace_test_started
.type micros_panic_trace_test_started, @function
micros_panic_trace_test_started:
    nop
    ret
.size micros_panic_trace_test_started, . - micros_panic_trace_test_started

.globl micros_panic_trace_test_fake_pc
.type micros_panic_trace_test_fake_pc, @function
micros_panic_trace_test_fake_pc:
    nop
    ret
.size micros_panic_trace_test_fake_pc, . - micros_panic_trace_test_fake_pc

.globl micros_panic_trace_test_error
.type micros_panic_trace_test_error, @function
micros_panic_trace_test_error:
    la t0, micros_panic_trace_test_observation
    li t1, 4
    sw t1, 8(t0)
.Ltrace_error:
    wfi
    j .Ltrace_error
.size micros_panic_trace_test_error, . - micros_panic_trace_test_error

.globl micros_panic_trace_test_trap
.type micros_panic_trace_test_trap, @function
micros_panic_trace_test_trap:
    addi s0, a0, 32
    li gp, 0
    la tp, micros_panic_trace_test_fake_pc
.globl micros_panic_trace_test_outer_fault
micros_panic_trace_test_outer_fault:
    .4byte 0xc0001073
    call micros_panic_trace_test_error
.size micros_panic_trace_test_trap, . - micros_panic_trace_test_trap

.globl micros_panic_trace_test_synthetic
.type micros_panic_trace_test_synthetic, @function
micros_panic_trace_test_synthetic:
    addi sp, sp, -1024
    mv t6, a0
    addi s0, sp, 128
    mv t0, s0
    li t1, 33
    la t3, micros_panic_trace_test_fake_pc
.Ltrace_fill:
    addi t2, t0, 16
    sd t2, -16(t0)
    sd t3, -8(t0)
    addi t0, t0, 16
    addi t1, t1, -1
    bnez t1, .Ltrace_fill
    sd zero, -32(t0)
    li t0, 6
    beq t6, t0, .Ltrace_null
    li t0, 7
    beq t6, t0, .Ltrace_misaligned
    li t0, 8
    beq t6, t0, .Ltrace_backward
    li t0, 9
    beq t6, t0, .Ltrace_equal
    li t0, 10
    beq t6, t0, .Ltrace_null_pc
    li t0, 11
    beq t6, t0, .Ltrace_user_pc
    li t0, 12
    bne t6, t0, .Ltrace_input_error
    j .Ltrace_panic
.Ltrace_null:
    li s0, 0
    j .Ltrace_panic
.Ltrace_misaligned:
    addi t0, s0, 1
    sd t0, -16(s0)
    j .Ltrace_panic
.Ltrace_backward:
    li t0, 0x40000000
    sd t0, -16(s0)
    j .Ltrace_panic
.Ltrace_equal:
    sd s0, -16(s0)
    j .Ltrace_panic
.Ltrace_null_pc:
    sd zero, -8(s0)
    j .Ltrace_panic
.Ltrace_user_pc:
    li t0, 0x40000000
    sd t0, -8(s0)
.Ltrace_panic:
    li a0, 0
    la a1, .Ltrace_reason
    la a2, .Ltrace_file
    li a3, __LINE__ + 1
    call micros_panic_entry
.Ltrace_input_error:
    call micros_panic_trace_test_error
.size micros_panic_trace_test_synthetic, . - micros_panic_trace_test_synthetic

.section .rodata, "a", @progbits
.Ltrace_reason:
    .asciz "panic-trace-test"
.Ltrace_file:
    .asciz "tests/qemu/panic_trace_kernel.S"
