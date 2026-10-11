#include "micros/runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/bootstrap_control.h"
#include "tests/qemu/vm_scratch_preemption_protocol.h"

volatile struct micros_bootstrap_service_config
    micros_bootstrap_service_config;
const volatile uint64_t micros_vm_scratch_preemption_driver_rodata =
    UINT64_C(0x5653504452495652);
static volatile uint64_t driver_data =
    UINT64_C(0x5653504452495644);

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

static void write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
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
            != MICROS_VM_SCRATCH_PREEMPTION_DRIVER_SERVICE_ID
        || micros_bootstrap_service_config.service_count
            != MICROS_VM_SCRATCH_PREEMPTION_SERVICE_COUNT
        || micros_bootstrap_service_config.self_endpoint
            != micros_bootstrap_service_config.services[
                MICROS_VM_SCRATCH_PREEMPTION_DRIVER_SERVICE_ID - 1
            ].endpoint
        || micros_bootstrap_service_config.launcher_endpoint
            != micros_bootstrap_service_config.services[
                MICROS_VM_SCRATCH_PREEMPTION_LAUNCHER_SERVICE_ID - 1
            ].endpoint
        || micros_bootstrap_service_config.manifest_view_address != 0
    ) {
        return false;
    }
    for (
        index = 0;
        index < MICROS_VM_SCRATCH_PREEMPTION_SERVICE_COUNT;
        ++index
    ) {
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
    return bytes_are_zero(
        (const uint8_t *)
            micros_bootstrap_service_config.reserved,
        sizeof(micros_bootstrap_service_config.reserved)
    );
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
        MICROS_VM_SCRATCH_PREEMPTION_DRIVER_SERVICE_ID
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
            == MICROS_VM_SCRATCH_PREEMPTION_DRIVER_SERVICE_ID
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

static bool reserve_process(uint64_t *transaction)
{
    struct micros_ipc_message message;
    micros_endpoint_t pm_endpoint =
        micros_bootstrap_service_config.services[
            MICROS_VM_SCRATCH_PREEMPTION_PM_SERVICE_ID - 1
        ].endpoint;

    clear_bytes(&message, sizeof(message));
    message.type = MICROS_VM_SCRATCH_PREEMPTION_RESERVE;
    write_u32_le(
        &message.payload[0],
        MICROS_VM_SCRATCH_PREEMPTION_VERSION
    );
    if (
        micros_runtime_call(pm_endpoint, &message)
            != MICROS_SYSCALL_ABI_OK
        || message.source != pm_endpoint
        || message.type
            != MICROS_VM_SCRATCH_PREEMPTION_RESERVATION
        || message.reply_token != 0
        || read_u32_le(&message.payload[0])
            != MICROS_VM_SCRATCH_PREEMPTION_VERSION
        || !bytes_are_zero(
            &message.payload[16],
            sizeof(message.payload) - 16
        )
    ) {
        return false;
    }
    *transaction = read_u64_le(&message.payload[8]);
    return *transaction != 0;
}

static _Noreturn void start_vm(uint64_t transaction)
{
    struct micros_ipc_message message;
    micros_endpoint_t vm_endpoint =
        micros_bootstrap_service_config.services[
            MICROS_VM_SCRATCH_PREEMPTION_VM_SERVICE_ID - 1
        ].endpoint;

    clear_bytes(&message, sizeof(message));
    message.type = MICROS_VM_SCRATCH_PREEMPTION_START;
    write_u32_le(
        &message.payload[0],
        MICROS_VM_SCRATCH_PREEMPTION_VERSION
    );
    write_u64_le(&message.payload[8], transaction);
    if (
        micros_runtime_call(vm_endpoint, &message)
            != MICROS_SYSCALL_ABI_OK
    ) {
        __builtin_trap();
    }
    __builtin_trap();
}

void micros_service_main(void)
{
    uint64_t transaction = 0;

    if (
        micros_vm_scratch_preemption_driver_rodata
            != UINT64_C(0x5653504452495652)
        || driver_data != UINT64_C(0x5653504452495644)
        || !validate_configuration()
        || !send_ready()
        || !reserve_process(&transaction)
    ) {
        __builtin_trap();
    }
    start_vm(transaction);
}
