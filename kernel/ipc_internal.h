#ifndef MICROS_KERNEL_IPC_INTERNAL_H
#define MICROS_KERNEL_IPC_INTERNAL_H

#include "micros/ipc_core.h"

void ipc_copy_bytes(void *destination, const void *source, size_t size);
void ipc_clear_bytes(void *storage, size_t size);
bool ipc_thread_handle_is_zero(struct micros_thread_handle handle);
bool ipc_thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
);
bool ipc_process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
);
bool ipc_thread_is_current(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle thread
);
enum micros_ipc_error ipc_endpoint_error_to_ipc(enum micros_endpoint_error error);
enum micros_ipc_error ipc_scheduler_error_to_ipc(enum micros_kernel_object_error error);
void ipc_canonicalize_message(
    struct micros_ipc_message *destination,
    const struct micros_ipc_message *source,
    micros_endpoint_t source_endpoint,
    uint64_t reply_token
);
void ipc_clear_message(struct micros_ipc_message *message);
enum micros_ipc_error ipc_resolve_held_thread(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    struct micros_thread **thread
);
enum micros_ipc_error ipc_resolve_operation_thread(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    micros_endpoint_t *source_endpoint
);

enum micros_ipc_error ipc_resolve_queue_tail(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle tail,
    struct micros_thread **thread
);
bool ipc_receiver_is_ready_for(
    const struct micros_endpoint_record *destination,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t source
);
bool ipc_sender_is_ready_for(
    const struct micros_endpoint_record *receiver_endpoint,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t source
);
void ipc_append_thread(
    struct micros_thread_handle thread_handle,
    struct micros_thread *previous_tail,
    struct micros_thread_handle *head,
    struct micros_thread_handle *tail
);
bool ipc_find_matching_sender(
    const struct micros_endpoint_record *endpoint,
    struct micros_kernel_objects *objects,
    micros_endpoint_t source,
    struct micros_thread_handle *previous,
    struct micros_thread_handle *matched,
    struct micros_thread **sender
);
bool ipc_find_matching_receiver_const(
    const struct micros_endpoint_record *endpoint,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t source,
    struct micros_thread_handle *previous,
    struct micros_thread_handle *matched,
    const struct micros_thread **receiver
);
bool ipc_find_matching_receiver(
    const struct micros_endpoint_record *endpoint,
    struct micros_kernel_objects *objects,
    micros_endpoint_t source,
    struct micros_thread_handle *previous,
    struct micros_thread_handle *matched,
    struct micros_thread **receiver
);
void ipc_unlink_thread(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle previous,
    struct micros_thread_handle matched,
    struct micros_thread_handle *head,
    struct micros_thread_handle *tail
);
void ipc_stage_no_message_completion(
    struct micros_thread *thread,
    enum micros_ipc_error result
);
void ipc_clear_delivered_sender(struct micros_thread *sender);
void ipc_stage_delivery(
    struct micros_thread *receiver,
    uintptr_t receive_buffer,
    const struct micros_ipc_message *message
);

struct ipc_reply_preflight {
    struct micros_thread_handle caller_handle;
    struct micros_thread *caller;
    struct micros_ipc_message message;
    micros_endpoint_t replying_endpoint;
    uintptr_t reply_buffer;
};

bool ipc_reply_token_is_available(
    const struct micros_kernel_objects *objects,
    uint64_t reply_token
);
enum micros_ipc_error ipc_preflight_reply(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle replier_handle,
    uint64_t reply_token,
    micros_endpoint_t expected_caller_endpoint,
    const struct micros_ipc_message *message,
    uint32_t required_operation,
    struct ipc_reply_preflight *plan
);
void ipc_commit_reply_delivery(const struct ipc_reply_preflight *plan);
enum micros_ipc_error ipc_preflight_deadlock(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle candidate,
    micros_endpoint_t dependency,
    struct micros_thread_handle cleared_dependency
);

#endif
