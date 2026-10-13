#include "servers/pm/pm_internal.h"

static enum micros_pm_spawn_rollback_resource
rollback_resource_for_stage(enum micros_pm_spawn_stage stage)
{
    switch (stage) {
    case MICROS_PM_SPAWN_PM_VFS_COMMITTED:
        return MICROS_PM_ROLLBACK_PM_VFS_COMMIT;
    case MICROS_PM_SPAWN_KERNEL_EXECUTION_PREPARED:
        return MICROS_PM_ROLLBACK_KERNEL_EXECUTION;
    case MICROS_PM_SPAWN_VFS_DESCRIPTORS_PREPARED:
        return MICROS_PM_ROLLBACK_VFS_DESCRIPTORS;
    case MICROS_PM_SPAWN_VM_MAPPINGS_FROZEN:
        return MICROS_PM_ROLLBACK_VM_MAPPINGS;
    case MICROS_PM_SPAWN_VFS_IMAGE_PREPARED:
        return MICROS_PM_ROLLBACK_VFS_IMAGE;
    case MICROS_PM_SPAWN_KERNEL_PROCESS_RESERVED:
        return MICROS_PM_ROLLBACK_KERNEL_PROCESS;
    case MICROS_PM_SPAWN_PM_RECORD_RESERVED:
    case MICROS_PM_SPAWN_NONE:
    default:
        return MICROS_PM_ROLLBACK_NONE;
    }
}

static void populate_rollback_action(
    const struct micros_pm_process_record *record,
    struct micros_pm_spawn_rollback_action *action
)
{
    pm_common_zero_bytes(action, sizeof(*action));
    action->resource = rollback_resource_for_stage(
        record->spawn_stage
    );
    action->expected_stage = record->spawn_stage;
    action->parent_lost = record->spawn.rollback_parent_lost;
    action->endpoint = MICROS_ENDPOINT_NONE;

    switch (action->resource) {
    case MICROS_PM_ROLLBACK_PM_VFS_COMMIT:
        action->token = record->spawn.pm_vfs_commit_token;
        break;
    case MICROS_PM_ROLLBACK_KERNEL_EXECUTION:
        action->token = record->spawn.kernel_prepared_token;
        action->endpoint = record->spawn.prepared_endpoint;
        break;
    case MICROS_PM_ROLLBACK_VFS_DESCRIPTORS:
        action->token = record->spawn.vfs_descriptor_token;
        break;
    case MICROS_PM_ROLLBACK_VM_MAPPINGS:
        action->token = record->spawn.vm_load_token;
        action->secondary_token = record->spawn.mapping_generation;
        break;
    case MICROS_PM_ROLLBACK_VFS_IMAGE:
        action->token = record->spawn.vfs_image_token;
        break;
    case MICROS_PM_ROLLBACK_KERNEL_PROCESS:
        action->token = record->spawn.kernel_transaction;
        break;
    case MICROS_PM_ROLLBACK_NONE:
    default:
        break;
    }
}

static void populate_completed_rollback(
    uint64_t parent_reply_token,
    bool parent_lost,
    struct micros_pm_spawn_rollback_action *action
)
{
    pm_common_zero_bytes(action, sizeof(*action));
    action->complete = true;
    action->parent_lost = parent_lost;
    action->reply_token = (
        parent_lost ? 0 : parent_reply_token
    );
    action->endpoint = MICROS_ENDPOINT_NONE;
}

