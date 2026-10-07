#ifndef MICROS_GRANT_H
#define MICROS_GRANT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/endpoint.h"

enum {
    MICROS_GRANT_CAPACITY = 64,
    MICROS_GRANT_SLOT_BITS = 6,
};

#define MICROS_GRANT_GENERATION_MAX UINT32_C(0x03ffffff)
#define MICROS_GRANT_NONE UINT32_MAX
#define MICROS_GRANT_PERMISSION_READ UINT32_C(0x00000001)
#define MICROS_GRANT_PERMISSION_WRITE UINT32_C(0x00000002)
#define MICROS_GRANT_PERMISSION_DEFINED_MASK \
    (MICROS_GRANT_PERMISSION_READ | MICROS_GRANT_PERMISSION_WRITE)

typedef uint32_t micros_grant_t;

enum micros_grant_slot_state {
    MICROS_GRANT_SLOT_FREE = 0,
    MICROS_GRANT_SLOT_ACTIVE,
    MICROS_GRANT_SLOT_QUARANTINED,
};

enum micros_grant_error {
    MICROS_GRANT_OK = 0,
    MICROS_GRANT_ERROR_ARGUMENT,
    MICROS_GRANT_ERROR_ALREADY_INITIALIZED,
    MICROS_GRANT_ERROR_NOT_INITIALIZED,
    MICROS_GRANT_ERROR_CAPACITY,
    MICROS_GRANT_ERROR_STALE_GRANT,
    MICROS_GRANT_ERROR_DEAD_ENDPOINT,
    MICROS_GRANT_ERROR_UNAUTHORIZED,
    MICROS_GRANT_ERROR_RANGE,
    MICROS_GRANT_ERROR_STATE,
    MICROS_GRANT_ERROR_FAULT,
    MICROS_GRANT_ERROR_PHASE,
    MICROS_GRANT_ERROR_INVARIANT,
};

struct micros_grant_record {
    enum micros_grant_slot_state state;
    uint32_t generation;
    struct micros_process_handle grantor;
    micros_endpoint_t grantor_endpoint;
    micros_endpoint_t grantee_endpoint;
    uintptr_t base;
    size_t length;
    uint32_t permissions;
};

struct micros_grant_registry {
    uint64_t initialization_magic;
    size_t active_count;
    struct micros_grant_record grants[MICROS_GRANT_CAPACITY];
};

struct micros_grant_cancel_plan {
    bool active;
    uintptr_t registry_identity;
    micros_endpoint_t endpoint;
    uint64_t selected_slots;
    uint32_t expected_generation[MICROS_GRANT_CAPACITY];
};

enum micros_grant_error micros_grant_pack(
    size_t slot,
    uint32_t generation,
    micros_grant_t *grant
);

enum micros_grant_error micros_grant_unpack(
    micros_grant_t grant,
    size_t *slot,
    uint32_t *generation
);

enum micros_grant_error micros_grant_registry_initialize(
    struct micros_grant_registry *registry
);

enum micros_grant_error micros_grant_registry_validate(
    const struct micros_grant_registry *registry,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects
);

enum micros_grant_error micros_grant_create(
    struct micros_grant_registry *registry,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle grantor,
    micros_endpoint_t grantee,
    uintptr_t base,
    size_t length,
    uint32_t permissions,
    micros_grant_t *grant
);

enum micros_grant_error micros_grant_inspect(
    const struct micros_grant_registry *registry,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle grantor,
    micros_grant_t grant,
    struct micros_grant_record *record
);

enum micros_grant_error micros_grant_revoke(
    struct micros_grant_registry *registry,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle grantor,
    micros_grant_t grant
);

enum micros_grant_error micros_grant_prepare_endpoint_cancel(
    const struct micros_grant_registry *registry,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    struct micros_grant_cancel_plan *plan
);

enum micros_grant_error micros_grant_commit_endpoint_cancel(
    struct micros_grant_registry *registry,
    struct micros_grant_cancel_plan *plan
);

#endif
