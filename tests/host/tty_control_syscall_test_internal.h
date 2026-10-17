#ifndef MICROS_TESTS_HOST_TTY_CONTROL_SYSCALL_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_TTY_CONTROL_SYSCALL_TEST_INTERNAL_H

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/bootstrap_runtime.h"
#include "kernel/ipc/ipc_runtime_internal.h"
#include "kernel/platform/plic.h"
#include "kernel/tty_control_syscall.h"
#include "kernel/tty_control_syscall_core.h"
#include "kernel/tty_fault.h"
#include "kernel/tty_handoff_runtime.h"
#include "kernel/memory/user_address_space_internal.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"

struct tty_control_syscall_test_fixture {
    struct micros_bootstrap_control_state bootstrap;
    struct micros_endpoint_registry registry;
    struct micros_kernel_objects objects;
    struct micros_tty_handoff_runtime_state tty_state;
    struct micros_syscall_context syscall_context;
    enum micros_tty_control_authority_result authority_result;
    enum micros_plic_phase plic_phase;
    bool runtime_valid;
    bool mapping_valid;
    bool uart_active;
    bool irq_enabled;
    bool timer_enabled;
    bool external_enabled;
    bool plic_prepare_valid;
    bool order_violation;
    unsigned int authority_calls;
    unsigned int owner_fault_count;
    int operation_stage;
    jmp_buf panic_target;
    bool panic_armed;
};

struct tty_control_syscall_test_fixture *tty_control_syscall_test_fixture(void);

struct micros_syscall_arguments tty_control_syscall_test_commit_arguments(
    struct tty_control_syscall_test_fixture *fixture
);

struct micros_syscall_arguments tty_control_syscall_test_complete_arguments(void);

enum micros_syscall_return tty_control_syscall_test_invoke(
    struct tty_control_syscall_test_fixture *fixture,
    struct micros_syscall_arguments *arguments,
    struct micros_trap_frame *frame
);

void tty_control_syscall_test_reset_fixture(
    struct tty_control_syscall_test_fixture *fixture,
    bool in_service
);

#define EXPECT_TRUE(expression) \
    do { \
        if (!(expression)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %s\n", \
                __FILE__, \
                __LINE__, \
                #expression \
            ); \
            return false; \
        } \
    } while (false)

#endif
