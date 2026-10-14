#include "servers/pm/pm_coordinator_internal.h"
#include "servers/pm/pm_internal.h"

struct coordinator_exit_transaction {
    struct micros_pm_coordinator *coordinator;
    const struct micros_pm_coordinator_backend *backend;
    void *context;
    struct micros_pm_process_handle process;
    micros_endpoint_t endpoint;
    uint64_t pid;
    uint64_t exit_token;
    uint64_t load_token;
    uint64_t mapping_generation;
};

static enum micros_pm_coordinator_error release_vfs_process(
    struct coordinator_exit_transaction *exit
)
{
    uint64_t arguments[5] = {exit->exit_token, exit->endpoint, exit->pid, 0, 0};
    struct micros_pm_coordinator_control_result result;

    if (pm_coordinator_vfs_exchange(
        exit->coordinator, exit->backend, exit->context, MICROS_VFS_CONTROL_EXIT_PROCESS,
        arguments, &result
    ) != MICROS_PM_COORDINATOR_OK || result.result != MICROS_VFS_CONTROL_RESULT_OK) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    return MICROS_PM_COORDINATOR_OK;
}

static enum micros_pm_coordinator_error detach_execution(
    struct coordinator_exit_transaction *exit
)
{
    if (
        exit->backend->exit_detach(exit->context, exit->exit_token, exit->endpoint)
            != MICROS_SYSCALL_ABI_OK
        || micros_pm_exit_detach(exit->coordinator->table, exit->process, exit->exit_token)
            != MICROS_PM_MODEL_OK
    ) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    return MICROS_PM_COORDINATOR_OK;
}

static enum micros_pm_coordinator_error release_vm_mappings(
    struct coordinator_exit_transaction *exit
)
{
    uint64_t arguments[5] = {
        exit->exit_token, exit->load_token, exit->mapping_generation, exit->pid, 0,
    };
    struct micros_pm_coordinator_control_result result;

    if (
        pm_coordinator_vm_exchange(
            exit->coordinator, exit->backend, exit->context, MICROS_VM_CONTROL_EXIT_RELEASE,
            arguments, &result
        ) != MICROS_PM_COORDINATOR_OK || result.result != MICROS_VM_CONTROL_RESULT_OK
        || micros_pm_exit_release_vm(
            exit->coordinator->table, exit->process, exit->exit_token
        ) != MICROS_PM_MODEL_OK
    ) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    return MICROS_PM_COORDINATOR_OK;
}

static enum micros_pm_coordinator_error release_process_and_complete_waits(
    struct coordinator_exit_transaction *exit
)
{
    struct micros_pm_wait_outcomes outcomes;

    if (
        exit->backend->release_exited_process(exit->context, exit->exit_token)
            != MICROS_SYSCALL_ABI_OK
        || micros_pm_exit_release_process(
            exit->coordinator->table, exit->process, exit->exit_token, &outcomes
        ) != MICROS_PM_MODEL_OK
    ) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    return pm_coordinator_complete_wait_outcomes(
        exit->coordinator, exit->backend, exit->context, &outcomes
    );
}

static enum micros_pm_coordinator_error run_stopped_exit(
    struct coordinator_exit_transaction *exit
)
{
    enum micros_pm_coordinator_error error = release_vfs_process(exit);

    if (error != MICROS_PM_COORDINATOR_OK) {
        return error;
    }
    error = detach_execution(exit);
    if (error != MICROS_PM_COORDINATOR_OK) {
        return error;
    }
    error = release_vm_mappings(exit);
    if (error != MICROS_PM_COORDINATOR_OK) {
        return error;
    }
    return release_process_and_complete_waits(exit);
}

static bool exit_backend_is_complete(const struct micros_pm_coordinator_backend *backend)
{
    return backend != NULL && backend->exit_begin != NULL && backend->exit_detach != NULL
        && backend->release_exited_process != NULL && backend->call != NULL && backend->reply != NULL;
}

