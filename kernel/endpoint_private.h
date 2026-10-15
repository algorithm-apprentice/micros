#ifndef MICROS_ENDPOINT_PRIVATE_H
#define MICROS_ENDPOINT_PRIVATE_H

#include "micros/endpoint.h"

#define MICROS_ENDPOINT_REGISTRY_MAGIC UINT64_C(0x4d4943524f534550)

bool endpoint_storage_is_zero(
    const struct micros_endpoint_registry *registry
);

void endpoint_clear_bytes(void *storage, size_t size);

void endpoint_copy_bytes(
    void *destination,
    const void *source,
    size_t size
);

bool endpoint_bytes_equal(
    const void *left,
    const void *right,
    size_t size
);

bool endpoint_bytes_are_zero(const void *storage, size_t size);

bool endpoint_names_equal(
    const char *left,
    const char *right
);

bool endpoint_profile_is_zero(
    const struct micros_privilege_profile *profile
);

bool endpoint_record_is_zero(
    const struct micros_endpoint_record *endpoint
);

bool endpoint_ipc_state_is_zero(
    const struct micros_endpoint_record *endpoint
);

bool endpoint_thread_handle_is_zero(
    struct micros_thread_handle handle
);

bool endpoint_queue_pair_is_shallow_valid(
    struct micros_thread_handle head,
    struct micros_thread_handle tail
);

bool endpoint_pending_notification_state_is_shallow_valid(
    const struct micros_endpoint_record *endpoint
);

void endpoint_clear_endpoint_record(
    struct micros_endpoint_record *endpoint
);

bool endpoint_process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
);

bool endpoint_thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
);

enum micros_endpoint_error endpoint_canonicalize_profile(
    const struct micros_privilege_profile *source,
    struct micros_privilege_profile *destination
);

enum micros_endpoint_error endpoint_require_initialized(
    const struct micros_endpoint_registry *registry
);

enum micros_endpoint_error endpoint_process_resolve_error(
    enum micros_kernel_object_error error
);

enum micros_endpoint_error endpoint_record_resolve_validated(
    const struct micros_endpoint_registry *registry,
    micros_endpoint_t endpoint,
    const struct micros_endpoint_record **record
);

bool endpoint_process_has_current_thread(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process
);

bool endpoint_process_threads_are_held(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    bool require_unassigned
);

bool endpoint_process_threads_have_clear_ipc_state(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process
);

#endif
