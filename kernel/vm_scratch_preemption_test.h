#ifndef MICROS_KERNEL_VM_SCRATCH_PREEMPTION_TEST_H
#define MICROS_KERNEL_VM_SCRATCH_PREEMPTION_TEST_H

#include <stdbool.h>

#include "kernel/trap/syscall.h"

struct micros_hart;
struct micros_trap_frame;

enum micros_vm_scratch_preemption_test_trap_result {
    MICROS_VM_SCRATCH_PREEMPTION_TEST_TRAP_USER_RETURN = 0,
    MICROS_VM_SCRATCH_PREEMPTION_TEST_TRAP_MISMATCH,
};

_Noreturn void micros_vm_scratch_preemption_test_launch(void);

enum micros_vm_scratch_preemption_test_trap_result
micros_vm_scratch_preemption_test_handle_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

bool micros_vm_scratch_preemption_test_before_user_timer(
    const struct micros_hart *hart,
    struct micros_trap_frame *frame
);

bool micros_vm_scratch_preemption_test_after_user_timer(
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame
);

bool micros_vm_scratch_preemption_test_after_syscall_return(
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame,
    enum micros_syscall_return syscall_return
);

#endif
