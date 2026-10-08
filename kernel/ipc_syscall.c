#include "kernel/ipc_syscall.h"

#include <stdbool.h>
#include <stdint.h>

#include "arch/riscv64/trap_context.h"
#include "kernel/ipc_runtime_internal.h"
#include "kernel/kernel_object_runtime_internal.h"
#include "micros/ipc_abi.h"
#include "micros/ipc_buffer.h"
#include "micros/ipc_core.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "micros/scheduler.h"
#include "micros/scheduler_core.h"
#include "micros/user_execution.h"

struct ipc_syscall_request {
    uint64_t operation;
    micros_endpoint_t endpoint;
    micros_endpoint_t source;
    uint64_t token;
    uint64_t event_mask;
    uintptr_t primary_buffer;
    uintptr_t receive_buffer;
    struct micros_ipc_message message;
};

static _Noreturn void panic_ipc_syscall(
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

static enum micros_syscall_return return_abi_error(
    struct micros_trap_frame *frame,
    enum micros_ipc_abi_result result
)
{
    frame->a0 = (uint64_t)(int64_t)result;
    return MICROS_SYSCALL_RETURN_NORMAL;
}

static bool capture_endpoint(uint64_t value, micros_endpoint_t *endpoint)
{
    if (endpoint == NULL || value > UINT32_MAX) {
        return false;
    }
    *endpoint = (micros_endpoint_t)value;
    return true;
}

static enum micros_ipc_abi_result map_buffer_error(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_ipc_buffer_error error
)
{
    switch (error) {
    case MICROS_IPC_BUFFER_OK:
        return MICROS_IPC_ABI_OK;
    case MICROS_IPC_BUFFER_ERROR_ARGUMENT:
    case MICROS_IPC_BUFFER_ERROR_MESSAGE_FAULT:
        return MICROS_IPC_ABI_MESSAGE_FAULT;
    case MICROS_IPC_BUFFER_ERROR_INVARIANT:
        panic_ipc_syscall(hart, frame, "ipc-buffer-invariant");
    }
    panic_ipc_syscall(hart, frame, "ipc-buffer-result");
}

static enum micros_ipc_abi_result preflight_request(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    struct micros_process_handle process,
    const struct micros_syscall_arguments *arguments,
    struct ipc_syscall_request *request
)
{
    enum micros_ipc_buffer_error buffer_error;

    if (arguments == NULL || request == NULL) {
        panic_ipc_syscall(hart, frame, "ipc-request-storage");
    }
    request->operation = arguments->a7;
    request->token = arguments->a0;
    request->event_mask = arguments->a1;
    request->primary_buffer = (uintptr_t)arguments->a1;
    request->receive_buffer = (uintptr_t)arguments->a3;
    switch (request->operation) {
    case MICROS_IPC_ABI_SEND:
        if (
            arguments->a2 != 0
            || arguments->a3 != 0
            || !capture_endpoint(arguments->a0, &request->endpoint)
        ) {
            return MICROS_IPC_ABI_ARGUMENT;
        }
        buffer_error = micros_ipc_buffer_snapshot(
            process,
            arguments->a1,
            MICROS_IPC_BUFFER_READ,
            &request->message
        );
        return map_buffer_error(hart, frame, buffer_error);
    case MICROS_IPC_ABI_RECEIVE:
        if (
            arguments->a2 != 0
            || arguments->a3 != 0
            || !capture_endpoint(arguments->a0, &request->source)
        ) {
            return MICROS_IPC_ABI_ARGUMENT;
        }
        buffer_error = micros_ipc_buffer_validate(
            process,
            arguments->a1,
            MICROS_IPC_BUFFER_WRITE
        );
        return map_buffer_error(hart, frame, buffer_error);
    case MICROS_IPC_ABI_CALL:
        if (
            arguments->a2 != 0
            || arguments->a3 != 0
            || !capture_endpoint(arguments->a0, &request->endpoint)
        ) {
            return MICROS_IPC_ABI_ARGUMENT;
        }
        buffer_error = micros_ipc_buffer_snapshot(
            process,
            arguments->a1,
            MICROS_IPC_BUFFER_READ | MICROS_IPC_BUFFER_WRITE,
            &request->message
        );
        return map_buffer_error(hart, frame, buffer_error);
    case MICROS_IPC_ABI_REPLY:
        if (arguments->a2 != 0 || arguments->a3 != 0) {
            return MICROS_IPC_ABI_ARGUMENT;
        }
        buffer_error = micros_ipc_buffer_snapshot(
            process,
            arguments->a1,
            MICROS_IPC_BUFFER_READ,
            &request->message
        );
        return map_buffer_error(hart, frame, buffer_error);
    case MICROS_IPC_ABI_REPLY_RECEIVE:
        if (!capture_endpoint(arguments->a2, &request->source)) {
            return MICROS_IPC_ABI_ARGUMENT;
        }
        buffer_error = micros_ipc_buffer_snapshot(
            process,
            arguments->a1,
            MICROS_IPC_BUFFER_READ,
            &request->message
        );
        if (buffer_error != MICROS_IPC_BUFFER_OK) {
            return map_buffer_error(hart, frame, buffer_error);
        }
        buffer_error = micros_ipc_buffer_validate(
            process,
            arguments->a3,
            MICROS_IPC_BUFFER_WRITE
        );
        return map_buffer_error(hart, frame, buffer_error);
    case MICROS_IPC_ABI_NOTIFY:
        if (
            arguments->a2 != 0
            || arguments->a3 != 0
            || arguments->a1 == 0
            || !capture_endpoint(arguments->a0, &request->endpoint)
        ) {
            return MICROS_IPC_ABI_ARGUMENT;
        }
        return MICROS_IPC_ABI_OK;
    }
    return MICROS_IPC_ABI_ARGUMENT;
}

static enum micros_ipc_error execute_portable(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread,
    const struct ipc_syscall_request *request
)
{
    switch (request->operation) {
    case MICROS_IPC_ABI_SEND:
        return micros_ipc_send(
            registry,
            objects,
            thread,
            request->endpoint,
            &request->message
        );
    case MICROS_IPC_ABI_RECEIVE:
        return micros_ipc_receive(
            registry,
            objects,
            thread,
            request->source,
            request->primary_buffer
        );
    case MICROS_IPC_ABI_CALL:
        return micros_ipc_call(
            registry,
            objects,
            thread,
            request->endpoint,
            &request->message,
            request->primary_buffer
        );
    case MICROS_IPC_ABI_REPLY:
        return micros_ipc_reply(
            registry,
            objects,
            thread,
            request->token,
            &request->message
        );
    case MICROS_IPC_ABI_REPLY_RECEIVE:
        return micros_ipc_reply_receive(
            registry,
            objects,
            thread,
            request->token,
            &request->message,
            request->source,
            request->receive_buffer
        );
    case MICROS_IPC_ABI_NOTIFY:
        return micros_ipc_notify(
            registry,
            objects,
            thread,
            request->endpoint,
            request->event_mask
        );
    }
    return MICROS_IPC_ERROR_INVARIANT;
}

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

enum micros_syscall_return micros_ipc_handle_captured_user_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_syscall_arguments *arguments
)
{
    struct micros_endpoint_registry *registry;
    const struct micros_thread *resolved;
    struct micros_scheduler_current_ipc_guard guard = {0};
    struct ipc_syscall_request request;
    enum micros_ipc_abi_result preflight_result;
    enum micros_ipc_error ipc_error;
    uint64_t abi_result;

    if (
        hart == NULL
        || frame == NULL
        || context == NULL
        || arguments == NULL
        || context->objects == NULL
    ) {
        panic_ipc_syscall(hart, frame, "ipc-syscall-argument");
    }
    registry = micros_ipc_runtime_authoritative_registry();
    if (registry == NULL) {
        panic_ipc_syscall(
            hart,
            frame,
            "ipc-runtime-not-initialized"
        );
    }
    if (micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK) {
        panic_ipc_syscall(hart, frame, "ipc-runtime-invariant");
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
        panic_ipc_syscall(hart, frame, "ipc-current-invariant");
    }
    preflight_result = preflight_request(
        hart,
        frame,
        context->process,
        arguments,
        &request
    );
    if (preflight_result != MICROS_IPC_ABI_OK) {
        return return_abi_error(frame, preflight_result);
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
        panic_ipc_syscall(hart, frame, "ipc-guard-begin");
    }
    ipc_error = execute_portable(
        registry,
        context->objects,
        context->current,
        &request
    );
    if (ipc_error != MICROS_IPC_OK) {
        if (
            micros_scheduler_rollback_current_ipc(
                context->objects,
                &guard
            ) != MICROS_KERNEL_OBJECT_OK
            || !micros_ipc_abi_map_error(ipc_error, &abi_result)
        ) {
            panic_ipc_syscall(
                hart,
                frame,
                "ipc-portable-invariant"
            );
        }
        frame->a0 = abi_result;
        return MICROS_SYSCALL_RETURN_NORMAL;
    }
    if (
        operation_stages_immediate_completion(request.operation)
    ) {
        if (
            micros_thread_resolve(
                context->objects,
                context->current,
                &resolved
            )
                != MICROS_KERNEL_OBJECT_OK
        ) {
            panic_ipc_syscall(
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
                panic_ipc_syscall(
                    hart,
                    frame,
                    "ipc-success-completion"
                );
            }
        } else if (
            request.operation != MICROS_IPC_ABI_SEND
            || resolved->runtime_flags
                != MICROS_THREAD_RTS_IPC_SEND
        ) {
            panic_ipc_syscall(
                hart,
                frame,
                "ipc-success-state"
            );
        }
    }
    if (
        micros_scheduler_commit_current_ipc(
            context->objects,
            &guard
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        panic_ipc_syscall(hart, frame, "ipc-guard-commit");
    }
    return MICROS_SYSCALL_RETURN_CAPTURED;
}
