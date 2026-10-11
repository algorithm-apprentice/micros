#include "tests/qemu/vfs_service_probe_internal.h"

void probe_clear_bytes(void *storage, size_t size) {
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

void probe_copy_bytes(uint8_t *destination, const uint8_t *source, size_t size) {
    size_t index;

    for (index = 0; index < size; ++index) {
        destination[index] = source[index];
    }
}

bool probe_bytes_equal(const uint8_t *left, const uint8_t *right, size_t size) {
    size_t index;

    for (index = 0; index < size; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

bool probe_bytes_are_zero(const uint8_t *bytes, size_t size) {
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

size_t probe_string_size(const char *string) {
    size_t size = 0;

    while (string[size] != '\0') {
        ++size;
    }
    return size;
}

void probe_write_u32_le(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

void probe_write_u64_le(uint8_t *bytes, uint64_t value) {
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

uint32_t probe_read_u32_le(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
            (uint32_t)bytes[3] << 24);
}

uint64_t probe_read_u64_le(const uint8_t *bytes) {
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static bool probe_vfs_result_matches(const struct micros_ipc_message *message,
                                     micros_endpoint_t vfs_endpoint, uint32_t request_type,
                                     struct probe_vfs_result *result) {
    int32_t result_code;

    if (message->source != vfs_endpoint || message->type != MICROS_VFS_MESSAGE_RESULT ||
        message->reply_token != 0 ||
        probe_read_u32_le(&message->payload[0]) != MICROS_VFS_PROTOCOL_VERSION ||
        probe_read_u32_le(&message->payload[4]) != request_type ||
        probe_read_u32_le(&message->payload[12]) != 0 ||
        !probe_bytes_are_zero(&message->payload[40], 8)) {
        return false;
    }
    result_code = (int32_t)probe_read_u32_le(&message->payload[8]);
    if (result_code < MICROS_VFS_RESULT_RANGE || result_code > MICROS_VFS_RESULT_OK) {
        return false;
    }
    result->result = (enum micros_vfs_result)result_code;
    result->descriptor = probe_read_u32_le(&message->payload[16]);
    result->mode = probe_read_u32_le(&message->payload[20]);
    result->transferred_count = probe_read_u64_le(&message->payload[24]);
    result->position = probe_read_u64_le(&message->payload[32]);
    if (result->result != MICROS_VFS_RESULT_OK) {
        return (result->descriptor == 0 && result->mode == 0 && result->transferred_count == 0 &&
                result->position == 0);
    }
    if (request_type == MICROS_VFS_MESSAGE_OPEN) {
        return (result->transferred_count == 0 && result->position == 0);
    }
    if (request_type == MICROS_VFS_MESSAGE_READ || request_type == MICROS_VFS_MESSAGE_WRITE ||
        request_type == MICROS_VFS_MESSAGE_GETDENTS) {
        return (result->descriptor == 0 && result->mode == 0 &&
                result->transferred_count <= MICROS_VFS_TRANSFER_MAX);
    }
    return (result->descriptor == 0 && result->mode == 0 && result->transferred_count == 0 &&
            result->position == 0);
}

bool probe_call_vfs(micros_endpoint_t vfs_endpoint, struct micros_ipc_message *message,
                    uint32_t request_type,
                    struct probe_vfs_result *result) {
    return (micros_runtime_call(vfs_endpoint, message) == MICROS_SYSCALL_ABI_OK &&
            probe_vfs_result_matches(message, vfs_endpoint, request_type, result));
}

static bool probe_create_grant(struct vfs_service_probe_context *context,
                               micros_endpoint_t vfs_endpoint, size_t length,
                               uint32_t permissions, micros_grant_t *grant) {
    return (length > 0 && length <= sizeof(context->buffer) &&
            micros_runtime_grant_create(vfs_endpoint, (uintptr_t)context->buffer, length,
                                        permissions, grant) == MICROS_SYSCALL_ABI_OK &&
            *grant != MICROS_GRANT_NONE);
}

bool probe_open(struct vfs_service_probe_context *context, micros_endpoint_t vfs_endpoint,
                const char *path, uint32_t open_flags, uint32_t mode,
                struct probe_vfs_result *result) {
    struct micros_ipc_message message;
    micros_grant_t grant = MICROS_GRANT_NONE;
    size_t path_length = probe_string_size(path) + 1;
    bool call_ok;
    bool revoke_ok;

    if (path_length > sizeof(context->buffer)) {
        return false;
    }
    probe_clear_bytes(context->buffer, sizeof(context->buffer));
    probe_copy_bytes(context->buffer, (const uint8_t *)path, path_length);
    if (!probe_create_grant(context, vfs_endpoint, path_length, MICROS_GRANT_PERMISSION_READ,
                            &grant)) {
        return false;
    }
    probe_clear_bytes(&message, sizeof(message));
    message.type = MICROS_VFS_MESSAGE_OPEN;
    probe_write_u32_le(&message.payload[0], MICROS_VFS_PROTOCOL_VERSION);
    probe_write_u32_le(&message.payload[8], grant);
    probe_write_u32_le(&message.payload[12], (uint32_t)path_length);
    probe_write_u64_le(&message.payload[16], 0);
    probe_write_u32_le(&message.payload[24], open_flags);
    probe_write_u32_le(&message.payload[28], mode);
    call_ok = probe_call_vfs(vfs_endpoint, &message, MICROS_VFS_MESSAGE_OPEN, result);
    revoke_ok = (micros_runtime_grant_revoke(grant) == MICROS_SYSCALL_ABI_OK);
    return call_ok && revoke_ok;
}

bool probe_path_call(struct vfs_service_probe_context *context, micros_endpoint_t vfs_endpoint,
                     uint32_t type, const char *path, uint32_t mode,
                     struct probe_vfs_result *result) {
    struct micros_ipc_message message;
    micros_grant_t grant = MICROS_GRANT_NONE;
    size_t path_length = probe_string_size(path) + 1;
    bool call_ok;
    bool revoke_ok;

    if (path_length > sizeof(context->buffer)) {
        return false;
    }
    probe_clear_bytes(context->buffer, sizeof(context->buffer));
    probe_copy_bytes(context->buffer, (const uint8_t *)path, path_length);
    if (!probe_create_grant(context, vfs_endpoint, path_length, MICROS_GRANT_PERMISSION_READ,
                            &grant)) {
        return false;
    }
    probe_clear_bytes(&message, sizeof(message));
    message.type = type;
    probe_write_u32_le(&message.payload[0], MICROS_VFS_PROTOCOL_VERSION);
    probe_write_u32_le(&message.payload[8], grant);
    probe_write_u32_le(&message.payload[12], (uint32_t)path_length);
    probe_write_u64_le(&message.payload[16], 0);
    if (type == MICROS_VFS_MESSAGE_MKDIR) {
        probe_write_u32_le(&message.payload[24], mode);
    }
    call_ok = probe_call_vfs(vfs_endpoint, &message, type, result);
    revoke_ok = (micros_runtime_grant_revoke(grant) == MICROS_SYSCALL_ABI_OK);
    return call_ok && revoke_ok;
}

bool probe_close(micros_endpoint_t vfs_endpoint, uint32_t descriptor) {
    struct micros_ipc_message message;
    struct probe_vfs_result result;

    probe_clear_bytes(&message, sizeof(message));
    message.type = MICROS_VFS_MESSAGE_CLOSE;
    probe_write_u32_le(&message.payload[0], MICROS_VFS_PROTOCOL_VERSION);
    probe_write_u32_le(&message.payload[8], descriptor);
    return (probe_call_vfs(vfs_endpoint, &message, MICROS_VFS_MESSAGE_CLOSE, &result) &&
            result.result == MICROS_VFS_RESULT_OK && result.descriptor == 0 && result.mode == 0 &&
            result.transferred_count == 0 && result.position == 0);
}

bool probe_transfer(struct vfs_service_probe_context *context, micros_endpoint_t vfs_endpoint,
                    uint32_t type, uint32_t descriptor, uint32_t count,
                    struct probe_vfs_result *result) {
    struct micros_ipc_message message;
    micros_grant_t grant = MICROS_GRANT_NONE;
    uint32_t permissions;
    bool call_ok;
    bool revoke_ok;

    permissions = type == MICROS_VFS_MESSAGE_WRITE ? MICROS_GRANT_PERMISSION_READ
                                                   : MICROS_GRANT_PERMISSION_WRITE;
    if (!probe_create_grant(context, vfs_endpoint, count, permissions, &grant)) {
        return false;
    }
    probe_clear_bytes(&message, sizeof(message));
    message.type = type;
    probe_write_u32_le(&message.payload[0], MICROS_VFS_PROTOCOL_VERSION);
    probe_write_u32_le(&message.payload[8], descriptor);
    probe_write_u32_le(&message.payload[12], grant);
    probe_write_u64_le(&message.payload[16], 0);
    probe_write_u32_le(&message.payload[24], count);
    call_ok = probe_call_vfs(vfs_endpoint, &message, type, result);
    revoke_ok = (micros_runtime_grant_revoke(grant) == MICROS_SYSCALL_ABI_OK);
    return call_ok && revoke_ok;
}

bool probe_write(struct vfs_service_probe_context *context, micros_endpoint_t vfs_endpoint,
                 uint32_t descriptor, const uint8_t *bytes, size_t size) {
    struct probe_vfs_result result;

    if (size == 0 || size > sizeof(context->buffer)) {
        return false;
    }
    probe_clear_bytes(context->buffer, sizeof(context->buffer));
    probe_copy_bytes(context->buffer, bytes, size);
    return (
        probe_transfer(context, vfs_endpoint, MICROS_VFS_MESSAGE_WRITE, descriptor,
                       (uint32_t)size, &result) &&
        result.result == MICROS_VFS_RESULT_OK && result.descriptor == 0 && result.mode == 0 &&
        result.transferred_count == size && result.position == 0);
}

bool probe_read_exact(struct vfs_service_probe_context *context, micros_endpoint_t vfs_endpoint,
                      uint32_t descriptor, const uint8_t *expected, size_t expected_size,
                      bool require_eof) {
    struct probe_vfs_result result;
    uint64_t position;

    probe_clear_bytes(context->buffer, sizeof(context->buffer));
    if (!probe_transfer(context, vfs_endpoint, MICROS_VFS_MESSAGE_READ, descriptor,
                        sizeof(context->buffer), &result) ||
        result.result != MICROS_VFS_RESULT_OK || result.descriptor != 0 || result.mode != 0 ||
        result.transferred_count != expected_size || result.position != expected_size ||
        !probe_bytes_equal(context->buffer, expected, expected_size)) {
        return false;
    }
    position = result.position;
    if (!require_eof) {
        return true;
    }
    probe_clear_bytes(context->buffer, sizeof(context->buffer));
    return (probe_transfer(context, vfs_endpoint, MICROS_VFS_MESSAGE_READ, descriptor,
                           sizeof(context->buffer), &result) &&
            result.result == MICROS_VFS_RESULT_OK && result.transferred_count == 0 &&
            result.position == position);
}
