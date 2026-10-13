#include "ipc_notify_test_internal.h"

bool micros_ipc_notify_test_run(void)
{
    struct ipc_notify_test_context *fixture = ipc_notify_test_context_get();
    static const struct {
        const char *name;
        bool (*run)(struct ipc_notify_test_context *);
    } tests[] = {
        {
            "notify coalesces and never blocks",
            test_notify_coalesces_and_never_blocks,
        },
        {
            "kernel notify coalesces and any receives",
            test_kernel_notify_coalesces_and_any_receives,
        },
        {
            "kernel notify wakes only any receiver",
            test_kernel_notify_wakes_only_any_receiver,
        },
        {
            "kernel notify prepare commit is retained",
            test_kernel_notify_prepare_commit_is_retained,
        },
        {
            "staged kernel notification is detected",
            test_staged_kernel_notification_is_detected,
        },
        {
            "kernel notify rejections are atomic",
            test_kernel_notify_rejections_are_atomic,
        },
        {
            "kernel notify validation and close rules",
            test_kernel_notify_validation_and_close_rules,
        },
        {
            "notify wakes first matching receiver",
            test_notify_wakes_first_matching_receiver,
        },
        {
            "call reply wait excludes notification",
            test_call_reply_wait_does_not_consume_notification,
        },
        {
            "receive prefers lowest notification before sender",
            test_receive_prefers_lowest_notification_before_sender,
        },
        {
            "reply receive prefers pending notification",
            test_reply_receive_prefers_pending_notification,
        },
        {
            "reply receive pending failure is atomic",
            test_reply_receive_pending_failure_is_atomic,
        },
        {
            "notification validators and close preflight",
            test_notification_validators_and_close_preflight,
        },
        {
            "notify rejections preserve state",
            test_notify_rejections_preserve_state,
        },
        {
            "notify scheduler failure is atomic",
            test_notify_scheduler_failure_is_atomic,
        },
    };
    size_t index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run(fixture)) {
            fprintf(
                stderr,
                "notify subtest %zu failed - %s\n",
                index + 1,
                tests[index].name
            );
            return false;
        }
    }
    return true;
}

bool micros_ipc_notify_model_test_run(void)
{
    struct ipc_notify_test_context *fixture = ipc_notify_test_context_get();
    return test_seeded_notify_model(fixture);
}
