#include "micros/ipc_abi.h"

#include <stdint.h>

bool micros_ipc_abi_map_error(
    enum micros_ipc_error error,
    uint64_t *result
)
{
    int64_t mapped;

    if (result == NULL) {
        return false;
    }
    switch (error) {
    case MICROS_IPC_OK:
        mapped = MICROS_IPC_ABI_OK;
        break;
    case MICROS_IPC_ERROR_ARGUMENT:
        mapped = MICROS_IPC_ABI_ARGUMENT;
        break;
    case MICROS_IPC_ERROR_DEAD_ENDPOINT:
        mapped = MICROS_IPC_ABI_DEAD_ENDPOINT;
        break;
    case MICROS_IPC_ERROR_UNAUTHORIZED:
        mapped = MICROS_IPC_ABI_UNAUTHORIZED;
        break;
    case MICROS_IPC_ERROR_STATE:
        mapped = MICROS_IPC_ABI_STATE;
        break;
    case MICROS_IPC_ERROR_DEADLOCK:
        mapped = MICROS_IPC_ABI_DEADLOCK;
        break;
    case MICROS_IPC_ERROR_MESSAGE_FAULT:
        mapped = MICROS_IPC_ABI_MESSAGE_FAULT;
        break;
    case MICROS_IPC_ERROR_REPLY_TOKEN:
        mapped = MICROS_IPC_ABI_REPLY_TOKEN;
        break;
    case MICROS_IPC_ERROR_REPLY_TOKEN_EXHAUSTED:
        mapped = MICROS_IPC_ABI_REPLY_TOKEN_EXHAUSTED;
        break;
    case MICROS_IPC_ERROR_ENDPOINT_CLOSING:
        mapped = MICROS_IPC_ABI_ENDPOINT_CLOSING;
        break;
    case MICROS_IPC_ERROR_NOT_READY:
    case MICROS_IPC_ERROR_INVARIANT:
        return false;
    }
    *result = (uint64_t)mapped;
    return true;
}
