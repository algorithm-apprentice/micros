#include "kernel/pm_control_syscall_internal.h"

#include "kernel/grant/grant_runtime_internal.h"
#include "kernel/ipc/ipc_runtime_internal.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_core.h"

static enum micros_syscall_return handle_exit_begin(
    const struct micros_pm_control_syscall_runtime *runtime,
    const struct micros_pm_control_request *request
)
{
    struct micros_pm_control_exit_begin_plan begin;
    struct micros_pm_control_output_plan output;
    enum micros_pm_control_output_error output_error;
    enum micros_pm_control_error control_error;

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
    control_error = micros_pm_control_exit_begin_preflight(
        runtime->state,
        runtime->objects,
        runtime->registry,
        runtime->handoff,
        request->endpoint,
        &begin
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
    micros_pm_control_exit_begin_commit_prevalidated(
        runtime->state,
        runtime->objects,
        runtime->registry,
        &begin
    );
    micros_pm_control_output_commit_prevalidated(
        &output,
        &begin.result
    );
    return micros_pm_control_syscall_return_result(
        runtime,
        MICROS_SYSCALL_ABI_OK
    );
}

static enum micros_syscall_return handle_exit_detach(
    const struct micros_pm_control_syscall_runtime *runtime,
    const struct micros_pm_control_request *request
)
{
    struct micros_pm_control_exit_detach_plan detach;
    struct micros_grant_cancel_plan grants;
    struct micros_grant_registry *grant_registry =
        micros_grant_runtime_authoritative_registry();
    enum micros_pm_control_error control_error;

    control_error = micros_pm_control_exit_detach_preflight(
        runtime->state,
        runtime->objects,
        runtime->registry,
        runtime->handoff,
        request->transaction,
        request->endpoint,
        &detach
    );
    if (control_error == MICROS_PM_CONTROL_ERROR_STATE) {
        return micros_pm_control_syscall_return_result(
            runtime,
            MICROS_SYSCALL_ABI_STATE
        );
    }
    if (
        control_error != MICROS_PM_CONTROL_OK
        || grant_registry == NULL
        || micros_grant_prepare_endpoint_cancel(
            grant_registry,
            runtime->registry,
            runtime->objects,
            request->endpoint,
            &grants
        ) != MICROS_GRANT_OK
    ) {
        micros_pm_control_syscall_fail(runtime);
    }
    if (
        micros_ipc_endpoint_close(
            runtime->registry,
            runtime->objects,
            request->endpoint
        ) != MICROS_IPC_OK
        || micros_grant_commit_endpoint_cancel(
            grant_registry,
            &grants
        ) != MICROS_GRANT_OK
    ) {
        micros_pm_control_syscall_fail(runtime);
    }
    micros_pm_control_exit_detach_commit_prevalidated(
        runtime->state,
        runtime->objects,
        runtime->handoff,
        &detach
    );
    return micros_pm_control_syscall_return_result(
        runtime,
        MICROS_SYSCALL_ABI_OK
    );
}

static enum micros_syscall_return handle_release_exited_process(
    const struct micros_pm_control_syscall_runtime *runtime,
    const struct micros_pm_control_request *request
)
{
    struct micros_pm_control_release_exited_plan release;
    enum micros_pm_control_error control_error;

    control_error = micros_pm_control_release_exited_preflight(
        runtime->state,
        runtime->objects,
        runtime->handoff,
        request->transaction,
        &release
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
    micros_pm_control_release_exited_commit_prevalidated(
        runtime->state,
        runtime->objects,
        &release
    );
    return micros_pm_control_syscall_return_result(
        runtime,
        MICROS_SYSCALL_ABI_OK
    );
}

enum micros_syscall_return micros_pm_control_syscall_handle_exit(
    const struct micros_pm_control_syscall_runtime *runtime,
    const struct micros_pm_control_request *request
)
{
    if (request->command == MICROS_PM_CONTROL_EXIT_BEGIN) {
        return handle_exit_begin(runtime, request);
    }
    if (request->command == MICROS_PM_CONTROL_EXIT_DETACH) {
        return handle_exit_detach(runtime, request);
    }
    if (
        request->command
            == MICROS_PM_CONTROL_RELEASE_EXITED_PROCESS
    ) {
        return handle_release_exited_process(runtime, request);
    }
    micros_pm_control_syscall_fail(runtime);
}
