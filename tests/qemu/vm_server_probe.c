#include "tests/qemu/vm_server_internal.h"

#if !defined(MICROS_PM_SERVICE_TEST) \
    && !defined(MICROS_VM_TTY_HANDOFF_TEST) \
    && !defined(MICROS_VM_SCRATCH_PREEMPTION_TEST)

bool vm_server_serve_probe(struct vm_server_context *context)
{
    struct micros_ipc_message request;
    struct micros_ipc_message reply;
    uint64_t raw_grant;
    micros_grant_t grant;
    uint32_t length;
    int probe_index = vm_server_find_service(
        MICROS_VM_HANDOFF_TEST_PROBE_SERVICE_ID
    );
    size_t index;

    if (probe_index < 0) {
        return false;
    }
    vm_server_clear_bytes(&request, sizeof(request));
    if (
        micros_runtime_receive(
            micros_bootstrap_service_config.services[probe_index]
                .endpoint,
            &request
        ) != MICROS_SYSCALL_ABI_OK
        || request.source
            != micros_bootstrap_service_config.services[probe_index]
                .endpoint
        || request.type != MICROS_VM_HANDOFF_TEST_REQUEST
        || request.reply_token == 0
        || vm_server_read_u32_le(&request.payload[12])
            != MICROS_VM_HANDOFF_TEST_PAYLOAD_MAGIC
    ) {
        return false;
    }
    raw_grant = vm_server_read_u64_le(&request.payload[0]);
    length = vm_server_read_u32_le(&request.payload[8]);
    if (
        raw_grant > UINT32_MAX
        || length != MICROS_VM_HANDOFF_TEST_DATA_SIZE
    ) {
        return false;
    }
    grant = (micros_grant_t)raw_grant;
    if (
        micros_runtime_grant_copy_from(
            request.source,
            grant,
            0,
            (uintptr_t)context->grant_buffer,
            sizeof(context->grant_buffer)
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return false;
    }
    for (index = 0; index < sizeof(context->grant_buffer); ++index) {
        if (context->grant_buffer[index] != (uint8_t)(UINT8_C(0x20) + index)) {
            return false;
        }
        context->grant_buffer[index] ^= UINT8_C(0x5a);
    }
    if (
        micros_runtime_grant_copy_to(
            request.source,
            grant,
            0,
            (uintptr_t)context->grant_buffer,
            sizeof(context->grant_buffer)
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return false;
    }
    vm_server_clear_bytes(&reply, sizeof(reply));
    reply.type = MICROS_VM_HANDOFF_TEST_REPLY;
    vm_server_write_u32_le(&reply.payload[0], sizeof(context->grant_buffer));
    vm_server_write_u32_le(
        &reply.payload[4],
        MICROS_VM_HANDOFF_TEST_PAYLOAD_MAGIC
    );
    return micros_runtime_reply(request.reply_token, &reply)
        == MICROS_SYSCALL_ABI_OK;
}

#endif
