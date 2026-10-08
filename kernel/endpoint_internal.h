#ifndef MICROS_ENDPOINT_INTERNAL_H
#define MICROS_ENDPOINT_INTERNAL_H

#include "micros/endpoint.h"

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
