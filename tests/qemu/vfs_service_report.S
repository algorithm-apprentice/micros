    .section .text
    .option push
    .option norvc
    .balign 4
    .globl micros_vfs_service_test_report
    .type micros_vfs_service_test_report, @function
micros_vfs_service_test_report:
    ebreak
    ret
    .size micros_vfs_service_test_report, \
        . - micros_vfs_service_test_report
    .option pop
