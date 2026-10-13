#include "servers/pm/pm_coordinator_internal.h"
#include "servers/pm/pm_internal.h"

enum pm_coordinator_step pm_coordinator_complete_phase(const struct pm_coordinator_spawn *spawn)
{
    return !spawn->init && spawn->coordinator->parent_lost
        ? PM_COORDINATOR_PARENT_GONE : PM_COORDINATOR_ADVANCED;
}

static enum pm_coordinator_step reserve_kernel_process(struct pm_coordinator_spawn *spawn)
{
    struct micros_pm_reservation_result result;
    micros_runtime_result_t status;

    pm_common_zero_bytes(&result, sizeof(result));
    status = spawn->backend->reserve(spawn->context, &result);
    if (status == MICROS_SYSCALL_ABI_CAPACITY) {
        spawn->rejection = MICROS_PM_RESULT_NO_SPACE;
        return PM_COORDINATOR_REJECTED;
    }
    if (
        status != MICROS_SYSCALL_ABI_OK || result.version != MICROS_PM_RESERVATION_VERSION
        || result.size != MICROS_PM_RESERVATION_SIZE || result.transaction == 0
        || result.reserved[0] != 0 || result.reserved[1] != 0
        || micros_pm_spawn_reserve_kernel(
            spawn->coordinator->table, spawn->child, result.transaction
        ) != MICROS_PM_MODEL_OK
    ) {
        return PM_COORDINATOR_CORRUPT;
    }
    return pm_coordinator_complete_phase(spawn);
}

static enum pm_coordinator_step prepare_vfs_image(struct pm_coordinator_spawn *spawn)
{
    struct micros_ipc_message message;
    struct micros_pm_coordinator_control_result result;
    micros_grant_t grant = 0;
    uint64_t arguments[5];
    micros_runtime_result_t status;
    enum pm_coordinator_step step;

    status = spawn->backend->grant_create(
        spawn->context, spawn->coordinator->bindings.vfs,
        (uintptr_t)spawn->coordinator->request_bytes, spawn->request_size,
        MICROS_GRANT_PERMISSION_READ, &grant
    );
    if (status == MICROS_SYSCALL_ABI_CAPACITY) {
        spawn->rejection = MICROS_PM_RESULT_NO_SPACE;
        return PM_COORDINATOR_REJECTED;
    }
    if (status != MICROS_SYSCALL_ABI_OK || grant == 0) {
        return PM_COORDINATOR_CORRUPT;
    }
    if (!spawn->init && spawn->coordinator->parent_lost) {
        return spawn->backend->grant_revoke(spawn->context, grant) == MICROS_SYSCALL_ABI_OK
            ? PM_COORDINATOR_PARENT_GONE : PM_COORDINATOR_CORRUPT;
    }
    arguments[0] = spawn->spawn_token;
    arguments[1] = spawn->pid;
    arguments[2] = spawn->init ? 0 : spawn->parent_endpoint;
    arguments[3] = (uint64_t)spawn->request_size << 32 | grant;
    arguments[4] = (uint64_t)spawn->init;
    if (micros_pm_coordinator_build_vfs_control(
        MICROS_VFS_CONTROL_PREPARE_IMAGE, arguments, &message
    ) != MICROS_PM_COORDINATOR_PROTOCOL_OK) {
        return PM_COORDINATOR_CORRUPT;
    }
    status = spawn->backend->call(
        spawn->context, spawn->coordinator->bindings.vfs, &message
    );
    if (spawn->backend->grant_revoke(spawn->context, grant) != MICROS_SYSCALL_ABI_OK) {
        return PM_COORDINATOR_CORRUPT;
    }
    if (
        status != MICROS_SYSCALL_ABI_OK
        || micros_pm_coordinator_decode_vfs_result(
            spawn->coordinator->bindings.vfs, MICROS_VFS_CONTROL_PREPARE_IMAGE, &message, &result
        ) != MICROS_PM_COORDINATOR_PROTOCOL_OK
    ) {
        return PM_COORDINATOR_CORRUPT;
    }
    step = pm_coordinator_classify_vfs_result(
        spawn, MICROS_VFS_CONTROL_PREPARE_IMAGE, result.result
    );
    if (step != PM_COORDINATOR_ADVANCED) {
        return step;
    }
    if (
        (uint32_t)(result.output[2] >> 32) != spawn->request_size
        || micros_pm_spawn_prepare_image(
            spawn->coordinator->table, spawn->child, result.output[0]
        ) != MICROS_PM_MODEL_OK
    ) {
        return PM_COORDINATOR_CORRUPT;
    }
    spawn->executable_grant = result.output[1];
    spawn->request_grant = result.output[2];
    return pm_coordinator_complete_phase(spawn);
}

