#include "servers/pm/pm_service_internal.h"

#ifndef MICROS_VM_SCRATCH_PREEMPTION_TEST
static enum micros_pm_result protocol_result(
    const struct pm_service_context *context,
    const struct micros_ipc_message *message
)
{
    struct micros_pm_request request;
    struct micros_pm_process_handle caller;
    uint64_t caller_pid;
    enum micros_pm_protocol_status status =
        micros_pm_decode_request(message, &request);
    enum micros_pm_model_error error;

    switch (status) {
    case MICROS_PM_PROTOCOL_BAD_TYPE:
        return MICROS_PM_RESULT_BAD_TYPE;
    case MICROS_PM_PROTOCOL_BAD_VERSION:
        return MICROS_PM_RESULT_BAD_VERSION;
    case MICROS_PM_PROTOCOL_MALFORMED:
        return MICROS_PM_RESULT_MALFORMED;
    case MICROS_PM_PROTOCOL_INVARIANT:
        __builtin_trap();
    case MICROS_PM_PROTOCOL_OK:
        break;
    }
    error = micros_pm_find_running_by_endpoint(
        &context->process_table,
        request.source,
        &caller,
        &caller_pid
    );
    if (error == MICROS_PM_MODEL_ERROR_STALE) {
        return MICROS_PM_RESULT_CALLER;
    }
    if (error != MICROS_PM_MODEL_OK) {
        __builtin_trap();
    }
    (void)caller;
    (void)caller_pid;
    return MICROS_PM_RESULT_STATE;
}

void pm_service_serve_request(
    struct pm_service_context *context,
    const struct micros_ipc_message *request,
    struct micros_ipc_message *reply
)
{
    enum micros_pm_result result;

#ifdef MICROS_PM_SERVICE_TEST
    if (
        !pm_service_bytes_equal(
            &context->process_table,
            &context->initial_table,
            sizeof(context->process_table)
        )
    ) {
        __builtin_trap();
    }
#endif
    result = protocol_result(context, request);
    if (
        micros_pm_build_result(
            request->type,
            result,
            0,
            MICROS_PM_EXIT_NONE,
            0,
            reply
        ) != MICROS_PM_MODEL_OK
        || micros_pm_table_validate(&context->process_table)
            != MICROS_PM_MODEL_OK
    ) {
        __builtin_trap();
    }
#ifdef MICROS_PM_SERVICE_TEST
    if (
        !pm_service_bytes_equal(
            &context->process_table,
            &context->initial_table,
            sizeof(context->process_table)
        )
    ) {
        __builtin_trap();
    }
#endif
}

#endif
