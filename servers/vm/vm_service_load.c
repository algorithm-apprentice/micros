#include "servers/vm/vm_service_internal.h"

static enum micros_vm_service_error service_copy_inputs(
    struct micros_vm_service *service, const struct micros_vm_service_backend *backend,
    void *context, const struct vm_service_request *request
)
{
    struct micros_vm_state *state = service->state;
    size_t executable_size = (size_t)(request->arguments[2] >> 32);
    size_t request_size = (size_t)(request->arguments[3] >> 32);
    size_t offset;

    for (offset = 0; offset < executable_size;) {
        size_t size = executable_size - offset;

        if (size > MICROS_APPLICATION_STACK_PAGE_SIZE) {
            size = MICROS_APPLICATION_STACK_PAGE_SIZE;
        }
        if (
            backend->copy_from(context, service->bindings.vfs,
                               (micros_grant_t)request->arguments[2], offset,
                               (uintptr_t)&state->executable_bytes[offset], size)
                != MICROS_SYSCALL_ABI_OK
        ) {
            return MICROS_VM_SERVICE_ERROR_INVARIANT;
        }
        offset += size;
    }
    if (
        backend->copy_from(context, service->bindings.vfs,
                           (micros_grant_t)request->arguments[3], 0,
                           (uintptr_t)state->request_bytes, request_size)
            != MICROS_SYSCALL_ABI_OK
    ) {
        return MICROS_VM_SERVICE_ERROR_INVARIANT;
    }
    return MICROS_VM_SERVICE_OK;
}

static enum micros_vm_service_error service_bind_load(
    struct micros_vm_state *state, const struct micros_vm_service_backend *backend,
    void *context
)
{
    struct micros_vm_load_begin_result result = {0};
    struct micros_process_handle process;

    if (
        backend->load_begin(context, state->transaction.process_reservation_token, &result)
            != MICROS_SYSCALL_ABI_OK
        || result.version != MICROS_VM_LOAD_BEGIN_RESULT_VERSION
        || result.size != MICROS_VM_LOAD_BEGIN_RESULT_SIZE
        || result.reserved0 != 0 || result.reserved1 != 0 || result.load_token == 0
    ) {
        return MICROS_VM_SERVICE_ERROR_INVARIANT;
    }
    process.slot = result.process_slot;
    process.generation = result.process_generation;
    return micros_vm_load_bind(state, process, result.load_token) == MICROS_VM_CORE_OK
        ? MICROS_VM_SERVICE_OK : MICROS_VM_SERVICE_ERROR_INVARIANT;
}

static enum micros_vm_service_error service_fill_pages(
    struct micros_vm_state *state, const struct micros_vm_service_backend *backend,
    void *context
)
{
    uint32_t index;

    for (index = 0; index < state->transaction.page_count; ++index) {
        struct micros_vm_load_page_action page;
        uint8_t *scratch;

        if (
            micros_vm_load_next_page(state, &page) != MICROS_VM_CORE_OK
            || backend->scratch_map(context, state->transaction.load_token, &page)
                != MICROS_SYSCALL_ABI_OK
            || micros_vm_load_scratch_mapped(state, page.page_index) != MICROS_VM_CORE_OK
        ) {
            return MICROS_VM_SERVICE_ERROR_INVARIANT;
        }
        scratch = backend->scratch_page(context);
        if (
            micros_vm_load_fill_scratch(state, page.page_index, scratch,
                                       MICROS_APPLICATION_STACK_PAGE_SIZE)
                != MICROS_VM_CORE_OK
            || backend->scratch_commit(context, state->transaction.load_token, &page)
                != MICROS_SYSCALL_ABI_OK
            || micros_vm_load_scratch_committed(state, page.page_index) != MICROS_VM_CORE_OK
        ) {
            return MICROS_VM_SERVICE_ERROR_INVARIANT;
        }
    }
    return MICROS_VM_SERVICE_OK;
}

