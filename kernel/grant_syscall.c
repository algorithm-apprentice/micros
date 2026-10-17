#include "kernel/grant_syscall.h"

#include <stdint.h>

#include "arch/riscv64/trap_context.h"
#include "grant_internal.h"
#include "kernel/grant_runtime_internal.h"
#include "kernel/ipc/ipc_runtime_internal.h"
#include "kernel/tty/tty_handoff_runtime.h"
#include "micros/grant_copy.h"
#include "micros/grant_runtime.h"
#include "micros/grant_syscall_core.h"
#include "micros/ipc_runtime.h"
#include "micros/panic.h"

struct grant_syscall_execution {
    struct micros_grant_registry *grant_registry;
    struct micros_endpoint_registry *endpoint_registry;
    struct micros_grant_create_plan create_plan;
    struct micros_grant_syscall_request request;
    struct micros_tty_device_authority device_authority;
    micros_grant_t created;
    enum micros_syscall_abi_result decode_result;
    enum micros_grant_error error;
    enum micros_tty_device_authority_status device_status;
    uint64_t abi_result;
};

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

static void grant_syscall_create_request(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    struct grant_syscall_execution *work
)
{
    work->error = micros_grant_prepare_create(
        work->grant_registry,
        work->endpoint_registry,
        context->objects,
        context->process,
        work->request.endpoint,
        work->request.local_address,
        work->request.length,
        work->request.permissions,
        &work->create_plan
    );
    if (work->error != MICROS_GRANT_OK) {
        return;
    }
    work->device_status =
        micros_tty_handoff_runtime_device_authority(
            work->endpoint_registry,
            context->objects,
            &work->device_authority
        );
    if (
        work->device_status
            == MICROS_TTY_DEVICE_AUTHORITY_INVARIANT
    ) {
        panic_grant_syscall(
            hart,
            frame,
            "grant-device-authority-invariant"
        );
    }
    if (
        work->device_status == MICROS_TTY_DEVICE_AUTHORITY_ACTIVE
        && micros_tty_device_range_intersects(
            &work->device_authority,
            context->process,
            work->request.local_address,
            work->request.length
        )
    ) {
        work->error = MICROS_GRANT_ERROR_FAULT;
        return;
    }
    micros_grant_commit_create_prevalidated(
        work->grant_registry,
        &work->create_plan,
        &work->created
    );
}

static void grant_syscall_execute_request(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    struct grant_syscall_execution *work
)
{
    switch (work->request.operation) {
    case MICROS_SYSCALL_ABI_GRANT_CREATE:
        grant_syscall_create_request(hart, frame, context, work);
        break;
    case MICROS_SYSCALL_ABI_GRANT_REVOKE:
        work->error = micros_grant_revoke(
            work->grant_registry,
            work->endpoint_registry,
            context->objects,
            context->process,
            work->request.grant
        );
        break;
    case MICROS_SYSCALL_ABI_GRANT_COPY_FROM:
        work->error = micros_grant_copy_from(
            work->grant_registry,
            work->endpoint_registry,
            context->objects,
            context->process,
            work->request.endpoint,
            work->request.grant,
            work->request.offset,
            work->request.local_address,
            work->request.length
        );
        break;
    case MICROS_SYSCALL_ABI_GRANT_COPY_TO:
        work->error = micros_grant_copy_to(
            work->grant_registry,
            work->endpoint_registry,
            context->objects,
            context->process,
            work->request.endpoint,
            work->request.grant,
            work->request.offset,
            work->request.local_address,
            work->request.length
        );
        break;
    case MICROS_SYSCALL_ABI_GRANT_VALIDATE:
        work->error = micros_grant_validate_range(
            work->grant_registry,
            work->endpoint_registry,
            context->objects,
            context->process,
            work->request.endpoint,
            work->request.grant,
            work->request.offset,
            work->request.length,
            work->request.permissions
        );
        break;
    default:
        panic_grant_syscall(hart, frame, "grant-request-invariant");
    }
}

enum micros_syscall_return
micros_grant_handle_captured_user_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_syscall_arguments *arguments
)
{
    struct grant_syscall_execution work;
    work.created = MICROS_GRANT_NONE;

    if (
        hart == NULL
        || frame == NULL
        || context == NULL
        || arguments == NULL
        || context->objects == NULL
    ) {
        panic_grant_syscall(hart, frame, "grant-syscall-argument");
    }
    work.grant_registry =
        micros_grant_runtime_authoritative_registry();
    work.endpoint_registry =
        micros_ipc_runtime_authoritative_registry();
    if (
        work.grant_registry == NULL
        || work.endpoint_registry == NULL
        || micros_grant_runtime_validate() != MICROS_GRANT_OK
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
    ) {
        panic_grant_syscall(hart, frame, "grant-runtime-invariant");
    }
    work.decode_result = micros_grant_syscall_decode(
        arguments,
        &work.request
    );
    if (work.decode_result != MICROS_SYSCALL_ABI_OK) {
        frame->a0 = (uint64_t)(int64_t)work.decode_result;
        return MICROS_SYSCALL_RETURN_NORMAL;
    }
    grant_syscall_execute_request(hart, frame, context, &work);
    if (work.error == MICROS_GRANT_OK) {
        frame->a0 = work.request.operation
                == MICROS_SYSCALL_ABI_GRANT_CREATE
            ? work.created
            : 0;
        return MICROS_SYSCALL_RETURN_NORMAL;
    }
    if (!micros_grant_abi_map_error(work.error, &work.abi_result)) {
        panic_grant_syscall(hart, frame, "grant-portable-invariant");
    }
    frame->a0 = work.abi_result;
    return MICROS_SYSCALL_RETURN_NORMAL;
}
