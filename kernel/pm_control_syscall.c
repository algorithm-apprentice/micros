#include "kernel/pm_control_syscall.h"

#include "kernel/bootstrap_runtime.h"
#include "kernel/ipc/ipc_runtime_internal.h"
#include "kernel/pm_control_runtime.h"
#include "kernel/pm_control_syscall_internal.h"
#include "kernel/vm_handoff_runtime.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_runtime.h"

static bool runtime_state_is_valid(
    const struct micros_pm_control_syscall_runtime *runtime
)
{
    return (
        runtime->handoff != NULL
        && runtime->ownership != NULL
        && micros_bootstrap_runtime_validate()
            == MICROS_BOOTSTRAP_OK
        && micros_vm_handoff_runtime_validate(
            runtime->bootstrap,
            runtime->registry,
            runtime->objects
        ) == MICROS_VM_HANDOFF_OK
        && micros_pm_control_runtime_validate(
            runtime->bootstrap,
            runtime->registry,
            runtime->objects
        ) == MICROS_PM_CONTROL_OK
        && micros_grant_runtime_validate() == MICROS_GRANT_OK
    );
}

static enum micros_syscall_return dispatch_command(
    const struct micros_pm_control_syscall_runtime *runtime,
    const struct micros_pm_control_request *request
)
{
    switch (request->command) {
    case MICROS_PM_CONTROL_RESERVE:
    case MICROS_PM_CONTROL_ABORT_RESERVED:
        return micros_pm_control_syscall_handle_reservation(
            runtime,
            request
        );
    case MICROS_PM_CONTROL_PREPARE_EXECUTION:
    case MICROS_PM_CONTROL_ABORT_PREPARED:
        return micros_pm_control_syscall_handle_execution(
            runtime,
            request
        );
    case MICROS_PM_CONTROL_ACTIVATE:
        return micros_pm_control_syscall_handle_activation(
            runtime,
            request
        );
    case MICROS_PM_CONTROL_EXIT_BEGIN:
    case MICROS_PM_CONTROL_EXIT_DETACH:
    case MICROS_PM_CONTROL_RELEASE_EXITED_PROCESS:
        return micros_pm_control_syscall_handle_exit(runtime, request);
    default:
        micros_pm_control_syscall_fail(runtime);
    }
}

enum micros_syscall_return
micros_pm_control_handle_captured_user_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_syscall_arguments *arguments
)
{
    struct micros_pm_control_request request;
    struct micros_pm_control_syscall_runtime runtime = {
        .hart = hart,
        .frame = frame,
        .context = context,
        .objects = context == NULL ? NULL : context->objects,
    };
    enum micros_pm_control_authority_result authority;

    if (
        hart == NULL
        || frame == NULL
        || context == NULL
        || arguments == NULL
        || context->objects == NULL
    ) {
        micros_pm_control_syscall_panic(
            &runtime,
            "pm-control-argument"
        );
    }
    if (
        micros_pm_control_decode(arguments, &request)
            != MICROS_SYSCALL_ABI_OK
    ) {
        return micros_pm_control_syscall_return_result(
            &runtime,
            MICROS_SYSCALL_ABI_ARGUMENT
        );
    }
    runtime.state = micros_pm_control_runtime_authoritative_state();
    runtime.bootstrap =
        micros_bootstrap_runtime_authoritative_state();
    runtime.registry = micros_ipc_runtime_authoritative_registry();
    authority = micros_pm_control_syscall_authority_resolve(
        runtime.state,
        runtime.bootstrap,
        runtime.registry,
        runtime.objects,
        context
    );
    if (authority == MICROS_PM_CONTROL_AUTHORITY_INVARIANT) {
        micros_pm_control_syscall_fail(&runtime);
    }
    if (authority == MICROS_PM_CONTROL_AUTHORITY_UNAUTHORIZED) {
        return micros_pm_control_syscall_return_result(
            &runtime,
            MICROS_SYSCALL_ABI_UNAUTHORIZED
        );
    }
    if (authority != MICROS_PM_CONTROL_AUTHORITY_AUTHORIZED) {
        micros_pm_control_syscall_fail(&runtime);
    }
    runtime.handoff =
        micros_vm_handoff_runtime_authoritative_state();
    runtime.ownership = micros_frame_ownership_runtime_ledger();
    if (!runtime_state_is_valid(&runtime)) {
        micros_pm_control_syscall_fail(&runtime);
    }
    if (!micros_pm_control_syscall_phase_is_ready(
        runtime.bootstrap->phase,
        runtime.handoff->phase,
        runtime.ownership->phase
    )) {
        return micros_pm_control_syscall_return_result(
            &runtime,
            MICROS_SYSCALL_ABI_STATE
        );
    }
    return dispatch_command(&runtime, &request);
}
