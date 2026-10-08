#include "micros/runtime.h"

#include "lib/runtime/raw_syscall.h"

static micros_runtime_result_t runtime_syscall(
    uint64_t a0,
    uint64_t a1,
    uint64_t a2,
    uint64_t a3,
    uint64_t a4,
    uint64_t a5,
    uint64_t a6,
    uint64_t a7
)
{
    return micros_runtime_raw_syscall(
        a0,
        a1,
        a2,
        a3,
        a4,
        a5,
        a6,
        a7
    );
}

micros_runtime_result_t micros_runtime_send(
    micros_endpoint_t destination,
    const struct micros_ipc_message *message
)
{
    return runtime_syscall(
        (uint64_t)destination,
        (uint64_t)(uintptr_t)message,
        0,
        0,
        0,
        0,
        0,
        MICROS_SYSCALL_ABI_SEND
    );
}

micros_runtime_result_t micros_runtime_receive(
    micros_endpoint_t source,
    struct micros_ipc_message *message
)
{
    return runtime_syscall(
        (uint64_t)source,
        (uint64_t)(uintptr_t)message,
        0,
        0,
        0,
        0,
        0,
        MICROS_SYSCALL_ABI_RECEIVE
    );
}

micros_runtime_result_t micros_runtime_call(
    micros_endpoint_t destination,
    struct micros_ipc_message *message
)
{
    return runtime_syscall(
        (uint64_t)destination,
        (uint64_t)(uintptr_t)message,
        0,
        0,
        0,
        0,
        0,
        MICROS_SYSCALL_ABI_CALL
    );
}

micros_runtime_result_t micros_runtime_reply(
    uint64_t reply_token,
    const struct micros_ipc_message *message
)
{
    return runtime_syscall(
        reply_token,
        (uint64_t)(uintptr_t)message,
        0,
        0,
        0,
        0,
        0,
        MICROS_SYSCALL_ABI_REPLY
    );
}

micros_runtime_result_t micros_runtime_reply_receive(
    uint64_t reply_token,
    const struct micros_ipc_message *reply_message,
    micros_endpoint_t source,
    struct micros_ipc_message *receive_message
)
{
    return runtime_syscall(
        reply_token,
        (uint64_t)(uintptr_t)reply_message,
        (uint64_t)source,
        (uint64_t)(uintptr_t)receive_message,
        0,
        0,
        0,
        MICROS_SYSCALL_ABI_REPLY_RECEIVE
    );
}

micros_runtime_result_t micros_runtime_notify(
    micros_endpoint_t destination,
    uint64_t event_mask
)
{
    return runtime_syscall(
        (uint64_t)destination,
        event_mask,
        0,
        0,
        0,
        0,
        0,
        MICROS_SYSCALL_ABI_NOTIFY
    );
}

micros_runtime_result_t micros_runtime_grant_create(
    micros_endpoint_t grantee,
    uintptr_t base,
    size_t length,
    uint32_t permissions,
    micros_grant_t *grant_out
)
{
    micros_runtime_result_t result;

    if (grant_out == NULL) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    result = runtime_syscall(
        (uint64_t)grantee,
        (uint64_t)base,
        (uint64_t)length,
        (uint64_t)permissions,
        0,
        0,
        0,
        MICROS_SYSCALL_ABI_GRANT_CREATE
    );
    if (result < 0) {
        return result;
    }
    *grant_out = (micros_grant_t)(uint64_t)result;
    return MICROS_SYSCALL_ABI_OK;
}

micros_runtime_result_t micros_runtime_grant_revoke(
    micros_grant_t grant
)
{
    return runtime_syscall(
        (uint64_t)grant,
        0,
        0,
        0,
        0,
        0,
        0,
        MICROS_SYSCALL_ABI_GRANT_REVOKE
    );
}

micros_runtime_result_t micros_runtime_grant_copy_from(
    micros_endpoint_t grantor,
    micros_grant_t grant,
    size_t grant_offset,
    uintptr_t local_address,
    size_t length
)
{
    return runtime_syscall(
        (uint64_t)grantor,
        (uint64_t)grant,
        (uint64_t)grant_offset,
        (uint64_t)local_address,
        (uint64_t)length,
        0,
        0,
        MICROS_SYSCALL_ABI_GRANT_COPY_FROM
    );
}

micros_runtime_result_t micros_runtime_grant_copy_to(
    micros_endpoint_t grantor,
    micros_grant_t grant,
    size_t grant_offset,
    uintptr_t local_address,
    size_t length
)
{
    return runtime_syscall(
        (uint64_t)grantor,
        (uint64_t)grant,
        (uint64_t)grant_offset,
        (uint64_t)local_address,
        (uint64_t)length,
        0,
        0,
        MICROS_SYSCALL_ABI_GRANT_COPY_TO
    );
}
