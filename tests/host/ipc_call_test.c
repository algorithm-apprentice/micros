#include "ipc_call_test_internal.h"

bool micros_ipc_call_test_run(void)
{
    struct ipc_call_test_context *fixture = ipc_call_test_context_get();
    static const struct {
        const char *name;
        bool (*run)(struct ipc_call_test_context *);
    } tests[] = {
        {
            "call delivers to waiting receiver",
            test_call_delivers_to_waiting_receiver,
        },
        {
            "call preserves independent receiver flags",
            test_call_preserves_independent_receiver_flags,
        },
        {
            "queued calls bind exact threads and deliver FIFO",
            test_queued_calls_bind_exact_threads_and_deliver_fifo,
        },
        {
            "low-level enqueue rejects reply tokens",
            test_low_level_enqueue_rejects_reply_tokens,
        },
        {
            "token exhaustion is preflighted",
            test_token_exhaustion_is_preflighted,
        },
        {
            "call rejections are atomic",
            test_call_rejections_are_atomic,
        },
        {
            "staged call token binding is validated",
            test_staged_call_token_binding_is_validated,
        },
        {
            "allocator validator rejects unissued token",
            test_allocator_validator_rejects_unissued_token,
        },
    };
    size_t index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run(fixture)) {
            fprintf(
                stderr,
                "IPC call subtest failed: %s\n",
                tests[index].name
            );
            return false;
        }
    }
    return true;
}
