.section .text, "ax", @progbits
.globl micros_trap_test_trigger
.type micros_trap_test_trigger, @function
micros_trap_test_trigger:
    la t0, micros_trap_test_caller_state
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

    li ra, 0x101
    li gp, 0x103
    li tp, 0x104
    li t1, 0x106
    li t2, 0x107
    li s0, 0x108
    li s1, 0x109
    li a0, 0x10a
    li a1, 0x10b
    li a2, 0x10c
    li a3, 0x10d
    li a4, 0x10e
    li a5, 0x10f
    li a6, 0x110
    li a7, 0x111
    li s2, 0x112
    li s3, 0x113
    li s4, 0x114
    li s5, 0x115
    li s6, 0x116
    li s7, 0x117
    li s8, 0x118
    li s9, 0x119
    li s10, 0x11a
    li s11, 0x11b
    li t3, 0x11c
    li t4, 0x11d
    li t5, 0x11e
    li t6, 0x11f
    li t0, 0x105
    la sp, micros_trap_test_entry_stack_top

.globl micros_trap_test_fault
micros_trap_test_fault:
    .4byte 0xc0001073
    .4byte 0xc0001073

.globl micros_trap_test_resume
micros_trap_test_resume:
    sd ra, -256(sp)
    sd sp, -248(sp)
    sd gp, -240(sp)
    sd tp, -232(sp)
    sd t0, -224(sp)
    sd t1, -216(sp)
    sd t2, -208(sp)
    sd s0, -200(sp)
    sd s1, -192(sp)
    sd a0, -184(sp)
    sd a1, -176(sp)
    sd a2, -168(sp)
    sd a3, -160(sp)
    sd a4, -152(sp)
    sd a5, -144(sp)
    sd a6, -136(sp)
    sd a7, -128(sp)
    sd s2, -120(sp)
    sd s3, -112(sp)
    sd s4, -104(sp)
    sd s5, -96(sp)
    sd s6, -88(sp)
    sd s7, -80(sp)
    sd s8, -72(sp)
    sd s9, -64(sp)
    sd s10, -56(sp)
    sd s11, -48(sp)
    sd t3, -40(sp)
    sd t4, -32(sp)
    sd t5, -24(sp)
    sd t6, -16(sp)
    csrr t0, sstatus
    sd t0, -8(sp)

    la t0, micros_trap_test_caller_state
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
    ld sp, 8(t0)
    ret
.size micros_trap_test_trigger, . - micros_trap_test_trigger

.globl micros_trap_panic_test_trigger
.type micros_trap_panic_test_trigger, @function
micros_trap_panic_test_trigger:
.globl micros_trap_panic_test_fault
micros_trap_panic_test_fault:
    .4byte 0xc0001073
    ret
.size micros_trap_panic_test_trigger, . - micros_trap_panic_test_trigger

.section .bss.trap_test, "aw", @nobits
.balign 16
.globl micros_trap_test_caller_state
micros_trap_test_caller_state:
    .skip 128

.balign 16
.globl micros_trap_test_entry_stack_bottom
micros_trap_test_entry_stack_bottom:
    .skip 256
.globl micros_trap_test_entry_stack_top
micros_trap_test_entry_stack_top:

.balign 16
.globl micros_trap_test_snapshot
micros_trap_test_snapshot:
    .skip 256
.globl micros_trap_test_return_sp
micros_trap_test_return_sp:
