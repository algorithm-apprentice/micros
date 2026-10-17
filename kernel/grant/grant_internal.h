#ifndef MICROS_KERNEL_GRANT_INTERNAL_H
#define MICROS_KERNEL_GRANT_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

#include "micros/grant.h"

struct micros_grant_create_plan {
    bool active;
    size_t slot;
    micros_grant_t grant;
    struct micros_grant_record record;
};

enum micros_grant_error micros_grant_prepare_create(
    const struct micros_grant_registry *registry,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle grantor,
    micros_endpoint_t grantee,
    uintptr_t base,
    size_t length,
    uint32_t permissions,
    struct micros_grant_create_plan *plan
);

void micros_grant_commit_create_prevalidated(
    struct micros_grant_registry *registry,
    struct micros_grant_create_plan *plan,
    micros_grant_t *grant
);

#endif
