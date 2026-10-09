#ifndef MICROS_SERVERS_PM_CORE_H
#define MICROS_SERVERS_PM_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "micros/bootstrap.h"
#include "micros/endpoint.h"
#include "micros/ipc.h"
#include "micros/pm.h"

#define MICROS_PM_RESOURCE_KERNEL_PROCESS UINT32_C(0x00000001)
#define MICROS_PM_RESOURCE_VFS_IMAGE UINT32_C(0x00000002)
#define MICROS_PM_RESOURCE_VM_MAPPINGS UINT32_C(0x00000004)
#define MICROS_PM_RESOURCE_VFS_DESCRIPTORS UINT32_C(0x00000008)
#define MICROS_PM_RESOURCE_KERNEL_EXECUTION UINT32_C(0x00000010)
#define MICROS_PM_RESOURCE_PM_VFS_COMMIT UINT32_C(0x00000020)

enum micros_pm_model_error {
    MICROS_PM_MODEL_OK = 0,
    MICROS_PM_MODEL_ERROR_ARGUMENT,
    MICROS_PM_MODEL_ERROR_STORAGE,
    MICROS_PM_MODEL_ERROR_STATE,
    MICROS_PM_MODEL_ERROR_CAPACITY,
    MICROS_PM_MODEL_ERROR_STALE,
    MICROS_PM_MODEL_ERROR_BUSY,
    MICROS_PM_MODEL_ERROR_INVARIANT,
};

enum micros_pm_protocol_status {
    MICROS_PM_PROTOCOL_OK = 0,
    MICROS_PM_PROTOCOL_BAD_TYPE,
    MICROS_PM_PROTOCOL_BAD_VERSION,
    MICROS_PM_PROTOCOL_MALFORMED,
    MICROS_PM_PROTOCOL_INVARIANT,
};

enum micros_pm_process_state {
    MICROS_PM_PROCESS_FREE = 0,
    MICROS_PM_PROCESS_SPAWNING,
    MICROS_PM_PROCESS_RUNNING,
    MICROS_PM_PROCESS_EXITING,
    MICROS_PM_PROCESS_ZOMBIE,
    MICROS_PM_PROCESS_QUARANTINED,
};

enum micros_pm_spawn_stage {
    MICROS_PM_SPAWN_NONE = 0,
    MICROS_PM_SPAWN_PM_RECORD_RESERVED,
    MICROS_PM_SPAWN_KERNEL_PROCESS_RESERVED,
    MICROS_PM_SPAWN_VFS_IMAGE_PREPARED,
    MICROS_PM_SPAWN_VM_MAPPINGS_FROZEN,
    MICROS_PM_SPAWN_VFS_DESCRIPTORS_PREPARED,
    MICROS_PM_SPAWN_KERNEL_EXECUTION_PREPARED,
    MICROS_PM_SPAWN_PM_VFS_COMMITTED,
};

enum micros_pm_exit_stage {
    MICROS_PM_EXIT_STAGE_NONE = 0,
    MICROS_PM_EXIT_STAGE_STOPPED,
    MICROS_PM_EXIT_STAGE_VM_RELEASE_PENDING,
    MICROS_PM_EXIT_STAGE_KERNEL_PROCESS_EMPTY,
};

enum micros_pm_wait_action {
    MICROS_PM_WAIT_ACTION_NONE = 0,
    MICROS_PM_WAIT_ACTION_REPLY,
    MICROS_PM_WAIT_ACTION_BLOCKED,
};

enum micros_pm_spawn_rollback_resource {
    MICROS_PM_ROLLBACK_NONE = 0,
    MICROS_PM_ROLLBACK_PM_VFS_COMMIT,
    MICROS_PM_ROLLBACK_KERNEL_EXECUTION,
    MICROS_PM_ROLLBACK_VFS_DESCRIPTORS,
    MICROS_PM_ROLLBACK_VM_MAPPINGS,
    MICROS_PM_ROLLBACK_VFS_IMAGE,
    MICROS_PM_ROLLBACK_KERNEL_PROCESS,
};

