#include "micros/runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/bootstrap.h"
#include "micros/bootstrap_control.h"
#include "servers/vfs/vfs_service_core.h"

#define MICROS_VFS_SERVICE_DATA_MAGIC \
    UINT64_C(0x5646535345525643)
enum {
    MICROS_VFS_SERVICE_DATA_WORDS = 4096 / sizeof(uint64_t),
};

volatile struct micros_bootstrap_service_config
    micros_bootstrap_service_config;

static struct micros_vfs_state vfs_state;
/* Close initialized data before the page-aligned VFS state BSS. */
static volatile uint64_t
    vfs_service_data[MICROS_VFS_SERVICE_DATA_WORDS] = {
        MICROS_VFS_SERVICE_DATA_MAGIC,
    };

struct vfs_service_runtime {
    struct micros_vfs_service_endpoints endpoints;
};

static void runtime_clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static bool runtime_bytes_are_zero(
    const uint8_t *bytes,
    size_t size
)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static void runtime_write_u32_le(
    uint8_t *bytes,
    uint32_t value
)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint32_t runtime_read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

static bool send_ready(
    const struct micros_vfs_service_endpoints *endpoints
)
{
    struct micros_ipc_message message;

    if (endpoints == NULL) {
        return false;
    }
    runtime_clear_bytes(&message, sizeof(message));
    message.type = MICROS_BOOTSTRAP_MESSAGE_READY;
    runtime_write_u32_le(
        &message.payload[0],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    runtime_write_u32_le(
        &message.payload[4],
        micros_bootstrap_service_config.service_id
    );
    runtime_write_u32_le(
        &message.payload[8],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    runtime_write_u32_le(
        &message.payload[16],
        endpoints->self
    );
    if (
        micros_runtime_call(
            endpoints->launcher,
            &message
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return false;
    }
    return (
        message.source == endpoints->launcher
        && message.type == MICROS_BOOTSTRAP_MESSAGE_READY_ACK
        && message.reply_token == 0
        && runtime_read_u32_le(&message.payload[0])
            == MICROS_BOOTSTRAP_MANIFEST_VERSION
        && runtime_read_u32_le(&message.payload[4])
            == MICROS_VFS_SERVICE_ID
        && runtime_read_u32_le(&message.payload[8])
            == MICROS_BOOTSTRAP_MANIFEST_VERSION
        && runtime_read_u32_le(&message.payload[12]) == 0
        && runtime_read_u32_le(&message.payload[16])
            == endpoints->self
        && runtime_bytes_are_zero(
            &message.payload[20],
            sizeof(message.payload) - 20
        )
    );
}

static enum micros_vfs_client_result classify_client_result(
    micros_runtime_result_t result
)
{
    switch (result) {
    case MICROS_SYSCALL_ABI_OK:
        return MICROS_VFS_CLIENT_OK;
    case MICROS_SYSCALL_ABI_ARGUMENT:
    case MICROS_SYSCALL_ABI_DEAD_ENDPOINT:
    case MICROS_SYSCALL_ABI_UNAUTHORIZED:
    case MICROS_SYSCALL_ABI_STATE:
    case MICROS_SYSCALL_ABI_MEMORY_FAULT:
    case MICROS_SYSCALL_ABI_STALE_GRANT:
    case MICROS_SYSCALL_ABI_RANGE:
        return MICROS_VFS_CLIENT_REJECTED;
    case MICROS_SYSCALL_ABI_DEADLOCK:
    case MICROS_SYSCALL_ABI_REPLY_TOKEN:
    case MICROS_SYSCALL_ABI_REPLY_TOKEN_EXHAUSTED:
    case MICROS_SYSCALL_ABI_ENDPOINT_CLOSING:
    case MICROS_SYSCALL_ABI_CAPACITY:
    default:
        return MICROS_VFS_CLIENT_INVARIANT;
    }
}

static enum micros_vfs_client_result client_validate(
    void *context,
    micros_endpoint_t endpoint,
    micros_grant_t grant,
    uint64_t offset,
    size_t length,
    uint32_t permission
)
{
    (void)context;
    if (
        offset > SIZE_MAX
        || (
            permission != MICROS_GRANT_PERMISSION_READ
            && permission != MICROS_GRANT_PERMISSION_WRITE
        )
    ) {
        return MICROS_VFS_CLIENT_INVARIANT;
    }
    return classify_client_result(
        micros_runtime_grant_validate(
            endpoint,
            grant,
            (size_t)offset,
            length,
            permission
        )
    );
}

static enum micros_vfs_client_result client_copy(
    void *context,
    enum micros_vfs_client_direction direction,
    micros_endpoint_t endpoint,
    micros_grant_t grant,
    uint64_t offset,
    uint8_t *local,
    size_t length
)
{
    micros_runtime_result_t result;

    (void)context;
    if (
        offset > SIZE_MAX
        || (length != 0 && local == NULL)
    ) {
        return MICROS_VFS_CLIENT_INVARIANT;
    }
    if (direction == MICROS_VFS_CLIENT_FROM_APPLICATION) {
        result = micros_runtime_grant_copy_from(
            endpoint,
            grant,
            (size_t)offset,
            (uintptr_t)local,
            length
        );
    } else if (
        direction == MICROS_VFS_CLIENT_TO_APPLICATION
    ) {
        result = micros_runtime_grant_copy_to(
            endpoint,
            grant,
            (size_t)offset,
            (uintptr_t)local,
            length
        );
    } else {
        return MICROS_VFS_CLIENT_INVARIANT;
    }
    return classify_client_result(result);
}

static enum micros_vfs_backend_grant_result backend_grant_create(
    void *context,
    micros_endpoint_t endpoint,
    uint8_t *local,
    size_t length,
    uint32_t permission,
    micros_grant_t *grant
)
{
    const struct vfs_service_runtime *runtime = context;
    micros_runtime_result_t result;

    if (
        runtime == NULL
        || grant == NULL
        || local == NULL
        || length == 0
        || length > MICROS_VFS_TRANSFER_MAX
        || (
            endpoint != runtime->endpoints.ramfs
            && endpoint != runtime->endpoints.tty
        )
        || (
            permission != MICROS_GRANT_PERMISSION_READ
            && permission != MICROS_GRANT_PERMISSION_WRITE
        )
    ) {
        return MICROS_VFS_BACKEND_GRANT_INVARIANT;
    }
    result = micros_runtime_grant_create(
        endpoint,
        (uintptr_t)local,
        length,
        permission,
        grant
    );
    if (result == MICROS_SYSCALL_ABI_OK) {
        return (
            *grant != MICROS_GRANT_NONE
            ? MICROS_VFS_BACKEND_GRANT_OK
            : MICROS_VFS_BACKEND_GRANT_INVARIANT
        );
    }
    if (result == MICROS_SYSCALL_ABI_CAPACITY) {
        return MICROS_VFS_BACKEND_GRANT_CAPACITY;
    }
    return MICROS_VFS_BACKEND_GRANT_INVARIANT;
}

static enum micros_vfs_backend_status backend_grant_revoke(
    void *context,
    micros_grant_t grant
)
{
    (void)context;
    return (
        grant != MICROS_GRANT_NONE
        && micros_runtime_grant_revoke(grant)
            == MICROS_SYSCALL_ABI_OK
        ? MICROS_VFS_BACKEND_OK
        : MICROS_VFS_BACKEND_INVARIANT
    );
}

static enum micros_vfs_backend_status ramfs_call(
    void *context,
    const struct micros_vfs_ramfs_request *request,
    struct micros_vfs_ramfs_response *response
)
{
    const struct vfs_service_runtime *runtime = context;
    struct micros_ipc_message message;

    if (
        runtime == NULL
        || request == NULL
        || response == NULL
        || micros_vfs_service_build_ramfs_call(
            request,
            &message
        ) != MICROS_VFS_SERVICE_OK
        || micros_runtime_call(
            runtime->endpoints.ramfs,
            &message
        ) != MICROS_SYSCALL_ABI_OK
        || micros_vfs_service_decode_ramfs_result(
            runtime->endpoints.ramfs,
            request,
            &message,
            response
        ) != MICROS_VFS_SERVICE_OK
    ) {
        return MICROS_VFS_BACKEND_INVARIANT;
    }
    return MICROS_VFS_BACKEND_OK;
}

static enum micros_vfs_backend_status tty_call(
    void *context,
    const struct micros_vfs_tty_request *request,
    struct micros_vfs_tty_response *response
)
{
    const struct vfs_service_runtime *runtime = context;
    struct micros_ipc_message message;

    if (
        runtime == NULL
        || request == NULL
        || response == NULL
        || micros_vfs_service_build_tty_call(
            request,
            &message
        ) != MICROS_VFS_SERVICE_OK
        || micros_runtime_call(
            runtime->endpoints.tty,
            &message
        ) != MICROS_SYSCALL_ABI_OK
        || micros_vfs_service_decode_tty_result(
            runtime->endpoints.tty,
            request,
            &message,
            response
        ) != MICROS_VFS_SERVICE_OK
    ) {
        return MICROS_VFS_BACKEND_INVARIANT;
    }
    return MICROS_VFS_BACKEND_OK;
}

void micros_service_main(void)
{
    struct vfs_service_runtime runtime;
#if defined(MICROS_BUILD_VFS_SERVICE_TEST)
    enum micros_vfs_trusted_result attach_result;
    micros_endpoint_t application_endpoint;
#endif
    const struct micros_vfs_io io = {
        .client_validate = client_validate,
        .client_copy = client_copy,
        .backend_grant_create = backend_grant_create,
        .backend_grant_revoke = backend_grant_revoke,
        .ramfs_call = ramfs_call,
        .tty_call = tty_call,
        .context = &runtime,
    };
    struct micros_ipc_message message;
    bool configuration_is_valid;

#if defined(MICROS_BUILD_VFS_SERVICE_TEST)
    configuration_is_valid =
        micros_vfs_service_validate_test_configuration(
            &micros_bootstrap_service_config,
            &runtime.endpoints,
            &application_endpoint
        );
#else
    configuration_is_valid =
        micros_vfs_service_validate_configuration(
            &micros_bootstrap_service_config,
            &runtime.endpoints
        );
#endif

    if (
        vfs_service_data[0] != MICROS_VFS_SERVICE_DATA_MAGIC
        || !configuration_is_valid
        || micros_vfs_state_initialize(
            &vfs_state,
            runtime.endpoints.self,
            runtime.endpoints.ramfs,
            runtime.endpoints.tty
        ) != MICROS_VFS_CORE_OK
        || micros_vfs_mount(&vfs_state, &io)
            != MICROS_VFS_CORE_OK
#if defined(MICROS_BUILD_VFS_SERVICE_TEST)
        || micros_vfs_attach_console(
            &vfs_state,
            application_endpoint,
            0,
            &attach_result
        ) != MICROS_VFS_CORE_OK
        || attach_result != MICROS_VFS_TRUSTED_OK
#endif
        || micros_vfs_state_validate(&vfs_state)
            != MICROS_VFS_CORE_OK
        || !send_ready(&runtime.endpoints)
    ) {
        __builtin_trap();
    }

    runtime_clear_bytes(&message, sizeof(message));
    if (
        micros_runtime_receive(
            MICROS_ENDPOINT_ANY,
            &message
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        __builtin_trap();
    }
    for (;;) {
        struct micros_vfs_service_reply_action action;
        struct micros_ipc_message next;
        micros_runtime_result_t result;

        if (
            micros_vfs_service_handle_message(
                &vfs_state,
                &message,
                &io,
                &action
            ) != MICROS_VFS_SERVICE_OK
        ) {
            __builtin_trap();
        }
        runtime_clear_bytes(&next, sizeof(next));
        if (action.active) {
            result = micros_runtime_reply_receive(
                action.reply_token,
                &action.message,
                MICROS_ENDPOINT_ANY,
                &next
            );
        } else {
            result = micros_runtime_receive(
                MICROS_ENDPOINT_ANY,
                &next
            );
        }
        if (result != MICROS_SYSCALL_ABI_OK) {
            __builtin_trap();
        }
        message = next;
    }
}
