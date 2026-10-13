#ifndef MICROS_SERVERS_PM_COORDINATOR_H
#define MICROS_SERVERS_PM_COORDINATOR_H

#include "micros/spawn.h"
#include "servers/pm/pm_core.h"

enum micros_pm_coordinator_error {
    MICROS_PM_COORDINATOR_OK = 0,
    MICROS_PM_COORDINATOR_ERROR_ARGUMENT,
    MICROS_PM_COORDINATOR_ERROR_STATE,
    MICROS_PM_COORDINATOR_ERROR_INVARIANT,
};

struct micros_pm_coordinator_bindings {
    micros_endpoint_t self;
    micros_endpoint_t vm;
    micros_endpoint_t vfs;
    uint32_t reserved;
};

struct micros_pm_coordinator {
    uint64_t initialization_magic;
    struct micros_pm_table *table;
    struct micros_pm_coordinator_bindings bindings;
    uint64_t last_spawn_token;
    uint8_t request_bytes[MICROS_SPAWN_REQUEST_SIZE_MAX];
};

enum micros_pm_coordinator_protocol_status {
    MICROS_PM_COORDINATOR_PROTOCOL_OK = 0,
    MICROS_PM_COORDINATOR_PROTOCOL_ERROR_ARGUMENT,
    MICROS_PM_COORDINATOR_PROTOCOL_ERROR_MESSAGE,
};

struct micros_pm_coordinator_control_result {
    int32_t result;
    uint32_t reserved;
    uint64_t output[4];
};

enum micros_pm_coordinator_error micros_pm_coordinator_initialize(
    struct micros_pm_coordinator *coordinator,
    struct micros_pm_table *table,
    const struct micros_pm_coordinator_bindings *bindings
);

enum micros_pm_coordinator_protocol_status
micros_pm_coordinator_build_vfs_control(
    uint32_t command,
    const uint64_t arguments[5],
    struct micros_ipc_message *message
);
enum micros_pm_coordinator_protocol_status
micros_pm_coordinator_build_vm_control(
    uint32_t command,
    const uint64_t arguments[5],
    struct micros_ipc_message *message
);
enum micros_pm_coordinator_protocol_status
micros_pm_coordinator_decode_vfs_result(
    micros_endpoint_t peer,
    uint32_t command,
    const struct micros_ipc_message *message,
    struct micros_pm_coordinator_control_result *result
);
enum micros_pm_coordinator_protocol_status
micros_pm_coordinator_decode_vm_result(
    micros_endpoint_t peer,
    uint32_t command,
    const struct micros_ipc_message *message,
    struct micros_pm_coordinator_control_result *result
);

#endif
