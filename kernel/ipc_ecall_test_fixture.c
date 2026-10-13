#include "ipc_ecall_test_internal.h"

const struct micros_privilege_profile ipc_ecall_test_profiles[2] = {
    {
        .id = 1,
        .name = "ECALL_CLIENT",
        .operations =
            MICROS_PRIVILEGE_OPERATION_SEND
            | MICROS_PRIVILEGE_OPERATION_NOTIFY,
        .notify_targets = UINT32_C(1) << 2,
    },
    {
        .id = 2,
        .name = "ECALL_SERVER",
        .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
    },
};

const uint8_t ipc_ecall_test_profile_ids[IPC_ECALL_PROCESS_COUNT] = {
    1,
    2,
};

uintptr_t micros_ipc_ecall_test_saved_sp;
uintptr_t micros_ipc_ecall_test_saved_gp;
uintptr_t micros_ipc_ecall_test_saved_tp;
static struct ipc_ecall_test_context test_context;

struct ipc_ecall_test_context *ipc_ecall_test_context_get(void)
{
    return &test_context;
}

void ipc_ecall_test_clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

void ipc_ecall_test_copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *output = destination;
    const unsigned char *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
    }
}

uint64_t ipc_ecall_test_user_address_of(const unsigned char *symbol)
{
    return IPC_ECALL_TEST_CODE_VIRTUAL_ADDRESS
        + (
            (uintptr_t)symbol
            - (uintptr_t)micros_ipc_ecall_test_payload_start
        );
}

bool ipc_ecall_test_thread_ipc_state_is_clear(
    const struct micros_thread *thread
)
{
    return thread != NULL && micros_thread_ipc_state_is_clear(thread);
}
