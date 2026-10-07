#ifndef MICROS_IPC_CORE_H
#define MICROS_IPC_CORE_H

#include <stdint.h>

#include "micros/endpoint.h"

/*
 * Portable operations consume a scheduler-held, non-current thread. Target
 * syscall integration owns the later current-thread transition and return
 * selection.
 */
enum micros_ipc_error micros_ipc_send(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle sender,
    micros_endpoint_t destination,
    const struct micros_ipc_message *message
);

enum micros_ipc_error micros_ipc_call(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle caller,
    micros_endpoint_t destination,
    struct micros_ipc_message *message
);

enum micros_ipc_error micros_ipc_reply(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle replier,
    uint64_t reply_token,
    const struct micros_ipc_message *message
);

enum micros_ipc_error micros_ipc_receive(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle receiver,
    micros_endpoint_t source,
    uintptr_t receive_buffer
);

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

enum micros_ipc_error micros_ipc_receiver_commit_delivery(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle receiver,
    micros_endpoint_t source,
    uintptr_t receive_buffer,
    struct micros_thread_handle *matched_sender
);

enum micros_ipc_error micros_ipc_sender_commit_delivery(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle sender,
    micros_endpoint_t destination,
    const struct micros_ipc_message *message,
    struct micros_thread_handle *matched_receiver
);

#endif
