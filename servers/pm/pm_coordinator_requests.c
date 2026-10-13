#include "servers/pm/pm_coordinator_internal.h"
#include "servers/pm/pm_internal.h"

static enum micros_pm_coordinator_error build_spawn_response(
    uint32_t request_type, uint64_t reply_token, enum micros_pm_result result, uint64_t pid,
    struct micros_pm_coordinator_response *response
)
{
    return pm_coordinator_reply_response(
        request_type, reply_token, result, pid, MICROS_PM_EXIT_NONE, 0, response
    );
}

static bool parent_grant_failure_is_expected(micros_runtime_result_t status)
{
    switch (status) {
    case MICROS_SYSCALL_ABI_ARGUMENT:
    case MICROS_SYSCALL_ABI_DEAD_ENDPOINT:
    case MICROS_SYSCALL_ABI_UNAUTHORIZED:
    case MICROS_SYSCALL_ABI_MEMORY_FAULT:
    case MICROS_SYSCALL_ABI_STALE_GRANT:
    case MICROS_SYSCALL_ABI_RANGE:
        return true;
    default:
        return false;
    }
}

static enum pm_coordinator_step copy_parent_request(
    struct pm_coordinator_spawn *spawn, const struct micros_pm_request *request,
    struct micros_spawn_stack_layout *layout
)
{
    micros_runtime_result_t status;

    pm_common_zero_bytes(spawn->coordinator->request_bytes,
                         sizeof(spawn->coordinator->request_bytes));
    status = spawn->backend->grant_copy_from(
        spawn->context, request->source, request->grant, 0,
        (uintptr_t)spawn->coordinator->request_bytes, request->request_size
    );
    if (status != MICROS_SYSCALL_ABI_OK) {
        if (!parent_grant_failure_is_expected(status)) {
            return PM_COORDINATOR_CORRUPT;
        }
        pm_common_zero_bytes(spawn->coordinator->request_bytes,
                             sizeof(spawn->coordinator->request_bytes));
        spawn->rejection = MICROS_PM_RESULT_GRANT;
        return PM_COORDINATOR_REJECTED;
    }
    return pm_coordinator_prepare_snapshot(
        spawn->coordinator, spawn->coordinator->request_bytes, request->request_size,
        false, layout, &spawn->rejection
    );
}

static enum micros_pm_coordinator_error finish_public_spawn(
    struct pm_coordinator_spawn *spawn, enum pm_coordinator_step step,
    struct micros_pm_coordinator_response *response
)
{
    struct micros_pm_coordinator_response built;
    enum micros_pm_coordinator_error error;
    bool lost = spawn->coordinator->parent_lost;

    if (step == PM_COORDINATOR_REJECTED || step == PM_COORDINATOR_PARENT_GONE) {
        error = pm_coordinator_rollback_spawn(spawn);
        if (error != MICROS_PM_COORDINATOR_OK) {
            return error;
        }
    } else if (step != PM_COORDINATOR_ADVANCED) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    spawn->coordinator->spawn_pending = false;
    spawn->coordinator->pending_parent_reply = 0;
    spawn->coordinator->parent_lost = false;
    pm_common_zero_bytes(spawn->coordinator->request_bytes,
                         sizeof(spawn->coordinator->request_bytes));
    if (lost) {
        pm_common_zero_bytes(&built, sizeof(built));
        built.kind = MICROS_PM_COORDINATOR_RESPONSE_PARENT_LOST;
        built.message.source = MICROS_ENDPOINT_NONE;
        *response = built;
        return MICROS_PM_COORDINATOR_OK;
    }
    return build_spawn_response(
        MICROS_PM_MESSAGE_SPAWN, spawn->parent_reply_token,
        step == PM_COORDINATOR_ADVANCED ? MICROS_PM_RESULT_OK : spawn->rejection,
        step == PM_COORDINATOR_ADVANCED ? spawn->pid : 0, response
    );
}

