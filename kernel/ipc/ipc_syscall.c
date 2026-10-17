#include "kernel/ipc/ipc_syscall_internal.h"

#include <stdbool.h>
#include <stdint.h>

#include "arch/riscv64/trap_context.h"
#include "kernel/boot/bootstrap_runtime.h"
#include "kernel/ipc/ipc_runtime_internal.h"
#include "kernel/objects/kernel_object_runtime_internal.h"
#include "micros/ipc_abi.h"
#include "micros/ipc_buffer.h"
#include "micros/ipc_core.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "micros/scheduler.h"
#include "micros/scheduler_core.h"
#include "micros/user_execution.h"

static bool operation_stages_immediate_completion(
    uint64_t operation
)
{
    return (
        operation == MICROS_IPC_ABI_SEND
        || operation == MICROS_IPC_ABI_REPLY
        || operation == MICROS_IPC_ABI_NOTIFY
    );
}

static struct micros_endpoint_registry *resolve_current_runtime(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context
)
{
    struct micros_endpoint_registry *registry;
    const struct micros_thread *resolved;

    registry = micros_ipc_runtime_authoritative_registry();
    if (registry == NULL) {
        ipc_syscall_panic(
            hart,
            frame,
            "ipc-runtime-not-initialized"
        );
    }
    if (micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK) {
        ipc_syscall_panic(hart, frame, "ipc-runtime-invariant");
    }
    if (
        micros_thread_resolve(
            context->objects,
            context->current,
            &resolved
        )
            != MICROS_KERNEL_OBJECT_OK
        || resolved->owner.slot != context->process.slot
        || resolved->owner.generation != context->process.generation
    ) {
        ipc_syscall_panic(hart, frame, "ipc-current-invariant");
    }
    return registry;
}

static void begin_current_guard(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    struct micros_scheduler_current_ipc_guard *guard
)
{
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
        ipc_syscall_panic(hart, frame, "ipc-guard-begin");
    }
}

static enum micros_syscall_return rollback_current_error(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    struct micros_scheduler_current_ipc_guard *guard,
    enum micros_ipc_error ipc_error
)
{
    uint64_t abi_result;

    if (
        micros_scheduler_rollback_current_ipc(context->objects, guard)
            != MICROS_KERNEL_OBJECT_OK
        || !micros_ipc_abi_map_error(ipc_error, &abi_result)
    ) {
        ipc_syscall_panic(hart, frame, "ipc-portable-invariant");
    }
    frame->a0 = abi_result;
    return MICROS_SYSCALL_RETURN_NORMAL;
}

static void stage_immediate_completion(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    struct micros_endpoint_registry *registry,
    uint64_t operation
)
{
    const struct micros_thread *resolved;

    if (
        operation_stages_immediate_completion(operation)
    ) {
        if (
            micros_thread_resolve(
                context->objects,
                context->current,
                &resolved
            )
                != MICROS_KERNEL_OBJECT_OK
        ) {
            ipc_syscall_panic(
                hart,
                frame,
                "ipc-success-thread"
            );
        }
        if (resolved->runtime_flags == 0) {
            if (
                resolved->ipc_delivery_pending
                || micros_ipc_stage_no_message_completion(
                    registry,
                    context->objects,
                    context->current,
                    MICROS_IPC_OK
                ) != MICROS_IPC_OK
            ) {
                ipc_syscall_panic(
                    hart,
                    frame,
                    "ipc-success-completion"
                );
            }
        } else if (
            operation != MICROS_IPC_ABI_SEND
            || resolved->runtime_flags
                != MICROS_THREAD_RTS_IPC_SEND
        ) {
            ipc_syscall_panic(
                hart,
                frame,
                "ipc-success-state"
            );
        }
    }
}

enum micros_syscall_return micros_ipc_handle_captured_user_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_syscall_arguments *arguments
)
{
    struct micros_endpoint_registry *registry;
    struct micros_scheduler_current_ipc_guard guard = {0};
    struct ipc_syscall_request request;
    enum micros_ipc_abi_result preflight_result;
    enum micros_ipc_error ipc_error;

    if (
        hart == NULL
        || frame == NULL
        || context == NULL
        || arguments == NULL
        || context->objects == NULL
    ) {
        ipc_syscall_panic(hart, frame, "ipc-syscall-argument");
    }
    registry = resolve_current_runtime(hart, frame, context);
    preflight_result = ipc_syscall_preflight_request(
        hart,
        frame,
        context->process,
        arguments,
        &request
    );
    if (preflight_result != MICROS_IPC_ABI_OK) {
        return ipc_syscall_return_abi_error(frame, preflight_result);
    }
    if (
        (
            request.operation == MICROS_IPC_ABI_REPLY
            || request.operation == MICROS_IPC_ABI_REPLY_RECEIVE
        )
        && micros_bootstrap_runtime_is_active_controller(context->process)
    ) {
        return ipc_syscall_return_abi_error(frame, MICROS_IPC_ABI_UNAUTHORIZED);
    }
    begin_current_guard(hart, frame, context, &guard);
    ipc_error = ipc_syscall_execute_portable(
        registry,
        context->objects,
        context->current,
        &request
    );
    if (ipc_error != MICROS_IPC_OK) {
        return rollback_current_error(hart, frame, context, &guard, ipc_error);
    }
    stage_immediate_completion(hart, frame, context, registry, request.operation);
    if (
        micros_scheduler_commit_current_ipc(
            context->objects,
            &guard
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        ipc_syscall_panic(hart, frame, "ipc-guard-commit");
    }
    return MICROS_SYSCALL_RETURN_CAPTURED;
}
