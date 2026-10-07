#ifndef MICROS_ENDPOINT_H
#define MICROS_ENDPOINT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/kernel_objects.h"

enum {
    MICROS_ENDPOINT_SLOT_BITS = 12,
    MICROS_PRIVILEGE_PROFILE_CAPACITY = 32,
    MICROS_PRIVILEGE_PROFILE_NAME_SIZE = 32,
};

typedef uint32_t micros_endpoint_t;

#define MICROS_ENDPOINT_NONE UINT32_C(0xfffffffe)
#define MICROS_ENDPOINT_ANY UINT32_C(0xffffffff)
#define MICROS_ENDPOINT_GENERATION_MAX UINT32_C(0x000fffff)

#define MICROS_PRIVILEGE_OPERATION_RECEIVE UINT32_C(0x00000001)
#define MICROS_PRIVILEGE_OPERATION_CALL UINT32_C(0x00000002)
#define MICROS_PRIVILEGE_OPERATION_SEND UINT32_C(0x00000004)
#define MICROS_PRIVILEGE_OPERATION_REPLY UINT32_C(0x00000008)
#define MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE UINT32_C(0x00000010)
#define MICROS_PRIVILEGE_OPERATION_NOTIFY UINT32_C(0x00000020)
#define MICROS_PRIVILEGE_OPERATION_DEFINED_MASK \
    ( \
        MICROS_PRIVILEGE_OPERATION_RECEIVE \
        | MICROS_PRIVILEGE_OPERATION_CALL \
        | MICROS_PRIVILEGE_OPERATION_SEND \
        | MICROS_PRIVILEGE_OPERATION_REPLY \
        | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE \
        | MICROS_PRIVILEGE_OPERATION_NOTIFY \
    )

enum micros_endpoint_state {
    MICROS_ENDPOINT_STATE_FREE = 0,
    MICROS_ENDPOINT_STATE_RESERVED,
    MICROS_ENDPOINT_STATE_ACTIVE,
};

struct micros_privilege_profile {
    uint8_t id;
    char name[MICROS_PRIVILEGE_PROFILE_NAME_SIZE];
    uint32_t operations;
    uint32_t call_targets;
    uint32_t send_targets;
    uint32_t notify_targets;
    uint64_t kernel_operations;
};

struct micros_endpoint_record {
    enum micros_endpoint_state state;
    struct micros_process_handle owner;
    micros_endpoint_t value;
    struct micros_thread_handle sender_head;
    struct micros_thread_handle sender_tail;
    struct micros_thread_handle receiver_head;
    struct micros_thread_handle receiver_tail;
    uint64_t pending_notification_sources;
    uint64_t pending_kernel_events;
    uint64_t pending_events[MICROS_PROCESS_CAPACITY];
};

struct micros_endpoint_registry {
    uint64_t initialization_magic;
    size_t profile_count;
    uint64_t last_reply_token;
    struct micros_privilege_profile
        profiles[MICROS_PRIVILEGE_PROFILE_CAPACITY];
    struct micros_endpoint_record endpoints[MICROS_PROCESS_CAPACITY];
};

enum micros_endpoint_error {
    MICROS_ENDPOINT_OK = 0,
    MICROS_ENDPOINT_ERROR_ARGUMENT,
    MICROS_ENDPOINT_ERROR_STORAGE,
    MICROS_ENDPOINT_ERROR_ALREADY_INITIALIZED,
    MICROS_ENDPOINT_ERROR_NOT_INITIALIZED,
    MICROS_ENDPOINT_ERROR_CAPACITY,
    MICROS_ENDPOINT_ERROR_ENDPOINT,
    MICROS_ENDPOINT_ERROR_STALE,
    MICROS_ENDPOINT_ERROR_STATE,
    MICROS_ENDPOINT_ERROR_PROFILE,
    MICROS_ENDPOINT_ERROR_UNAUTHORIZED,
    MICROS_ENDPOINT_ERROR_INVARIANT,
};

enum micros_endpoint_error micros_endpoint_pack(
    struct micros_process_handle process,
    micros_endpoint_t *endpoint
);

enum micros_endpoint_error micros_endpoint_unpack(
    micros_endpoint_t endpoint,
    struct micros_process_handle *process
);

enum micros_endpoint_error micros_endpoint_registry_initialize(
    struct micros_endpoint_registry *registry,
    const struct micros_privilege_profile *profiles,
    size_t profile_count
);

enum micros_endpoint_error micros_endpoint_registry_validate(
    const struct micros_endpoint_registry *registry
);

enum micros_endpoint_error micros_endpoint_registry_validate_objects(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
);

enum micros_endpoint_error micros_endpoint_reserve(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    micros_endpoint_t *endpoint
);

enum micros_endpoint_error micros_endpoint_install_profile(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    uint8_t profile_id
);

enum micros_endpoint_error micros_endpoint_activate(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint
);

enum micros_endpoint_error micros_endpoint_resolve_internal(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    const struct micros_endpoint_record **record
);

enum micros_endpoint_error micros_endpoint_resolve_active(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    const struct micros_endpoint_record **record
);

enum micros_endpoint_error micros_endpoint_close(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint
);

enum micros_endpoint_error micros_privilege_profile_resolve(
    const struct micros_endpoint_registry *registry,
    uint8_t profile_id,
    const struct micros_privilege_profile **profile
);

enum micros_endpoint_error micros_privilege_profile_allows_operation(
    const struct micros_endpoint_registry *registry,
    uint8_t profile_id,
    uint32_t operation
);

enum micros_endpoint_error micros_privilege_profile_allows_target(
    const struct micros_endpoint_registry *registry,
    uint8_t source_profile_id,
    uint32_t operation,
    uint8_t destination_profile_id
);

enum micros_endpoint_error micros_privilege_profile_allows_kernel_operation(
    const struct micros_endpoint_registry *registry,
    uint8_t profile_id,
    uint8_t operation
);

enum micros_endpoint_error micros_endpoint_authorize_operation(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t source,
    uint32_t operation
);

enum micros_endpoint_error micros_endpoint_authorize_target(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t source,
    uint32_t operation,
    micros_endpoint_t destination
);

enum micros_endpoint_error micros_endpoint_authorize_kernel_operation(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t source,
    uint8_t operation
);

#endif
