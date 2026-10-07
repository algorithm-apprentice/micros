.section .text.address_space_handoff_payload, "ax", @progbits
.balign 4
.globl micros_address_space_handoff_payload_start
micros_address_space_handoff_payload_start:
    li a0, 0x4a5
    li a7, 0x40
.globl micros_address_space_handoff_payload_ecall
micros_address_space_handoff_payload_ecall:
    ecall
1:
    wfi
    j 1b
.globl micros_address_space_handoff_payload_end
micros_address_space_handoff_payload_end:

.section .text, "ax", @progbits
.balign 4
.globl micros_address_space_handoff_test_enter
.type micros_address_space_handoff_test_enter, @function
micros_address_space_handoff_test_enter:
    la t0, micros_address_space_handoff_test_saved_state
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
    csrr t1, sstatus
    sd t1, 128(t0)
    call micros_address_space_handoff_test_enter_production
1:
    wfi
    j 1b
.size micros_address_space_handoff_test_enter, . - micros_address_space_handoff_test_enter

.globl micros_address_space_handoff_test_supervisor_resume
.type micros_address_space_handoff_test_supervisor_resume, @function
micros_address_space_handoff_test_supervisor_resume:
    la t0, micros_address_space_handoff_test_saved_state
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
    ld t1, 128(t0)
    csrw sstatus, t1
    ld sp, 8(t0)

    la t0, micros_address_space_handoff_test_restored_state
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
    csrr t1, sstatus
    sd t1, 128(t0)
    ret
.size micros_address_space_handoff_test_supervisor_resume, . - micros_address_space_handoff_test_supervisor_resume

.section .bss.address_space_handoff_test, "aw", @nobits
.balign 16
.globl micros_address_space_handoff_test_saved_state
micros_address_space_handoff_test_saved_state:
    .skip 136

.globl micros_address_space_handoff_test_restored_state
micros_address_space_handoff_test_restored_state:
    .skip 136