struct micros_pm_request {
    uint32_t type;
    uint32_t flags;
    micros_endpoint_t source;
    uint32_t reserved;
    uint64_t reply_token;
    uint64_t child_pid;
    int32_t exit_code;
};

struct micros_pm_spawn_resources {
    uint64_t parent_reply_token;
    uint64_t kernel_transaction;
    uint64_t vfs_image_token;
    uint64_t vm_load_token;
    uint64_t mapping_generation;
    uint64_t vfs_descriptor_token;
    uint64_t kernel_prepared_token;
    uint64_t pm_vfs_commit_token;
    micros_endpoint_t prepared_endpoint;
    bool rollback_active;
    bool rollback_parent_lost;
    uint16_t reserved0;
    uint32_t reserved;
};

struct micros_pm_spawn_rollback_action {
    enum micros_pm_spawn_rollback_resource resource;
    enum micros_pm_spawn_stage expected_stage;
    bool complete;
    bool parent_lost;
    uint16_t reserved0;
    uint32_t reserved1;
    uint64_t token;
    uint64_t secondary_token;
    uint64_t reply_token;
    micros_endpoint_t endpoint;
    uint32_t reserved2;
};

struct micros_pm_wait_state {
    bool active;
    bool result_ready;
    uint16_t reserved;
    uint32_t flags;
    uint64_t selector;
    uint64_t reply_token;
    struct micros_pm_process_handle pending_child;
};

struct micros_pm_wait_outcome {
    enum micros_pm_wait_action action;
    enum micros_pm_result result;
    uint64_t reply_token;
    uint64_t child_pid;
    enum micros_pm_exit_kind exit_kind;
    int32_t exit_code;
    struct micros_pm_process_handle child;
};

struct micros_pm_exit_effects {
    bool aborted_spawn;
    uint8_t reserved0[3];
    uint32_t rollback_resources;
    struct micros_pm_process_handle aborted_child;
    uint32_t reparented_count;
    uint32_t reserved1;
    struct micros_pm_wait_outcome reaper_wait;
};

struct micros_pm_process_record {
    enum micros_pm_process_state state;
    uint32_t generation;
    uint64_t pid;
    struct micros_pm_process_handle parent;
    bool init_candidate;
    bool reaper;
    uint16_t reserved0;
    micros_endpoint_t endpoint;
    enum micros_pm_spawn_stage spawn_stage;
    struct micros_pm_spawn_resources spawn;
    enum micros_pm_exit_stage exit_stage;
    enum micros_pm_exit_kind exit_kind;
    int32_t exit_code;
    uint32_t reserved1;
    uint64_t exit_transaction;
    struct micros_pm_wait_state wait;
    bool reap_pending;
    uint8_t reserved2[7];
};

struct micros_pm_table {
    uint64_t initialization_magic;
    bool runtime_enabled;
    bool init_attempted;
    bool pid_exhausted;
    uint8_t reserved0[5];
    uint64_t last_pid;
    struct micros_pm_process_handle reaper;
    struct micros_pm_process_record
        records[MICROS_PM_PROCESS_CAPACITY];
};

enum micros_pm_protocol_status micros_pm_decode_request(
    const struct micros_ipc_message *message,
    struct micros_pm_request *request
);

enum micros_pm_model_error micros_pm_build_result(
    uint32_t request_type,
    enum micros_pm_result result,
    uint64_t child_pid,
    enum micros_pm_exit_kind exit_kind,
    int32_t exit_code,
    struct micros_ipc_message *message
);

enum micros_pm_model_error micros_pm_table_initialize(
    struct micros_pm_table *table
);

enum micros_pm_model_error micros_pm_table_validate(
    const struct micros_pm_table *table
);

