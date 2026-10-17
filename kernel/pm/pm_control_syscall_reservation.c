#include "kernel/pm/pm_control_syscall_internal.h"

#include "kernel/pm/pm_control_runtime_core.h"

static enum micros_syscall_return handle_reserve(
    const struct micros_pm_control_syscall_runtime *runtime,
    const struct micros_pm_control_request *request
)
{
    struct micros_pm_control_reserve_plan reserve;
    struct micros_pm_control_output_plan output;
    enum micros_pm_control_output_error output_error;
    enum micros_pm_control_error control_error;

    control_error = micros_pm_control_runtime_reserve_preflight(
        runtime->state,
        runtime->handoff
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
    output_error = micros_pm_control_syscall_prepare_output(
        runtime,
        request->output_address,
        &output
    );
    if (output_error == MICROS_PM_CONTROL_OUTPUT_ERROR_MEMORY_FAULT) {
        return micros_pm_control_syscall_return_result(
            runtime,
            MICROS_SYSCALL_ABI_MEMORY_FAULT
        );
    }
    if (output_error != MICROS_PM_CONTROL_OUTPUT_OK) {
        micros_pm_control_syscall_fail(runtime);
    }
    control_error = micros_pm_control_reserve_preflight(
        runtime->state,
        runtime->objects,
        &reserve
    );
    if (control_error == MICROS_PM_CONTROL_ERROR_CAPACITY) {
        return micros_pm_control_syscall_return_result(
            runtime,
            MICROS_SYSCALL_ABI_CAPACITY
        );
    }
    if (control_error == MICROS_PM_CONTROL_ERROR_STATE) {
        return micros_pm_control_syscall_return_result(
            runtime,
            MICROS_SYSCALL_ABI_STATE
        );
    }
    if (control_error != MICROS_PM_CONTROL_OK) {
        micros_pm_control_syscall_fail(runtime);
    }
    micros_pm_control_reserve_commit_prevalidated(
        runtime->state,
        runtime->objects,
        &reserve
    );
    micros_pm_control_output_commit_prevalidated(
        &output,
        &reserve.result
    );
    return micros_pm_control_syscall_return_result(
        runtime,
        MICROS_SYSCALL_ABI_OK
    );
}

static enum micros_syscall_return handle_abort_reserved(
    const struct micros_pm_control_syscall_runtime *runtime,
    const struct micros_pm_control_request *request
)
{
    struct micros_pm_control_abort_plan abort;
    enum micros_pm_control_error control_error;

    control_error = micros_pm_control_runtime_abort_preflight(
        runtime->state,
        runtime->handoff
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
    control_error = micros_pm_control_abort_preflight(
        runtime->state,
        runtime->objects,
        request->transaction,
        &abort
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
    micros_pm_control_abort_commit_prevalidated(
        runtime->state,
        runtime->objects,
        &abort
    );
    return micros_pm_control_syscall_return_result(
        runtime,
        MICROS_SYSCALL_ABI_OK
    );
}

enum micros_syscall_return
micros_pm_control_syscall_handle_reservation(
    const struct micros_pm_control_syscall_runtime *runtime,
    const struct micros_pm_control_request *request
)
{
    if (request->command == MICROS_PM_CONTROL_RESERVE) {
        return handle_reserve(runtime, request);
    }
    if (request->command == MICROS_PM_CONTROL_ABORT_RESERVED) {
        return handle_abort_reserved(runtime, request);
    }
    micros_pm_control_syscall_fail(runtime);
}
