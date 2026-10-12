#include "tests/host/pm_control_execution_test_internal.h"

bool micros_pm_control_execution_test_run(void)
{
    struct pm_control_execution_test_context *context =
        pm_control_execution_test_context_get();

    return (
        test_prepare_abort_activate(context)
        && test_prepared_corruption_is_invariant(context)
        && test_token_exhaustion_is_invariant(context)
        && test_exit_begin_detach_release(context)
    );
}
