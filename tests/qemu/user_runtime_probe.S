.section .text.user_runtime_probe, "ax", @progbits
.option push
.option norvc
.balign 4

.globl micros_user_runtime_test_probe_rodata
.type micros_user_runtime_test_probe_rodata, @function
micros_user_runtime_test_probe_rodata:
.globl micros_user_runtime_test_rodata_fault
micros_user_runtime_test_rodata_fault:
    sd a1, 0(a0)
.globl micros_user_runtime_test_rodata_resume
micros_user_runtime_test_rodata_resume:
    ret
.size micros_user_runtime_test_probe_rodata, . - micros_user_runtime_test_probe_rodata

.globl micros_user_runtime_test_raw_send_probe
.type micros_user_runtime_test_raw_send_probe, @function
micros_user_runtime_test_raw_send_probe:
    addi sp, sp, -128
    sd ra, 0(sp)
    sd s0, 8(sp)
    sd s1, 16(sp)
    sd s2, 24(sp)
    sd s3, 32(sp)
    sd s4, 40(sp)
    sd s5, 48(sp)
    sd s6, 56(sp)
    sd s7, 64(sp)
    sd s8, 72(sp)
    sd s9, 80(sp)
    sd s10, 88(sp)
    sd s11, 96(sp)
    sd a0, 104(sp)
    sd a1, 112(sp)
    sd sp, 120(sp)

    li s0, 0x1100
    li s1, 0x1101
    li s2, 0x1102
    li s3, 0x1103
    li s4, 0x1104
    li s5, 0x1105
    li s6, 0x1106
    li s7, 0x1107
    li s8, 0x1108
    li s9, 0x1109
    li s10, 0x110a
    li s11, 0x110b
    li a2, 0
    li a3, 0
    li a4, 0
    li a5, 0
    li a6, 0
    li a7, 1
    call micros_runtime_raw_syscall

    bnez a0, .Lraw_probe_result
    ld t0, 112(sp)
    bne a1, t0, .Lraw_probe_a1
    bnez a2, .Lraw_probe_a2
    bnez a3, .Lraw_probe_a3
    bnez a4, .Lraw_probe_a4
    bnez a5, .Lraw_probe_a5
    bnez a6, .Lraw_probe_a6
    li t0, 1
    bne a7, t0, .Lraw_probe_a7
    ld t0, 120(sp)
    bne sp, t0, .Lraw_probe_sp
    bnez gp, .Lraw_probe_gp
    bnez tp, .Lraw_probe_tp
    li t0, 0x1100
    bne s0, t0, .Lraw_probe_s0
    li t0, 0x1101
    bne s1, t0, .Lraw_probe_s1
    li t0, 0x1102
    bne s2, t0, .Lraw_probe_s2
    li t0, 0x1103
    bne s3, t0, .Lraw_probe_s3
    li t0, 0x1104
    bne s4, t0, .Lraw_probe_s4
    li t0, 0x1105
    bne s5, t0, .Lraw_probe_s5
    li t0, 0x1106
    bne s6, t0, .Lraw_probe_s6
    li t0, 0x1107
    bne s7, t0, .Lraw_probe_s7
    li t0, 0x1108
    bne s8, t0, .Lraw_probe_s8
    li t0, 0x1109
    bne s9, t0, .Lraw_probe_s9
    li t0, 0x110a
    bne s10, t0, .Lraw_probe_s10
    li t0, 0x110b
    bne s11, t0, .Lraw_probe_s11
    li a0, 0
    j .Lraw_probe_restore

.Lraw_probe_result:
    neg a0, a0
    addi a0, a0, 100
    j .Lraw_probe_restore
.Lraw_probe_a1:
    li a0, 2
    j .Lraw_probe_restore
.Lraw_probe_a2:
    li a0, 3
    j .Lraw_probe_restore
.Lraw_probe_a3:
    li a0, 4
    j .Lraw_probe_restore
.Lraw_probe_a4:
    li a0, 5
    j .Lraw_probe_restore
.Lraw_probe_a5:
    li a0, 6
    j .Lraw_probe_restore
.Lraw_probe_a6:
    li a0, 7
    j .Lraw_probe_restore
.Lraw_probe_a7:
    li a0, 8
    j .Lraw_probe_restore
.Lraw_probe_sp:
    li a0, 9
    j .Lraw_probe_restore
.Lraw_probe_gp:
    li a0, 10
    j .Lraw_probe_restore
.Lraw_probe_tp:
    li a0, 11
    j .Lraw_probe_restore
.Lraw_probe_s0:
    li a0, 12
    j .Lraw_probe_restore
.Lraw_probe_s1:
    li a0, 13
    j .Lraw_probe_restore
.Lraw_probe_s2:
    li a0, 14
    j .Lraw_probe_restore
.Lraw_probe_s3:
    li a0, 15
    j .Lraw_probe_restore
.Lraw_probe_s4:
    li a0, 16
    j .Lraw_probe_restore
.Lraw_probe_s5:
    li a0, 17
    j .Lraw_probe_restore
.Lraw_probe_s6:
    li a0, 18
    j .Lraw_probe_restore
.Lraw_probe_s7:
    li a0, 19
    j .Lraw_probe_restore
.Lraw_probe_s8:
    li a0, 20
    j .Lraw_probe_restore
.Lraw_probe_s9:
    li a0, 21
    j .Lraw_probe_restore
.Lraw_probe_s10:
    li a0, 22
    j .Lraw_probe_restore
.Lraw_probe_s11:
    li a0, 23

.Lraw_probe_restore:
    ld ra, 0(sp)
    ld s0, 8(sp)
    ld s1, 16(sp)
    ld s2, 24(sp)
    ld s3, 32(sp)
    ld s4, 40(sp)
    ld s5, 48(sp)
    ld s6, 56(sp)
    ld s7, 64(sp)
    ld s8, 72(sp)
    ld s9, 80(sp)
    ld s10, 88(sp)
    ld s11, 96(sp)
    addi sp, sp, 128
    ret
.size micros_user_runtime_test_raw_send_probe, . - micros_user_runtime_test_raw_send_probe

.option pop
.section .note.GNU-stack, "", @progbits
