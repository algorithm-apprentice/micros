#include "servers/pm/pm_internal.h"

static enum micros_pm_model_error resolve_forward_spawning_record(
    struct micros_pm_table *table,
    struct micros_pm_process_handle handle,
    enum micros_pm_spawn_stage expected_stage,
    struct micros_pm_process_record **record
)
{
    enum micros_pm_model_error error;

    error = pm_common_resolve_spawning_record(
        table,
        handle,
        expected_stage,
        record
    );
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if ((*record)->spawn.rollback_active) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    return MICROS_PM_MODEL_OK;
}

static bool table_has_spawning_record(
    const struct micros_pm_table *table
)
{
    size_t index;

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        if (
            table->records[index].state
                == MICROS_PM_PROCESS_SPAWNING
        ) {
            return true;
        }
    }
    return false;
}

static bool find_free_slot(
    const struct micros_pm_table *table,
    uint16_t *slot
)
{
    uint16_t index;

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        if (
            table->records[index].state == MICROS_PM_PROCESS_FREE
            && table->records[index].generation < UINT32_MAX
        ) {
            *slot = index;
            return true;
        }
    }
    return false;
}

static enum micros_pm_model_error allocate_record(
    struct micros_pm_table *table,
    struct micros_pm_process_handle parent,
    bool init_candidate,
    uint64_t parent_reply_token,
    struct micros_pm_process_handle *child,
    uint64_t *pid
)
{
    struct micros_pm_process_record *record;
    struct micros_pm_process_handle new_handle;
    uint64_t new_pid;
    uint16_t slot;

    if (table->pid_exhausted || table->last_pid == UINT64_MAX) {
        return MICROS_PM_MODEL_ERROR_CAPACITY;
    }
    if (!find_free_slot(table, &slot)) {
        return MICROS_PM_MODEL_ERROR_CAPACITY;
    }

    new_pid = table->last_pid + 1;
    record = &table->records[slot];
    ++record->generation;
    pm_common_clear_record(record, MICROS_PM_PROCESS_SPAWNING);
    record->pid = new_pid;
    record->parent = parent;
    record->init_candidate = init_candidate;
    record->spawn.parent_reply_token = parent_reply_token;
    record->spawn_stage = MICROS_PM_SPAWN_PM_RECORD_RESERVED;

    table->last_pid = new_pid;
    if (new_pid == UINT64_MAX) {
        table->pid_exhausted = true;
    }
    new_handle = pm_common_record_handle(slot, record);
    *child = new_handle;
    *pid = new_pid;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_begin_init(
    struct micros_pm_table *table,
    struct micros_pm_process_handle *child,
    uint64_t *pid
)
{
    struct micros_pm_process_handle parent;
    enum micros_pm_model_error error;

    if (table == NULL || child == NULL || pid == NULL) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (
        !table->runtime_enabled
        || table->init_attempted
        || table_has_spawning_record(table)
        || !pm_common_handle_is_none(table->reaper)
        || table->last_pid != 0
    ) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }

    pm_common_zero_bytes(&parent, sizeof(parent));
    error = allocate_record(table, parent, true, 0, child, pid);
    if (error == MICROS_PM_MODEL_OK) {
        table->init_attempted = true;
    }
    return error;
}

enum micros_pm_model_error micros_pm_begin_spawn(
    struct micros_pm_table *table,
    struct micros_pm_process_handle parent,
    uint64_t reply_token,
    struct micros_pm_process_handle *child,
    uint64_t *pid
)
{
    const struct micros_pm_process_record *parent_record;
    enum micros_pm_model_error error;

    if (
        table == NULL
        || reply_token == 0
        || child == NULL
        || pid == NULL
    ) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (
        !table->runtime_enabled
        || pm_common_handle_is_none(table->reaper)
        || table_has_spawning_record(table)
    ) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    error = pm_common_resolve_record(table, parent, &parent_record);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (parent_record->state != MICROS_PM_PROCESS_RUNNING) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    return allocate_record(
        table,
        parent,
        false,
        reply_token,
        child,
        pid
    );
}

