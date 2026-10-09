    .section .text
    .align 2
    .globl micros_vm_trigger_self_fault
    .type micros_vm_trigger_self_fault, @function
micros_vm_trigger_self_fault:
    li t0, 0x70000000
    .globl micros_vm_self_fault_instruction
micros_vm_self_fault_instruction:
    sd zero, 0(t0)
    ret
    .size micros_vm_trigger_self_fault, .-micros_vm_trigger_self_fault
