#include "servers/pm/pm_service_internal.h"

volatile struct micros_bootstrap_service_config
    micros_bootstrap_service_config;

static struct pm_service_context service_context = {
    .pm_service_data = UINT64_C(0x504d534552564943),
};

struct pm_service_context *pm_service_context_get(void)
{
    return &service_context;
}

void pm_service_clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

#ifdef MICROS_PM_SERVICE_TEST
void pm_service_copy_bytes(
    void *destination,
    const void *source,
    size_t size
)
{
    uint8_t *destination_bytes = destination;
    const uint8_t *source_bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}
#endif

#ifdef MICROS_PM_SERVICE_TEST
bool pm_service_bytes_equal(
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
#endif

#ifdef MICROS_PM_SERVICE_TEST
void pm_service_fill_bytes(
    void *storage,
    size_t size,
    uint8_t value
)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = value;
    }
}
#endif

#ifdef MICROS_PM_SERVICE_TEST
bool pm_service_bytes_have_value(
    const void *storage,
    size_t size,
    uint8_t value
)
{
    const uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != value) {
            return false;
        }
    }
    return true;
}
#endif

bool pm_service_bytes_are_zero(
    const uint8_t *bytes,
    size_t size
)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

void pm_service_write_u32_le(
    uint8_t *bytes,
    uint32_t value
)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

#if defined(MICROS_VM_SCRATCH_PREEMPTION_TEST) \
    || defined(MICROS_PM_SERVICE_TEST)
void pm_service_write_u64_le(
    uint8_t *bytes,
    uint64_t value
)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}
#endif

#ifdef MICROS_PM_SERVICE_TEST
uint64_t pm_service_read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}
#endif

uint32_t pm_service_read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

bool pm_service_validate_configuration(void)
{
    size_t index;

    if (
        micros_bootstrap_service_config.version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.manifest_version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.service_id
            != MICROS_PM_SERVICE_ID
        || micros_bootstrap_service_config.service_count < 3
        || micros_bootstrap_service_config.service_count
            > MICROS_BOOTSTRAP_SERVICE_CAPACITY
        || micros_bootstrap_service_config.self_endpoint
            != micros_bootstrap_service_config.services[2].endpoint
        || micros_bootstrap_service_config.launcher_endpoint
            != micros_bootstrap_service_config.services[0].endpoint
        || micros_bootstrap_service_config.manifest_view_address != 0
    ) {
        return false;
    }
#ifdef MICROS_PM_SERVICE_TEST
    if (
        micros_bootstrap_service_config.service_count
            != MICROS_PM_TEST_SERVICE_COUNT
    ) {
        return false;
    }
#endif
#ifdef MICROS_VM_SCRATCH_PREEMPTION_TEST
    if (
        micros_bootstrap_service_config.service_count
            != MICROS_VM_SCRATCH_PREEMPTION_SERVICE_COUNT
    ) {
        return false;
    }
#endif
    for (
        index = 0;
        index < micros_bootstrap_service_config.service_count;
        ++index
    ) {
        micros_endpoint_t endpoint =
            micros_bootstrap_service_config.services[index].endpoint;

        if (
            micros_bootstrap_service_config.services[index].service_id
                != index + 1
            || endpoint == MICROS_ENDPOINT_NONE
            || endpoint == MICROS_ENDPOINT_ANY
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
