    .section .text
    .globl micros_tty_service_test_enable_and_wait
    .type micros_tty_service_test_enable_and_wait, @function
micros_tty_service_test_enable_and_wait:
    addi sp, sp, -16
    sd s11, 8(sp)
    li s11, 0
    add a0, a0, a1
    sb a2, 0(a0)
    fence iorw, iorw
1:
    beqz s11, 1b
    ld s11, 8(sp)
    addi sp, sp, 16
    ret
    .size micros_tty_service_test_enable_and_wait, . - micros_tty_service_test_enable_and_wait
