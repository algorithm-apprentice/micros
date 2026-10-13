#include "servers/pm/pm_coordinator_internal.h"
#include "servers/pm/pm_internal.h"

static bool coordinator_bindings_are_valid(
    const struct micros_pm_coordinator_bindings *bindings
)
{
    return (
        bindings->reserved == 0
        && pm_common_endpoint_is_concrete(bindings->self)
        && pm_common_endpoint_is_concrete(bindings->vm)
        && pm_common_endpoint_is_concrete(bindings->vfs)
        && bindings->self != bindings->vm
        && bindings->self != bindings->vfs
        && bindings->vm != bindings->vfs
    );
}

enum micros_pm_coordinator_error micros_pm_coordinator_initialize(
    struct micros_pm_coordinator *coordinator,
    struct micros_pm_table *table,
    const struct micros_pm_coordinator_bindings *bindings
)
{
    struct micros_pm_coordinator_bindings captured_bindings;

    if (
        coordinator == NULL || table == NULL || bindings == NULL
        || !coordinator_bindings_are_valid(bindings)
    ) {
        return MICROS_PM_COORDINATOR_ERROR_ARGUMENT;
    }
    if (micros_pm_table_validate(table) != MICROS_PM_MODEL_OK) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    if (
        coordinator->initialization_magic != 0
        || table->init_attempted || table->last_pid != 0
    ) {
        return MICROS_PM_COORDINATOR_ERROR_STATE;
    }
    captured_bindings = *bindings;
    pm_common_zero_bytes(coordinator, sizeof(*coordinator));
    coordinator->table = table;
    coordinator->bindings = captured_bindings;
    coordinator->initialization_magic = MICROS_PM_COORDINATOR_INITIALIZATION_MAGIC;
    return MICROS_PM_COORDINATOR_OK;
}

bool pm_coordinator_spawn_backend_is_complete(const struct micros_pm_coordinator_backend *backend)
{
    return backend != NULL && backend->reserve != NULL && backend->abort_reserved != NULL
        && backend->prepare_execution != NULL && backend->abort_prepared != NULL
        && backend->activate != NULL && backend->grant_create != NULL
        && backend->grant_revoke != NULL && backend->call != NULL;
}

enum micros_pm_coordinator_error pm_coordinator_validate_instance(
    const struct micros_pm_coordinator *coordinator
)
{
    return (
        coordinator != NULL
        && coordinator->initialization_magic == MICROS_PM_COORDINATOR_INITIALIZATION_MAGIC
        && coordinator->table != NULL
        && coordinator_bindings_are_valid(&coordinator->bindings)
        && pm_common_bytes_are_zero(coordinator->reserved, sizeof(coordinator->reserved))
        && coordinator->spawn_pending == (coordinator->pending_parent_reply != 0)
        && (!coordinator->parent_lost || coordinator->spawn_pending)
        && micros_pm_table_validate(coordinator->table) == MICROS_PM_MODEL_OK
    ) ? MICROS_PM_COORDINATOR_OK : MICROS_PM_COORDINATOR_ERROR_INVARIANT;
}

enum pm_coordinator_step pm_coordinator_prepare_snapshot(
    struct micros_pm_coordinator *coordinator, const uint8_t *bytes, size_t size,
    bool init, struct micros_spawn_stack_layout *layout, enum micros_pm_result *rejection
)
{
    struct micros_spawn_request request;
    enum micros_spawn_stack_layout_error error;
    size_t index;

    if (
        bytes == NULL || size < MICROS_SPAWN_REQUEST_SIZE_MIN
        || size > MICROS_SPAWN_REQUEST_SIZE_MAX
        || micros_spawn_request_decode(bytes, size, &request) != MICROS_SPAWN_REQUEST_OK
        || (init && (request.descriptor_action_count != 0 || bytes[request.path.offset] != '/'))
    ) {
        *rejection = MICROS_PM_RESULT_MALFORMED;
        return PM_COORDINATOR_REJECTED;
    }
    error = micros_spawn_stack_layout_plan(&request, layout);
    if (error == MICROS_SPAWN_STACK_LAYOUT_ERROR_TOO_BIG) {
        *rejection = MICROS_PM_RESULT_TOO_BIG;
        return PM_COORDINATOR_REJECTED;
    }
    if (error != MICROS_SPAWN_STACK_LAYOUT_OK) {
        return PM_COORDINATOR_CORRUPT;
    }
    if (bytes != coordinator->request_bytes) {
        for (index = 0; index < size; ++index) {
            coordinator->request_bytes[index] = bytes[index];
        }
    }
    pm_common_zero_bytes(&coordinator->request_bytes[size], sizeof(coordinator->request_bytes) - size);
    return PM_COORDINATOR_ADVANCED;
}

