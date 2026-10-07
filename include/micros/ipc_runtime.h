#ifndef MICROS_IPC_RUNTIME_H
#define MICROS_IPC_RUNTIME_H

#include <stddef.h>

#include "micros/endpoint.h"

enum micros_endpoint_error micros_ipc_runtime_initialize(
    const struct micros_privilege_profile *profiles,
    size_t profile_count
);

const struct micros_endpoint_registry *micros_ipc_runtime_registry(void);

enum micros_endpoint_error micros_ipc_runtime_validate(void);

#endif
