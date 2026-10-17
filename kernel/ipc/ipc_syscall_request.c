#include "kernel/ipc/ipc_syscall_internal.h"

#include "micros/ipc_buffer.h"

static bool capture_endpoint(uint64_t value, micros_endpoint_t *endpoint)
{
    if (endpoint == NULL || value > UINT32_MAX) {
        return false;
    }
    *endpoint = (micros_endpoint_t)value;
    return true;
}

enum micros_ipc_abi_result ipc_syscall_preflight_request(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    struct micros_process_handle process,
    const struct micros_syscall_arguments *arguments,
    struct ipc_syscall_request *request
)
{
    enum micros_ipc_buffer_error buffer_error;

    if (arguments == NULL || request == NULL) {
        ipc_syscall_panic(hart, frame, "ipc-request-storage");
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
        return ipc_syscall_map_buffer_error(hart, frame, buffer_error);
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
        return ipc_syscall_map_buffer_error(hart, frame, buffer_error);
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
        return ipc_syscall_map_buffer_error(hart, frame, buffer_error);
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
        return ipc_syscall_map_buffer_error(hart, frame, buffer_error);
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
            return ipc_syscall_map_buffer_error(hart, frame, buffer_error);
        }
        buffer_error = micros_ipc_buffer_validate(
            process,
            arguments->a3,
            MICROS_IPC_BUFFER_WRITE
        );
        return ipc_syscall_map_buffer_error(hart, frame, buffer_error);
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

enum micros_ipc_error ipc_syscall_execute_portable(
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
