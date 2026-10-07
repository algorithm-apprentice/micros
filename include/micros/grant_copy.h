#ifndef MICROS_GRANT_COPY_H
#define MICROS_GRANT_COPY_H

#include <stddef.h>
#include <stdint.h>

#include "micros/grant.h"

enum {
    MICROS_GRANT_COPY_MAX = 4096,
};

struct micros_grant_copy_authority {
    struct micros_process_handle grantor;
    struct micros_process_handle grantee;
    uintptr_t remote_address;
    size_t length;
    uint32_t required_permission;
};

struct micros_grant_copy_chunk {
    uint64_t physical_address;
    size_t length;
};

struct micros_grant_copy_range_plan {
    struct micros_grant_copy_chunk chunks[2];
    size_t chunk_count;
};

enum micros_grant_error micros_grant_prepare_copy_authority(
    const struct micros_grant_registry *grant_registry,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle grantee,
    micros_endpoint_t grantor_endpoint,
    micros_grant_t grant,
    size_t grant_offset,
    size_t length,
    uint32_t required_permission,
    struct micros_grant_copy_authority *authority
);

enum micros_grant_error micros_grant_copy_plan_validate(
    const struct micros_grant_copy_range_plan *source,
    const struct micros_grant_copy_range_plan *destination,
    size_t length
);

void micros_grant_copy_commit(
    const struct micros_grant_copy_range_plan *source,
    const struct micros_grant_copy_range_plan *destination,
    size_t length
);

enum micros_grant_error micros_grant_copy_from(
    const struct micros_grant_registry *grant_registry,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle grantee,
    micros_endpoint_t grantor_endpoint,
    micros_grant_t grant,
    size_t grant_offset,
    uintptr_t local_address,
    size_t length
);

enum micros_grant_error micros_grant_copy_to(
    const struct micros_grant_registry *grant_registry,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle grantee,
    micros_endpoint_t grantor_endpoint,
    micros_grant_t grant,
    size_t grant_offset,
    uintptr_t local_address,
    size_t length
);

#endif