static enum micros_pm_coordinator_error stop_and_commit_exit(
    struct coordinator_exit_transaction *exit, const struct pm_exit_begin_plan *plan,
    bool *rejected
)
{
    struct micros_pm_exit_begin_result result;
    struct micros_pm_exit_effects effects;
    micros_runtime_result_t status;

    *rejected = false;
    pm_common_zero_bytes(&result, sizeof(result));
    status = exit->backend->exit_begin(exit->context, exit->endpoint, &result);
    if (status == MICROS_SYSCALL_ABI_STATE) {
        *rejected = true;
        return MICROS_PM_COORDINATOR_OK;
    }
    if (
        status != MICROS_SYSCALL_ABI_OK || result.version != MICROS_PM_EXIT_BEGIN_VERSION
        || result.size != MICROS_PM_EXIT_BEGIN_SIZE || result.exit_token == 0
        || result.reserved[0] != 0 || result.reserved[1] != 0
    ) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    exit->exit_token = result.exit_token;
    pm_exit_commit_begin(plan, result.exit_token, &effects);
    return MICROS_PM_COORDINATOR_OK;
}

enum micros_pm_coordinator_error micros_pm_coordinator_exit_request(
    struct micros_pm_coordinator *coordinator,
    const struct micros_pm_coordinator_backend *backend,
    void *backend_context,
    const struct micros_ipc_message *message,
    struct micros_pm_coordinator_response *response
)
{
    struct micros_pm_request request;
    struct coordinator_exit_transaction exit;
    struct pm_exit_begin_plan plan;
    enum micros_pm_result rejection;
    enum micros_pm_model_error model_error;
    enum micros_pm_coordinator_error error;
    enum pm_coordinator_step step;
    bool rejected;

    if (coordinator == NULL || message == NULL || response == NULL
        || !exit_backend_is_complete(backend)) {
        return MICROS_PM_COORDINATOR_ERROR_ARGUMENT;
    }
    pm_common_zero_bytes(&exit, sizeof(exit));
    step = pm_coordinator_preflight_call(
        coordinator, MICROS_PM_MESSAGE_EXIT, message, &request, &exit.process, &rejection
    );
    if (step == PM_COORDINATOR_CORRUPT) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    if (step == PM_COORDINATOR_REJECTED) {
        return pm_coordinator_reply_response(
            message->type, message->reply_token, rejection, 0, MICROS_PM_EXIT_NONE, 0, response
        );
    }
    model_error = pm_exit_prepare_begin(
        coordinator->table, exit.process, MICROS_PM_EXIT_NORMAL, request.exit_code, &plan
    );
    if (model_error == MICROS_PM_MODEL_ERROR_BUSY) {
        return pm_coordinator_reply_response(
            request.type, request.reply_token, MICROS_PM_RESULT_BUSY, 0,
            MICROS_PM_EXIT_NONE, 0, response
        );
    }
    if (model_error != MICROS_PM_MODEL_OK) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    exit.coordinator = coordinator;
    exit.backend = backend;
    exit.context = backend_context;
    exit.endpoint = request.source;
    exit.pid = plan.record->pid;
    exit.load_token = plan.record->load_token;
    exit.mapping_generation = plan.record->mapping_generation;
    error = stop_and_commit_exit(&exit, &plan, &rejected);
    if (error != MICROS_PM_COORDINATOR_OK) {
        return error;
    }
    if (rejected) {
        return pm_coordinator_reply_response(
            request.type, request.reply_token, MICROS_PM_RESULT_STATE, 0,
            MICROS_PM_EXIT_NONE, 0, response
        );
    }
    error = run_stopped_exit(&exit);
    if (error != MICROS_PM_COORDINATOR_OK) {
        return error;
    }
    pm_coordinator_no_reply_response(MICROS_PM_COORDINATOR_RESPONSE_EXIT_ACCEPTED, response);
    return MICROS_PM_COORDINATOR_OK;
}
