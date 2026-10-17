#include "kernel/tests/ipc/ipc_test_internal.h"

bool micros_ipc_runtime_run_self_test(void)
{
    struct ipc_test_context *fixture = ipc_test_context_get();
    struct ipc_test_run_state state;

    state.passed = false;
    state.failure_stage = 1;
    state.saved_status = riscv_irq_save();
    state.passed =
        ipc_test_prepare_environment(fixture, &state)
        && ipc_test_immediate_send(fixture, &state)
        && ipc_test_blocked_send(fixture, &state)
        && ipc_test_call_reply_and_stale_token(fixture, &state)
        && ipc_test_prepare_combined_reply(fixture, &state)
        && ipc_test_reply_receive_and_peer_send(fixture, &state)
        && ipc_test_finish_combined_delivery(fixture, &state)
        && ipc_test_notification_reply_exclusion(fixture, &state)
        && ipc_test_kernel_notifications(fixture, &state)
        && ipc_test_deadlock_and_matching_recovery(fixture, &state)
        && ipc_test_close_cancellation(fixture, &state)
        && ipc_test_generation_reuse_and_stale_identity(fixture, &state)
        && ipc_test_cleanup_resources(fixture, &state)
        && ipc_test_validate_final_baseline(fixture, &state);
    return ipc_test_report_result(&state);
}
