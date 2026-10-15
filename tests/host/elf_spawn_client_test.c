#include "tests/host/elf_spawn_fixture_test_internal.h"

#include <stdio.h>
#include <string.h>

#include "micros/application_runtime.h"
#include "micros/runtime.h"
#include "micros/vfs.h"
#include "tests/qemu/elf_spawn_client.h"

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #condition); \
            return false; \
        } \
    } while (0)

micros_endpoint_t micros_application_pm_endpoint = UINT32_C(0x1002);
micros_endpoint_t micros_application_vfs_endpoint = UINT32_C(0x1005);
uint64_t micros_application_pid = MICROS_ELF_SPAWN_INIT_PID;
uint64_t micros_application_page_size = 4096;

static struct {
    micros_runtime_result_t call_result;
    micros_runtime_result_t revoke_result;
    struct micros_ipc_message sent;
    micros_endpoint_t grantee;
    uintptr_t base;
    size_t length;
    uint32_t permission;
    uint32_t request_type;
    size_t calls;
    size_t revokes;
    bool malformed_reply;
} client_context;

static void client_test_write_u32(uint8_t *bytes, uint32_t value)
{
    size_t index;

    for (index = 0; index < 4; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

static void client_test_write_u64(uint8_t *bytes, uint64_t value)
{
    client_test_write_u32(bytes, (uint32_t)value);
    client_test_write_u32(bytes + 4, (uint32_t)(value >> 32));
}

static uint32_t client_test_read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

micros_runtime_result_t micros_runtime_grant_create(
    micros_endpoint_t grantee, uintptr_t base, size_t length,
    uint32_t permissions, micros_grant_t *grant
)
{
    client_context.grantee = grantee;
    client_context.base = base;
    client_context.length = length;
    client_context.permission = permissions;
    *grant = 64;
    return MICROS_SYSCALL_ABI_OK;
}

micros_runtime_result_t micros_runtime_grant_revoke(micros_grant_t grant)
{
    if (grant != 64) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    ++client_context.revokes;
    return client_context.revoke_result;
}

static void client_test_pm_reply(struct micros_ipc_message *message, uint32_t type)
{
    message->type = MICROS_PM_MESSAGE_RESULT;
    client_test_write_u32(message->payload, MICROS_PM_PROTOCOL_VERSION);
    client_test_write_u32(message->payload + 4, type);
    client_test_write_u64(message->payload + 16, MICROS_ELF_SPAWN_CHILD_PID);
    if (type == MICROS_PM_MESSAGE_WAIT) {
        client_test_write_u32(message->payload + 24, MICROS_PM_EXIT_NORMAL);
        client_test_write_u32(message->payload + 28, MICROS_ELF_SPAWN_CHILD_EXIT_CODE);
    }
}

static void client_test_vfs_reply(struct micros_ipc_message *message, uint32_t type)
{
    message->type = MICROS_VFS_MESSAGE_RESULT;
    client_test_write_u32(message->payload, MICROS_VFS_PROTOCOL_VERSION);
    client_test_write_u32(message->payload + 4, type);
    if (type == MICROS_VFS_MESSAGE_OPEN) {
        client_test_write_u32(message->payload + 16, 3);
        client_test_write_u32(message->payload + 20, UINT32_C(0x8000) | UINT32_C(0444));
    } else if (type == MICROS_VFS_MESSAGE_WRITE) {
        client_test_write_u64(message->payload + 24,
            client_test_read_u32(client_context.sent.payload + 24));
    } else if (type == MICROS_VFS_MESSAGE_READ) {
        memcpy((void *)client_context.base, MICROS_ELF_SPAWN_DATA,
               sizeof(MICROS_ELF_SPAWN_DATA) - 1);
        client_test_write_u64(message->payload + 24, sizeof(MICROS_ELF_SPAWN_DATA) - 1);
        client_test_write_u64(message->payload + 32, sizeof(MICROS_ELF_SPAWN_DATA) - 1);
    }
}

micros_runtime_result_t micros_runtime_call(
    micros_endpoint_t endpoint, struct micros_ipc_message *message
)
{
    uint32_t type = message->type;

    ++client_context.calls;
    client_context.sent = *message;
    client_context.request_type = type;
    memset(message, 0, sizeof(*message));
    message->source = endpoint;
    if (endpoint == micros_application_pm_endpoint) {
        client_test_pm_reply(message, type);
    } else {
        client_test_vfs_reply(message, type);
    }
    if (client_context.malformed_reply) {
        message->payload[47] = 1;
    }
    return client_context.call_result;
}

static bool client_pm_wire_and_failure_are_exact(void)
{
    struct micros_elf_spawn_client client;
    struct micros_elf_spawn_pm_result result;
    struct micros_elf_spawn_pm_result before;

    memset(&client_context, 0, sizeof(client_context));
    CHECK(micros_elf_spawn_client_spawn(&client, MICROS_ELF_SPAWN_REQUEST_CHILD, &result));
    CHECK(result.result == MICROS_PM_RESULT_OK && result.pid == MICROS_ELF_SPAWN_CHILD_PID);
    CHECK(result.exit_kind == MICROS_PM_EXIT_NONE && result.exit_code == 0);
    CHECK(client_context.grantee == micros_application_pm_endpoint);
    CHECK(client_context.permission == MICROS_GRANT_PERMISSION_READ);
    CHECK(client_context.base == (uintptr_t)client.bytes && client_context.revokes == 1);
    CHECK(client_test_read_u32(client_context.sent.payload + 8) == 64);
    CHECK(client_test_read_u32(client_context.sent.payload + 12) == client_context.length);
    CHECK(micros_elf_spawn_client_wait(MICROS_ELF_SPAWN_CHILD_PID, &result));
    CHECK(result.pid == MICROS_ELF_SPAWN_CHILD_PID
          && result.exit_kind == MICROS_PM_EXIT_NORMAL
          && result.exit_code == MICROS_ELF_SPAWN_CHILD_EXIT_CODE);
    memset(&result, 0xa5, sizeof(result));
    memcpy(&before, &result, sizeof(before));
    client_context.malformed_reply = true;
    CHECK(!micros_elf_spawn_client_spawn(&client, MICROS_ELF_SPAWN_REQUEST_CHILD, &result));
    CHECK(memcmp(&before, &result, sizeof(result)) == 0 && client_context.revokes == 2);
    client_context.malformed_reply = false;
    client_context.call_result = MICROS_SYSCALL_ABI_DEAD_ENDPOINT;
    CHECK(!micros_elf_spawn_client_spawn(&client, MICROS_ELF_SPAWN_REQUEST_CHILD, &result));
    CHECK(client_context.revokes == 3);
    client_context.call_result = MICROS_SYSCALL_ABI_OK;
    client_context.revoke_result = MICROS_SYSCALL_ABI_STALE_GRANT;
    CHECK(!micros_elf_spawn_client_spawn(&client, MICROS_ELF_SPAWN_REQUEST_CHILD, &result));
    CHECK(memcmp(&before, &result, sizeof(result)) == 0);
    return true;
}

static bool client_vfs_wire_and_startup_are_exact(void)
{
    struct micros_elf_spawn_client client;
    static const uint8_t output[] = "fixture\n";
    char *argv[] = {MICROS_ELF_SPAWN_INIT_ARGUMENT, NULL};
    char *envp[] = {MICROS_ELF_SPAWN_ENVIRONMENT, NULL};

    memset(&client_context, 0, sizeof(client_context));
    CHECK(micros_elf_spawn_client_write(&client, 1, output, sizeof(output) - 1));
    CHECK(client_context.grantee == micros_application_vfs_endpoint);
    CHECK(client_context.permission == MICROS_GRANT_PERMISSION_READ
          && client_context.revokes == 1);
    CHECK(client_test_read_u32(client_context.sent.payload + 8) == 1);
    CHECK(micros_elf_spawn_client_read_data(&client));
    CHECK(client_context.calls == 4 && client_context.revokes == 3);
    CHECK(micros_elf_spawn_client_drain());
    CHECK(client_context.calls == 5);
    CHECK(micros_elf_spawn_client_startup(1, argv, envp, true));
    envp[0] = "WRONG=environment";
    CHECK(!micros_elf_spawn_client_startup(1, argv, envp, true));
    return true;
}

bool test_elf_spawn_client_protocol(void)
{
    return client_pm_wire_and_failure_are_exact() && client_vfs_wire_and_startup_are_exact();
}