static enum micros_pm_coordinator_error reserve_public_child(
    struct pm_coordinator_spawn *spawn, const struct micros_pm_request *request,
    struct micros_pm_process_handle parent, struct micros_pm_coordinator_response *response
)
{
    enum micros_pm_model_error error;

    if (!pm_spawn_capacity_is_available(spawn->coordinator->table)) {
        return build_spawn_response(
            request->type, request->reply_token, MICROS_PM_RESULT_NO_SPACE, 0, response
        );
    }
    if (spawn->coordinator->last_spawn_token == UINT64_MAX) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    error = micros_pm_begin_spawn(
        spawn->coordinator->table, parent, request->reply_token, &spawn->child, &spawn->pid
    );
    if (error == MICROS_PM_MODEL_ERROR_STATE || error == MICROS_PM_MODEL_ERROR_BUSY) {
        return build_spawn_response(
            request->type, request->reply_token, MICROS_PM_RESULT_BUSY, 0, response
        );
    }
    if (error == MICROS_PM_MODEL_ERROR_CAPACITY) {
        return build_spawn_response(
            request->type, request->reply_token, MICROS_PM_RESULT_NO_SPACE, 0, response
        );
    }
    if (error != MICROS_PM_MODEL_OK) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    ++spawn->coordinator->last_spawn_token;
    spawn->spawn_token = spawn->coordinator->last_spawn_token;
    spawn->coordinator->spawn_pending = true;
    spawn->coordinator->pending_parent_reply = request->reply_token;
    spawn->coordinator->parent_lost = false;
    if (micros_pm_process_resolve(
        spawn->coordinator->table, spawn->child, &spawn->record
    ) != MICROS_PM_MODEL_OK) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    return finish_public_spawn(spawn, pm_coordinator_run_spawn(spawn), response);
}

enum micros_pm_coordinator_error micros_pm_coordinator_spawn_request(
    struct micros_pm_coordinator *coordinator,
    const struct micros_pm_coordinator_backend *backend,
    void *backend_context,
    const struct micros_ipc_message *message,
    struct micros_pm_coordinator_response *response
)
{
    struct micros_pm_request request;
    struct micros_pm_process_handle parent;
    struct micros_spawn_stack_layout layout;
    struct pm_coordinator_spawn spawn;
    enum micros_pm_result result;
    enum pm_coordinator_step step;

    if (coordinator == NULL || message == NULL || response == NULL
        || !pm_coordinator_spawn_backend_is_complete(backend)
        || backend->grant_copy_from == NULL) {
        return MICROS_PM_COORDINATOR_ERROR_ARGUMENT;
    }
    step = pm_coordinator_preflight_call(
        coordinator, MICROS_PM_MESSAGE_SPAWN, message, &request, &parent, &result
    );
    if (step == PM_COORDINATOR_CORRUPT) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    if (step == PM_COORDINATOR_REJECTED) {
        return build_spawn_response(message->type, message->reply_token, result, 0, response);
    }
    if (coordinator->spawn_pending) {
        return MICROS_PM_COORDINATOR_ERROR_STATE;
    }
    pm_common_zero_bytes(&spawn, sizeof(spawn));
    spawn.coordinator = coordinator;
    spawn.backend = backend;
    spawn.context = backend_context;
    spawn.parent_endpoint = request.source;
    spawn.parent_reply_token = request.reply_token;
    spawn.request_size = request.request_size;
    step = copy_parent_request(&spawn, &request, &layout);
    if (step == PM_COORDINATOR_REJECTED) {
        return build_spawn_response(
            request.type, request.reply_token, spawn.rejection, 0, response
        );
    }
    if (step != PM_COORDINATOR_ADVANCED) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    spawn.expected_stack = MICROS_APPLICATION_STACK_TOP - layout.occupied_size;
    return reserve_public_child(&spawn, &request, parent, response);
}
