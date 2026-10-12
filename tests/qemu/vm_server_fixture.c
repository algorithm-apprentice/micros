#include "tests/qemu/vm_server_internal.h"

volatile struct micros_bootstrap_service_config micros_bootstrap_service_config;

_Alignas(MICROS_VM_BOOT_INFO_ALIGNMENT)
__attribute__((section(".data.vm_boot_info"), used))
struct micros_vm_boot_info micros_vm_boot_info = {0};

static struct vm_server_context service_context = {
    .vm_data = UINT64_C(0x564d44415441564d),
};

struct vm_server_context *vm_server_context_get(void)
{
    return &service_context;
}

void vm_server_clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

bool vm_server_bytes_are_zero(const void *storage, size_t size)
{
    const uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

void vm_server_write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

uint32_t vm_server_read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

uint64_t vm_server_read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

int vm_server_find_service(uint32_t service_id)
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

bool vm_server_validate_configuration(void)
{
    int self_index;
    int launcher_index;
    int probe_index;
    size_t index;

    if (
        micros_bootstrap_service_config.version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.manifest_version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.service_id
            != MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID
        || micros_bootstrap_service_config.service_count
            != MICROS_VM_HANDOFF_TEST_SERVICE_COUNT
        || micros_bootstrap_service_config.manifest_view_address != 0
        || !vm_server_bytes_are_zero(
            (const void *)micros_bootstrap_service_config.reserved,
            sizeof(micros_bootstrap_service_config.reserved)
        )
    ) {
        return false;
    }
    self_index = vm_server_find_service(MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID);
    launcher_index = vm_server_find_service(
        MICROS_VM_HANDOFF_TEST_LAUNCHER_SERVICE_ID
    );
    probe_index = vm_server_find_service(
        MICROS_VM_HANDOFF_TEST_PROBE_SERVICE_ID
    );
    if (
        self_index < 0
        || launcher_index < 0
        || probe_index < 0
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
