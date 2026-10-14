#include "servers/pm/pm_coordinator_internal.h"
#include "servers/pm/pm_internal.h"

enum micros_pm_coordinator_error pm_coordinator_reply_response(
    uint32_t request_type, uint64_t token, enum micros_pm_result result, uint64_t pid,
    enum micros_pm_exit_kind exit_kind, int32_t exit_code,
    struct micros_pm_coordinator_response *response
)
{
    struct micros_pm_coordinator_response built;

    pm_common_zero_bytes(&built, sizeof(built));
    built.kind = MICROS_PM_COORDINATOR_RESPONSE_REPLY;
    built.reply_token = token;
    if (micros_pm_build_result(
        request_type, result, pid, exit_kind, exit_code, &built.message
    ) != MICROS_PM_MODEL_OK) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    *response = built;
    return MICROS_PM_COORDINATOR_OK;
}

void pm_coordinator_no_reply_response(
    enum micros_pm_coordinator_response_kind kind,
    struct micros_pm_coordinator_response *response
)
{
    pm_common_zero_bytes(response, sizeof(*response));
    response->kind = kind;
    response->message.source = MICROS_ENDPOINT_NONE;
}

enum pm_coordinator_step pm_coordinator_preflight_call(
    const struct micros_pm_coordinator *coordinator, uint32_t expected_type,
    const struct micros_ipc_message *message, struct micros_pm_request *request,
    struct micros_pm_process_handle *caller, enum micros_pm_result *rejection
)
{
    enum micros_pm_protocol_status status = micros_pm_decode_request(message, request);
    enum micros_pm_model_error error;
    uint64_t pid;

    if (status == MICROS_PM_PROTOCOL_INVARIANT
        || pm_coordinator_validate_instance(coordinator) != MICROS_PM_COORDINATOR_OK) {
        return PM_COORDINATOR_CORRUPT;
    }
    switch (status) {
    case MICROS_PM_PROTOCOL_BAD_TYPE:
        *rejection = MICROS_PM_RESULT_BAD_TYPE;
        return PM_COORDINATOR_REJECTED;
    case MICROS_PM_PROTOCOL_BAD_VERSION:
        *rejection = MICROS_PM_RESULT_BAD_VERSION;
        return PM_COORDINATOR_REJECTED;
    case MICROS_PM_PROTOCOL_MALFORMED:
        *rejection = MICROS_PM_RESULT_MALFORMED;
        return PM_COORDINATOR_REJECTED;
    case MICROS_PM_PROTOCOL_OK:
        break;
    case MICROS_PM_PROTOCOL_INVARIANT:
    default:
        return PM_COORDINATOR_CORRUPT;
    }
    if (request->type != expected_type) {
        *rejection = MICROS_PM_RESULT_BAD_TYPE;
        return PM_COORDINATOR_REJECTED;
    }
    error = micros_pm_find_running_by_endpoint(
        coordinator->table, request->source, caller, &pid
    );
    if (error == MICROS_PM_MODEL_ERROR_STALE) {
        *rejection = MICROS_PM_RESULT_CALLER;
        return PM_COORDINATOR_REJECTED;
    }
    if (error != MICROS_PM_MODEL_OK) {
        return PM_COORDINATOR_CORRUPT;
    }
    if (!coordinator->table->runtime_enabled) {
        *rejection = MICROS_PM_RESULT_STATE;
        return PM_COORDINATOR_REJECTED;
    }
    return PM_COORDINATOR_ADVANCED;
}
