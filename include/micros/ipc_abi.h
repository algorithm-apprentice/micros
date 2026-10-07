#ifndef MICROS_IPC_ABI_H
#define MICROS_IPC_ABI_H

#include <stdbool.h>
#include <stdint.h>

#include "micros/ipc.h"

enum micros_ipc_abi_operation {
    MICROS_IPC_ABI_SEND = 1,
    MICROS_IPC_ABI_RECEIVE = 2,
    MICROS_IPC_ABI_CALL = 3,
    MICROS_IPC_ABI_REPLY = 4,
    MICROS_IPC_ABI_REPLY_RECEIVE = 5,
    MICROS_IPC_ABI_NOTIFY = 6,
};

enum micros_ipc_abi_result {
    MICROS_IPC_ABI_OK = 0,
    MICROS_IPC_ABI_ARGUMENT = -1,
    MICROS_IPC_ABI_DEAD_ENDPOINT = -2,
    MICROS_IPC_ABI_UNAUTHORIZED = -3,
    MICROS_IPC_ABI_STATE = -4,
    MICROS_IPC_ABI_DEADLOCK = -5,
    MICROS_IPC_ABI_MESSAGE_FAULT = -6,
    MICROS_IPC_ABI_REPLY_TOKEN = -7,
    MICROS_IPC_ABI_REPLY_TOKEN_EXHAUSTED = -8,
    MICROS_IPC_ABI_ENDPOINT_CLOSING = -9,
};

bool micros_ipc_abi_map_error(
    enum micros_ipc_error error,
    uint64_t *result
);

#endif
