#ifndef MICROS_KERNEL_IPC_INTERNAL_H
#define MICROS_KERNEL_IPC_INTERNAL_H

#include "micros/ipc_core.h"
#include "kernel/endpoint/endpoint_internal.h"

struct ipc_participant {
    struct micros_thread_handle handle;
    struct micros_thread *thread;
    struct micros_endpoint_selection endpoint;
};

struct ipc_queue_selection {
    struct micros_thread_handle previous;
    struct micros_thread_handle handle;
    const struct micros_thread *thread;
};

enum micros_ipc_error ipc_admit_held_thread(
    const struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    struct ipc_participant *participant
);
enum micros_ipc_error ipc_admit_operation_thread(
    const struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    struct ipc_participant *participant
);
enum micros_ipc_error ipc_admit_active_endpoint(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    struct micros_endpoint_selection *selection
);
enum micros_ipc_error ipc_select_queue(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_selection *destination,
    enum micros_ipc_queue_kind kind,
    micros_endpoint_t source,
    struct ipc_queue_selection *selection
);
enum micros_ipc_error ipc_prepare_queue_append(
    const struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    const struct micros_endpoint_selection *destination,
    enum micros_ipc_queue_kind kind,
    struct micros_thread **tail
);

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
void ipc_append_thread(
    struct micros_thread_handle thread_handle,
    struct micros_thread *previous_tail,
    struct micros_thread_handle *head,
    struct micros_thread_handle *tail
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

struct ipc_pending_notification_plan {
    bool kernel_origin;
    size_t source_slot;
    micros_endpoint_t source_endpoint;
    uint64_t event_mask;
    struct micros_ipc_message message;
};

enum ipc_receive_choice {
    IPC_RECEIVE_WAIT = 0,
    IPC_RECEIVE_NOTIFICATION,
    IPC_RECEIVE_SENDER,
};

struct ipc_receive_selection {
    struct ipc_participant receiver;
    micros_endpoint_t source;
    uintptr_t receive_buffer;
    enum ipc_receive_choice choice;
    struct ipc_pending_notification_plan notification;
    struct ipc_queue_selection sender;
};

_Static_assert(sizeof(struct ipc_receive_selection) <= 256, "selected receive storage is bounded");

enum micros_ipc_error ipc_select_receive(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct ipc_participant *receiver,
    micros_endpoint_t source,
    uintptr_t receive_buffer,
    struct ipc_receive_selection *selection
);
enum micros_ipc_error ipc_deliver_selected_receive(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    const struct ipc_receive_selection *selection
);
void ipc_publish_receive_wait(
    struct micros_endpoint_registry *registry,
    const struct ipc_receive_selection *selection,
    struct micros_thread *tail
);
enum micros_ipc_error ipc_deliver_selected_sender(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    const struct ipc_participant *sender,
    const struct micros_endpoint_selection *destination,
    const struct ipc_queue_selection *receiver,
    const struct micros_ipc_message *message,
    uint64_t reply_token,
    uintptr_t reply_buffer
);
void ipc_publish_sender_wait(
    struct micros_endpoint_registry *registry,
    const struct ipc_participant *sender,
    const struct micros_endpoint_selection *destination,
    struct micros_thread *tail,
    const struct micros_ipc_message *message,
    uint64_t reply_token,
    uintptr_t reply_buffer
);

enum micros_ipc_error ipc_find_pending_notification(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_record *destination,
    micros_endpoint_t source_filter,
    struct ipc_pending_notification_plan *plan
);
void ipc_consume_pending_notification(
    struct micros_endpoint_record *destination,
    const struct ipc_pending_notification_plan *plan
);

struct ipc_reply_preflight {
    struct ipc_participant replier;
    struct micros_thread_handle caller_handle;
    struct micros_thread *caller;
    struct micros_ipc_message message;
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
