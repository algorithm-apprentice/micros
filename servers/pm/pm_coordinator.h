#ifndef MICROS_SERVERS_PM_COORDINATOR_H
#define MICROS_SERVERS_PM_COORDINATOR_H

#include "micros/spawn.h"
#include "micros/runtime.h"
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
    uint64_t pending_parent_reply;
    bool spawn_pending;
    bool parent_lost;
    uint8_t reserved[6];
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

struct micros_pm_coordinator_backend {
    micros_runtime_result_t (*reserve)(
        void *context, struct micros_pm_reservation_result *result
    );
    micros_runtime_result_t (*abort_reserved)(void *context, uint64_t transaction);
    micros_runtime_result_t (*prepare_execution)(
        void *context, uint64_t transaction, uint64_t load_token,
        uint64_t mapping_generation, struct micros_pm_prepared_result *result
    );
    micros_runtime_result_t (*abort_prepared)(
        void *context, uint64_t transaction, uint64_t load_token,
        uint64_t mapping_generation, uint64_t prepared_token
    );
    micros_runtime_result_t (*activate)(
        void *context, uint64_t transaction, uint64_t load_token,
        uint64_t mapping_generation, uint64_t prepared_token
    );
    micros_runtime_result_t (*exit_begin)(
        void *context, micros_endpoint_t endpoint, struct micros_pm_exit_begin_result *result
    );
    micros_runtime_result_t (*exit_detach)(
        void *context, uint64_t exit_token, micros_endpoint_t endpoint
    );
    micros_runtime_result_t (*release_exited_process)(void *context, uint64_t exit_token);
    micros_runtime_result_t (*grant_create)(
        void *context, micros_endpoint_t grantee, uintptr_t base,
        size_t length, uint32_t permissions, micros_grant_t *grant
    );
    micros_runtime_result_t (*grant_revoke)(void *context, micros_grant_t grant);
    micros_runtime_result_t (*grant_copy_from)(
        void *context, micros_endpoint_t grantor, micros_grant_t grant,
        size_t offset, uintptr_t local_address, size_t length
    );
    micros_runtime_result_t (*call)(
        void *context, micros_endpoint_t peer, struct micros_ipc_message *message
    );
    micros_runtime_result_t (*reply)(
        void *context, uint64_t token, const struct micros_ipc_message *message
    );
};

struct micros_pm_coordinator_child {
    struct micros_pm_process_handle process;
    uint64_t pid;
    micros_endpoint_t endpoint;
    uint32_t reserved;
};

enum micros_pm_coordinator_response_kind {
    MICROS_PM_COORDINATOR_RESPONSE_REPLY = 0,
    MICROS_PM_COORDINATOR_RESPONSE_PARENT_LOST,
};

struct micros_pm_coordinator_response {
    enum micros_pm_coordinator_response_kind kind;
    uint32_t reserved;
    uint64_t reply_token;
    struct micros_ipc_message message;
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

enum micros_pm_coordinator_error micros_pm_coordinator_spawn_init(
    struct micros_pm_coordinator *coordinator,
    const struct micros_pm_coordinator_backend *backend,
    void *backend_context,
    const uint8_t *request_bytes,
    size_t request_size,
    struct micros_pm_coordinator_child *child
);

enum micros_pm_coordinator_error micros_pm_coordinator_spawn_request(
    struct micros_pm_coordinator *coordinator,
    const struct micros_pm_coordinator_backend *backend,
    void *backend_context,
    const struct micros_ipc_message *message,
    struct micros_pm_coordinator_response *response
);
enum micros_pm_coordinator_error micros_pm_coordinator_parent_lost(
    struct micros_pm_coordinator *coordinator,
    uint64_t reply_token
);

#endif
