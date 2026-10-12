#include "tests/host/pm_test_internal.h"

bool micros_pm_control_test_run(void);
bool micros_pm_control_execution_test_run(void);

int main(void)
{
    if (
        !micros_pm_control_test_run()
        || !micros_pm_control_execution_test_run()
        || !test_protocol_contract()
        || !test_runtime_gate_and_spawn()
        || !test_reverse_rollback_and_identity()
        || !test_rollback_acknowledgment_identity()
        || !test_generation_and_pid_exhaustion()
        || !test_failed_init_is_terminal()
        || !test_endpoint_lookup_and_failure_preservation()
        || !test_orphaned_lifecycle_is_invalid()
        || !test_exit_wait_and_reply_restore()
        || !test_wait_selection()
        || !test_parent_exit_reparents_and_aborts()
        || !test_nested_parent_exit_stages_independent_waits()
        || !test_replayable_transition_model()
    ) {
        return 1;
    }
    return 0;
}
