#ifndef MICROS_IPC_H
#define MICROS_IPC_H

#include <stddef.h>
#include <stdint.h>

struct micros_ipc_message {
    uint32_t source;
    uint32_t type;
    uint64_t reply_token;
    uint8_t payload[48];
};

enum micros_ipc_queue_kind {
    MICROS_IPC_QUEUE_NONE = 0,
    MICROS_IPC_QUEUE_SENDER,
    MICROS_IPC_QUEUE_RECEIVER,
};

enum micros_ipc_error {
    MICROS_IPC_OK = 0,
    MICROS_IPC_ERROR_ARGUMENT,
    MICROS_IPC_ERROR_DEAD_ENDPOINT,
    MICROS_IPC_ERROR_UNAUTHORIZED,
    MICROS_IPC_ERROR_STATE,
    MICROS_IPC_ERROR_NOT_READY,
    MICROS_IPC_ERROR_DEADLOCK,
    MICROS_IPC_ERROR_MESSAGE_FAULT,
    MICROS_IPC_ERROR_REPLY_TOKEN,
    MICROS_IPC_ERROR_REPLY_TOKEN_EXHAUSTED,
    MICROS_IPC_ERROR_ENDPOINT_CLOSING,
    MICROS_IPC_ERROR_INVARIANT,
};

_Static_assert(
    sizeof(struct micros_ipc_message) == 64,
    "IPC message size must remain 64 bytes"
);
_Static_assert(
    _Alignof(struct micros_ipc_message) == 8,
    "IPC message alignment must remain eight bytes"
);
_Static_assert(
    offsetof(struct micros_ipc_message, source) == 0,
    "IPC source offset changed"
);
_Static_assert(
    offsetof(struct micros_ipc_message, type) == 4,
    "IPC type offset changed"
);
_Static_assert(
    offsetof(struct micros_ipc_message, reply_token) == 8,
    "IPC reply-token offset changed"
);
_Static_assert(
    offsetof(struct micros_ipc_message, payload) == 16,
    "IPC payload offset changed"
);

#endif
