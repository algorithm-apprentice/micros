.section .text, "ax", @progbits
.balign 4
.globl micros_vm_scratch_preemption_peer_hold
.type micros_vm_scratch_preemption_peer_hold, @function
micros_vm_scratch_preemption_peer_hold:
.globl micros_vm_scratch_preemption_peer_hold_trap
micros_vm_scratch_preemption_peer_hold_trap:
    ebreak
.globl micros_vm_scratch_preemption_peer_hold_resume
micros_vm_scratch_preemption_peer_hold_resume:
    ret
.size micros_vm_scratch_preemption_peer_hold, . - micros_vm_scratch_preemption_peer_hold

.balign 4
.globl micros_vm_scratch_preemption_peer_run
.type micros_vm_scratch_preemption_peer_run, @function
micros_vm_scratch_preemption_peer_run:
.globl micros_vm_scratch_preemption_peer_run_trap
micros_vm_scratch_preemption_peer_run_trap:
    ebreak
.globl micros_vm_scratch_preemption_peer_run_resume
micros_vm_scratch_preemption_peer_run_resume:
    ret
.size micros_vm_scratch_preemption_peer_run, . - micros_vm_scratch_preemption_peer_run
