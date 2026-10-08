#include "kernel/bootstrap_syscall.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/bootstrap_runtime.h"
#include "kernel/endpoint_internal.h"
#include "kernel/grant_runtime_internal.h"
#include "kernel/ipc_runtime_internal.h"
#include "kernel/scheduler_core_internal.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_core.h"
#include "micros/ipc_runtime.h"
#include "micros/panic.h"
#include "micros/scheduler_core.h"
#include "micros/user_execution.h"

static _Noreturn void panic_bootstrap_syscall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const char *reason
)
{
    MICROS_TRAP_PANIC(
        hart == NULL ? 0 : hart->hardware_id,
        reason,
        frame
    );
}

static enum micros_syscall_return return_result(
    struct micros_trap_frame *frame,
    enum micros_syscall_abi_result result
)
{
    frame->a0 = (uint64_t)(int64_t)result;
    return MICROS_SYSCALL_RETURN_NORMAL;
}

static enum micros_syscall_abi_result map_bootstrap_error(
    enum micros_bootstrap_error error,
    bool endpoint_identity
)
{
    switch (error) {
    case MICROS_BOOTSTRAP_OK:
        return MICROS_SYSCALL_ABI_OK;
    case MICROS_BOOTSTRAP_ERROR_ARGUMENT:
        return MICROS_SYSCALL_ABI_ARGUMENT;
    case MICROS_BOOTSTRAP_ERROR_IDENTITY:
        return endpoint_identity
            ? MICROS_SYSCALL_ABI_DEAD_ENDPOINT
            : MICROS_SYSCALL_ABI_ARGUMENT;
    case MICROS_BOOTSTRAP_ERROR_RANGE:
        return MICROS_SYSCALL_ABI_RANGE;
    case MICROS_BOOTSTRAP_ERROR_STATE:
        return MICROS_SYSCALL_ABI_STATE;
    default:
        return MICROS_SYSCALL_ABI_STATE;
    }
}

static enum micros_syscall_abi_result map_ipc_error(
    enum micros_ipc_error error
)
{
    switch (error) {
    case MICROS_IPC_OK:
        return MICROS_SYSCALL_ABI_OK;
    case MICROS_IPC_ERROR_ARGUMENT:
        return MICROS_SYSCALL_ABI_ARGUMENT;
    case MICROS_IPC_ERROR_DEAD_ENDPOINT:
        return MICROS_SYSCALL_ABI_DEAD_ENDPOINT;
    case MICROS_IPC_ERROR_UNAUTHORIZED:
        return MICROS_SYSCALL_ABI_UNAUTHORIZED;
    case MICROS_IPC_ERROR_STATE:
    case MICROS_IPC_ERROR_NOT_READY:
        return MICROS_SYSCALL_ABI_STATE;
    case MICROS_IPC_ERROR_REPLY_TOKEN:
        return MICROS_SYSCALL_ABI_REPLY_TOKEN;
    case MICROS_IPC_ERROR_ENDPOINT_CLOSING:
        return MICROS_SYSCALL_ABI_ENDPOINT_CLOSING;
    default:
        return MICROS_SYSCALL_ABI_STATE;
    }
}

static bool controller_is_authorized(
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_syscall_context *context
)
{
    const struct micros_process *process;
    const struct micros_endpoint_record *endpoint;
    const struct micros_privilege_profile *profile;

    return (
        state != NULL
        && state->phase == MICROS_BOOTSTRAP_PHASE_RUNNING
        && context->process.slot == state->controller_process.slot
        && context->process.generation
            == state->controller_process.generation
        && context->current.slot == state->controller_thread.slot
        && context->current.generation
            == state->controller_thread.generation
        && micros_process_resolve(
            objects,
            context->process,
            &process
        ) == MICROS_KERNEL_OBJECT_OK
        && process->primary_endpoint == state->controller_endpoint
        && micros_endpoint_resolve_active(
            registry,
            objects,
            state->controller_endpoint,
            &endpoint
        ) == MICROS_ENDPOINT_OK
        && endpoint->owner.slot == context->process.slot
        && endpoint->owner.generation
            == context->process.generation
        && micros_privilege_profile_resolve(
            registry,
            (uint8_t)process->privilege_profile,
            &profile
        ) == MICROS_ENDPOINT_OK
        && (
            profile->kernel_operations
            & MICROS_KERNEL_OPERATION_BOOTSTRAP_CONTROL
        ) != 0
    );
}

