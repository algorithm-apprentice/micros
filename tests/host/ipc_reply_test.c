#include "ipc_reply_test_internal.h"

bool micros_ipc_reply_test_run(void)
{
    struct ipc_reply_test_context *fixture = ipc_reply_test_context_get();
    static const struct {
        const char *name;
        bool (*run)(struct ipc_reply_test_context *);
    } tests[] = {
        {
            "staged call tokens are unique",
            test_staged_call_tokens_are_unique,
        },
        {
            "reply requires request return",
            test_reply_requires_request_return,
        },
        {
            "reply routes exact tokens without send target",
            test_reply_routes_exact_tokens_without_send_target,
        },
        {
            "reply preserves independent caller flags",
            test_reply_preserves_independent_caller_flags,
        },
        {
            "reply rejections are atomic",
            test_reply_rejections_are_atomic,
        },
        {
            "reply scheduler failure preserves token",
            test_reply_scheduler_failure_preserves_token,
        },
        {
            "reply rejects source-only caller",
            test_reply_rejects_source_only_caller,
        },
        {
            "reply receive blocks and later receives",
            test_reply_receive_blocks_and_later_receives,
        },
        {
            "reply receive selects specific sender atomically",
            test_reply_receive_selects_specific_sender_atomically,
        },
        {
            "reply receive requires combined operation",
            test_reply_receive_requires_combined_operation,
        },
        {
            "reply receive rejections are atomic",
            test_reply_receive_rejections_are_atomic,
        },
        {
            "reply receive scheduler failures are atomic",
            test_reply_receive_scheduler_failures_are_atomic,
        },
    };
    size_t index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run(fixture)) {
            fprintf(
                stderr,
                "reply subtest failed: %s\n",
                tests[index].name
            );
            return false;
        }
    }
    return true;
}
