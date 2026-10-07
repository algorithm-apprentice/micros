#ifndef MICROS_IPC_BUFFER_H
#define MICROS_IPC_BUFFER_H

#include <stdint.h>

#include "micros/ipc.h"
#include "micros/kernel_objects.h"

enum micros_ipc_buffer_access {
    MICROS_IPC_BUFFER_READ = 1U << 0,
    MICROS_IPC_BUFFER_WRITE = 1U << 1,
};

enum micros_ipc_buffer_error {
    MICROS_IPC_BUFFER_OK = 0,
    MICROS_IPC_BUFFER_ERROR_ARGUMENT,
    MICROS_IPC_BUFFER_ERROR_MESSAGE_FAULT,
    MICROS_IPC_BUFFER_ERROR_INVARIANT,
};

enum micros_ipc_buffer_error micros_ipc_buffer_validate(
    struct micros_process_handle process,
    uint64_t user_address,
    uint32_t access
);

enum micros_ipc_buffer_error micros_ipc_buffer_snapshot(
    struct micros_process_handle process,
    uint64_t user_address,
    uint32_t access,
    struct micros_ipc_message *message
);

enum micros_ipc_buffer_error micros_ipc_buffer_write(
    struct micros_process_handle process,
    uint64_t user_address,
    const struct micros_ipc_message *message
);

#endif
