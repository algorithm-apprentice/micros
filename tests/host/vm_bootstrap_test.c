#include "vm_bootstrap_test_internal.h"

bool micros_vm_handoff_test_run(void);

int main(void)
{
    struct vm_bootstrap_test_context *context =
        vm_bootstrap_test_context();

    return (
        vm_bootstrap_test_test_abi_contract(context)
        && vm_bootstrap_test_test_valid_snapshot(context)
        && vm_bootstrap_test_test_finalize_is_failure_atomic(context)
        && vm_bootstrap_test_test_rejections(context)
        && vm_bootstrap_test_test_misaligned_storage(context)
        && vm_bootstrap_test_test_builds_from_authoritative_state(context)
        && vm_bootstrap_test_test_build_failure_clears_candidate(context)
        && micros_vm_handoff_test_run()
    )
        ? 0
        : 1;
}
