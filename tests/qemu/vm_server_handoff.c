#include "tests/qemu/vm_server_internal.h"

#include "lib/runtime/raw_syscall.h"
#include "micros/vm.h"

bool vm_server_send_ready(void)
{
    struct micros_ipc_message message;
    size_t index;

    vm_server_clear_bytes(&message, sizeof(message));
    message.type = MICROS_BOOTSTRAP_MESSAGE_READY;
    vm_server_write_u32_le(
        &message.payload[0],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    vm_server_write_u32_le(
        &message.payload[4],
        MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID
    );
    vm_server_write_u32_le(
        &message.payload[8],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    vm_server_write_u32_le(&message.payload[12], 0);
    vm_server_write_u32_le(
        &message.payload[16],
        micros_bootstrap_service_config.self_endpoint
    );
    if (
        micros_runtime_call(
            micros_bootstrap_service_config.launcher_endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
        || message.source
            != micros_bootstrap_service_config.launcher_endpoint
        || message.type != MICROS_BOOTSTRAP_MESSAGE_READY_ACK
        || message.reply_token != 0
        || vm_server_read_u32_le(&message.payload[0])
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || vm_server_read_u32_le(&message.payload[4])
            != MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID
        || vm_server_read_u32_le(&message.payload[8])
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || vm_server_read_u32_le(&message.payload[12]) != 0
        || vm_server_read_u32_le(&message.payload[16])
            != micros_bootstrap_service_config.self_endpoint
    ) {
        return false;
    }
    for (index = 20; index < sizeof(message.payload); ++index) {
        if (message.payload[index] != 0) {
            return false;
        }
    }
    return true;
}

bool vm_server_complete_handoff(void)
{
    uint64_t packed_counts =
        (
            (uint64_t)micros_vm_boot_info.header.address_space_count
            << 16
        ) | micros_vm_boot_info.header.managed_range_count;

    return micros_runtime_raw_syscall(
        MICROS_VM_HANDOFF_READY,
        micros_vm_boot_info.header.version,
        packed_counts,
        micros_vm_boot_info.header.managed_frame_count,
        micros_vm_boot_info.header.free_frame_count,
        micros_vm_boot_info.header.mapping_count,
        micros_vm_boot_info.header.digest,
        MICROS_SYSCALL_ABI_VM_HANDOFF
    ) == MICROS_SYSCALL_ABI_OK;
}