static bool role_gate_ready(
    const struct micros_bootstrap_control_state *state,
    uint32_t service_id,
    bool release
)
{
    const struct micros_bootstrap_binding *binding =
        micros_bootstrap_control_find_binding(state, service_id);
    const struct micros_bootstrap_manifest_entry *entry;

    if (binding == NULL) {
        return true;
    }
    entry = &state->manifest.entries[binding->manifest_index];
    if (
        (
            entry->role_flags
            & MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER
        ) != 0
    ) {
        return false;
    }
    if (
        !release
        && (entry->role_flags & MICROS_BOOTSTRAP_ROLE_VM) != 0
    ) {
        return false;
    }
    return true;
}

static enum micros_bootstrap_diagnostic_reason map_failure_reason(
    enum micros_bootstrap_failure_reason reason
)
{
    switch (reason) {
    case MICROS_BOOTSTRAP_FAILURE_READY_MALFORMED:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_READY_MALFORMED;
    case MICROS_BOOTSTRAP_FAILURE_READY_FOREIGN:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_READY_FOREIGN;
    case MICROS_BOOTSTRAP_FAILURE_READY_EARLY:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_READY_EARLY;
    case MICROS_BOOTSTRAP_FAILURE_READY_DUPLICATE:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_READY_DUPLICATE;
    case MICROS_BOOTSTRAP_FAILURE_RELEASE_ORDER:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_ORDER;
    case MICROS_BOOTSTRAP_FAILURE_RELEASE_TRANSITION:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_TRANSITION;
    case MICROS_BOOTSTRAP_FAILURE_AUTHORITY:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY;
    case MICROS_BOOTSTRAP_FAILURE_COMPLETION:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_COMPLETION;
    case MICROS_BOOTSTRAP_FAILURE_READY_ROLE_GATE:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_READY_ROLE_GATE;
    }
    return MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY;
}

static uint64_t failure_detail(
    const struct micros_bootstrap_control_state *state,
    const struct micros_bootstrap_control_request *request
)
{
    if (
        request->failure_reason
        == MICROS_BOOTSTRAP_FAILURE_READY_MALFORMED
    ) {
        return request->failure_detail;
    }
    if (
        request->failure_reason
            == MICROS_BOOTSTRAP_FAILURE_RELEASE_ORDER
        && state->transitions.next_order_index
            < state->transitions.entry_count
    ) {
        return state->transitions.ordered_service_ids[
            state->transitions.next_order_index
        ];
    }
    return 0;
}

static enum micros_bootstrap_error validate_failure_request(
    const struct micros_bootstrap_control_state *state,
    const struct micros_bootstrap_control_request *request
)
{
    const struct micros_bootstrap_binding *binding =
        micros_bootstrap_control_find_binding(
            state,
            request->service_id
        );

    if (
        micros_bootstrap_control_validate_failure_detail(request)
        != MICROS_SYSCALL_ABI_OK
    ) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    switch (request->failure_reason) {
    case MICROS_BOOTSTRAP_FAILURE_READY_FOREIGN:
        if (binding == NULL) {
            return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
        }
        return (
            state->transitions.starting_service_id != 0
            && request->service_id
                == state->transitions.starting_service_id
            && request->endpoint != MICROS_ENDPOINT_NONE
            && request->endpoint != MICROS_ENDPOINT_ANY
        )
            ? MICROS_BOOTSTRAP_OK
            : MICROS_BOOTSTRAP_ERROR_STATE;
    case MICROS_BOOTSTRAP_FAILURE_READY_EARLY:
    case MICROS_BOOTSTRAP_FAILURE_READY_DUPLICATE:
        if (binding == NULL) {
            return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
        }
        return request->endpoint == binding->endpoint
            ? MICROS_BOOTSTRAP_OK
            : MICROS_BOOTSTRAP_ERROR_STATE;
    case MICROS_BOOTSTRAP_FAILURE_READY_MALFORMED:
        if (binding == NULL) {
            return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
        }
        return (
            request->service_id
                == state->transitions.starting_service_id
            || request->endpoint == binding->endpoint
        )
            ? MICROS_BOOTSTRAP_OK
            : MICROS_BOOTSTRAP_ERROR_STATE;
    case MICROS_BOOTSTRAP_FAILURE_RELEASE_ORDER:
    case MICROS_BOOTSTRAP_FAILURE_RELEASE_TRANSITION:
        if (binding == NULL) {
            return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
        }
        return request->endpoint == MICROS_ENDPOINT_NONE
            ? MICROS_BOOTSTRAP_OK
            : MICROS_BOOTSTRAP_ERROR_STATE;
    case MICROS_BOOTSTRAP_FAILURE_AUTHORITY:
    case MICROS_BOOTSTRAP_FAILURE_COMPLETION:
        if (request->service_id != 0 && binding == NULL) {
            return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
        }
        return request->endpoint == MICROS_ENDPOINT_NONE
            ? MICROS_BOOTSTRAP_OK
            : MICROS_BOOTSTRAP_ERROR_STATE;
    case MICROS_BOOTSTRAP_FAILURE_READY_ROLE_GATE:
        if (binding == NULL) {
            return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
        }
        return (
            request->service_id
                == state->transitions.starting_service_id
            && request->endpoint == binding->endpoint
        )
            ? MICROS_BOOTSTRAP_OK
            : MICROS_BOOTSTRAP_ERROR_STATE;
    }
    return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
}

