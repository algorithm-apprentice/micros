#ifndef MICROS_VM_COMMAND_WORK_H
#define MICROS_VM_COMMAND_WORK_H

#include "tests/qemu/vm_validation_work.h"

#if defined(MICROS_TEST_VM_COMMAND_COUNTS)
#include "kernel/vm/vm_handoff_syscall.h"

enum micros_syscall_return micros_vm01_dispatch(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_syscall_arguments *arguments
);
bool micros_vm01_work_finish(void);
bool micros_vm01_work_report(void);

#define MICROS_VM01_DISPATCH micros_vm01_dispatch
#define MICROS_VM01_FINISH() micros_vm01_work_finish()
#define MICROS_VM01_REPORT() micros_vm01_work_report()
#else
#define MICROS_VM01_DISPATCH micros_vm_handoff_handle_captured_user_ecall
#define MICROS_VM01_FINISH() true
#define MICROS_VM01_REPORT() true
#endif

#endif
