#include "tests/qemu/ramfs_service_vfs_internal.h"

static void vfs_initialize_ramfs_request(
    struct micros_ipc_message *message,
    uint32_t type,
    uint32_t version
)
{
    vfs_clear_bytes(message, sizeof(*message));
    message->type = type;
    vfs_write_u32_le(&message->payload[0], version);
}

static bool vfs_ramfs_result_matches(
    struct vfs_service_fixture *fixture,
    const struct micros_ipc_message *message,
    uint32_t request_type,
    struct vfs_ramfs_result *result
)
{
    if (
        message == NULL
        || result == NULL
        || message->source != fixture->vfs_ramfs_endpoint
        || message->type != MICROS_RAMFS_MESSAGE_RESULT
        || message->reply_token != 0
        || vfs_read_u32_le(&message->payload[0])
            != MICROS_RAMFS_PROTOCOL_VERSION
        || vfs_read_u32_le(&message->payload[4]) != request_type
        || vfs_read_u32_le(&message->payload[12]) != 0
    ) {
        return false;
    }
    result->result = (enum micros_ramfs_result)(
        (int32_t)vfs_read_u32_le(&message->payload[8])
    );
    result->node = vfs_read_u64_le(&message->payload[16]);
    result->file_size = vfs_read_u64_le(&message->payload[24]);
    result->position = vfs_read_u64_le(&message->payload[32]);
    result->count = vfs_read_u32_le(&message->payload[40]);
    result->mode = vfs_read_u32_le(&message->payload[44]);
    if (
        result->result != MICROS_RAMFS_RESULT_OK
        && (
            result->node != 0
            || result->file_size != 0
            || result->position != 0
            || result->count != 0
            || result->mode != 0
        )
    ) {
        return false;
    }
    return true;
}

static bool vfs_call_ramfs(
    struct vfs_service_fixture *fixture,
    struct micros_ipc_message *message,
    uint32_t request_type,
    struct vfs_ramfs_result *result
)
{
    return (
        micros_runtime_call(fixture->vfs_ramfs_endpoint, message)
            == MICROS_SYSCALL_ABI_OK
        && vfs_ramfs_result_matches(fixture, message, request_type, result)
    );
}

static bool vfs_call_ramfs_with_grant(
    struct vfs_service_fixture *fixture,
    struct micros_ipc_message *message,
    uint32_t request_type,
    size_t grant_field_offset,
    uint32_t permissions,
    struct vfs_ramfs_result *result
)
{
    micros_grant_t grant = MICROS_GRANT_NONE;
    bool call_ok;
    bool revoke_ok;

    if (
        micros_runtime_grant_create(
            fixture->vfs_ramfs_endpoint,
            (uintptr_t)fixture->vfs_ramfs_buffer,
            sizeof(fixture->vfs_ramfs_buffer),
            permissions,
            &grant
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return false;
    }
    vfs_write_u32_le(&message->payload[grant_field_offset], grant);
    call_ok = vfs_call_ramfs(fixture, message, request_type, result);
    revoke_ok = (
        micros_runtime_grant_revoke(grant) == MICROS_SYSCALL_ABI_OK
    );
    return call_ok && revoke_ok;
}

bool vfs_result_has_no_fields(
    const struct vfs_ramfs_result *result
)
{
    return (
        result->node == 0
        && result->file_size == 0
        && result->position == 0
        && result->count == 0
        && result->mode == 0
    );
}

bool vfs_mount(
    struct vfs_service_fixture *fixture,
    uint64_t *root
)
{
    struct micros_ipc_message message;
    struct vfs_ramfs_result result;

    vfs_initialize_ramfs_request(
        &message,
        MICROS_RAMFS_MESSAGE_MOUNT,
        MICROS_RAMFS_PROTOCOL_VERSION
    );
    if (
        root == NULL
        || !vfs_call_ramfs(
            fixture,
            &message,
            MICROS_RAMFS_MESSAGE_MOUNT,
            &result
        )
        || result.result != MICROS_RAMFS_RESULT_OK
        || result.node == 0
        || result.file_size != 0
        || result.position != 0
        || result.count != 0
        || result.mode
            != (MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755))
    ) {
        return false;
    }
    *root = result.node;
    return true;
}

bool vfs_lookup(
    struct vfs_service_fixture *fixture,
    uint64_t start,
    uint64_t root,
    const uint8_t *path,
    size_t length,
    struct vfs_ramfs_result *result
)
{
    struct micros_ipc_message message;

    if (
        path == NULL
        || length == 0
        || length > sizeof(fixture->vfs_ramfs_buffer)
    ) {
        return false;
    }
    vfs_clear_bytes(fixture->vfs_ramfs_buffer, sizeof(fixture->vfs_ramfs_buffer));
    vfs_copy_bytes(fixture->vfs_ramfs_buffer, path, length);
    vfs_initialize_ramfs_request(
        &message,
        MICROS_RAMFS_MESSAGE_LOOKUP,
        MICROS_RAMFS_PROTOCOL_VERSION
    );
    vfs_write_u64_le(&message.payload[8], start);
    vfs_write_u64_le(&message.payload[16], root);
    vfs_write_u32_le(&message.payload[28], (uint32_t)length);
    return vfs_call_ramfs_with_grant(
        fixture,
        &message,
        MICROS_RAMFS_MESSAGE_LOOKUP,
        24,
        MICROS_GRANT_PERMISSION_READ,
        result
    );
}

