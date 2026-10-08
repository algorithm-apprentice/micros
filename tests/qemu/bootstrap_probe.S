    .section .text
    .align 2
    .globl micros_bootstrap_probe_report
    .type micros_bootstrap_probe_report, @function
micros_bootstrap_probe_report:
    ebreak
1:
    j 1b
    .size micros_bootstrap_probe_report, .-micros_bootstrap_probe_report
