.section .text, "ax", @progbits
.balign 4
.globl micros_vm_scratch_preemption_vm_hook
.type micros_vm_scratch_preemption_vm_hook, @function
micros_vm_scratch_preemption_vm_hook:
.globl micros_vm_scratch_preemption_vm_hook_trap
micros_vm_scratch_preemption_vm_hook_trap:
    ebreak
.globl micros_vm_scratch_preemption_vm_hook_spin
micros_vm_scratch_preemption_vm_hook_spin:
    j micros_vm_scratch_preemption_vm_hook_spin
.globl micros_vm_scratch_preemption_vm_hook_resume
micros_vm_scratch_preemption_vm_hook_resume:
    ret
.size micros_vm_scratch_preemption_vm_hook, . - micros_vm_scratch_preemption_vm_hook

.balign 4
.globl micros_vm_scratch_preemption_stale_load
.type micros_vm_scratch_preemption_stale_load, @function
micros_vm_scratch_preemption_stale_load:
.globl micros_vm_scratch_preemption_stale_load_fault
micros_vm_scratch_preemption_stale_load_fault:
    ld a0, 0(a0)
.globl micros_vm_scratch_preemption_stale_load_unexpected
micros_vm_scratch_preemption_stale_load_unexpected:
    ebreak
1:
    j 1b
.size micros_vm_scratch_preemption_stale_load, . - micros_vm_scratch_preemption_stale_load
