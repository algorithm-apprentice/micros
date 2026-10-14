#include "bootstrap_manifest_test_internal.h"

int main(void)
{
    if (!test_manifest_contract()) {
        fprintf(stderr, "manifest contract failed\n");
        return 1;
    }
    if (!test_manifest_validation()) {
        fprintf(stderr, "manifest validation failed\n");
        return 1;
    }
    if (!test_manifest_rejections()) {
        fprintf(stderr, "manifest rejections failed\n");
        return 1;
    }
    if (!test_vm_mapping_capacity()) {
        fprintf(stderr, "VM mapping capacity failed\n");
        return 1;
    }
    if (!test_three_service_topology()) {
        fprintf(stderr, "three-service topology failed\n");
        return 1;
    }
    if (!test_tty_service_contract()) {
        fprintf(stderr, "TTY service contract failed\n");
        return 1;
    }
    if (!test_runtime_transitions()) {
        fprintf(stderr, "runtime transitions failed\n");
        return 1;
    }
    if (!test_retained_runtime_deadline()) {
        fprintf(stderr, "retained runtime deadline failed\n");
        return 1;
    }
    if (!test_replayable_runtime_model()) {
        fprintf(stderr, "runtime model failed\n");
        return 1;
    }
    return 0;
}
