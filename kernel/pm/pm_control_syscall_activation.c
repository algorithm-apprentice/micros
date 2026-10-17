#include "kernel/pm/pm_control_syscall_internal.h"

enum micros_syscall_return
micros_pm_control_syscall_handle_activation(
    const struct micros_pm_control_syscall_runtime *runtime,
    const struct micros_pm_control_request *request
)
{
    struct micros_pm_control_activate_plan activate;
    enum micros_pm_control_error control_error;

    control_error = micros_pm_control_activate_preflight(
        runtime->state,
        runtime->objects,
        runtime->registry,
        runtime->handoff,
        request->transaction,
        request->load_token,
        request->mapping_generation,
        request->prepared_token,
        &activate
    );
    if (control_error == MICROS_PM_CONTROL_ERROR_STATE) {
        return micros_pm_control_syscall_return_result(
            runtime,
            MICROS_SYSCALL_ABI_STATE
        );
    }
    if (control_error != MICROS_PM_CONTROL_OK) {
        micros_pm_control_syscall_fail(runtime);
    }
    micros_pm_control_activate_commit_prevalidated(
        runtime->state,
        runtime->objects,
        runtime->registry,
        runtime->handoff,
        &activate
    );
    return micros_pm_control_syscall_return_result(
        runtime,
        MICROS_SYSCALL_ABI_OK
    );
}
