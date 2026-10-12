#include "tests/host/pm_control_test_internal.h"

bool micros_pm_control_test_run(void)
{
    struct pm_control_test_context *context =
        pm_control_test_context_get();

    return (
        test_contract_and_decode()
        && test_syscall_authority()
        && test_syscall_authority_resolution()
        && test_syscall_phase_and_retained_output()
        && test_state_preparation()
        && test_reserve_abort_lifecycle()
        && test_capacity_and_quarantine()
        && test_invariant_rejection()
        && test_runtime_accepts_exact_frozen_vm_reservation(
            context
        )
    );
}
