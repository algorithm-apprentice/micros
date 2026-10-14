#include "servers/pm/pm_service_internal.h"

static micros_runtime_result_t service_backend_reserve(
    void *context, struct micros_pm_reservation_result *result
)
{
    (void)context;
    return micros_pm_control_reserve(result);
}

static micros_runtime_result_t service_backend_abort_reserved(void *context, uint64_t transaction)
{
    (void)context;
    return micros_pm_control_abort(transaction);
}

static micros_runtime_result_t service_backend_prepare(
    void *context, uint64_t transaction, uint64_t load_token, uint64_t generation,
    struct micros_pm_prepared_result *result
)
{
    (void)context;
    return micros_pm_control_prepare_execution(transaction, load_token, generation, result);
}

static micros_runtime_result_t service_backend_abort_prepared(
    void *context, uint64_t transaction, uint64_t load_token, uint64_t generation, uint64_t prepared
)
{
    (void)context;
    return micros_pm_control_abort_prepared(transaction, load_token, generation, prepared);
}

static micros_runtime_result_t service_backend_activate(
    void *context, uint64_t transaction, uint64_t load_token, uint64_t generation, uint64_t prepared
)
{
    (void)context;
    return micros_pm_control_activate(transaction, load_token, generation, prepared);
}

static micros_runtime_result_t service_backend_exit_begin(
    void *context, micros_endpoint_t endpoint, struct micros_pm_exit_begin_result *result
)
{
    (void)context;
    return micros_pm_control_exit_begin(endpoint, result);
}

static micros_runtime_result_t service_backend_exit_detach(
    void *context, uint64_t transaction, micros_endpoint_t endpoint
)
{
    (void)context;
    return micros_pm_control_exit_detach(transaction, endpoint);
}

static micros_runtime_result_t service_backend_release(void *context, uint64_t transaction)
{
    (void)context;
    return micros_pm_control_release_exited_process(transaction);
}

static micros_runtime_result_t service_backend_grant_create(
    void *context, micros_endpoint_t grantee, uintptr_t base, size_t size,
    uint32_t permissions, micros_grant_t *grant
)
{
    (void)context;
    return micros_runtime_grant_create(grantee, base, size, permissions, grant);
}

static micros_runtime_result_t service_backend_grant_revoke(void *context, micros_grant_t grant)
{
    (void)context;
    return micros_runtime_grant_revoke(grant);
}

static micros_runtime_result_t service_backend_grant_copy(
    void *context, micros_endpoint_t grantor, micros_grant_t grant, size_t offset,
    uintptr_t local, size_t size
)
{
    (void)context;
    return micros_runtime_grant_copy_from(grantor, grant, offset, local, size);
}

static micros_runtime_result_t service_backend_call(
    void *context, micros_endpoint_t peer, struct micros_ipc_message *message
)
{
    (void)context;
    return micros_runtime_call(peer, message);
}

static micros_runtime_result_t service_backend_reply(
    void *context, uint64_t token, const struct micros_ipc_message *message
)
{
    (void)context;
    return micros_runtime_reply(token, message);
}

const struct micros_pm_coordinator_backend *pm_service_backend_get(void)
{
    static const struct micros_pm_coordinator_backend backend = {
        .reserve = service_backend_reserve,
        .abort_reserved = service_backend_abort_reserved,
        .prepare_execution = service_backend_prepare,
        .abort_prepared = service_backend_abort_prepared,
        .activate = service_backend_activate,
        .exit_begin = service_backend_exit_begin,
        .exit_detach = service_backend_exit_detach,
        .release_exited_process = service_backend_release,
        .grant_create = service_backend_grant_create,
        .grant_revoke = service_backend_grant_revoke,
        .grant_copy_from = service_backend_grant_copy,
        .call = service_backend_call,
        .reply = service_backend_reply,
    };

    return &backend;
}
