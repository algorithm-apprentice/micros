#ifndef MICROS_IPC_CORE_H
#define MICROS_IPC_CORE_H

#include <stdint.h>

#include "micros/endpoint.h"

enum micros_ipc_error micros_ipc_sender_enqueue(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle sender,
    micros_endpoint_t destination,
    const struct micros_ipc_message *message,
    uint64_t reply_token,
    uintptr_t reply_buffer
);

enum micros_ipc_error micros_ipc_receiver_enqueue(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle receiver,
    micros_endpoint_t source,
    uintptr_t receive_buffer
);

#endif
