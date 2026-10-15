#include "endpoint_test_internal.h"

#include "arch/riscv64/interrupt.h"

bool micros_endpoint_runtime_run_self_test(void)
{
    struct endpoint_qemu_work work;
    uintptr_t saved_status;
    bool passed = false;

    saved_status = riscv_irq_save();
    if (!endpoint_qemu_capture_baseline(&work)) {
        goto done;
    }
    if (
        !endpoint_qemu_initialize_runtimes(&work)
        || !endpoint_qemu_reserve_clients(&work)
    ) {
        goto done;
    }
    if (!endpoint_qemu_require_held_owners(&work)) {
        goto done;
    }
    if (!endpoint_qemu_publish_and_check_authority(&work)) {
        goto done;
    }
    if (!endpoint_qemu_prepare_grant_cancellation(&work)) {
        goto done;
    }
    if (!endpoint_qemu_close_commit_and_reuse(&work)) {
        goto done;
    }
    if (!endpoint_qemu_reject_stale_and_cleanup(&work)) {
        goto done;
    }
    passed = true;

done:
    riscv_irq_restore(saved_status);
    return passed;
}
