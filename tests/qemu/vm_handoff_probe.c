#include "micros/runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/bootstrap_control.h"
#include "tests/qemu/vm_handoff_protocol.h"

volatile struct micros_bootstrap_service_config
    micros_bootstrap_service_config;

struct probe_buffer {
    uint64_t prefix;
    uint8_t bytes[MICROS_VM_HANDOFF_TEST_DATA_SIZE];
    uint64_t suffix;
};

static volatile struct probe_buffer transfer_buffer;
static volatile uint64_t probe_data = UINT64_C(0x50524f4245564d48);

_Noreturn void micros_vm_handoff_probe_report(uint64_t result);

static void clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static void write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
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
    int vm_index;
    int launcher_index;
    size_t index;

    if (
        micros_bootstrap_service_config.version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.manifest_version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.service_id
            != MICROS_VM_HANDOFF_TEST_PROBE_SERVICE_ID
        || micros_bootstrap_service_config.service_count
            != MICROS_VM_HANDOFF_TEST_SERVICE_COUNT
        || micros_bootstrap_service_config.manifest_view_address != 0
    ) {
        return false;
    }
    for (
        index = 0;
        index
            < sizeof(micros_bootstrap_service_config.reserved)
                / sizeof(micros_bootstrap_service_config.reserved[0]);
        ++index
    ) {
        if (micros_bootstrap_service_config.reserved[index] != 0) {
            return false;
        }
    }
    self_index = find_service(
        MICROS_VM_HANDOFF_TEST_PROBE_SERVICE_ID
    );
    vm_index = find_service(MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID);
    launcher_index = find_service(
        MICROS_VM_HANDOFF_TEST_LAUNCHER_SERVICE_ID
    );
    if (
        self_index < 0
        || vm_index < 0
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
        index < MICROS_VM_HANDOFF_TEST_SERVICE_COUNT;
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
    return true;
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
        MICROS_VM_HANDOFF_TEST_PROBE_SERVICE_ID
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
    return (
        micros_runtime_call(
            micros_bootstrap_service_config.launcher_endpoint,
            &message
        ) == MICROS_SYSCALL_ABI_OK
        && message.source
            == micros_bootstrap_service_config.launcher_endpoint
        && message.type == MICROS_BOOTSTRAP_MESSAGE_READY_ACK
        && message.reply_token == 0
        && read_u32_le(&message.payload[4])
            == MICROS_VM_HANDOFF_TEST_PROBE_SERVICE_ID
    );
}

void micros_service_main(void)
{
    struct micros_ipc_message message;
    micros_grant_t grant;
    int vm_index;
    size_t index;

    if (
        probe_data != UINT64_C(0x50524f4245564d48)
        || !validate_configuration()
        || !send_ready()
    ) {
        __builtin_trap();
    }
    vm_index = find_service(MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID);
    if (vm_index < 0) {
        __builtin_trap();
    }
    transfer_buffer.prefix = UINT64_C(0x1122334455667788);
    transfer_buffer.suffix = UINT64_C(0x8877665544332211);
    for (index = 0; index < MICROS_VM_HANDOFF_TEST_DATA_SIZE; ++index) {
        transfer_buffer.bytes[index] =
            (uint8_t)(UINT8_C(0x20) + index);
    }
    if (
        micros_runtime_grant_create(
            micros_bootstrap_service_config.services[vm_index].endpoint,
            (uintptr_t)transfer_buffer.bytes,
            sizeof(transfer_buffer.bytes),
            MICROS_GRANT_PERMISSION_READ
                | MICROS_GRANT_PERMISSION_WRITE,
            &grant
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        __builtin_trap();
    }
    clear_bytes(&message, sizeof(message));
    message.type = MICROS_VM_HANDOFF_TEST_REQUEST;
    write_u64_le(&message.payload[0], grant);
    write_u32_le(
        &message.payload[8],
        MICROS_VM_HANDOFF_TEST_DATA_SIZE
    );
    write_u32_le(
        &message.payload[12],
        MICROS_VM_HANDOFF_TEST_PAYLOAD_MAGIC
    );
    if (
        micros_runtime_call(
            micros_bootstrap_service_config.services[vm_index].endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
        || message.type != MICROS_VM_HANDOFF_TEST_REPLY
        || message.source
            != micros_bootstrap_service_config.services[vm_index].endpoint
        || message.reply_token != 0
        || read_u32_le(&message.payload[0])
            != MICROS_VM_HANDOFF_TEST_DATA_SIZE
        || read_u32_le(&message.payload[4])
            != MICROS_VM_HANDOFF_TEST_PAYLOAD_MAGIC
        || transfer_buffer.prefix != UINT64_C(0x1122334455667788)
        || transfer_buffer.suffix != UINT64_C(0x8877665544332211)
    ) {
        __builtin_trap();
    }
    for (index = 0; index < MICROS_VM_HANDOFF_TEST_DATA_SIZE; ++index) {
        if (
            transfer_buffer.bytes[index]
                != (uint8_t)((UINT8_C(0x20) + index) ^ UINT8_C(0x5a))
        ) {
            __builtin_trap();
        }
    }
    if (
        micros_runtime_grant_revoke(grant)
            != MICROS_SYSCALL_ABI_OK
    ) {
        __builtin_trap();
    }
    micros_vm_handoff_probe_report(
        MICROS_VM_HANDOFF_TEST_REPORT_MAGIC
    );
}
