.section .text.scheduler_payload, "ax", @progbits
.balign 4
.globl micros_scheduler_payload_start
micros_scheduler_payload_start:
    bnez s1, micros_scheduler_payload_loop
.globl micros_scheduler_payload_ecall
micros_scheduler_payload_ecall:
    ecall
    li s1, 1
.globl micros_scheduler_payload_loop
micros_scheduler_payload_loop:
    amoadd.d zero, t1, (s0)
#ifdef MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST
    li sp, 1
#endif
    j micros_scheduler_payload_loop
.globl micros_scheduler_payload_end
micros_scheduler_payload_end:

.section .text, "ax", @progbits
.balign 4
.globl micros_scheduler_test_enter
.type micros_scheduler_test_enter, @function
micros_scheduler_test_enter:
    la t0, micros_scheduler_test_saved_state
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
    call micros_scheduler_test_start_production
1:
    wfi
    j 1b
.size micros_scheduler_test_enter, . - micros_scheduler_test_enter

.globl micros_scheduler_test_supervisor_resume
.type micros_scheduler_test_supervisor_resume, @function
micros_scheduler_test_supervisor_resume:
    csrr t1, sstatus
    la t0, micros_scheduler_test_immediate_status
    sd t1, 0(t0)
    la t0, micros_scheduler_test_saved_state
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
    la t0, micros_scheduler_test_restored_state
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
.size micros_scheduler_test_supervisor_resume, . - micros_scheduler_test_supervisor_resume

.section .bss.scheduler_test, "aw", @nobits
.balign 16
.globl micros_scheduler_test_saved_state
micros_scheduler_test_saved_state:
    .skip 136
.globl micros_scheduler_test_restored_state
micros_scheduler_test_restored_state:
    .skip 136
.globl micros_scheduler_test_immediate_status
micros_scheduler_test_immediate_status:
    .skip 8
