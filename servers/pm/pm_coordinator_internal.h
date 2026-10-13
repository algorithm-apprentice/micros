#ifndef MICROS_SERVERS_PM_COORDINATOR_INTERNAL_H
#define MICROS_SERVERS_PM_COORDINATOR_INTERNAL_H

#include "servers/pm/pm_coordinator.h"
#include "lib/spawn/spawn_request.h"
#include "micros/vfs.h"
#include "micros/vm_service.h"

#define MICROS_PM_COORDINATOR_INITIALIZATION_MAGIC UINT64_C(0x504d434f4f524431)

enum pm_coordinator_step {
    PM_COORDINATOR_ADVANCED = 0,
    PM_COORDINATOR_REJECTED,
    PM_COORDINATOR_CORRUPT,
    PM_COORDINATOR_PARENT_GONE,
};

struct pm_coordinator_spawn {
    struct micros_pm_coordinator *coordinator;
    const struct micros_pm_coordinator_backend *backend;
    void *context;
    struct micros_pm_process_handle child;
    const struct micros_pm_process_record *record;
    uint64_t pid;
    uint64_t spawn_token;
    uint64_t expected_stack;
    uint64_t executable_grant;
    uint64_t request_grant;
    size_t request_size;
    enum micros_pm_result rejection;
    micros_endpoint_t parent_endpoint;
    uint64_t parent_reply_token;
    bool init;
};

bool pm_coordinator_spawn_backend_is_complete(
    const struct micros_pm_coordinator_backend *backend
);
enum micros_pm_coordinator_error pm_coordinator_validate_instance(
    const struct micros_pm_coordinator *coordinator
);
enum pm_coordinator_step pm_coordinator_prepare_snapshot(
    struct micros_pm_coordinator *coordinator, const uint8_t *bytes, size_t size,
    bool init, struct micros_spawn_stack_layout *layout, enum micros_pm_result *rejection
);
enum pm_coordinator_step pm_coordinator_complete_phase(
    const struct pm_coordinator_spawn *spawn
);

enum pm_coordinator_step pm_coordinator_classify_vfs_result(
    struct pm_coordinator_spawn *spawn, uint32_t command, int32_t result
);

enum pm_coordinator_step pm_coordinator_vfs_call(
    struct pm_coordinator_spawn *spawn, uint32_t command,
    const uint64_t arguments[5], struct micros_pm_coordinator_control_result *result
);
enum pm_coordinator_step pm_coordinator_vm_call(
    struct pm_coordinator_spawn *spawn, uint32_t command,
    const uint64_t arguments[5], struct micros_pm_coordinator_control_result *result
);
enum pm_coordinator_step pm_coordinator_run_spawn(struct pm_coordinator_spawn *spawn);
enum micros_pm_coordinator_error pm_coordinator_rollback_spawn(
    struct pm_coordinator_spawn *spawn
);

#endif