bool vfs_create_node(
    struct vfs_service_fixture *fixture,
    uint32_t type,
    uint64_t parent,
    uint32_t mode,
    const uint8_t *name,
    size_t length,
    struct vfs_ramfs_result *result
)
{
    struct micros_ipc_message message;

    if (
        name == NULL
        || length == 0
        || length > sizeof(fixture->vfs_ramfs_buffer)
    ) {
        return false;
    }
    vfs_clear_bytes(fixture->vfs_ramfs_buffer, sizeof(fixture->vfs_ramfs_buffer));
    vfs_copy_bytes(fixture->vfs_ramfs_buffer, name, length);
    vfs_initialize_ramfs_request(
        &message,
        type,
        MICROS_RAMFS_PROTOCOL_VERSION
    );
    vfs_write_u64_le(&message.payload[8], parent);
    vfs_write_u32_le(&message.payload[16], mode);
    vfs_write_u64_le(&message.payload[24], 0);
    vfs_write_u32_le(&message.payload[32], (uint32_t)length);
    return vfs_call_ramfs_with_grant(
        fixture,
        &message,
        type,
        20,
        MICROS_GRANT_PERMISSION_READ,
        result
    );
}

bool vfs_transfer(
    struct vfs_service_fixture *fixture,
    uint32_t type,
    uint64_t node,
    uint64_t file_offset,
    uint32_t count,
    uint32_t permissions,
    struct vfs_ramfs_result *result
)
{
    struct micros_ipc_message message;

    vfs_initialize_ramfs_request(
        &message,
        type,
        MICROS_RAMFS_PROTOCOL_VERSION
    );
    vfs_write_u64_le(&message.payload[8], node);
    vfs_write_u64_le(&message.payload[16], file_offset);
    vfs_write_u32_le(&message.payload[28], count);
    return vfs_call_ramfs_with_grant(
        fixture,
        &message,
        type,
        24,
        permissions,
        result
    );
}

bool vfs_transfer_without_grant(
    struct vfs_service_fixture *fixture,
    uint32_t type,
    uint64_t node,
    uint64_t file_offset,
    uint32_t count,
    struct vfs_ramfs_result *result
)
{
    struct micros_ipc_message message;

    vfs_initialize_ramfs_request(
        &message,
        type,
        MICROS_RAMFS_PROTOCOL_VERSION
    );
    vfs_write_u64_le(&message.payload[8], node);
    vfs_write_u64_le(&message.payload[16], file_offset);
    vfs_write_u32_le(&message.payload[24], MICROS_GRANT_NONE);
    vfs_write_u32_le(&message.payload[28], count);
    return vfs_call_ramfs(fixture, &message, type, result);
}

bool vfs_getdent(
    struct vfs_service_fixture *fixture,
    uint64_t node,
    uint64_t cursor,
    struct vfs_ramfs_result *result
)
{
    struct micros_ipc_message message;

    vfs_fill_bytes(
        fixture->vfs_ramfs_buffer,
        UINT8_C(0xa5),
        sizeof(fixture->vfs_ramfs_buffer)
    );
    vfs_initialize_ramfs_request(
        &message,
        MICROS_RAMFS_MESSAGE_GETDENTS,
        MICROS_RAMFS_PROTOCOL_VERSION
    );
    vfs_write_u64_le(&message.payload[8], node);
    vfs_write_u64_le(&message.payload[16], cursor);
    vfs_write_u32_le(
        &message.payload[28],
        MICROS_RAMFS_DIRECTORY_RECORD_SIZE
    );
    return vfs_call_ramfs_with_grant(
        fixture,
        &message,
        MICROS_RAMFS_MESSAGE_GETDENTS,
        24,
        MICROS_GRANT_PERMISSION_WRITE,
        result
    );
}

bool vfs_putnode(
    struct vfs_service_fixture *fixture,
    uint64_t node,
    uint32_t count,
    enum micros_ramfs_result expected
)
{
    struct micros_ipc_message message;
    struct vfs_ramfs_result result;

    vfs_initialize_ramfs_request(
        &message,
        MICROS_RAMFS_MESSAGE_PUTNODE,
        MICROS_RAMFS_PROTOCOL_VERSION
    );
    vfs_write_u64_le(&message.payload[8], node);
    vfs_write_u32_le(&message.payload[16], count);
    return (
        vfs_call_ramfs(
            fixture,
            &message,
            MICROS_RAMFS_MESSAGE_PUTNODE,
            &result
        )
        && result.result == expected
        && vfs_result_has_no_fields(&result)
    );
}

bool vfs_malformed_version_is_exact(
    struct vfs_service_fixture *fixture,
    uint64_t root
)
{
    struct micros_ipc_message message;
    struct vfs_ramfs_result result;

    vfs_initialize_ramfs_request(
        &message,
        MICROS_RAMFS_MESSAGE_PUTNODE,
        MICROS_RAMFS_PROTOCOL_VERSION + 1
    );
    vfs_write_u64_le(&message.payload[8], root);
    vfs_write_u32_le(&message.payload[16], 1);
    return (
        vfs_call_ramfs(
            fixture,
            &message,
            MICROS_RAMFS_MESSAGE_PUTNODE,
            &result
        )
        && result.result == MICROS_RAMFS_RESULT_BAD_VERSION
        && vfs_result_has_no_fields(&result)
    );
}
