#ifndef MICROS_BOOTSTRAP_COMMAND_WORK_H
#define MICROS_BOOTSTRAP_COMMAND_WORK_H

#if defined(MICROS_TEST_BOOTSTRAP_COMMAND_COUNTS)
#if MICROS_TEST_BOOTSTRAP_COMMAND_COUNTS != 1 \
    || !defined(MICROS_TEST_WORK_COUNTS) || MICROS_TEST_WORK_COUNTS != 1
#error "bootstrap command observations require the test-owned collector"
#endif

#include "kernel/boot/bootstrap_syscall.h"

enum micros_syscall_return micros_boot02_dispatch(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_syscall_arguments *arguments
);
bool micros_boot02_work_report(void);

#define MICROS_BOOT02_DISPATCH micros_boot02_dispatch
#define MICROS_BOOT02_REPORT() micros_boot02_work_report()
#else
#define MICROS_BOOT02_DISPATCH micros_bootstrap_handle_captured_user_ecall
#define MICROS_BOOT02_REPORT() true
#endif

#endif
