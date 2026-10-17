#ifndef MICROS_KERNEL_VM_SCRATCH_PREEMPTION_TEST_FIXTURE_H
#define MICROS_KERNEL_VM_SCRATCH_PREEMPTION_TEST_FIXTURE_H

#include "kernel/boot/bootstrap_image.h"
#include "tests/qemu/vm_scratch_preemption_protocol.h"

extern const struct micros_bootstrap_manifest
    micros_vm_scratch_preemption_test_manifest;
extern const struct micros_bootstrap_image
    micros_vm_scratch_preemption_test_images[
        MICROS_VM_SCRATCH_PREEMPTION_SERVICE_COUNT
    ];
extern const uint64_t
    micros_vm_scratch_preemption_peer_hold_trap_address;
extern const uint64_t
    micros_vm_scratch_preemption_peer_hold_resume_address;
extern const uint64_t
    micros_vm_scratch_preemption_peer_run_trap_address;
extern const uint64_t
    micros_vm_scratch_preemption_peer_run_resume_address;
extern const uint64_t
    micros_vm_scratch_preemption_vm_hook_trap_address;
extern const uint64_t
    micros_vm_scratch_preemption_vm_hook_spin_address;
extern const uint64_t
    micros_vm_scratch_preemption_vm_hook_resume_address;
extern const uint64_t
    micros_vm_scratch_preemption_stale_load_fault_address;
extern const uint64_t
    micros_vm_scratch_preemption_stale_load_unexpected_address;

#endif
