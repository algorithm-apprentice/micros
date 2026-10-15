#include "tests/qemu/elf_spawn_client.h"

#include "micros/application_runtime.h"
#include "micros/runtime.h"
#include "micros/spawn.h"

uint32_t elf_spawn_client_read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

uint64_t elf_spawn_client_read_u64(const uint8_t *bytes)
{
    return elf_spawn_client_read_u32(bytes)
        | (uint64_t)elf_spawn_client_read_u32(bytes + 4) << 32;
}

void elf_spawn_client_write_u32(uint8_t *bytes, uint32_t value)
{
    size_t index;

    for (index = 0; index < 4; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

void elf_spawn_client_write_u64(uint8_t *bytes, uint64_t value)
{
    elf_spawn_client_write_u32(bytes, (uint32_t)value);
    elf_spawn_client_write_u32(bytes + 4, (uint32_t)(value >> 32));
}

bool elf_spawn_client_bytes_equal(const void *left, const void *right, size_t size)
{
    const uint8_t *a = left;
    const uint8_t *b = right;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (a[index] != b[index]) {
            return false;
        }
    }
    return true;
}

bool elf_spawn_client_bytes_zero(const uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool elf_spawn_decode_pm_result(
    const struct micros_ipc_message *message, uint32_t request_type,
    struct micros_elf_spawn_pm_result *result
)
{
    struct micros_elf_spawn_pm_result candidate = {0};

    if (
        message->source != micros_application_pm_endpoint
        || message->type != MICROS_PM_MESSAGE_RESULT || message->reply_token != 0
        || elf_spawn_client_read_u32(message->payload) != MICROS_PM_PROTOCOL_VERSION
        || elf_spawn_client_read_u32(message->payload + 4) != request_type
        || elf_spawn_client_read_u32(message->payload + 12) != 0
        || !elf_spawn_client_bytes_zero(message->payload + 32, 16)
    ) {
        return false;
    }
    candidate.result = (enum micros_pm_result)(int32_t)elf_spawn_client_read_u32(message->payload + 8);
    candidate.pid = elf_spawn_client_read_u64(message->payload + 16);
    candidate.exit_kind = (enum micros_pm_exit_kind)elf_spawn_client_read_u32(message->payload + 24);
    candidate.exit_code = (int32_t)elf_spawn_client_read_u32(message->payload + 28);
    if (candidate.result < MICROS_PM_RESULT_DESCRIPTOR || candidate.result > MICROS_PM_RESULT_OK) {
        return false;
    }
    if (candidate.result != MICROS_PM_RESULT_OK) {
        if (candidate.pid != 0 || candidate.exit_kind != MICROS_PM_EXIT_NONE || candidate.exit_code != 0) {
            return false;
        }
    } else if (
        candidate.pid == 0
        || (request_type == MICROS_PM_MESSAGE_SPAWN
            && (candidate.exit_kind != MICROS_PM_EXIT_NONE || candidate.exit_code != 0))
        || (request_type == MICROS_PM_MESSAGE_WAIT
            && candidate.exit_kind != MICROS_PM_EXIT_NORMAL
            && candidate.exit_kind != MICROS_PM_EXIT_FAULT)
    ) {
        return false;
    }
    *result = candidate;
    return true;
}

bool micros_elf_spawn_client_spawn(
    struct micros_elf_spawn_client *client,
    enum micros_elf_spawn_request_kind kind,
    struct micros_elf_spawn_pm_result *result
)
{
    struct micros_ipc_message message = {0};
    micros_grant_t grant = MICROS_GRANT_NONE;
    size_t size;
    bool called;
    bool revoked;

    if (
        client == NULL || result == NULL
        || kind == MICROS_ELF_SPAWN_REQUEST_INIT
        || !micros_elf_spawn_build_request(client->bytes, sizeof(client->bytes), kind, &size)
        || micros_runtime_grant_create(
            micros_application_pm_endpoint, (uintptr_t)client->bytes, size,
            MICROS_GRANT_PERMISSION_READ, &grant
        ) != MICROS_SYSCALL_ABI_OK
        || grant == MICROS_GRANT_NONE
    ) {
        return false;
    }
    message.type = MICROS_PM_MESSAGE_SPAWN;
    elf_spawn_client_write_u32(message.payload, MICROS_PM_PROTOCOL_VERSION);
    elf_spawn_client_write_u32(message.payload + 8, grant);
    elf_spawn_client_write_u32(message.payload + 12, (uint32_t)size);
    called = micros_runtime_call(micros_application_pm_endpoint, &message) == MICROS_SYSCALL_ABI_OK;
    revoked = micros_runtime_grant_revoke(grant) == MICROS_SYSCALL_ABI_OK;
    return called && revoked && elf_spawn_decode_pm_result(&message, MICROS_PM_MESSAGE_SPAWN, result);
}

bool micros_elf_spawn_client_wait(uint64_t pid, struct micros_elf_spawn_pm_result *result)
{
    struct micros_ipc_message message = {0};
    struct micros_elf_spawn_pm_result candidate;

    if (pid == 0 || result == NULL) {
        return false;
    }
    message.type = MICROS_PM_MESSAGE_WAIT;
    elf_spawn_client_write_u32(message.payload, MICROS_PM_PROTOCOL_VERSION);
    elf_spawn_client_write_u64(message.payload + 8, pid);
    if (
        micros_runtime_call(micros_application_pm_endpoint, &message) != MICROS_SYSCALL_ABI_OK
        || !elf_spawn_decode_pm_result(&message, MICROS_PM_MESSAGE_WAIT, &candidate)
        || (candidate.result == MICROS_PM_RESULT_OK && candidate.pid != pid)
    ) {
        return false;
    }
    *result = candidate;
    return true;
}

bool micros_elf_spawn_client_startup(int argc, char *argv[], char *envp[], bool init)
{
    if (
        argc != (init ? 1 : 2) || argv == NULL || envp == NULL
        || argv[0] == NULL || envp[0] == NULL || envp[1] != NULL
        || argv[argc] != NULL
        || micros_application_pid != (uint64_t)(init ? MICROS_ELF_SPAWN_INIT_PID
                                                    : MICROS_ELF_SPAWN_CHILD_PID)
        || micros_application_page_size != MICROS_APPLICATION_STACK_PAGE_SIZE
        || micros_application_pm_endpoint == micros_application_vfs_endpoint
    ) {
        return false;
    }
    if (init) {
        if (!elf_spawn_client_bytes_equal(argv[0], MICROS_ELF_SPAWN_INIT_ARGUMENT,
                                          sizeof(MICROS_ELF_SPAWN_INIT_ARGUMENT))) {
            return false;
        }
    } else if (
        argv[1] == NULL
        || !elf_spawn_client_bytes_equal(argv[0], MICROS_ELF_SPAWN_CHILD_ARGUMENT,
                                         sizeof(MICROS_ELF_SPAWN_CHILD_ARGUMENT))
        || !elf_spawn_client_bytes_equal(argv[1], MICROS_ELF_SPAWN_CHILD_SECOND_ARGUMENT,
                                         sizeof(MICROS_ELF_SPAWN_CHILD_SECOND_ARGUMENT))
    ) {
        return false;
    }
    return elf_spawn_client_bytes_equal(envp[0], MICROS_ELF_SPAWN_ENVIRONMENT,
                                        sizeof(MICROS_ELF_SPAWN_ENVIRONMENT));
}
