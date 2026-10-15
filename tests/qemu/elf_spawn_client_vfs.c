#include "tests/qemu/elf_spawn_client.h"

#include "micros/application_runtime.h"
#include "micros/runtime.h"
#include "micros/vfs.h"
#include "servers/vfs/vfs_core.h"

struct elf_spawn_vfs_result {
    int32_t result;
    uint32_t descriptor;
    uint32_t mode;
    uint64_t count;
    uint64_t position;
};

static bool elf_spawn_vfs_call(
    struct micros_ipc_message *message, uint32_t request_type,
    struct elf_spawn_vfs_result *result
)
{
    struct elf_spawn_vfs_result candidate;

    if (
        micros_runtime_call(micros_application_vfs_endpoint, message) != MICROS_SYSCALL_ABI_OK
        || message->source != micros_application_vfs_endpoint
        || message->type != MICROS_VFS_MESSAGE_RESULT || message->reply_token != 0
        || elf_spawn_client_read_u32(message->payload) != MICROS_VFS_PROTOCOL_VERSION
        || elf_spawn_client_read_u32(message->payload + 4) != request_type
        || elf_spawn_client_read_u32(message->payload + 12) != 0
        || !elf_spawn_client_bytes_zero(message->payload + 40, 8)
    ) {
        return false;
    }
    candidate.result = (int32_t)elf_spawn_client_read_u32(message->payload + 8);
    candidate.descriptor = elf_spawn_client_read_u32(message->payload + 16);
    candidate.mode = elf_spawn_client_read_u32(message->payload + 20);
    candidate.count = elf_spawn_client_read_u64(message->payload + 24);
    candidate.position = elf_spawn_client_read_u64(message->payload + 32);
    if (candidate.result < MICROS_VFS_RESULT_RANGE || candidate.result > MICROS_VFS_RESULT_OK) {
        return false;
    }
    if (candidate.result != MICROS_VFS_RESULT_OK
        && (candidate.descriptor != 0 || candidate.mode != 0
            || candidate.count != 0 || candidate.position != 0)) {
        return false;
    }
    *result = candidate;
    return true;
}

static bool elf_spawn_vfs_transfer(
    struct micros_elf_spawn_client *client, uint32_t type, uint32_t descriptor,
    size_t count, struct elf_spawn_vfs_result *result
)
{
    struct micros_ipc_message message = {0};
    micros_grant_t grant = MICROS_GRANT_NONE;
    uint32_t permission = type == MICROS_VFS_MESSAGE_WRITE
        ? MICROS_GRANT_PERMISSION_READ : MICROS_GRANT_PERMISSION_WRITE;
    bool called;
    bool revoked;

    if (
        client == NULL || count == 0 || count > sizeof(client->bytes)
        || descriptor >= MICROS_SPAWN_DESCRIPTOR_CAPACITY
        || micros_runtime_grant_create(
            micros_application_vfs_endpoint, (uintptr_t)client->bytes, count,
            permission, &grant
        ) != MICROS_SYSCALL_ABI_OK || grant == MICROS_GRANT_NONE
    ) {
        return false;
    }
    message.type = type;
    elf_spawn_client_write_u32(message.payload, MICROS_VFS_PROTOCOL_VERSION);
    elf_spawn_client_write_u32(message.payload + 8, descriptor);
    elf_spawn_client_write_u32(message.payload + 12, grant);
    elf_spawn_client_write_u32(message.payload + 24, (uint32_t)count);
    called = elf_spawn_vfs_call(&message, type, result);
    revoked = micros_runtime_grant_revoke(grant) == MICROS_SYSCALL_ABI_OK;
    return called && revoked && result->result == MICROS_VFS_RESULT_OK
        && result->descriptor == 0 && result->mode == 0 && result->count <= count;
}

bool micros_elf_spawn_client_write(
    struct micros_elf_spawn_client *client, uint32_t descriptor,
    const uint8_t *bytes, size_t size
)
{
    struct elf_spawn_vfs_result result;
    size_t index;

    if (client == NULL || bytes == NULL || size == 0 || size > sizeof(client->bytes)) {
        return false;
    }
    for (index = 0; index < size; ++index) {
        client->bytes[index] = bytes[index];
    }
    return elf_spawn_vfs_transfer(client, MICROS_VFS_MESSAGE_WRITE, descriptor, size, &result)
        && result.count == size && result.position == 0;
}

