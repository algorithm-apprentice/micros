#ifndef MICROS_VM_HANDOFF_TEST_INTERNAL_H
#define MICROS_VM_HANDOFF_TEST_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "kernel/vm/vm_handoff_core.h"
#include "micros/sv39.h"

struct micros_vm_boot_summary vm_handoff_test_valid_summary(void);
struct micros_syscall_arguments vm_handoff_test_valid_arguments(void);

struct micros_syscall_arguments
vm_handoff_test_valid_tty_mapping_arguments(void);

struct micros_process_handle vm_handoff_test_dynamic_process(void);
struct micros_vm_handoff_state vm_handoff_test_handed_off_state(void);

struct micros_vm_load_freeze_plan vm_handoff_test_valid_freeze_plan(
    const struct micros_vm_handoff_state *state
);

bool vm_handoff_test_decode_preserves_output(
    struct micros_syscall_arguments arguments
);

bool vm_handoff_test_tty_mapping_decode_preserves_output(
    struct micros_syscall_arguments arguments
);

bool vm_handoff_test_append_page(
    struct micros_vm_handoff_state *state,
    uint64_t virtual_address,
    uint64_t physical_address,
    uint32_t permissions
);

bool vm_handoff_test_begin_and_fill_load(
    struct micros_vm_handoff_state *state,
    uint64_t reservation_token
);

bool vm_handoff_test_test_decodes_exact_shape(void);
bool vm_handoff_test_test_decodes_exact_tty_mapping(void);
bool vm_handoff_test_test_decodes_load_commands(void);
bool vm_handoff_test_test_prepares_matches_and_commits(void);
bool vm_handoff_test_test_rejects_invalid_state_without_mutation(void);
bool vm_handoff_test_test_classifies_vm_self_fault_phase(void);
bool vm_handoff_test_test_load_transaction_lifecycle(void);
bool vm_handoff_test_test_load_abort_before_and_after_freeze(void);
bool vm_handoff_test_test_load_failures_preserve_state(void);
bool vm_handoff_test_test_freeze_stack_boundaries(void);
bool vm_handoff_test_test_rejects_oversized_later_mapping_record(void);

#endif
