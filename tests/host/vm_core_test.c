#include "tests/host/vm_core_test_internal.h"
#include "tests/host/vm_service_test_internal.h"

#include <stdio.h>

int main(void)
{
    struct vm_core_test_context *context =
        vm_core_test_context_get();

    if (
        !test_record_abi(context)
        || !test_boot_import(context)
        || !test_load_lifecycle(context)
        || !test_abort_paths(context)
        || !test_capacity_and_invariants(context)
        || !test_staged_inputs_and_immutable_database(context)
        || !test_vm_service_configuration(context)
        || !test_vm_service_load_and_release(context)
        || !test_vm_service_abort_and_failure(context)
        || !test_vm_service_protocol_boundaries(context)
        || !test_vm_service_release_identity(context)
        || !test_vm_service_expected_load_rejection(context)
        || !test_vm_service_kernel_disagreement(context)
        || !test_vm_service_runtime_backend(context)
    ) {
        return 1;
    }
    puts("vm core tests passed");
    return 0;
}
