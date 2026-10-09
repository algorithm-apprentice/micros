#include "micros/runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/bootstrap_control.h"
#include "micros/pm.h"
#include "tests/qemu/pm_service_protocol.h"

volatile struct micros_bootstrap_service_config
    micros_bootstrap_service_config;
const volatile uint64_t micros_pm_service_probe_rodata =
    UINT64_C(0x504d50524f424552);
static volatile uint64_t pm_service_probe_data =
    UINT64_C(0x504d50524f424544);

static void clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static bool bytes_are_zero(const uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static void write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

static uint64_t read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static bool validate_configuration(void)
{
    size_t index;

    if (
        micros_bootstrap_service_config.version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.manifest_version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.service_id
            != MICROS_PM_TEST_PROBE_SERVICE_ID
        || micros_bootstrap_service_config.service_count
            != MICROS_PM_TEST_SERVICE_COUNT
        || micros_bootstrap_service_config.self_endpoint
            != micros_bootstrap_service_config.services[
                MICROS_PM_TEST_PROBE_SERVICE_ID - 1
            ].endpoint
        || micros_bootstrap_service_config.launcher_endpoint
            != micros_bootstrap_service_config.services[
                MICROS_PM_TEST_LAUNCHER_SERVICE_ID - 1
            ].endpoint
        || micros_bootstrap_service_config.manifest_view_address != 0
    ) {
        return false;
    }
    for (index = 0; index < MICROS_PM_TEST_SERVICE_COUNT; ++index) {
        if (
            micros_bootstrap_service_config.services[index].service_id
                != index + 1
            || micros_bootstrap_service_config.services[index].endpoint
                == MICROS_ENDPOINT_NONE
            || micros_bootstrap_service_config.services[index].endpoint
                == MICROS_ENDPOINT_ANY
        ) {
            return false;
        }
    }
    for (
        index = 0;
        index < sizeof(micros_bootstrap_service_config.reserved)
                / sizeof(micros_bootstrap_service_config.reserved[0]);
        ++index
    ) {
        if (micros_bootstrap_service_config.reserved[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool result_matches(
    const struct micros_ipc_message *message,
    micros_endpoint_t pm_endpoint,
    enum micros_pm_result expected
)
{
    return (
        message->source == pm_endpoint
        && message->type == MICROS_PM_MESSAGE_RESULT
        && message->reply_token == 0
        && read_u32_le(&message->payload[0])
            == MICROS_PM_PROTOCOL_VERSION
        && read_u32_le(&message->payload[4])
            == MICROS_PM_MESSAGE_WAIT
        && (int32_t)read_u32_le(&message->payload[8])
            == expected
        && read_u32_le(&message->payload[12]) == 0
        && read_u64_le(&message->payload[16]) == 0
        && read_u32_le(&message->payload[24])
            == MICROS_PM_EXIT_NONE
        && read_u32_le(&message->payload[28]) == 0
        && bytes_are_zero(
            &message->payload[32],
            sizeof(message->payload) - 32
        )
    );
}

static bool call_pm(
    uint32_t version,
    uint32_t flags,
    enum micros_pm_result expected
)
{
    struct micros_ipc_message message;
    micros_endpoint_t pm_endpoint =
        micros_bootstrap_service_config.services[
            MICROS_PM_TEST_PM_SERVICE_ID - 1
        ].endpoint;

    clear_bytes(&message, sizeof(message));
    message.type = MICROS_PM_MESSAGE_WAIT;
    write_u32_le(&message.payload[0], version);
    write_u32_le(&message.payload[4], flags);
    if (
        micros_runtime_call(pm_endpoint, &message)
            != MICROS_SYSCALL_ABI_OK
    ) {
        return false;
    }
    return result_matches(&message, pm_endpoint, expected);
}

static bool send_ready(void)
{
    struct micros_ipc_message message;

    clear_bytes(&message, sizeof(message));
    message.type = MICROS_BOOTSTRAP_MESSAGE_READY;
    write_u32_le(
        &message.payload[0],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    write_u32_le(
        &message.payload[4],
        MICROS_PM_TEST_PROBE_SERVICE_ID
    );
    write_u32_le(
        &message.payload[8],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    write_u32_le(
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
        && read_u32_le(&message.payload[0])
            == MICROS_BOOTSTRAP_MANIFEST_VERSION
        && read_u32_le(&message.payload[4])
            == MICROS_PM_TEST_PROBE_SERVICE_ID
        && read_u32_le(&message.payload[8])
            == MICROS_BOOTSTRAP_MANIFEST_VERSION
        && read_u32_le(&message.payload[12]) == 0
        && read_u32_le(&message.payload[16])
            == micros_bootstrap_service_config.self_endpoint
        && bytes_are_zero(
            &message.payload[20],
            sizeof(message.payload) - 20
        )
    );
}

void micros_service_main(void)
{
    struct micros_ipc_message unexpected;

    if (
        micros_pm_service_probe_rodata
            != UINT64_C(0x504d50524f424552)
        || pm_service_probe_data
            != UINT64_C(0x504d50524f424544)
        || !validate_configuration()
        || !call_pm(
            MICROS_PM_PROTOCOL_VERSION + 1,
            0,
            MICROS_PM_RESULT_BAD_VERSION
        )
        || !call_pm(
            MICROS_PM_PROTOCOL_VERSION,
            UINT32_C(0x2),
            MICROS_PM_RESULT_MALFORMED
        )
        || !call_pm(
            MICROS_PM_PROTOCOL_VERSION,
            0,
            MICROS_PM_RESULT_CALLER
        )
        || !send_ready()
    ) {
        __builtin_trap();
    }
    clear_bytes(&unexpected, sizeof(unexpected));
    if (
        micros_runtime_receive(MICROS_ENDPOINT_ANY, &unexpected)
            != MICROS_SYSCALL_ABI_OK
    ) {
        __builtin_trap();
    }
    __builtin_trap();
}
