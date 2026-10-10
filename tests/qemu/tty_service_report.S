    .section .text
    .globl micros_tty_service_test_report
    .type micros_tty_service_test_report, @function
micros_tty_service_test_report:
    ebreak
1:
    wfi
    j 1b
    .size micros_tty_service_test_report, . - micros_tty_service_test_report
