#include "tests/qemu/bootstrap_launcher_internal.h"

volatile struct micros_bootstrap_service_config
    micros_bootstrap_service_config;
const volatile uint64_t micros_bootstrap_launcher_rodata =
    UINT64_C(0x4c41554e43484552);
volatile uint64_t micros_bootstrap_launcher_data =
    UINT64_C(0x4253544c41554e43);

uint32_t bootstrap_launcher_read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

uint64_t bootstrap_launcher_read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

bool bootstrap_launcher_bytes_are_zero(const void *storage, size_t size)
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

bool bootstrap_launcher_name_is_canonical(
    const char name[MICROS_BOOTSTRAP_NAME_SIZE]
)
{
    bool terminated = false;
    size_t index;

    if (name[0] == '\0') {
        return false;
    }
    for (index = 0; index < MICROS_BOOTSTRAP_NAME_SIZE; ++index) {
        if (name[index] == '\0') {
            terminated = true;
        } else if (terminated) {
            return false;
        }
    }
    return terminated;
}

bool bootstrap_launcher_names_equal(
    const char left[MICROS_BOOTSTRAP_NAME_SIZE],
    const char right[MICROS_BOOTSTRAP_NAME_SIZE]
)
{
    size_t index;

    for (index = 0; index < MICROS_BOOTSTRAP_NAME_SIZE; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

int bootstrap_launcher_find_config_endpoint(micros_endpoint_t endpoint)
{
    size_t index;

    for (
        index = 0;
        index < micros_bootstrap_service_config.service_count;
        ++index
    ) {
        if (
            micros_bootstrap_service_config.services[index].endpoint
                == endpoint
        ) {
            return (int)index;
        }
    }
    return -1;
}

int bootstrap_launcher_find_manifest_service(
    const struct micros_bootstrap_manifest *manifest,
    uint32_t service_id
)
{
    size_t index;

    for (index = 0; index < manifest->header.entry_count; ++index) {
        if (manifest->entries[index].service_id == service_id) {
            return (int)index;
        }
    }
    return -1;
}
