    .section .text
    .align 2
    .globl micros_pm_service_report
    .type micros_pm_service_report, @function
micros_pm_service_report:
    ebreak
1:
    j 1b
    .size micros_pm_service_report, .-micros_pm_service_report