static enum pm_coordinator_step freeze_vm_mappings(struct pm_coordinator_spawn *spawn)
{
    uint64_t arguments[5] = {
        spawn->record->spawn.kernel_transaction, spawn->pid,
        spawn->executable_grant, spawn->request_grant, 0,
    };
    struct micros_pm_coordinator_control_result result;
    enum pm_coordinator_step step = pm_coordinator_vm_call(
        spawn, MICROS_VM_CONTROL_LOAD, arguments, &result
    );

    if (step != PM_COORDINATOR_ADVANCED) {
        return step;
    }
    if (
        result.output[3] != spawn->expected_stack
        || micros_pm_spawn_freeze_mappings(
            spawn->coordinator->table, spawn->child, result.output[0], result.output[1]
        ) != MICROS_PM_MODEL_OK
    ) {
        return PM_COORDINATOR_CORRUPT;
    }
    return pm_coordinator_complete_phase(spawn);
}

static enum pm_coordinator_step prepare_vfs_descriptors(struct pm_coordinator_spawn *spawn)
{
    uint64_t arguments[5] = {
        spawn->spawn_token, spawn->record->spawn.vfs_image_token, spawn->pid, 0, 0,
    };
    struct micros_pm_coordinator_control_result result;
    enum pm_coordinator_step step = pm_coordinator_vfs_call(
        spawn, MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS, arguments, &result
    );

    if (step != PM_COORDINATOR_ADVANCED) {
        return step;
    }
    return micros_pm_spawn_prepare_descriptors(
        spawn->coordinator->table, spawn->child, result.output[0]
    ) == MICROS_PM_MODEL_OK ? pm_coordinator_complete_phase(spawn) : PM_COORDINATOR_CORRUPT;
}

static enum pm_coordinator_step prepare_kernel_execution(struct pm_coordinator_spawn *spawn)
{
    const struct micros_pm_spawn_resources *resources = &spawn->record->spawn;
    struct micros_pm_prepared_result result;
    micros_runtime_result_t status;

    pm_common_zero_bytes(&result, sizeof(result));
    status = spawn->backend->prepare_execution(
        spawn->context, resources->kernel_transaction, resources->vm_load_token,
        resources->mapping_generation, &result
    );
    if (status == MICROS_SYSCALL_ABI_CAPACITY) {
        spawn->rejection = MICROS_PM_RESULT_NO_SPACE;
        return PM_COORDINATOR_REJECTED;
    }
    if (
        status != MICROS_SYSCALL_ABI_OK || result.version != MICROS_PM_PREPARED_VERSION
        || result.size != MICROS_PM_PREPARED_SIZE || result.prepared_token == 0
        || result.reserved0 != 0 || result.reserved1 != 0
        || result.endpoint == spawn->coordinator->bindings.self
        || result.endpoint == spawn->coordinator->bindings.vm
        || result.endpoint == spawn->coordinator->bindings.vfs
        || micros_pm_spawn_prepare_execution(
            spawn->coordinator->table, spawn->child, result.prepared_token, result.endpoint
        ) != MICROS_PM_MODEL_OK
    ) {
        return PM_COORDINATOR_CORRUPT;
    }
    return pm_coordinator_complete_phase(spawn);
}

