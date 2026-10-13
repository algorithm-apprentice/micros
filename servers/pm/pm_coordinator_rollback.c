#include "servers/pm/pm_coordinator_internal.h"

static bool release_spawn_resource(
    struct pm_coordinator_spawn *spawn, const struct micros_pm_spawn_rollback_action *action
)
{
    const struct micros_pm_spawn_resources *resources = &spawn->record->spawn;
    struct micros_pm_coordinator_control_result result;
    uint64_t arguments[5];

    switch (action->resource) {
    case MICROS_PM_ROLLBACK_PM_VFS_COMMIT:
        arguments[0] = spawn->spawn_token;
        arguments[1] = resources->vfs_image_token;
        arguments[2] = resources->vfs_descriptor_token;
        arguments[3] = resources->prepared_endpoint;
        arguments[4] = 0;
        return pm_coordinator_vfs_call(
            spawn, MICROS_VFS_CONTROL_REVERT_HIDDEN, arguments, &result
        ) == PM_COORDINATOR_ADVANCED;
    case MICROS_PM_ROLLBACK_KERNEL_EXECUTION:
        return spawn->backend->abort_prepared(
            spawn->context, resources->kernel_transaction, resources->vm_load_token,
            resources->mapping_generation, resources->kernel_prepared_token
        ) == MICROS_SYSCALL_ABI_OK;
    case MICROS_PM_ROLLBACK_VFS_DESCRIPTORS:
        arguments[0] = spawn->spawn_token;
        arguments[1] = resources->vfs_image_token;
        arguments[2] = resources->vfs_descriptor_token;
        arguments[3] = 0;
        arguments[4] = 0;
        return pm_coordinator_vfs_call(
            spawn, MICROS_VFS_CONTROL_ABORT_DESCRIPTORS, arguments, &result
        ) == PM_COORDINATOR_ADVANCED;
    case MICROS_PM_ROLLBACK_VM_MAPPINGS:
        arguments[0] = resources->vm_load_token;
        arguments[1] = resources->mapping_generation;
        arguments[2] = spawn->pid;
        arguments[3] = 0;
        arguments[4] = 0;
        return pm_coordinator_vm_call(
            spawn, MICROS_VM_CONTROL_ABORT_LOAD, arguments, &result
        ) == PM_COORDINATOR_ADVANCED;
    case MICROS_PM_ROLLBACK_VFS_IMAGE:
        arguments[0] = spawn->spawn_token;
        arguments[1] = resources->vfs_image_token;
        arguments[2] = 0;
        arguments[3] = 0;
        arguments[4] = 0;
        return pm_coordinator_vfs_call(
            spawn, MICROS_VFS_CONTROL_RELEASE_IMAGE, arguments, &result
        ) == PM_COORDINATOR_ADVANCED;
    case MICROS_PM_ROLLBACK_KERNEL_PROCESS:
        return spawn->backend->abort_reserved(
            spawn->context, resources->kernel_transaction
        ) == MICROS_SYSCALL_ABI_OK;
    case MICROS_PM_ROLLBACK_NONE:
    default:
        return false;
    }
}

enum micros_pm_coordinator_error pm_coordinator_rollback_spawn(
    struct pm_coordinator_spawn *spawn
)
{
    struct micros_pm_spawn_rollback_action action;

    if (micros_pm_spawn_rollback_begin(
        spawn->coordinator->table, spawn->child, spawn->record->spawn_stage, 0, false, &action
    ) != MICROS_PM_MODEL_OK) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    while (!action.complete) {
        if (!release_spawn_resource(spawn, &action)
            || micros_pm_spawn_rollback_ack(
                spawn->coordinator->table, spawn->child, action.expected_stage, action.resource,
                action.token, action.secondary_token, action.endpoint, &action
            ) != MICROS_PM_MODEL_OK) {
            return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
        }
    }
    return MICROS_PM_COORDINATOR_OK;
}
