#include "tests/host/vfs_model_internal.h"

#include <stdio.h>

static struct vfs_model model;

int main(void) {
    size_t previous_transition_checks;

    if (!vfs_model_initialize(&model)) {
        fputs("VFS model initialization failed\n", stderr);
        return 1;
    }
    for (model.step = 0; model.step < MODEL_STEP_COUNT; ++model.step) {
        previous_transition_checks = model.transition_checks;
        if (!vfs_model_execute_step(&model)) {
            return 1;
        }
        if (model.transition_checks == previous_transition_checks) {
            model_failure(&model, "each step compares at least one core transition", __LINE__);
            return 1;
        }
    }
    --model.step;
    if (!vfs_model_coverage_is_complete(&model)) {
        return 1;
    }
    printf("VFS_MODEL_TEST_PASS seed=0x%016llx steps=%u\n", (unsigned long long)model.seed,
           MODEL_STEP_COUNT);
    return 0;
}
