#ifndef MICROS_KERNEL_PM_CONTROL_SYSCALL_INTERNAL_H
#define MICROS_KERNEL_PM_CONTROL_SYSCALL_INTERNAL_H

#include <stdint.h>

#include "kernel/boot/bootstrap_control_internal.h"
#include "kernel/pm/pm_control_runtime.h"
#include "kernel/pm/pm_control_syscall.h"
#include "kernel/pm/pm_control_syscall_core.h"
#include "kernel/vm/vm_handoff_core.h"
#include "micros/frame_ownership.h"

struct micros_pm_control_syscall_runtime {
    struct micros_hart *hart;
    struct micros_trap_frame *frame;
    const struct micros_syscall_context *context;
    struct micros_pm_control_state *state;
    struct micros_bootstrap_control_state *bootstrap;
    struct micros_endpoint_registry *registry;
    struct micros_kernel_objects *objects;
    struct micros_vm_handoff_state *handoff;
    const struct micros_frame_ownership *ownership;
};

_Noreturn void micros_pm_control_syscall_panic(
    const struct micros_pm_control_syscall_runtime *runtime,
    const char *reason
);

_Noreturn void micros_pm_control_syscall_fail(
    const struct micros_pm_control_syscall_runtime *runtime
);

enum micros_syscall_return micros_pm_control_syscall_return_result(
    const struct micros_pm_control_syscall_runtime *runtime,
    enum micros_syscall_abi_result result
);

enum micros_pm_control_output_error
micros_pm_control_syscall_prepare_output(
    const struct micros_pm_control_syscall_runtime *runtime,
    uint64_t user_address,
    struct micros_pm_control_output_plan *plan
);

enum micros_syscall_return
micros_pm_control_syscall_handle_reservation(
    const struct micros_pm_control_syscall_runtime *runtime,
    const struct micros_pm_control_request *request
);

enum micros_syscall_return
micros_pm_control_syscall_handle_execution(
    const struct micros_pm_control_syscall_runtime *runtime,
    const struct micros_pm_control_request *request
);

enum micros_syscall_return
micros_pm_control_syscall_handle_activation(
    const struct micros_pm_control_syscall_runtime *runtime,
    const struct micros_pm_control_request *request
);

enum micros_syscall_return micros_pm_control_syscall_handle_exit(
    const struct micros_pm_control_syscall_runtime *runtime,
    const struct micros_pm_control_request *request
);

#endif
