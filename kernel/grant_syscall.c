#include "kernel/grant_syscall.h"

#include <stdint.h>

#include "arch/riscv64/trap_context.h"
#include "kernel/grant_runtime_internal.h"
#include "kernel/ipc_runtime_internal.h"
#include "micros/grant_copy.h"
#include "micros/grant_runtime.h"
#include "micros/grant_syscall_core.h"
#include "micros/ipc_runtime.h"
#include "micros/panic.h"

static _Noreturn void panic_grant_syscall(
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

enum micros_syscall_return
micros_grant_handle_captured_user_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_syscall_arguments *arguments
)
{
    struct micros_grant_registry *grant_registry;
    struct micros_endpoint_registry *endpoint_registry;
    struct micros_grant_syscall_request request;
    micros_grant_t created = MICROS_GRANT_NONE;
    enum micros_syscall_abi_result decode_result;
    enum micros_grant_error error;
    uint64_t abi_result;

    if (
        hart == NULL
        || frame == NULL
        || context == NULL
        || arguments == NULL
        || context->objects == NULL
    ) {
        panic_grant_syscall(hart, frame, "grant-syscall-argument");
    }
    grant_registry =
        micros_grant_runtime_authoritative_registry();
    endpoint_registry =
        micros_ipc_runtime_authoritative_registry();
    if (
        grant_registry == NULL
        || endpoint_registry == NULL
        || micros_grant_runtime_validate() != MICROS_GRANT_OK
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
    ) {
        panic_grant_syscall(hart, frame, "grant-runtime-invariant");
    }
    decode_result = micros_grant_syscall_decode(
        arguments,
        &request
    );
    if (decode_result != MICROS_SYSCALL_ABI_OK) {
        frame->a0 = (uint64_t)(int64_t)decode_result;
        return MICROS_SYSCALL_RETURN_NORMAL;
    }
    switch (request.operation) {
    case MICROS_SYSCALL_ABI_GRANT_CREATE:
        error = micros_grant_create(
            grant_registry,
            endpoint_registry,
            context->objects,
            context->process,
            request.endpoint,
            request.local_address,
            request.length,
            request.permissions,
            &created
        );
        break;
    case MICROS_SYSCALL_ABI_GRANT_REVOKE:
        error = micros_grant_revoke(
            grant_registry,
            endpoint_registry,
            context->objects,
            context->process,
            request.grant
        );
        break;
    case MICROS_SYSCALL_ABI_GRANT_COPY_FROM:
        error = micros_grant_copy_from(
            grant_registry,
            endpoint_registry,
            context->objects,
            context->process,
            request.endpoint,
            request.grant,
            request.offset,
            request.local_address,
            request.length
        );
        break;
    case MICROS_SYSCALL_ABI_GRANT_COPY_TO:
        error = micros_grant_copy_to(
            grant_registry,
            endpoint_registry,
            context->objects,
            context->process,
            request.endpoint,
            request.grant,
            request.offset,
            request.local_address,
            request.length
        );
        break;
    default:
        panic_grant_syscall(hart, frame, "grant-request-invariant");
    }
    if (error == MICROS_GRANT_OK) {
        frame->a0 = request.operation
                == MICROS_SYSCALL_ABI_GRANT_CREATE
            ? created
            : 0;
        return MICROS_SYSCALL_RETURN_NORMAL;
    }
    if (!micros_grant_abi_map_error(error, &abi_result)) {
        panic_grant_syscall(hart, frame, "grant-portable-invariant");
    }
    frame->a0 = abi_result;
    return MICROS_SYSCALL_RETURN_NORMAL;
}