static bool elf_spawn_vfs_open_data(
    struct micros_elf_spawn_client *client, uint32_t *descriptor
)
{
    struct micros_ipc_message message = {0};
    struct elf_spawn_vfs_result result;
    micros_grant_t grant = MICROS_GRANT_NONE;
    size_t index;
    bool called;
    bool revoked;

    for (index = 0; index < sizeof(MICROS_ELF_SPAWN_DATA_PATH); ++index) {
        client->bytes[index] = (uint8_t)MICROS_ELF_SPAWN_DATA_PATH[index];
    }
    if (micros_runtime_grant_create(
        micros_application_vfs_endpoint, (uintptr_t)client->bytes,
        sizeof(MICROS_ELF_SPAWN_DATA_PATH), MICROS_GRANT_PERMISSION_READ, &grant
    ) != MICROS_SYSCALL_ABI_OK || grant == MICROS_GRANT_NONE) {
        return false;
    }
    message.type = MICROS_VFS_MESSAGE_OPEN;
    elf_spawn_client_write_u32(message.payload, MICROS_VFS_PROTOCOL_VERSION);
    elf_spawn_client_write_u32(message.payload + 8, grant);
    elf_spawn_client_write_u32(message.payload + 12, sizeof(MICROS_ELF_SPAWN_DATA_PATH));
    elf_spawn_client_write_u32(message.payload + 24, MICROS_VFS_OPEN_READ);
    called = elf_spawn_vfs_call(&message, MICROS_VFS_MESSAGE_OPEN, &result);
    revoked = micros_runtime_grant_revoke(grant) == MICROS_SYSCALL_ABI_OK;
    if (
        !called || !revoked || result.result != MICROS_VFS_RESULT_OK
        || result.descriptor != 3 || result.mode != (UINT32_C(0x8000) | UINT32_C(0444))
        || result.count != 0 || result.position != 0
    ) {
        return false;
    }
    *descriptor = result.descriptor;
    return true;
}

static bool elf_spawn_vfs_close(uint32_t descriptor)
{
    struct micros_ipc_message message = {0};
    struct elf_spawn_vfs_result result;

    message.type = MICROS_VFS_MESSAGE_CLOSE;
    elf_spawn_client_write_u32(message.payload, MICROS_VFS_PROTOCOL_VERSION);
    elf_spawn_client_write_u32(message.payload + 8, descriptor);
    return elf_spawn_vfs_call(&message, MICROS_VFS_MESSAGE_CLOSE, &result)
        && result.result == MICROS_VFS_RESULT_OK && result.descriptor == 0
        && result.mode == 0 && result.count == 0 && result.position == 0;
}

bool micros_elf_spawn_client_read_data(struct micros_elf_spawn_client *client)
{
    struct elf_spawn_vfs_result result;
    uint32_t descriptor;
    bool read_ok;
    bool closed;

    if (client == NULL || !elf_spawn_vfs_open_data(client, &descriptor)) {
        return false;
    }
    read_ok = elf_spawn_vfs_transfer(
        client, MICROS_VFS_MESSAGE_READ, descriptor, sizeof(client->bytes), &result
    ) && result.count == sizeof(MICROS_ELF_SPAWN_DATA) - 1
        && result.position == sizeof(MICROS_ELF_SPAWN_DATA) - 1
        && elf_spawn_client_bytes_equal(client->bytes, MICROS_ELF_SPAWN_DATA,
                                         sizeof(MICROS_ELF_SPAWN_DATA) - 1);
    closed = elf_spawn_vfs_close(descriptor);
    return read_ok && closed;
}

bool micros_elf_spawn_client_drain(void)
{
    struct micros_ipc_message message = {0};
    struct elf_spawn_vfs_result result;

    message.type = MICROS_VFS_TEST_MESSAGE_DRAIN;
    elf_spawn_client_write_u32(message.payload, MICROS_VFS_PROTOCOL_VERSION);
    return elf_spawn_vfs_call(&message, MICROS_VFS_TEST_MESSAGE_DRAIN, &result)
        && result.result == MICROS_VFS_RESULT_OK && result.descriptor == 0
        && result.mode == 0 && result.count == 0 && result.position == 0;
}