static enum micros_syscall_return handle_accept_ready(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_bootstrap_control_request *request,
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry
)
{
    struct micros_bootstrap_ready_plan plan;
    struct micros_scheduler_current_ipc_guard guard = {0};
    const struct micros_thread *thread;
    enum micros_bootstrap_error error;
    enum micros_ipc_error ipc_error;
    uint64_t now = riscv_read_time();

    micros_bootstrap_runtime_check_deadline(now);
    error = micros_bootstrap_control_prepare_ready(
        state,
        registry,
        context->objects,
        request->service_id,
        request->endpoint,
        now,
        role_gate_ready(state, request->service_id, false),
        &plan
    );
    if (error != MICROS_BOOTSTRAP_OK) {
        if (
            error != MICROS_BOOTSTRAP_ERROR_ARGUMENT
            && error != MICROS_BOOTSTRAP_ERROR_IDENTITY
            && error != MICROS_BOOTSTRAP_ERROR_RANGE
            && error != MICROS_BOOTSTRAP_ERROR_STATE
        ) {
            panic_bootstrap_syscall(
                hart,
                frame,
                "bootstrap-ready-invariant"
            );
        }
        return return_result(
            frame,
            map_bootstrap_error(error, true)
        );
    }
    if (
        micros_user_execution_store_context(
            context->current,
            (const struct micros_user_context *)frame
        ) != MICROS_USER_EXECUTION_OK
        || micros_scheduler_begin_current_ipc(
            context->objects,
            context->hart,
            &guard
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        panic_bootstrap_syscall(hart, frame, "bootstrap-ready-guard");
    }
    ipc_error = micros_ipc_reply(
        registry,
        context->objects,
        context->current,
        request->reply_token,
        &plan.acknowledgment
    );
    if (ipc_error != MICROS_IPC_OK) {
        if (
            micros_scheduler_rollback_current_ipc(
                context->objects,
                &guard
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            panic_bootstrap_syscall(
                hart,
                frame,
                "bootstrap-ready-rollback"
            );
        }
        return return_result(frame, map_ipc_error(ipc_error));
    }
    micros_bootstrap_control_commit_ready_prevalidated(state, &plan);
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
        panic_bootstrap_syscall(
            hart,
            frame,
            "bootstrap-ready-commit"
        );
    }
    return MICROS_SYSCALL_RETURN_CAPTURED;
}

static enum micros_syscall_return handle_complete(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry,
    const struct micros_grant_registry *grants
)
{
    struct micros_bootstrap_complete_plan plan;
    struct micros_scheduler_current_ipc_guard guard = {0};
    const struct micros_bootstrap_binding *controller;
    enum micros_bootstrap_error error;

    error = micros_bootstrap_control_prepare_complete(
        state,
        registry,
        context->objects,
        grants,
        &plan
    );
    if (error != MICROS_BOOTSTRAP_OK) {
        if (error != MICROS_BOOTSTRAP_ERROR_STATE) {
            panic_bootstrap_syscall(
                hart,
                frame,
                "bootstrap-complete-invariant"
            );
        }
        return return_result(
            frame,
            map_bootstrap_error(error, false)
        );
    }
    if (
        micros_scheduler_preflight_current_ipc(
            context->objects,
            context->hart
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return return_result(frame, MICROS_SYSCALL_ABI_STATE);
    }
    controller = &state->bindings[
        plan.controller_binding_index
    ];
    if (
        micros_scheduler_begin_current_ipc(
            context->objects,
            context->hart,
            &guard
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        panic_bootstrap_syscall(
            hart,
            frame,
            "bootstrap-complete-guard"
        );
    }
    if (
        micros_thread_scheduler_remove(
            context->objects,
            controller->thread
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        panic_bootstrap_syscall(
            hart,
            frame,
            "bootstrap-complete-remove"
        );
    }
    micros_endpoint_commit_source_only_prevalidated(
        registry,
        controller->endpoint
    );
    micros_bootstrap_control_commit_complete_prevalidated(
        state,
        &plan
    );
    if (
        micros_bootstrap_control_validate(
            state,
            registry,
            context->objects
        ) != MICROS_BOOTSTRAP_OK
        || micros_bootstrap_runtime_validate()
            != MICROS_BOOTSTRAP_OK
    ) {
        panic_bootstrap_syscall(
            hart,
            frame,
            "bootstrap-complete-commit"
        );
    }
    return MICROS_SYSCALL_RETURN_CAPTURED;
}

enum micros_syscall_return
micros_bootstrap_handle_captured_user_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_syscall_arguments *arguments
)
{
    struct micros_bootstrap_control_request request;
    struct micros_bootstrap_control_state *state;
    struct micros_endpoint_registry *registry;
    struct micros_grant_registry *grants;
    enum micros_syscall_abi_result decode_result;
    enum micros_bootstrap_error error;

    if (
        hart == NULL
        || frame == NULL
        || context == NULL
        || arguments == NULL
        || context->objects == NULL
    ) {
        panic_bootstrap_syscall(
            hart,
            frame,
            "bootstrap-syscall-argument"
        );
    }
    decode_result = micros_bootstrap_control_decode(
        arguments,
        &request
    );
    if (decode_result != MICROS_SYSCALL_ABI_OK) {
        return return_result(frame, decode_result);
    }
    state = micros_bootstrap_runtime_authoritative_state();
    registry = micros_ipc_runtime_authoritative_registry();
    grants = micros_grant_runtime_authoritative_registry();
    if (
        state == NULL
        || registry == NULL
        || grants == NULL
    ) {
        panic_bootstrap_syscall(
            hart,
            frame,
            "bootstrap-runtime-invariant"
        );
    }
    if (
        !controller_is_authorized(
            state,
            registry,
            context->objects,
            context
        )
    ) {
        return return_result(
            frame,
            MICROS_SYSCALL_ABI_UNAUTHORIZED
        );
    }
    if (
        micros_bootstrap_runtime_validate() != MICROS_BOOTSTRAP_OK
    ) {
        panic_bootstrap_syscall(
            hart,
            frame,
            "bootstrap-control-invariant"
        );
    }
    switch (request.command) {
    case MICROS_BOOTSTRAP_COMMAND_RELEASE:
        error = micros_bootstrap_control_release(
            state,
            registry,
            context->objects,
            context->hart,
            request.service_id,
            riscv_read_time(),
            role_gate_ready(state, request.service_id, true)
        );
        if (
            error != MICROS_BOOTSTRAP_OK
            && error != MICROS_BOOTSTRAP_ERROR_ARGUMENT
            && error != MICROS_BOOTSTRAP_ERROR_RANGE
            && error != MICROS_BOOTSTRAP_ERROR_STATE
        ) {
            panic_bootstrap_syscall(
                hart,
                frame,
                "bootstrap-release-invariant"
            );
        }
        if (
            error == MICROS_BOOTSTRAP_OK
            && micros_bootstrap_runtime_validate()
                != MICROS_BOOTSTRAP_OK
        ) {
            panic_bootstrap_syscall(
                hart,
                frame,
                "bootstrap-release-commit"
            );
        }
        return return_result(
            frame,
            map_bootstrap_error(error, false)
        );
    case MICROS_BOOTSTRAP_COMMAND_ACCEPT_READY:
        return handle_accept_ready(
            hart,
            frame,
            context,
            &request,
            state,
            registry
        );
    case MICROS_BOOTSTRAP_COMMAND_FAIL:
        error = validate_failure_request(state, &request);
        if (error != MICROS_BOOTSTRAP_OK) {
            return return_result(
                frame,
                map_bootstrap_error(error, false)
            );
        }
        micros_bootstrap_runtime_fail(
            map_failure_reason(request.failure_reason),
            request.service_id,
            request.endpoint,
            failure_detail(state, &request)
        );
    case MICROS_BOOTSTRAP_COMMAND_COMPLETE:
        return handle_complete(
            hart,
            frame,
            context,
            state,
            registry,
            grants
        );
    }
    panic_bootstrap_syscall(
        hart,
        frame,
        "bootstrap-command-invariant"
    );
}