enum micros_pm_model_error micros_pm_enable_runtime(
    struct micros_pm_table *table,
    const struct micros_ipc_message *message
);

enum micros_pm_model_error micros_pm_begin_init(
    struct micros_pm_table *table,
    struct micros_pm_process_handle *child,
    uint64_t *pid
);

enum micros_pm_model_error micros_pm_begin_spawn(
    struct micros_pm_table *table,
    struct micros_pm_process_handle parent,
    uint64_t reply_token,
    struct micros_pm_process_handle *child,
    uint64_t *pid
);

enum micros_pm_model_error micros_pm_spawn_reserve_kernel(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    uint64_t transaction
);

enum micros_pm_model_error micros_pm_spawn_prepare_image(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    uint64_t image_token
);

enum micros_pm_model_error micros_pm_spawn_freeze_mappings(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    uint64_t load_token,
    uint64_t mapping_generation
);

enum micros_pm_model_error micros_pm_spawn_prepare_descriptors(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    uint64_t descriptor_token
);

enum micros_pm_model_error micros_pm_spawn_prepare_execution(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    uint64_t prepared_token,
    micros_endpoint_t endpoint
);

enum micros_pm_model_error micros_pm_spawn_commit_hidden(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    uint64_t commit_token
);

enum micros_pm_model_error micros_pm_spawn_activate(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    micros_endpoint_t endpoint,
    uint64_t *reply_token
);

enum micros_pm_model_error micros_pm_spawn_rollback_begin(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    enum micros_pm_spawn_stage expected_stage,
    uint64_t parent_reply_token,
    bool parent_lost,
    struct micros_pm_spawn_rollback_action *action
);

enum micros_pm_model_error micros_pm_spawn_rollback_ack(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    enum micros_pm_spawn_stage expected_stage,
    enum micros_pm_spawn_rollback_resource resource,
    uint64_t token,
    uint64_t secondary_token,
    micros_endpoint_t endpoint,
    struct micros_pm_spawn_rollback_action *action
);

enum micros_pm_model_error micros_pm_exit_begin(
    struct micros_pm_table *table,
    struct micros_pm_process_handle process,
    uint64_t transaction,
    enum micros_pm_exit_kind exit_kind,
    int32_t exit_code,
    struct micros_pm_exit_effects *effects
);

enum micros_pm_model_error micros_pm_exit_detach(
    struct micros_pm_table *table,
    struct micros_pm_process_handle process,
    uint64_t transaction
);

enum micros_pm_model_error micros_pm_exit_release_vm(
    struct micros_pm_table *table,
    struct micros_pm_process_handle process,
    uint64_t transaction
);

enum micros_pm_model_error micros_pm_exit_release_process(
    struct micros_pm_table *table,
    struct micros_pm_process_handle process,
    uint64_t transaction,
    struct micros_pm_wait_outcome *outcome
);

enum micros_pm_model_error micros_pm_wait_begin(
    struct micros_pm_table *table,
    struct micros_pm_process_handle parent,
    uint64_t selector,
    uint32_t flags,
    uint64_t reply_token,
    struct micros_pm_wait_outcome *outcome
);

enum micros_pm_model_error micros_pm_wait_resume(
    struct micros_pm_table *table,
    struct micros_pm_process_handle parent,
    struct micros_pm_wait_outcome *outcome
);

enum micros_pm_model_error micros_pm_wait_reply_complete(
    struct micros_pm_table *table,
    struct micros_pm_process_handle parent,
    uint64_t reply_token,
    bool accepted
);

enum micros_pm_model_error micros_pm_find_running_by_endpoint(
    const struct micros_pm_table *table,
    micros_endpoint_t endpoint,
    struct micros_pm_process_handle *process,
    uint64_t *pid
);

enum micros_pm_model_error micros_pm_process_resolve(
    const struct micros_pm_table *table,
    struct micros_pm_process_handle handle,
    const struct micros_pm_process_record **record
);

#endif
