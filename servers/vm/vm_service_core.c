#include "servers/vm/vm_service_internal.h"

#define VM_SERVICE_INITIALIZATION_MAGIC UINT64_C(0x564d5356434f5245)

static bool service_endpoint_is_concrete(micros_endpoint_t endpoint)
{
    uint32_t slot_mask = (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) - 1;
    uint32_t generation = endpoint >> MICROS_ENDPOINT_SLOT_BITS;

    return endpoint != MICROS_ENDPOINT_NONE && endpoint != MICROS_ENDPOINT_ANY
        && (endpoint & slot_mask) < MICROS_PROCESS_CAPACITY
        && generation != 0 && generation <= MICROS_ENDPOINT_GENERATION_MAX;
}

static bool service_bindings_are_valid(const struct micros_vm_service_bindings *bindings)
{
    return bindings->reserved == 0
        && service_endpoint_is_concrete(bindings->self)
        && service_endpoint_is_concrete(bindings->pm)
        && service_endpoint_is_concrete(bindings->vfs)
        && bindings->self != bindings->pm && bindings->self != bindings->vfs
        && bindings->pm != bindings->vfs;
}

enum micros_vm_service_error micros_vm_service_initialize(
    struct micros_vm_service *service, struct micros_vm_state *state,
    const struct micros_vm_service_bindings *bindings
)
{
    struct micros_vm_service_bindings captured;

    if (service == NULL || state == NULL || bindings == NULL) {
        return MICROS_VM_SERVICE_ERROR_ARGUMENT;
    }
    captured = *bindings;
    if (
        service->initialization_magic != 0 || !service_bindings_are_valid(&captured)
        || micros_vm_state_validate(state) != MICROS_VM_CORE_OK
        || state->phase != MICROS_VM_LOADER_IDLE
        || captured.self != state->boot_info->header.self_endpoint
    ) {
        return MICROS_VM_SERVICE_ERROR_ARGUMENT;
    }
    vm_service_clear_bytes(service, sizeof(*service));
    service->state = state;
    service->bindings = captured;
    service->initialization_magic = VM_SERVICE_INITIALIZATION_MAGIC;
    return MICROS_VM_SERVICE_OK;
}

static bool service_instance_is_valid(const struct micros_vm_service *service)
{
    return service->initialization_magic == VM_SERVICE_INITIALIZATION_MAGIC
        && service->state != NULL && service_bindings_are_valid(&service->bindings)
        && micros_vm_state_validate(service->state) == MICROS_VM_CORE_OK
        && service->bindings.self == service->state->boot_info->header.self_endpoint;
}

static bool service_backend_is_complete(
    const struct micros_vm_service_backend *backend, uint32_t command
)
{
    switch (command) {
    case MICROS_VM_CONTROL_LOAD:
        return backend->copy_from != NULL && backend->load_begin != NULL
            && backend->scratch_map != NULL && backend->scratch_page != NULL
            && backend->scratch_commit != NULL && backend->freeze != NULL;
    case MICROS_VM_CONTROL_ABORT_LOAD:
        return backend->abort_load != NULL;
    case MICROS_VM_CONTROL_COMMIT_LOAD:
        return backend->commit_load != NULL;
    case MICROS_VM_CONTROL_EXIT_RELEASE:
        return backend->exit_release != NULL;
    default:
        return false;
    }
}

enum micros_vm_service_error micros_vm_service_handle_message(
    struct micros_vm_service *service, const struct micros_vm_service_backend *backend,
    void *backend_context, const struct micros_ipc_message *request,
    struct micros_ipc_message *reply
)
{
    struct vm_service_request decoded;
    enum micros_vm_control_result result;
    enum micros_vm_service_error error;
    uint64_t outputs[4] = {0};

    if (service == NULL || backend == NULL || request == NULL || reply == NULL) {
        return MICROS_VM_SERVICE_ERROR_ARGUMENT;
    }
    if (!service_instance_is_valid(service) || request->reply_token == 0) {
        return MICROS_VM_SERVICE_ERROR_INVARIANT;
    }
    result = vm_service_decode_request(service, request, &decoded);
    if (result == MICROS_VM_CONTROL_RESULT_OK) {
        if (!service_backend_is_complete(backend, decoded.command)) {
            return MICROS_VM_SERVICE_ERROR_ARGUMENT;
        }
        error = decoded.command == MICROS_VM_CONTROL_LOAD
            ? vm_service_load(service, backend, backend_context, &decoded, &result, outputs)
            : vm_service_release(service, backend, backend_context, &decoded, &result);
        if (error != MICROS_VM_SERVICE_OK) {
            return error;
        }
    }
    vm_service_build_reply(decoded.command, result, outputs, reply);
    return MICROS_VM_SERVICE_OK;
}
