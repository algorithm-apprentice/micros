#ifndef MICROS_TESTS_HOST_PM_CONTROL_EXECUTION_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_PM_CONTROL_EXECUTION_TEST_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "kernel/pm/pm_control_core.h"
#include "kernel/vm/vm_handoff_core.h"
#include "micros/endpoint.h"
#include "micros/frame_allocator.h"
#include "micros/frame_ownership.h"
#include "micros/sv39.h"

struct pm_control_execution_test_context {
    struct micros_kernel_objects objects;
    struct micros_endpoint_registry endpoints;
    struct micros_pm_control_state control;
    struct micros_vm_handoff_state vm;
    struct micros_frame_allocator allocator;
    struct micros_frame_ownership ownership;
    struct micros_process_handle child;
    struct micros_process_handle pm_process;
    struct micros_thread_handle pm_thread;
    struct micros_hart_handle hart;
    uint32_t pm_endpoint;
    uint64_t reservation_token;
    uint64_t load_token;
    uint64_t mapping_generation;
    _Alignas(MICROS_SV39_PAGE_SIZE)
    unsigned char kernel_stack[MICROS_THREAD_KERNEL_STACK_SIZE];
};

struct pm_control_execution_test_context *
pm_control_execution_test_context_get(void);

bool micros_pm_control_execution_test_run(void);

uint32_t pm_control_execution_test_process_endpoint(
    struct micros_process_handle process
);

bool pm_control_execution_test_initialize_fixture(
    struct pm_control_execution_test_context *context
);

struct micros_pm_control_execution_binding
pm_control_execution_test_binding(
    const struct pm_control_execution_test_context *context,
    const struct micros_pm_control_prepare_plan *plan
);

bool pm_control_execution_test_activate_child(
    struct pm_control_execution_test_context *context,
    struct micros_thread_handle *thread
);

bool pm_control_execution_test_block_child_in_exit_call(
    struct pm_control_execution_test_context *context,
    struct micros_thread_handle thread_handle
);

bool test_prepare_abort_activate(
    struct pm_control_execution_test_context *context
);

bool test_prepared_corruption_is_invariant(
    struct pm_control_execution_test_context *context
);

bool test_token_exhaustion_is_invariant(
    struct pm_control_execution_test_context *context
);

bool test_exit_begin_detach_release(
    struct pm_control_execution_test_context *context
);


#endif