static enum pm_coordinator_step commit_hidden_records(struct pm_coordinator_spawn *spawn)
{
    const struct micros_pm_spawn_resources *resources = &spawn->record->spawn;
    uint64_t arguments[5] = {
        spawn->spawn_token, resources->vfs_image_token, resources->vfs_descriptor_token,
        resources->prepared_endpoint, 0,
    };
    struct micros_pm_coordinator_control_result result;
    enum pm_coordinator_step step = pm_coordinator_vfs_call(
        spawn, MICROS_VFS_CONTROL_COMMIT_HIDDEN, arguments, &result
    );

    if (step != PM_COORDINATOR_ADVANCED) {
        return step;
    }
    return micros_pm_spawn_commit_hidden(
        spawn->coordinator->table, spawn->child, spawn->spawn_token
    ) == MICROS_PM_MODEL_OK ? pm_coordinator_complete_phase(spawn) : PM_COORDINATOR_CORRUPT;
}

static enum pm_coordinator_step activate_and_commit_peers(struct pm_coordinator_spawn *spawn)
{
    struct pm_spawn_activation_plan plan;
    const struct micros_pm_spawn_resources *resources = &spawn->record->spawn;
    struct micros_pm_coordinator_control_result result;
    uint64_t vm_arguments[5] = {
        resources->vm_load_token, resources->mapping_generation, spawn->pid, 0, 0,
    };
    uint64_t vfs_arguments[5] = {
        spawn->spawn_token, resources->vfs_image_token, 0, 0, 0,
    };
    uint64_t reply_token;
    micros_runtime_result_t status;

    if (pm_coordinator_complete_phase(spawn) != PM_COORDINATOR_ADVANCED) {
        return PM_COORDINATOR_PARENT_GONE;
    }
    if (pm_spawn_prepare_activation(
        spawn->coordinator->table, spawn->child, resources->prepared_endpoint, &plan
    ) != MICROS_PM_MODEL_OK) {
        return PM_COORDINATOR_CORRUPT;
    }
    status = spawn->backend->activate(
        spawn->context, resources->kernel_transaction, resources->vm_load_token,
        resources->mapping_generation, resources->kernel_prepared_token
    );
    if (status == MICROS_SYSCALL_ABI_CAPACITY) {
        spawn->rejection = MICROS_PM_RESULT_NO_SPACE;
        return PM_COORDINATOR_REJECTED;
    }
    if (status != MICROS_SYSCALL_ABI_OK) {
        return PM_COORDINATOR_CORRUPT;
    }
    pm_spawn_commit_activation(&plan, &reply_token);
    if (reply_token != spawn->parent_reply_token
        || pm_coordinator_vm_call(spawn, MICROS_VM_CONTROL_COMMIT_LOAD, vm_arguments, &result)
            != PM_COORDINATOR_ADVANCED
        || pm_coordinator_vfs_call(spawn, MICROS_VFS_CONTROL_RELEASE_IMAGE, vfs_arguments, &result)
            != PM_COORDINATOR_ADVANCED) {
        return PM_COORDINATOR_CORRUPT;
    }
    return PM_COORDINATOR_ADVANCED;
}

enum pm_coordinator_step pm_coordinator_run_spawn(struct pm_coordinator_spawn *spawn)
{
    enum pm_coordinator_step step = reserve_kernel_process(spawn);

    if (step != PM_COORDINATOR_ADVANCED) {
        return step;
    }
    step = prepare_vfs_image(spawn);
    if (step != PM_COORDINATOR_ADVANCED) {
        return step;
    }
    step = freeze_vm_mappings(spawn);
    if (step != PM_COORDINATOR_ADVANCED) {
        return step;
    }
    step = prepare_vfs_descriptors(spawn);
    if (step != PM_COORDINATOR_ADVANCED) {
        return step;
    }
    step = prepare_kernel_execution(spawn);
    if (step != PM_COORDINATOR_ADVANCED) {
        return step;
    }
    step = commit_hidden_records(spawn);
    if (step != PM_COORDINATOR_ADVANCED) {
        return step;
    }
    return activate_and_commit_peers(spawn);
}