enum micros_pm_model_error micros_pm_spawn_reserve_kernel(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    uint64_t transaction
)
{
    struct micros_pm_process_record *record;
    enum micros_pm_model_error error;

    if (table == NULL || transaction == 0) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = resolve_forward_spawning_record(
        table,
        child,
        MICROS_PM_SPAWN_PM_RECORD_RESERVED,
        &record
    );
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }

    record->spawn.kernel_transaction = transaction;
    record->spawn_stage = MICROS_PM_SPAWN_KERNEL_PROCESS_RESERVED;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_spawn_prepare_image(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    uint64_t image_token
)
{
    struct micros_pm_process_record *record;
    enum micros_pm_model_error error;

    if (table == NULL || image_token == 0) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = resolve_forward_spawning_record(
        table,
        child,
        MICROS_PM_SPAWN_KERNEL_PROCESS_RESERVED,
        &record
    );
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }

    record->spawn.vfs_image_token = image_token;
    record->spawn_stage = MICROS_PM_SPAWN_VFS_IMAGE_PREPARED;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_spawn_freeze_mappings(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    uint64_t load_token,
    uint64_t mapping_generation
)
{
    struct micros_pm_process_record *record;
    enum micros_pm_model_error error;

    if (
        table == NULL
        || load_token == 0
        || mapping_generation == 0
    ) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = resolve_forward_spawning_record(
        table,
        child,
        MICROS_PM_SPAWN_VFS_IMAGE_PREPARED,
        &record
    );
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }

    record->spawn.vm_load_token = load_token;
    record->spawn.mapping_generation = mapping_generation;
    record->spawn_stage = MICROS_PM_SPAWN_VM_MAPPINGS_FROZEN;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_spawn_prepare_descriptors(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    uint64_t descriptor_token
)
{
    struct micros_pm_process_record *record;
    enum micros_pm_model_error error;

    if (table == NULL || descriptor_token == 0) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = resolve_forward_spawning_record(
        table,
        child,
        MICROS_PM_SPAWN_VM_MAPPINGS_FROZEN,
        &record
    );
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }

    record->spawn.vfs_descriptor_token = descriptor_token;
    record->spawn_stage
        = MICROS_PM_SPAWN_VFS_DESCRIPTORS_PREPARED;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_spawn_prepare_execution(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    uint64_t prepared_token,
    micros_endpoint_t endpoint
)
{
    struct micros_pm_process_record *record;
    enum micros_pm_model_error error;

    if (
        table == NULL
        || prepared_token == 0
        || !pm_common_endpoint_is_concrete(endpoint)
    ) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = resolve_forward_spawning_record(
        table,
        child,
        MICROS_PM_SPAWN_VFS_DESCRIPTORS_PREPARED,
        &record
    );
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (pm_common_endpoint_is_published_elsewhere(table, endpoint, child.slot)) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }

    record->spawn.kernel_prepared_token = prepared_token;
    record->spawn.prepared_endpoint = endpoint;
    record->spawn_stage
        = MICROS_PM_SPAWN_KERNEL_EXECUTION_PREPARED;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_spawn_commit_hidden(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    uint64_t commit_token
)
{
    struct micros_pm_process_record *record;
    enum micros_pm_model_error error;

    if (table == NULL || commit_token == 0) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = resolve_forward_spawning_record(
        table,
        child,
        MICROS_PM_SPAWN_KERNEL_EXECUTION_PREPARED,
        &record
    );
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }

    record->spawn.pm_vfs_commit_token = commit_token;
    record->spawn_stage = MICROS_PM_SPAWN_PM_VFS_COMMITTED;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_spawn_activate(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    micros_endpoint_t endpoint,
    uint64_t *reply_token
)
{
    struct micros_pm_process_record *record;
    bool init_candidate;
    uint64_t parent_reply_token;
    enum micros_pm_model_error error;

    if (
        table == NULL
        || reply_token == NULL
        || !pm_common_endpoint_is_concrete(endpoint)
    ) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = resolve_forward_spawning_record(
        table,
        child,
        MICROS_PM_SPAWN_PM_VFS_COMMITTED,
        &record
    );
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (record->spawn.prepared_endpoint != endpoint) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    if (pm_common_endpoint_is_published_elsewhere(table, endpoint, child.slot)) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }

    init_candidate = record->init_candidate;
    parent_reply_token = record->spawn.parent_reply_token;
    record->state = MICROS_PM_PROCESS_RUNNING;
    record->init_candidate = false;
    record->endpoint = endpoint;
    record->load_token = record->spawn.vm_load_token;
    record->mapping_generation = record->spawn.mapping_generation;
    record->spawn_stage = MICROS_PM_SPAWN_NONE;
    pm_common_zero_bytes(&record->spawn, sizeof(record->spawn));
    record->spawn.prepared_endpoint = MICROS_ENDPOINT_NONE;
    if (init_candidate) {
        record->reaper = true;
        table->reaper = child;
    }
    *reply_token = parent_reply_token;
    return MICROS_PM_MODEL_OK;
}
