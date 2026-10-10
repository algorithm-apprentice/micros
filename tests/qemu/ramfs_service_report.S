    .section .text
    .option push
    .option norvc
    .balign 4
    .globl micros_ramfs_service_test_report
    .type micros_ramfs_service_test_report, @function
micros_ramfs_service_test_report:
    ebreak
    ret
    .size micros_ramfs_service_test_report, \
        . - micros_ramfs_service_test_report
    .option pop
