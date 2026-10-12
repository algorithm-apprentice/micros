#include "servers/pm/pm_service_internal.h"

bool pm_service_send_ready(void)
{
    struct micros_ipc_message message;

    pm_service_clear_bytes(&message, sizeof(message));
    message.type = MICROS_BOOTSTRAP_MESSAGE_READY;
    pm_service_write_u32_le(
        &message.payload[0],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    pm_service_write_u32_le(
        &message.payload[4],
        micros_bootstrap_service_config.service_id
    );
    pm_service_write_u32_le(
        &message.payload[8],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    pm_service_write_u32_le(
        &message.payload[16],
        micros_bootstrap_service_config.self_endpoint
    );
    if (
        micros_runtime_call(
            micros_bootstrap_service_config.launcher_endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return false;
    }
    return (
        message.source
            == micros_bootstrap_service_config.launcher_endpoint
        && message.type == MICROS_BOOTSTRAP_MESSAGE_READY_ACK
        && message.reply_token == 0
        && pm_service_read_u32_le(&message.payload[0])
            == MICROS_BOOTSTRAP_MANIFEST_VERSION
        && pm_service_read_u32_le(&message.payload[4])
            == micros_bootstrap_service_config.service_id
        && pm_service_read_u32_le(&message.payload[8])
            == MICROS_BOOTSTRAP_MANIFEST_VERSION
        && pm_service_read_u32_le(&message.payload[12]) == 0
        && pm_service_read_u32_le(&message.payload[16])
            == micros_bootstrap_service_config.self_endpoint
        && pm_service_bytes_are_zero(
            &message.payload[20],
            sizeof(message.payload) - 20
        )
    );
}