enum micros_pm_coordinator_error micros_pm_coordinator_spawn_init(
    struct micros_pm_coordinator *coordinator,
    const struct micros_pm_coordinator_backend *backend,
    void *backend_context,
    const uint8_t *request_bytes,
    size_t request_size,
    struct micros_pm_coordinator_child *child
)
{
    struct pm_coordinator_spawn spawn;
    struct micros_spawn_stack_layout layout;
    struct micros_pm_coordinator_child activated;
    enum micros_pm_model_error model_error;
    enum micros_pm_coordinator_error error;
    enum pm_coordinator_step step;

    if (coordinator == NULL || child == NULL || !pm_coordinator_spawn_backend_is_complete(backend)) {
        return MICROS_PM_COORDINATOR_ERROR_ARGUMENT;
    }
    if (pm_coordinator_validate_instance(coordinator) != MICROS_PM_COORDINATOR_OK) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    if (!coordinator->table->runtime_enabled || coordinator->table->init_attempted) {
        return MICROS_PM_COORDINATOR_ERROR_STATE;
    }
    step = pm_coordinator_prepare_snapshot(
        coordinator, request_bytes, request_size, true, &layout, &spawn.rejection
    );
    if (step != PM_COORDINATOR_ADVANCED) {
        return step == PM_COORDINATOR_REJECTED
            ? MICROS_PM_COORDINATOR_ERROR_ARGUMENT : MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    if (coordinator->last_spawn_token == UINT64_MAX) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    pm_common_zero_bytes(&spawn, sizeof(spawn));
    spawn.coordinator = coordinator;
    spawn.backend = backend;
    spawn.context = backend_context;
    spawn.init = true;
    spawn.parent_endpoint = MICROS_ENDPOINT_NONE;
    spawn.request_size = request_size;
    spawn.expected_stack = MICROS_APPLICATION_STACK_TOP - layout.occupied_size;
    model_error = micros_pm_begin_init(coordinator->table, &spawn.child, &spawn.pid);
    if (model_error != MICROS_PM_MODEL_OK) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    ++coordinator->last_spawn_token;
    spawn.spawn_token = coordinator->last_spawn_token;
    if (micros_pm_process_resolve(coordinator->table, spawn.child, &spawn.record)
        != MICROS_PM_MODEL_OK) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    step = pm_coordinator_run_spawn(&spawn);
    if (step == PM_COORDINATOR_REJECTED) {
        error = pm_coordinator_rollback_spawn(&spawn);
        return error == MICROS_PM_COORDINATOR_OK
            ? MICROS_PM_COORDINATOR_ERROR_INVARIANT : error;
    }
    if (step != PM_COORDINATOR_ADVANCED) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    pm_common_zero_bytes(&activated, sizeof(activated));
    activated.process = spawn.child;
    activated.pid = spawn.pid;
    activated.endpoint = spawn.record->endpoint;
    *child = activated;
    return MICROS_PM_COORDINATOR_OK;
}

enum micros_pm_coordinator_error micros_pm_coordinator_parent_lost(
    struct micros_pm_coordinator *coordinator,
    uint64_t reply_token
)
{
    if (coordinator == NULL || reply_token == 0) {
        return MICROS_PM_COORDINATOR_ERROR_ARGUMENT;
    }
    if (pm_coordinator_validate_instance(coordinator) != MICROS_PM_COORDINATOR_OK) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    if (!coordinator->spawn_pending || coordinator->parent_lost
        || coordinator->pending_parent_reply != reply_token) {
        return MICROS_PM_COORDINATOR_ERROR_STATE;
    }
    coordinator->parent_lost = true;
    return MICROS_PM_COORDINATOR_OK;
}
