#include "grant_test_internal.h"

bool micros_grant_test_run(void)
{
    static const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {"grant token boundaries", grant_test_token_boundaries},
        {
            "grant create inspect revoke reuse",
            grant_test_create_inspect_revoke_and_reuse,
        },
        {
            "grant create failures and capacity",
            grant_test_create_failures_and_capacity,
        },
        {
            "grant terminal generation quarantines",
            grant_test_terminal_generation_quarantines,
        },
        {
            "grant endpoint cancellation and reuse",
            grant_test_endpoint_cancellation_and_reuse,
        },
        {
            "failed IPC close preserves cancel plan",
            grant_test_failed_ipc_close_preserves_cancel_plan,
        },
        {
            "grant error precedence and outputs",
            grant_test_error_precedence_and_output_preservation,
        },
        {
            "grant validator rejects corruption",
            grant_test_validator_rejects_corruption,
        },
    };
    size_t index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run()) {
            fprintf(
                stderr,
                "grant subtest %zu failed - %s\n",
                index + 1,
                tests[index].name
            );
            return false;
        }
    }
    return true;
}
