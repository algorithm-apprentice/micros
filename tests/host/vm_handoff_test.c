#include "vm_handoff_test_internal.h"

bool micros_vm_handoff_test_run(void);

bool micros_vm_handoff_test_run(void)
{
    return (
        vm_handoff_test_test_decodes_exact_shape()
        && vm_handoff_test_test_decodes_exact_tty_mapping()
        && vm_handoff_test_test_decodes_load_commands()
        && vm_handoff_test_test_prepares_matches_and_commits()
        && vm_handoff_test_test_rejects_invalid_state_without_mutation()
        && vm_handoff_test_test_load_transaction_lifecycle()
        && vm_handoff_test_test_load_abort_before_and_after_freeze()
        && vm_handoff_test_test_load_failures_preserve_state()
        && vm_handoff_test_test_freeze_stack_boundaries()
        && vm_handoff_test_test_rejects_oversized_later_mapping_record()
        && vm_handoff_test_test_classifies_vm_self_fault_phase()
    );
}
