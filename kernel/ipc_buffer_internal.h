#ifndef MICROS_KERNEL_IPC_BUFFER_INTERNAL_H
#define MICROS_KERNEL_IPC_BUFFER_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "micros/ipc_buffer.h"

struct micros_ipc_buffer_chunk {
    uint64_t physical_address;
    size_t size;
};

struct micros_ipc_buffer_plan {
    struct micros_ipc_buffer_chunk chunks[2];
    size_t chunk_count;
};

enum micros_ipc_buffer_error micros_ipc_buffer_prepare_write(
    struct micros_process_handle process,
    uint64_t user_address,
    struct micros_ipc_buffer_plan *plan
);

void micros_ipc_buffer_commit_write(
    const struct micros_ipc_buffer_plan *plan,
    const struct micros_ipc_message *message
);

#endif
