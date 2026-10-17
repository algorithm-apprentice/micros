#ifndef MICROS_KERNEL_TRAP_INTERNAL_H
#define MICROS_KERNEL_TRAP_INTERNAL_H

#include "kernel/trap/syscall.h"
#include "kernel/trap/trap_route_core.h"
#include "micros/trap.h"

#define MICROS_SCAUSE_INTERRUPT (UINT64_C(1) << 63)
#define MICROS_SCAUSE_CODE_MASK (MICROS_SCAUSE_INTERRUPT - 1)

enum {
    MICROS_EXCEPTION_ILLEGAL_INSTRUCTION = 2,
    MICROS_EXCEPTION_USER_ECALL = 8,
    MICROS_EXCEPTION_INSTRUCTION_PAGE_FAULT = 12,
    MICROS_EXCEPTION_LOAD_PAGE_FAULT = 13,
    MICROS_EXCEPTION_STORE_PAGE_FAULT = 15,
};

uintptr_t trap_read_sscratch(void);
uintptr_t trap_read_stvec(void);
uintptr_t trap_read_tp(void);
#if defined(MICROS_BUILD_TRAP_TEST) \
    || defined(MICROS_BUILD_NESTED_TRAP_TEST)
uintptr_t trap_read_sstatus(void);
#endif
bool trap_frame_is_on_primary_stack(
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame
);
struct micros_hart *trap_resolve_trap_hart(
    const struct micros_trap_frame *frame
);

void trap_fail_active_bootstrap_service_trap(struct micros_hart *hart);
void trap_record_tty_owner_failure(void);
bool trap_handle_vm_self_fault(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    uint64_t cause_code
);

void trap_handle_user_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_trap_interrupt_route interrupt_route,
    uint64_t cause_code
);
void trap_handle_user_external(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);
void trap_handle_user_timer(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);
bool trap_handle_supervisor_interrupt(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_trap_interrupt_route interrupt_route
);
void trap_handle_user_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);
bool trap_handle_supervisor_test(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

void trap_test_hooks_pre_capture(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);
bool trap_test_hooks_scratch_fault(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer,
    bool user_external,
    uint64_t cause_code
);
bool trap_test_hooks_ramfs_fault(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer,
    bool user_external,
    uint64_t cause_code
);
void trap_test_hooks_vfs_fault(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer,
    bool user_external,
    uint64_t cause_code
);
bool trap_test_hooks_user_faults(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer,
    uint64_t cause_code
);
bool trap_test_hooks_control_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer,
    uint64_t cause_code
);
bool trap_test_hooks_address_space_handoff(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer,
    uint64_t cause_code
);
void trap_test_hooks_before_user_timer(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);
void trap_test_hooks_after_user_timer(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);
void trap_test_hooks_before_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);
void trap_test_hooks_after_dispatch(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_syscall_return syscall_return
);
void trap_test_hooks_after_syscall_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_syscall_return syscall_return
);
void trap_test_hooks_after_user_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer
);
#if defined(MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST)
void trap_test_hooks_invalid_user_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer
);
#elif defined(MICROS_BUILD_SCHEDULER_TEST)
bool trap_test_hooks_scheduler_user_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer
);
#elif defined(MICROS_BUILD_USER_EXECUTION_TEST)
void trap_test_hooks_user_execution_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);
#endif
bool trap_test_hooks_supervisor_faults(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

#endif
