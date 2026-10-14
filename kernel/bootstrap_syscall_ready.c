#include "kernel/bootstrap_syscall_internal.h"

#include <stddef.h>

#include "arch/riscv64/platform.h"
#include "kernel/scheduler_core_internal.h"
#include "kernel/tty_handoff_runtime.h"
#ifdef MICROS_BUILD_TTY_SERVICE_TEST
#include "kernel/tty_service_test.h"
#endif
#include "micros/user_execution.h"

static enum micros_bootstrap_error prepare_ready_plan(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_bootstrap_control_request *request,
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *registry,
    uint64_t now,
    struct micros_bootstrap_ready_plan *plan,
    struct micros_tty_handoff *tty_ready,
    bool *tty_ready_active
)
{
    const struct micros_bootstrap_binding *binding;
    const struct micros_bootstrap_manifest_entry *entry;
    enum micros_bootstrap_error error;
    enum micros_tty_handoff_error tty_error;
    bool gate_ready;

    binding = micros_bootstrap_control_find_binding(
        state,
        request->service_id
    );
    entry = binding == NULL
        ? NULL
        : &state->manifest.entries[binding->manifest_index];
    gate_ready = bootstrap_syscall_role_gate_ready(
        state,
        registry,
        context->objects,
        request->service_id,
        false
    );
    if (
        entry != NULL
        && (
            entry->role_flags
            & MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER
        ) != 0
        && gate_ready
    ) {
        tty_error = micros_tty_handoff_runtime_prepare_ready(
            now,
            tty_ready
        );
        if (tty_error != MICROS_TTY_HANDOFF_OK) {
            bootstrap_syscall_fail_active_bootstrap(
                hart,
                frame,
                MICROS_BOOTSTRAP_DIAGNOSTIC_READY_ROLE_GATE,
                request->service_id,
                request->endpoint
            );
        }
        *tty_ready_active = true;
    }
    error = micros_bootstrap_control_prepare_ready(
        state,
        registry,
        context->objects,
        request->service_id,
        request->endpoint,
        now,
        gate_ready,
        plan
    );
    if (error == MICROS_BOOTSTRAP_ERROR_ROLE) {
        if (binding == NULL) {
            bootstrap_syscall_fail_active_bootstrap(
                hart,
                frame,
                MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
                request->service_id,
                request->endpoint
            );
        }
        bootstrap_syscall_fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_READY_ROLE_GATE,
            binding->service_id,
            binding->endpoint
        );
    }
    if (error != MICROS_BOOTSTRAP_OK) {
        if (
            error != MICROS_BOOTSTRAP_ERROR_ARGUMENT
            && error != MICROS_BOOTSTRAP_ERROR_IDENTITY
            && error != MICROS_BOOTSTRAP_ERROR_RANGE
            && error != MICROS_BOOTSTRAP_ERROR_STATE
        ) {
            bootstrap_syscall_fail_active_bootstrap(
                hart,
                frame,
                MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
                request->service_id,
                request->endpoint
            );
        }
    }
    return error;
}

static enum micros_ipc_error reply_ready_with_guard(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_bootstrap_control_request *request,
    struct micros_endpoint_registry *registry,
    const struct micros_bootstrap_ready_plan *plan,
    struct micros_scheduler_current_ipc_guard *guard
)
{
    enum micros_ipc_error ipc_error;

    if (
        micros_user_execution_store_context(
            context->current,
            (const struct micros_user_context *)frame
        ) != MICROS_USER_EXECUTION_OK
        || micros_scheduler_begin_current_ipc(
            context->objects,
            context->hart,
            guard
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        bootstrap_syscall_fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            request->service_id,
            request->endpoint
        );
    }
    ipc_error = micros_ipc_reply_expected_caller(
        registry,
        context->objects,
        context->current,
        request->reply_token,
        request->endpoint,
        &plan->acknowledgment
    );
    if (ipc_error != MICROS_IPC_OK) {
        if (
            micros_scheduler_rollback_current_ipc(
                context->objects,
                guard
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            bootstrap_syscall_fail_active_bootstrap(
                hart,
                frame,
                MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
                request->service_id,
                request->endpoint
            );
        }
    }
    return ipc_error;
}

enum micros_syscall_return bootstrap_syscall_handle_accept_ready(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_bootstrap_control_request *request,
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry
)
{
    struct micros_bootstrap_ready_plan plan;
    struct micros_tty_handoff tty_ready;
    struct micros_scheduler_current_ipc_guard guard = {0};
    const struct micros_thread *thread;
    enum micros_bootstrap_error error;
    enum micros_ipc_error ipc_error;
    bool tty_ready_active = false;
    uint64_t now = riscv_read_time();

    micros_bootstrap_runtime_check_deadline(now);
    error = prepare_ready_plan(
        hart,
        frame,
        context,
        request,
        state,
        registry,
        now,
        &plan,
        &tty_ready,
        &tty_ready_active
    );
    if (error != MICROS_BOOTSTRAP_OK) {
        return bootstrap_syscall_return_result(
            frame,
            bootstrap_syscall_map_bootstrap_error(error, true)
        );
    }
    ipc_error = reply_ready_with_guard(
        hart,
        frame,
        context,
        request,
        registry,
        &plan,
        &guard
    );
    if (ipc_error != MICROS_IPC_OK) {
        return bootstrap_syscall_return_result(
            frame,
            bootstrap_syscall_map_ipc_error(ipc_error)
        );
    }
    micros_bootstrap_control_commit_ready_prevalidated(state, &plan);
    if (tty_ready_active) {
        micros_tty_handoff_runtime_commit_ready_prevalidated(
            &tty_ready
        );
#ifdef MICROS_BUILD_TTY_SERVICE_TEST
        micros_tty_service_test_record_ready(
            tty_ready.route_phase == MICROS_TTY_ROUTE_IN_SERVICE
        );
#endif
    }
    if (
        micros_thread_resolve(
            context->objects,
            context->current,
            &thread
        ) != MICROS_KERNEL_OBJECT_OK
        || thread->runtime_flags != 0
        || thread->ipc_delivery_pending
        || micros_ipc_stage_no_message_completion(
            registry,
            context->objects,
            context->current,
            MICROS_IPC_OK
        ) != MICROS_IPC_OK
        || micros_scheduler_commit_current_ipc(
            context->objects,
            &guard
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_bootstrap_runtime_validate()
            != MICROS_BOOTSTRAP_OK
    ) {
        bootstrap_syscall_fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            request->service_id,
            request->endpoint
        );
    }
    return MICROS_SYSCALL_RETURN_CAPTURED;
}
