#include "ipc_close_test_internal.h"

bool micros_ipc_close_test_run(void)
{
    struct ipc_close_test_context *fixture = ipc_close_test_context_get();
    static const struct {
        const char *name;
        bool (*run)(struct ipc_close_test_context *);
    } tests[] = {
        {
            "complete cancellation transaction",
            test_close_cancels_complete_transaction,
        },
        {
            "cascading close discards owned dead-endpoint result",
            test_cascading_close_discards_owned_dead_endpoint_result,
        },
        {
            "source-only close cancels queued sender",
            test_source_only_close_cancels_queued_sender,
        },
        {
            "source-only notification dependency blocks strict close",
            test_source_only_notification_dependency_blocks_strict_close,
        },
        {
            "successful staged deliveries block close atomically",
            test_successful_staged_deliveries_block_close_atomically,
        },
        {
            "stale corrupt and late failures are atomic",
            test_close_failures_are_atomic,
        },
    };
    size_t index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run(fixture)) {
            fprintf(
                stderr,
                "IPC close subtest failed: %s\n",
                tests[index].name
            );
            return false;
        }
    }
    return true;
}

bool micros_ipc_close_model_test_run(void)
{
    struct ipc_close_test_context *fixture = ipc_close_test_context_get();
    return test_seeded_close_scenarios(fixture);
}
