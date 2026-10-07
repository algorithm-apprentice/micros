#ifndef MICROS_ENDPOINT_INTERNAL_H
#define MICROS_ENDPOINT_INTERNAL_H

#include "micros/endpoint.h"

void micros_endpoint_close_commit_prevalidated(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_process_handle owner
);

#endif