static enum micros_vm_service_error service_freeze_load(
    struct micros_vm_state *state, const struct micros_vm_service_backend *backend,
    void *context
)
{
    struct micros_vm_freeze_plan plan;
    struct micros_vm_load_freeze_plan input = {0};
    struct micros_vm_load_freeze_result output = {0};
    size_t index;

    if (micros_vm_load_freeze_plan(state, &plan) != MICROS_VM_CORE_OK) {
        return MICROS_VM_SERVICE_ERROR_INVARIANT;
    }
    input.version = MICROS_VM_LOAD_FREEZE_PLAN_VERSION;
    input.size = MICROS_VM_LOAD_FREEZE_PLAN_SIZE;
    input.load_token = plan.load_token;
    for (index = 0; index < MICROS_VM_PAGE_TABLE_FRAME_COUNT; ++index) {
        input.table_frames[index] = plan.table_frames[index];
    }
    input.entry = plan.entry;
    input.stack_pointer = plan.stack_pointer;
    input.image_page_count = plan.image_page_count;
    input.data_page_count = plan.data_page_count;
    input.page_plan_digest = plan.page_plan_digest;
    if (
        backend->freeze(context, &input, &output) != MICROS_SYSCALL_ABI_OK
        || output.version != MICROS_VM_LOAD_FREEZE_RESULT_VERSION
        || output.size != MICROS_VM_LOAD_FREEZE_RESULT_SIZE
        || output.reserved[0] != 0 || output.reserved[1] != 0
        || micros_vm_load_frozen(state, output.mapping_generation) != MICROS_VM_CORE_OK
    ) {
        return MICROS_VM_SERVICE_ERROR_INVARIANT;
    }
    return MICROS_VM_SERVICE_OK;
}

static enum micros_vm_service_error service_prepare_result(
    enum micros_vm_core_error error, enum micros_vm_control_result *result
)
{
    switch (error) {
    case MICROS_VM_CORE_OK:
        *result = MICROS_VM_CONTROL_RESULT_OK;
        break;
    case MICROS_VM_CORE_ERROR_NO_SPACE:
        *result = MICROS_VM_CONTROL_RESULT_NO_SPACE;
        break;
    case MICROS_VM_CORE_ERROR_EXECUTABLE:
        *result = MICROS_VM_CONTROL_RESULT_EXECUTABLE;
        break;
    case MICROS_VM_CORE_ERROR_TOO_BIG:
        *result = MICROS_VM_CONTROL_RESULT_TOO_BIG;
        break;
    case MICROS_VM_CORE_ERROR_REQUEST:
        *result = MICROS_VM_CONTROL_RESULT_MALFORMED;
        break;
    default:
        return MICROS_VM_SERVICE_ERROR_INVARIANT;
    }
    return MICROS_VM_SERVICE_OK;
}

enum micros_vm_service_error vm_service_load(
    struct micros_vm_service *service, const struct micros_vm_service_backend *backend,
    void *backend_context, const struct vm_service_request *request,
    enum micros_vm_control_result *result, uint64_t outputs[4]
)
{
    struct micros_vm_state *state = service->state;
    struct micros_application_stack_values values = {
        .pm_endpoint = service->bindings.pm,
        .vfs_endpoint = service->bindings.vfs,
        .pid = request->arguments[1],
    };
    size_t executable_size = (size_t)(request->arguments[2] >> 32);
    size_t request_size = (size_t)(request->arguments[3] >> 32);
    enum micros_vm_service_error error;

    if (state->phase != MICROS_VM_LOADER_IDLE) {
        *result = MICROS_VM_CONTROL_RESULT_STATE;
        return MICROS_VM_SERVICE_OK;
    }
    if (
        executable_size > sizeof(state->executable_bytes)
        || request_size > sizeof(state->request_bytes)
    ) {
        *result = MICROS_VM_CONTROL_RESULT_TOO_BIG;
        return MICROS_VM_SERVICE_OK;
    }
    error = service_copy_inputs(service, backend, backend_context, request);
    if (error != MICROS_VM_SERVICE_OK) {
        return error;
    }
    error = service_prepare_result(
        micros_vm_load_prepare(state, request->arguments[0], request->arguments[1],
                               state->executable_bytes, executable_size,
                               state->request_bytes, request_size, &values),
        result
    );
    if (error != MICROS_VM_SERVICE_OK || *result != MICROS_VM_CONTROL_RESULT_OK) {
        return error;
    }
    if (
        service_bind_load(state, backend, backend_context) != MICROS_VM_SERVICE_OK
        || service_fill_pages(state, backend, backend_context) != MICROS_VM_SERVICE_OK
        || service_freeze_load(state, backend, backend_context) != MICROS_VM_SERVICE_OK
    ) {
        return MICROS_VM_SERVICE_ERROR_INVARIANT;
    }
    outputs[0] = state->transaction.load_token;
    outputs[1] = state->transaction.mapping_generation;
    outputs[2] = state->transaction.elf_plan.entry;
    outputs[3] = state->transaction.stack_plan.stack_pointer;
    return MICROS_VM_SERVICE_OK;
}
