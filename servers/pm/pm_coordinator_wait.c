#include "servers/pm/pm_coordinator_internal.h"
#include "servers/pm/pm_internal.h"

static bool wait_reply_failure_is_expected(micros_runtime_result_t result)
{
    return result == MICROS_SYSCALL_ABI_REPLY_TOKEN
        || result == MICROS_SYSCALL_ABI_DEAD_ENDPOINT
        || result == MICROS_SYSCALL_ABI_ENDPOINT_CLOSING;
}

enum micros_pm_coordinator_error pm_coordinator_complete_wait_outcomes(
    struct micros_pm_coordinator *coordinator,
    const struct micros_pm_coordinator_backend *backend, void *context,
    const struct micros_pm_wait_outcomes *outcomes
)
{
    struct micros_ipc_message message;
    size_t index;

    if (outcomes->count > MICROS_PM_WAIT_OUTCOME_CAPACITY || outcomes->reserved != 0) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    for (index = 0; index < outcomes->count; ++index) {
        const struct micros_pm_wait_outcome *outcome = &outcomes->entries[index];
        micros_runtime_result_t status;

        if (
            outcome->action != MICROS_PM_WAIT_ACTION_REPLY || outcome->reply_token == 0
            || outcome->result != MICROS_PM_RESULT_OK || outcome->child_pid == 0
            || micros_pm_build_result(
                MICROS_PM_MESSAGE_WAIT, outcome->result, outcome->child_pid,
                outcome->exit_kind, outcome->exit_code, &message
            ) != MICROS_PM_MODEL_OK
        ) {
            return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
        }
        status = backend->reply(context, outcome->reply_token, &message);
        if (status != MICROS_SYSCALL_ABI_OK && !wait_reply_failure_is_expected(status)) {
            return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
        }
        if (micros_pm_wait_reply_complete(
            coordinator->table, outcome->parent, outcome->reply_token,
            status == MICROS_SYSCALL_ABI_OK
        ) != MICROS_PM_MODEL_OK) {
            return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
        }
    }
    return MICROS_PM_COORDINATOR_OK;
}

enum micros_pm_coordinator_error micros_pm_coordinator_wait_request(
    struct micros_pm_coordinator *coordinator,
    const struct micros_pm_coordinator_backend *backend,
    void *backend_context,
    const struct micros_ipc_message *message,
    struct micros_pm_coordinator_response *response
)
{
    struct micros_pm_request request;
    struct micros_pm_process_handle caller;
    struct micros_pm_wait_outcome outcome;
    struct micros_pm_wait_outcomes outcomes;
    enum micros_pm_result rejection;
    enum pm_coordinator_step step;
    enum micros_pm_coordinator_error error;

    if (coordinator == NULL || backend == NULL || backend->reply == NULL
        || message == NULL || response == NULL) {
        return MICROS_PM_COORDINATOR_ERROR_ARGUMENT;
    }
    step = pm_coordinator_preflight_call(
        coordinator, MICROS_PM_MESSAGE_WAIT, message, &request, &caller, &rejection
    );
    if (step == PM_COORDINATOR_CORRUPT) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    if (step == PM_COORDINATOR_REJECTED) {
        return pm_coordinator_reply_response(
            message->type, message->reply_token, rejection, 0, MICROS_PM_EXIT_NONE, 0, response
        );
    }
    if (micros_pm_wait_begin(
        coordinator->table, caller, request.child_pid, request.flags,
        request.reply_token, &outcome
    ) != MICROS_PM_MODEL_OK) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    if (outcome.action == MICROS_PM_WAIT_ACTION_BLOCKED) {
        pm_coordinator_no_reply_response(MICROS_PM_COORDINATOR_RESPONSE_WAIT_BLOCKED, response);
        return MICROS_PM_COORDINATOR_OK;
    }
    if (outcome.action != MICROS_PM_WAIT_ACTION_REPLY) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    if (outcome.child_pid == 0) {
        return pm_coordinator_reply_response(
            request.type, outcome.reply_token, outcome.result, 0, MICROS_PM_EXIT_NONE, 0, response
        );
    }
    pm_common_zero_bytes(&outcomes, sizeof(outcomes));
    outcomes.count = 1;
    outcomes.entries[0] = outcome;
    error = pm_coordinator_complete_wait_outcomes(coordinator, backend, backend_context, &outcomes);
    if (error != MICROS_PM_COORDINATOR_OK) {
        return error;
    }
    pm_coordinator_no_reply_response(MICROS_PM_COORDINATOR_RESPONSE_WAIT_REPLY_COMPLETED, response);
    return MICROS_PM_COORDINATOR_OK;
}
