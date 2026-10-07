#include "micros/ipc_abi.h"

#include <stdbool.h>
#include <stdint.h>

bool micros_ipc_abi_test_run(void);

bool micros_ipc_abi_test_run(void)
{
    static const struct {
        enum micros_ipc_error error;
        int64_t result;
    } mappings[] = {
        {MICROS_IPC_OK, MICROS_IPC_ABI_OK},
        {MICROS_IPC_ERROR_ARGUMENT, MICROS_IPC_ABI_ARGUMENT},
        {MICROS_IPC_ERROR_DEAD_ENDPOINT, MICROS_IPC_ABI_DEAD_ENDPOINT},
        {MICROS_IPC_ERROR_UNAUTHORIZED, MICROS_IPC_ABI_UNAUTHORIZED},
        {MICROS_IPC_ERROR_STATE, MICROS_IPC_ABI_STATE},
        {MICROS_IPC_ERROR_DEADLOCK, MICROS_IPC_ABI_DEADLOCK},
        {MICROS_IPC_ERROR_MESSAGE_FAULT, MICROS_IPC_ABI_MESSAGE_FAULT},
        {MICROS_IPC_ERROR_REPLY_TOKEN, MICROS_IPC_ABI_REPLY_TOKEN},
        {
            MICROS_IPC_ERROR_REPLY_TOKEN_EXHAUSTED,
            MICROS_IPC_ABI_REPLY_TOKEN_EXHAUSTED,
        },
        {
            MICROS_IPC_ERROR_ENDPOINT_CLOSING,
            MICROS_IPC_ABI_ENDPOINT_CLOSING,
        },
    };
    uint64_t output;
    size_t index;

    if (
        MICROS_IPC_ABI_SEND != 1
        || MICROS_IPC_ABI_RECEIVE != 2
        || MICROS_IPC_ABI_CALL != 3
        || MICROS_IPC_ABI_REPLY != 4
        || MICROS_IPC_ABI_REPLY_RECEIVE != 5
        || MICROS_IPC_ABI_NOTIFY != 6
    ) {
        return false;
    }
    for (index = 0; index < sizeof(mappings) / sizeof(mappings[0]); ++index) {
        output = UINT64_C(0xa5a5a5a5a5a5a5a5);
        if (
            !micros_ipc_abi_map_error(
                mappings[index].error,
                &output
            )
            || output != (uint64_t)mappings[index].result
        ) {
            return false;
        }
    }
    output = UINT64_C(0x5a5a5a5a5a5a5a5a);
    if (
        micros_ipc_abi_map_error(
            MICROS_IPC_ERROR_NOT_READY,
            &output
        )
        || output != UINT64_C(0x5a5a5a5a5a5a5a5a)
        || micros_ipc_abi_map_error(
            MICROS_IPC_ERROR_INVARIANT,
            &output
        )
        || output != UINT64_C(0x5a5a5a5a5a5a5a5a)
        || micros_ipc_abi_map_error(
            MICROS_IPC_OK,
            NULL
        )
    ) {
        return false;
    }
    return true;
}
