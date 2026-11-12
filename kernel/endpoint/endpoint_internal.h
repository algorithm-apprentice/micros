#ifndef MICROS_ENDPOINT_INTERNAL_H
#define MICROS_ENDPOINT_INTERNAL_H

#include "micros/endpoint.h"

struct micros_endpoint_selection {
    const struct micros_endpoint_record *record;
    const struct micros_process *process;
};

enum micros_endpoint_error endpoint_preflight_registry(
    const struct micros_endpoint_registry *registry
);

bool micros_endpoint_names_slot(micros_endpoint_t endpoint, uint16_t slot);

enum micros_endpoint_error micros_endpoint_select(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    struct micros_endpoint_selection *selection
);

bool micros_endpoint_queue_member_is_valid(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_selection *destination,
    struct micros_thread_handle handle,
    enum micros_ipc_queue_kind kind,
    const struct micros_thread **result
);

bool micros_endpoint_thread_ipc_state_is_valid(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle handle
);

bool micros_endpoint_staged_request_matches(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle receiver_handle,
    const struct micros_thread *caller,
    const struct micros_endpoint_record *caller_endpoint
);

bool endpoint_pending_notification_state_is_valid(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_record *endpoint
);

enum micros_endpoint_error micros_endpoint_preflight_publish(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    uint8_t profile_id
);

void micros_endpoint_commit_publish_prevalidated(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    uint8_t profile_id
);

enum micros_endpoint_error
micros_endpoint_preflight_source_only(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint
);

enum micros_endpoint_error
micros_endpoint_preflight_source_only_for_teardown(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint
);

void micros_endpoint_commit_source_only_prevalidated(
    struct micros_endpoint_registry *registry,
    micros_endpoint_t endpoint
);

void micros_endpoint_close_commit_prevalidated(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_process_handle owner
);

#endif
