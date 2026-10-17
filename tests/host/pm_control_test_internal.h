#ifndef MICROS_TESTS_HOST_PM_CONTROL_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_PM_CONTROL_TEST_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/pm_control_core.h"
#include "kernel/pm_control_syscall_core.h"
#include "kernel/vm/vm_handoff_core.h"
#include "micros/frame_allocator.h"
#include "micros/frame_ownership.h"

struct pm_control_test_context {
    struct micros_frame_allocator runtime_allocator;
    struct micros_frame_ownership runtime_ownership;
    struct micros_vm_handoff_state runtime_vm;
};

struct pm_control_output_translation_fixture {
    uint64_t base;
    size_t call_count;
    size_t chunk_count;
    size_t chunk_sizes[3];
    size_t fault_call;
    unsigned char
        chunks[3][sizeof(struct micros_pm_reservation_result)];
};

struct pm_control_test_context *pm_control_test_context_get(void);

bool micros_pm_control_test_run(void);

uint32_t pm_control_test_process_endpoint(
    struct micros_process_handle process
);

bool pm_control_test_initialize_fixture(
    struct micros_kernel_objects *objects,
    struct micros_pm_control_state *state
);

enum micros_pm_control_output_error
pm_control_test_fake_output_translate(
    void *context,
    uint64_t user_address,
    size_t requested_size,
    uintptr_t *physical_address,
    size_t *contiguous_size
);

bool pm_control_test_output_chunks_are(
    const struct pm_control_output_translation_fixture *fixture,
    unsigned char value
);

void pm_control_test_initialize_output_fixture(
    struct pm_control_output_translation_fixture *fixture
);

bool test_contract_and_decode(void);
bool test_syscall_authority(void);
bool test_syscall_authority_resolution(void);
bool test_syscall_phase_and_retained_output(void);
bool test_state_preparation(void);
bool test_reserve_abort_lifecycle(void);
bool test_capacity_and_quarantine(void);
bool test_invariant_rejection(void);
bool test_runtime_accepts_exact_frozen_vm_reservation(
    struct pm_control_test_context *context
);

#endif
