#include "kernel/pm_control_syscall_internal.h"

#include "kernel/pm_control_internal.h"
#include "micros/user_execution.h"

static enum micros_syscall_return handle_prepare_execution(
    const struct micros_pm_control_syscall_runtime *runtime,
    const struct micros_pm_control_request *request
)
{
    struct micros_pm_control_prepare_plan prepare;
    struct micros_pm_control_execution_binding binding;
    struct micros_user_execution_binding execution;
    struct micros_pm_control_output_plan output;
    enum micros_pm_control_output_error output_error;
    enum micros_pm_control_error control_error;
    enum micros_user_execution_error execution_error;

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
    control_error = micros_pm_control_prepare_preflight(
        runtime->state,
        runtime->objects,
        runtime->registry,
        runtime->handoff,
        request->transaction,
        request->load_token,
        request->mapping_generation,
        &prepare
    );
    if (control_error == MICROS_PM_CONTROL_ERROR_STATE) {
        return micros_pm_control_syscall_return_result(
            runtime,
            MICROS_SYSCALL_ABI_STATE
        );
    }
    if (control_error == MICROS_PM_CONTROL_ERROR_CAPACITY) {
        return micros_pm_control_syscall_return_result(
            runtime,
            MICROS_SYSCALL_ABI_CAPACITY
        );
    }
    if (control_error != MICROS_PM_CONTROL_OK) {
        micros_pm_control_syscall_fail(runtime);
    }
    execution_error =
        micros_user_execution_preflight_post_handoff(
            prepare.process,
            prepare.thread,
            &prepare.initial_context,
            &execution
        );
    if (execution_error != MICROS_USER_EXECUTION_OK) {
        micros_pm_control_syscall_fail(runtime);
    }
    binding.thread = execution.thread;
    binding.hart = execution.hart;
    binding.kernel_stack_bottom = execution.kernel_stack_bottom;
    binding.kernel_stack_top = execution.kernel_stack_top;
    micros_pm_control_copy_bytes(
        &binding.context,
        &execution.context,
        sizeof(binding.context)
    );
    micros_user_execution_commit_post_handoff_prevalidated(
        &execution
    );
    micros_pm_control_prepare_commit_prevalidated(
        runtime->state,
        runtime->objects,
        runtime->registry,
        runtime->handoff,
        &prepare,
        &binding
    );
    __asm__ volatile("fence.i" : : : "memory");
    micros_pm_control_output_commit_prevalidated(
        &output,
        &prepare.result
    );
    return micros_pm_control_syscall_return_result(
        runtime,
        MICROS_SYSCALL_ABI_OK
    );
}

static enum micros_syscall_return handle_abort_prepared(
    const struct micros_pm_control_syscall_runtime *runtime,
    const struct micros_pm_control_request *request
)
{
    struct micros_pm_control_abort_prepared_plan abort;
    enum micros_pm_control_error control_error;

    control_error = micros_pm_control_abort_prepared_preflight(
        runtime->state,
        runtime->objects,
        runtime->registry,
        runtime->handoff,
        request->transaction,
        request->load_token,
        request->mapping_generation,
        request->prepared_token,
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
    micros_pm_control_abort_prepared_commit_prevalidated(
        runtime->state,
        runtime->objects,
        runtime->registry,
        runtime->handoff,
        &abort
    );
    return micros_pm_control_syscall_return_result(
        runtime,
        MICROS_SYSCALL_ABI_OK
    );
}

enum micros_syscall_return
micros_pm_control_syscall_handle_execution(
    const struct micros_pm_control_syscall_runtime *runtime,
    const struct micros_pm_control_request *request
)
{
    if (request->command == MICROS_PM_CONTROL_PREPARE_EXECUTION) {
        return handle_prepare_execution(runtime, request);
    }
    if (request->command == MICROS_PM_CONTROL_ABORT_PREPARED) {
        return handle_abort_prepared(runtime, request);
    }
    micros_pm_control_syscall_fail(runtime);
}
