#ifndef MICROS_KERNEL_VM_SCRATCH_PREEMPTION_TEST_INTERNAL_H
#define MICROS_KERNEL_VM_SCRATCH_PREEMPTION_TEST_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "kernel/boot/bootstrap_runtime.h"
#include "kernel/pm/pm_control_core.h"
#include "kernel/vm/vm_handoff_core.h"
#include "kernel/tests/vm/vm_scratch_preemption_test.h"
#include "kernel/tests/vm/vm_scratch_preemption_test_fixture.h"

enum {
    MICROS_TEST_EXCEPTION_BREAKPOINT = 3,
    MICROS_TEST_EXCEPTION_LOAD_PAGE_FAULT = 13,
};

enum scratch_preemption_phase {
    SCRATCH_PREEMPTION_IDLE = 0,
    SCRATCH_PREEMPTION_WAITING_PEER_HOLD,
    SCRATCH_PREEMPTION_PEER_HELD,
    SCRATCH_PREEMPTION_WAITING_TIMER,
    SCRATCH_PREEMPTION_PEER_PREEMPTED,
    SCRATCH_PREEMPTION_WAITING_VM_REACTIVATION,
    SCRATCH_PREEMPTION_VM_REACTIVATED,
    SCRATCH_PREEMPTION_WAITING_STALE_FAULT,
};

struct vm_scratch_preemption_test_context {
    volatile enum scratch_preemption_phase test_phase;
    bool timer_observed;
    uint64_t scratch_physical_address;
    uint64_t scratch_load_token;
    struct micros_process_handle scratch_process;
};

extern const struct micros_bootstrap_runtime_config
    vm_scratch_test_runtime_config;
struct vm_scratch_preemption_test_context *vm_scratch_test_context_get(void);
_Noreturn void vm_scratch_test_failure(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const char *reason
);
uint64_t vm_scratch_test_read_satp(void);
uint64_t vm_scratch_test_expected_satp(uint64_t root);
bool vm_scratch_test_current_is(
    const struct micros_hart *hart,
    const struct micros_bootstrap_binding *binding
);
bool vm_scratch_test_runtime_is_valid(
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_vm_handoff_state *handoff,
    const struct micros_pm_control_state *control,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
);
_Noreturn void vm_scratch_test_report_success(
    const struct vm_scratch_preemption_test_context *context,
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

#endif