enum micros_pm_model_error micros_pm_spawn_rollback_begin(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    enum micros_pm_spawn_stage expected_stage,
    uint64_t parent_reply_token,
    bool parent_lost,
    struct micros_pm_spawn_rollback_action *action
)
{
    struct micros_pm_spawn_rollback_action committed;
    struct micros_pm_process_record *record;
    enum micros_pm_model_error error;

    if (
        table == NULL
        || action == NULL
        || expected_stage < MICROS_PM_SPAWN_PM_RECORD_RESERVED
        || expected_stage > MICROS_PM_SPAWN_PM_VFS_COMMITTED
    ) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = pm_common_resolve_spawning_record(
        table,
        child,
        expected_stage,
        &record
    );
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (record->spawn.rollback_active) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    if (record->spawn.parent_reply_token != parent_reply_token) {
        return MICROS_PM_MODEL_ERROR_STALE;
    }

    if (expected_stage == MICROS_PM_SPAWN_PM_RECORD_RESERVED) {
        populate_completed_rollback(
            parent_reply_token,
            parent_lost,
            &committed
        );
        pm_common_release_record(record);
    } else {
        record->spawn.rollback_active = true;
        record->spawn.rollback_parent_lost = parent_lost;
        populate_rollback_action(record, &committed);
    }
    *action = committed;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_spawn_rollback_ack(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    enum micros_pm_spawn_stage expected_stage,
    enum micros_pm_spawn_rollback_resource resource,
    uint64_t token,
    uint64_t secondary_token,
    micros_endpoint_t endpoint,
    struct micros_pm_spawn_rollback_action *action
)
{
    struct micros_pm_spawn_rollback_action committed;
    struct micros_pm_spawn_rollback_action expected;
    struct micros_pm_process_record *record;
    uint64_t parent_reply_token;
    bool parent_lost;
    enum micros_pm_model_error error;

    if (
        table == NULL
        || action == NULL
        || expected_stage < MICROS_PM_SPAWN_KERNEL_PROCESS_RESERVED
        || expected_stage > MICROS_PM_SPAWN_PM_VFS_COMMITTED
        || resource <= MICROS_PM_ROLLBACK_NONE
        || resource > MICROS_PM_ROLLBACK_KERNEL_PROCESS
    ) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = pm_common_resolve_spawning_record(
        table,
        child,
        expected_stage,
        &record
    );
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (!record->spawn.rollback_active) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    populate_rollback_action(record, &expected);
    if (resource != expected.resource) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    if (
        token != expected.token
        || secondary_token != expected.secondary_token
        || endpoint != expected.endpoint
    ) {
        return MICROS_PM_MODEL_ERROR_STALE;
    }

    parent_reply_token = record->spawn.parent_reply_token;
    parent_lost = record->spawn.rollback_parent_lost;
    switch (resource) {
    case MICROS_PM_ROLLBACK_PM_VFS_COMMIT:
        record->spawn.pm_vfs_commit_token = 0;
        record->spawn_stage
            = MICROS_PM_SPAWN_KERNEL_EXECUTION_PREPARED;
        break;
    case MICROS_PM_ROLLBACK_KERNEL_EXECUTION:
        record->spawn.kernel_prepared_token = 0;
        record->spawn.prepared_endpoint = MICROS_ENDPOINT_NONE;
        record->spawn_stage
            = MICROS_PM_SPAWN_VFS_DESCRIPTORS_PREPARED;
        break;
    case MICROS_PM_ROLLBACK_VFS_DESCRIPTORS:
        record->spawn.vfs_descriptor_token = 0;
        record->spawn_stage
            = MICROS_PM_SPAWN_VM_MAPPINGS_FROZEN;
        break;
    case MICROS_PM_ROLLBACK_VM_MAPPINGS:
        record->spawn.vm_load_token = 0;
        record->spawn.mapping_generation = 0;
        record->spawn_stage = MICROS_PM_SPAWN_VFS_IMAGE_PREPARED;
        break;
    case MICROS_PM_ROLLBACK_VFS_IMAGE:
        record->spawn.vfs_image_token = 0;
        record->spawn_stage
            = MICROS_PM_SPAWN_KERNEL_PROCESS_RESERVED;
        break;
    case MICROS_PM_ROLLBACK_KERNEL_PROCESS:
        record->spawn.kernel_transaction = 0;
        record->spawn_stage = MICROS_PM_SPAWN_PM_RECORD_RESERVED;
        break;
    case MICROS_PM_ROLLBACK_NONE:
    default:
        return MICROS_PM_MODEL_ERROR_INVARIANT;
    }

    if (record->spawn_stage == MICROS_PM_SPAWN_PM_RECORD_RESERVED) {
        populate_completed_rollback(
            parent_reply_token,
            parent_lost,
            &committed
        );
        pm_common_release_record(record);
    } else {
        populate_rollback_action(record, &committed);
    }
    *action = committed;
    return MICROS_PM_MODEL_OK;
}
