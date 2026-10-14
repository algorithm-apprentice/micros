#include "servers/vm/vm_service_internal.h"

static enum micros_vm_service_error service_release_frozen(
    struct micros_vm_state *state, const struct micros_vm_service_backend *backend,
    void *context, const struct vm_service_request *request,
    enum micros_vm_control_result *result
)
{
    uint64_t token = request->arguments[0];
    uint64_t generation = request->arguments[1];
    uint64_t pid = request->arguments[2];
    micros_runtime_result_t kernel_result;
    enum micros_vm_core_error local_result;

    if (
        state->phase != MICROS_VM_LOADER_FROZEN
        || state->transaction.load_token != token
        || state->transaction.mapping_generation != generation
        || state->transaction.semantic_pid != pid
    ) {
        *result = MICROS_VM_CONTROL_RESULT_STATE;
        return MICROS_VM_SERVICE_OK;
    }
    kernel_result = request->command == MICROS_VM_CONTROL_ABORT_LOAD
        ? backend->abort_load(context, token, generation)
        : backend->commit_load(context, token, generation);
    if (kernel_result != MICROS_SYSCALL_ABI_OK) {
        return MICROS_VM_SERVICE_ERROR_INVARIANT;
    }
    local_result = request->command == MICROS_VM_CONTROL_ABORT_LOAD
        ? micros_vm_load_aborted(state, token, generation)
        : micros_vm_load_committed(state, token, generation, pid);
    return local_result == MICROS_VM_CORE_OK
        ? MICROS_VM_SERVICE_OK : MICROS_VM_SERVICE_ERROR_INVARIANT;
}

enum micros_vm_service_error vm_service_release(
    struct micros_vm_service *service, const struct micros_vm_service_backend *backend,
    void *backend_context, const struct vm_service_request *request,
    enum micros_vm_control_result *result
)
{
    struct micros_vm_exit_action action;
    enum micros_vm_core_error error;

    if (request->command != MICROS_VM_CONTROL_EXIT_RELEASE) {
        return service_release_frozen(service->state, backend, backend_context, request, result);
    }
    error = micros_vm_exit_release_begin(service->state, request->arguments[1],
                                        request->arguments[2], request->arguments[3], &action);
    if (error == MICROS_VM_CORE_ERROR_STATE) {
        *result = MICROS_VM_CONTROL_RESULT_STATE;
        return MICROS_VM_SERVICE_OK;
    }
    if (
        error != MICROS_VM_CORE_OK
        || backend->exit_release(backend_context, request->arguments[0],
                                 action.load_token, action.mapping_generation)
            != MICROS_SYSCALL_ABI_OK
        || micros_vm_exit_release_complete(service->state, &action) != MICROS_VM_CORE_OK
    ) {
        return MICROS_VM_SERVICE_ERROR_INVARIANT;
    }
    return MICROS_VM_SERVICE_OK;
}
