#include "tests/qemu/vm_server_internal.h"

bool vm_server_configure_control(struct vm_server_context *context)
{
    struct micros_vm_service_bindings bindings = {0};
    int pm_index;
    int vfs_index;

    if (context->control_mode != VM_SERVER_CONTROL_UNINITIALIZED) {
        return false;
    }
    if (micros_bootstrap_service_config.service_count < 6) {
        context->control_mode = VM_SERVER_CONTROL_STATIC_ONLY;
        return true;
    }
    pm_index = vm_server_find_service(3);
    vfs_index = vm_server_find_service(6);
    if (pm_index < 0 || vfs_index < 0) {
        return false;
    }
    bindings.self = micros_bootstrap_service_config.self_endpoint;
    bindings.pm = micros_bootstrap_service_config.services[pm_index].endpoint;
    bindings.vfs = micros_bootstrap_service_config.services[vfs_index].endpoint;
    if (
        micros_vm_service_initialize(&context->control, &context->vm_state, &bindings)
            != MICROS_VM_SERVICE_OK
    ) {
        return false;
    }
    context->control_mode = VM_SERVER_CONTROL_READY;
    return true;
}

bool vm_server_serve_control(
    struct vm_server_context *context, const struct micros_ipc_message *request
)
{
    struct micros_ipc_message reply;
#if defined(MICROS_TEST_VM_COMMAND_COUNTS)
    uint32_t command = (uint32_t)(vm_server_read_u64_le(request->payload) >> 32);

    if (!micros_vm01_service_begin(command)) {
        return false;
    }
#endif

    if (
        context->control_mode != VM_SERVER_CONTROL_READY
        || micros_vm_service_handle_message(&context->control,
                                            micros_vm_service_runtime_backend(), NULL,
                                            request, &reply) != MICROS_VM_SERVICE_OK
    ) {
        return false;
    }
#if defined(MICROS_TEST_VM_COMMAND_COUNTS)
    if (vm_server_read_u64_le(&reply.payload[8]) != MICROS_VM_CONTROL_RESULT_OK
        || !micros_vm01_service_finish(command)) {
        return false;
    }
#endif
    return micros_runtime_reply(request->reply_token, &reply) == MICROS_SYSCALL_ABI_OK;
}
