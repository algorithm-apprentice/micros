    .section .text
    .align 2
    .globl micros_vm_handoff_probe_report
    .type micros_vm_handoff_probe_report, @function
micros_vm_handoff_probe_report:
    ebreak
1:
    j 1b
    .size micros_vm_handoff_probe_report, .-micros_vm_handoff_probe_report
