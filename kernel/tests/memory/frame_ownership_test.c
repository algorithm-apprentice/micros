#include "kernel/tests/memory/frame_ownership_test_internal.h"

#include <stdbool.h>

#include "arch/riscv64/interrupt.h"

bool micros_frame_ownership_runtime_run_self_test(void);

bool micros_frame_ownership_runtime_run_self_test(void)
{
    struct micros_frame_ownership_test_context *context;
    uintptr_t saved_status;
    bool passed;

    saved_status = riscv_irq_save();
    context = micros_frame_ownership_test_context();
    passed = (
        micros_frame_ownership_test_prepare_runtime_baseline(context)
        && micros_frame_ownership_test_prepare_process_generations(context)
        && micros_frame_ownership_test_prepare_owner_records(context)
        && micros_frame_ownership_test_allocate_owned_frames(context)
        && micros_frame_ownership_test_verify_bootstrap_rejections(
            context
        )
        && micros_frame_ownership_test_verify_owner_lookups(context)
        && micros_frame_ownership_test_verify_registry_mismatch_rejections(
            context
        )
        && micros_frame_ownership_test_verify_handoff_rejections(context)
        && micros_frame_ownership_test_commit_handoff(context)
        && micros_frame_ownership_test_verify_handoff_state(context)
        && micros_frame_ownership_test_verify_sealed_rejections(context)
        && micros_frame_ownership_test_verify_final_state(context)
    );
    riscv_irq_restore(saved_status);
    return passed;
}
