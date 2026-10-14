#include "tests/host/elf_spawn_model_internal.h"

micros_runtime_result_t elf_model_grant_create(
    void *opaque, micros_endpoint_t grantee, uintptr_t base, size_t length,
    uint32_t permissions, micros_grant_t *grant
)
{
    struct elf_spawn_model_context *context = opaque;

    if (!elf_model_record_stage(context, ELF_STAGE_REQUEST_GRANT)
        || !elf_model_kernel_require(context, grantee == ELF_MODEL_VFS
            && base == (uintptr_t)context->coordinator.request_bytes && length <= 4096
            && permissions == MICROS_GRANT_PERMISSION_READ)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    if (context->operation == ELF_MODEL_GRANT_FAILURE) {
        return MICROS_SYSCALL_ABI_CAPACITY;
    }
    *grant = vfs_test_fixture_add_application_grant(
        &context->filesystem, ELF_MODEL_PM, permissions, (const uint8_t *)base, length
    );
    return *grant == MICROS_GRANT_NONE ? MICROS_SYSCALL_ABI_CAPACITY : MICROS_SYSCALL_ABI_OK;
}

micros_runtime_result_t elf_model_grant_revoke(void *opaque, micros_grant_t grant)
{
    struct elf_spawn_model_context *context = opaque;

    if (!elf_model_record_stage(context, ELF_STAGE_REVOKE_REQUEST)
        || !elf_model_kernel_require(context,
            vfs_test_fixture_revoke_application_grant(&context->filesystem, grant))) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    return MICROS_SYSCALL_ABI_OK;
}

micros_runtime_result_t elf_model_parent_copy(
    void *opaque, micros_endpoint_t grantor, micros_grant_t grant,
    size_t offset, uintptr_t destination, size_t length
)
{
    struct elf_spawn_model_context *context = opaque;
    enum micros_vfs_client_result result;

    if (!elf_model_kernel_require(context, offset == 0 && length <= 4096
        && destination == (uintptr_t)context->coordinator.request_bytes)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    if (context->operation == ELF_MODEL_BAD_PARENT_GRANT) {
        return MICROS_SYSCALL_ABI_STALE_GRANT;
    }
    result = context->filesystem.io.client_copy(
        context->filesystem.io.context, MICROS_VFS_CLIENT_FROM_APPLICATION,
        grantor, grant, offset, (uint8_t *)destination, length
    );
    return result == MICROS_VFS_CLIENT_OK ? MICROS_SYSCALL_ABI_OK : MICROS_SYSCALL_ABI_STALE_GRANT;
}

static uint32_t vfs_command_stage(uint32_t command)
{
    switch (command) {
    case MICROS_VFS_CONTROL_PREPARE_IMAGE: return ELF_STAGE_IMAGE;
    case MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS: return ELF_STAGE_DESCRIPTORS;
    case MICROS_VFS_CONTROL_COMMIT_HIDDEN: return ELF_STAGE_HIDDEN;
    case MICROS_VFS_CONTROL_REVERT_HIDDEN: return ELF_STAGE_REVERT_HIDDEN;
    case MICROS_VFS_CONTROL_ABORT_DESCRIPTORS: return ELF_STAGE_ABORT_DESCRIPTORS;
    case MICROS_VFS_CONTROL_RELEASE_IMAGE: return ELF_STAGE_RELEASE_IMAGE;
    case MICROS_VFS_CONTROL_EXIT_PROCESS: return ELF_STAGE_EXIT_VFS;
    default: return 0;
    }
}

static uint32_t vm_command_stage(uint32_t command)
{
    switch (command) {
    case MICROS_VM_CONTROL_LOAD: return ELF_STAGE_LOAD;
    case MICROS_VM_CONTROL_ABORT_LOAD: return ELF_STAGE_ABORT_LOAD;
    case MICROS_VM_CONTROL_COMMIT_LOAD: return ELF_STAGE_COMMIT_LOAD;
    case MICROS_VM_CONTROL_EXIT_RELEASE: return ELF_STAGE_EXIT_VM;
    default: return 0;
    }
}

micros_runtime_result_t elf_model_call(
    void *opaque, micros_endpoint_t peer, struct micros_ipc_message *message
)
{
    struct elf_spawn_model_context *context = opaque;
    struct micros_ipc_message request = *message;
    uint32_t command = (uint32_t)(elf_model_read_word(request.payload) >> 32);
    uint32_t stage = peer == ELF_MODEL_VFS ? vfs_command_stage(command) : vm_command_stage(command);

    if (!elf_model_record_stage(context, stage)
        || !elf_model_kernel_require(context, (peer == ELF_MODEL_VFS || peer == ELF_MODEL_VM)
            && request.source == MICROS_ENDPOINT_NONE && request.reply_token == 0
            && request.type == (peer == ELF_MODEL_VFS ? MICROS_VFS_MESSAGE_PM_CONTROL
                                                     : MICROS_VM_MESSAGE_PM_CONTROL))) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    request.source = ELF_MODEL_PM;
    request.reply_token = ++context->last_reply;
    if (peer == ELF_MODEL_VFS) {
        struct micros_vfs_service_reply_action action;

        if (!elf_model_kernel_require(context,
            micros_vfs_service_handle_message(&context->vfs, &request,
                &context->filesystem.io, &action) == MICROS_VFS_SERVICE_OK
            && action.active && action.reply_token == request.reply_token)) {
            return MICROS_SYSCALL_ABI_STATE;
        }
        *message = action.message;
    } else {
        if (!elf_model_kernel_require(context,
            micros_vm_service_handle_message(&context->vm_service, elf_model_vm_backend(),
                context, &request, message) == MICROS_VM_SERVICE_OK)) {
            return MICROS_SYSCALL_ABI_STATE;
        }
    }
    message->source = peer;
    message->reply_token = 0;
    if (stage == ELF_STAGE_LOAD && elf_model_read_word(&message->payload[8]) == 0) {
        elf_model_cancel_at(context, stage);
    }
    return MICROS_SYSCALL_ABI_OK;
}

micros_runtime_result_t elf_model_reply(
    void *opaque, uint64_t token, const struct micros_ipc_message *message
)
{
    struct elf_spawn_model_context *context = opaque;

    if (!elf_model_kernel_require(context, context->reply_count < 2 && token != 0
        && message->type == MICROS_PM_MESSAGE_RESULT && message->reply_token == 0
        && elf_model_read_word(&message->payload[0])
            == (UINT64_C(1) | (uint64_t)MICROS_PM_MESSAGE_WAIT << 32)
        && elf_model_read_word(&message->payload[8]) == 0
        && (uint32_t)elf_model_read_word(&message->payload[24]) == MICROS_PM_EXIT_NORMAL
        && vm_core_test_bytes_are_zero(&message->payload[32], 16))) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    context->replies[context->reply_count++] = (struct elf_model_wait_reply){
        .token = token,
        .pid = elf_model_read_word(&message->payload[16]),
        .status = (int32_t)(elf_model_read_word(&message->payload[24]) >> 32),
    };
    return MICROS_SYSCALL_ABI_OK;
}
