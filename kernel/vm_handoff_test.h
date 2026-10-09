#ifndef MICROS_KERNEL_VM_HANDOFF_TEST_H
#define MICROS_KERNEL_VM_HANDOFF_TEST_H

struct micros_hart;
struct micros_trap_frame;

_Noreturn void micros_vm_handoff_test_launch(void);

_Noreturn void micros_vm_handoff_test_handle_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

#endif
