#include "micros/runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/bootstrap_control.h"
#include "tests/qemu/bootstrap_protocol.h"

volatile struct micros_bootstrap_service_config
    micros_bootstrap_service_config;
const volatile uint64_t micros_bootstrap_probe_rodata =
    UINT64_C(0x42535450524f4245);
volatile uint64_t micros_bootstrap_probe_data =
    UINT64_C(0x50524f4245444154);

_Noreturn void micros_bootstrap_probe_report(int64_t result);

static void clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static bool bytes_equal(
    const void *left,
    const void *right,
    size_t size
)
{
    const uint8_t *left_bytes = left;
    const uint8_t *right_bytes = right;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
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

static int find_service(uint32_t service_id)
{
    size_t index;

    for (
        index = 0;
        index < micros_bootstrap_service_config.service_count;
        ++index
    ) {
        if (
            micros_bootstrap_service_config.services[index].service_id
                == service_id
        ) {
            return (int)index;
        }
    }
    return -1;
}

static bool validate_configuration(void)
{
    int self_index;
    int launcher_index;
    size_t index;

    if (
        micros_bootstrap_service_config.version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.manifest_version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || (
            micros_bootstrap_service_config.service_id
                != MICROS_BOOTSTRAP_TEST_FIRST_SERVICE_ID
            && micros_bootstrap_service_config.service_id
                != MICROS_BOOTSTRAP_TEST_LAST_SERVICE_ID
        )
        || micros_bootstrap_service_config.service_count
            != MICROS_BOOTSTRAP_TEST_SERVICE_COUNT
        || micros_bootstrap_service_config.manifest_view_address != 0
    ) {
        return false;
    }
    self_index = find_service(
        micros_bootstrap_service_config.service_id
    );
    launcher_index = find_service(
        MICROS_BOOTSTRAP_TEST_LAUNCHER_SERVICE_ID
    );
    if (
        self_index < 0
        || launcher_index < 0
        || micros_bootstrap_service_config.services[self_index].endpoint
            != micros_bootstrap_service_config.self_endpoint
        || micros_bootstrap_service_config.services[launcher_index].endpoint
            != micros_bootstrap_service_config.launcher_endpoint
    ) {
        return false;
    }
    for (
        index = 0;
        index < micros_bootstrap_service_config.service_count;
        ++index
    ) {
        if (
            micros_bootstrap_service_config.services[index].service_id
                != index + 1
            || micros_bootstrap_service_config.services[index].endpoint
                == MICROS_ENDPOINT_NONE
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

static bool validate_ack(
    const struct micros_ipc_message *message
)
{
    size_t index;

    if (
        message->source
            != micros_bootstrap_service_config.launcher_endpoint
        || message->type != MICROS_BOOTSTRAP_MESSAGE_READY_ACK
        || message->reply_token != 0
        || read_u32_le(&message->payload[0])
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || read_u32_le(&message->payload[4])
            != micros_bootstrap_service_config.service_id
        || read_u32_le(&message->payload[8])
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || read_u32_le(&message->payload[12]) != 0
        || read_u32_le(&message->payload[16])
            != micros_bootstrap_service_config.self_endpoint
    ) {
        return false;
    }
    for (index = 20; index < sizeof(message->payload); ++index) {
        if (message->payload[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool unreleased_endpoint_is_hidden(void)
{
    struct micros_ipc_message message;
    struct micros_ipc_message snapshot;
    int target_index = find_service(
        MICROS_BOOTSTRAP_TEST_LAST_SERVICE_ID
    );

    if (target_index < 0) {
        return false;
    }
    clear_bytes(&message, sizeof(message));
    message.type = UINT32_C(0x11223344);
    message.payload[0] = UINT8_C(0xa5);
    snapshot = message;
    return (
        micros_runtime_call(
            micros_bootstrap_service_config.services[target_index]
                .endpoint,
            &message
        ) == MICROS_SYSCALL_ABI_STATE
        && bytes_equal(&message, &snapshot, sizeof(message))
    );
}

void micros_service_main(void)
{
    struct micros_ipc_message message;
    struct micros_ipc_message snapshot;
    int64_t result;

    if (
        micros_bootstrap_probe_rodata
            != UINT64_C(0x42535450524f4245)
        || micros_bootstrap_probe_data
            != UINT64_C(0x50524f4245444154)
        || !validate_configuration()
        || (
            micros_bootstrap_service_config.service_id
                == MICROS_BOOTSTRAP_TEST_FIRST_SERVICE_ID
            && !unreleased_endpoint_is_hidden()
        )
    ) {
        __builtin_trap();
    }
    clear_bytes(&message, sizeof(message));
    message.type = MICROS_BOOTSTRAP_MESSAGE_READY;
    write_u32_le(
        &message.payload[0],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    write_u32_le(
        &message.payload[4],
        micros_bootstrap_service_config.service_id
    );
    write_u32_le(
        &message.payload[8],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    write_u32_le(&message.payload[12], 0);
    write_u32_le(
        &message.payload[16],
        micros_bootstrap_service_config.self_endpoint
    );
    if (
        micros_runtime_call(
            micros_bootstrap_service_config.launcher_endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
        || !validate_ack(&message)
    ) {
        __builtin_trap();
    }
    if (
        micros_bootstrap_service_config.service_id
        == MICROS_BOOTSTRAP_TEST_FIRST_SERVICE_ID
    ) {
        clear_bytes(&message, sizeof(message));
        (void)micros_runtime_receive(MICROS_ENDPOINT_ANY, &message);
        __builtin_trap();
    }
    clear_bytes(&message, sizeof(message));
    message.type = UINT32_C(0x55667788);
    message.payload[0] = UINT8_C(0x5a);
    snapshot = message;
    result = micros_runtime_call(
        micros_bootstrap_service_config.launcher_endpoint,
        &message
    );
    if (
        result != MICROS_SYSCALL_ABI_ENDPOINT_CLOSING
        || !bytes_equal(&message, &snapshot, sizeof(message))
    ) {
        __builtin_trap();
    }
    micros_bootstrap_probe_report(result);
}
