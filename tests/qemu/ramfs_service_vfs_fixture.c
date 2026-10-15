#include "tests/qemu/ramfs_service_vfs_internal.h"

volatile struct micros_bootstrap_service_config
    micros_bootstrap_service_config;

static struct vfs_service_fixture vfs_fixture;
static volatile uint64_t vfs_initialized_data =
    UINT64_C(0x52414d4653564653);

void vfs_clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

void vfs_fill_bytes(uint8_t *bytes, uint8_t value, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = value;
    }
}

void vfs_copy_bytes(
    uint8_t *destination,
    const uint8_t *source,
    size_t size
)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        destination[index] = source[index];
    }
}

bool vfs_bytes_equal(
    const uint8_t *left,
    const uint8_t *right,
    size_t size
)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

bool vfs_bytes_are_zero(const uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

void vfs_write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

void vfs_write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

uint32_t vfs_read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

uint64_t vfs_read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static bool vfs_endpoint_is_canonical(
    micros_endpoint_t endpoint,
    size_t expected_slot
)
{
    uint32_t slot_mask =
        (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) - 1;
    uint32_t slot = endpoint & slot_mask;
    uint32_t generation = endpoint >> MICROS_ENDPOINT_SLOT_BITS;

    return (
        endpoint != MICROS_ENDPOINT_NONE
        && endpoint != MICROS_ENDPOINT_ANY
        && slot == expected_slot
        && generation != 0
        && generation <= MICROS_ENDPOINT_GENERATION_MAX
    );
}

bool vfs_validate_configuration(
    struct vfs_service_fixture *fixture
)
{
    size_t index;
    size_t other;

    if (
        micros_bootstrap_service_config.version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.manifest_version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.service_id
            != MICROS_RAMFS_TEST_VFS_SERVICE_ID
        || micros_bootstrap_service_config.service_count
            != MICROS_RAMFS_TEST_SERVICE_COUNT
        || micros_bootstrap_service_config.self_endpoint
            != micros_bootstrap_service_config.services[
                MICROS_RAMFS_TEST_VFS_SERVICE_ID - 1
            ].endpoint
        || micros_bootstrap_service_config.launcher_endpoint
            != micros_bootstrap_service_config.services[
                MICROS_RAMFS_TEST_LAUNCHER_SERVICE_ID - 1
            ].endpoint
        || micros_bootstrap_service_config.manifest_view_address != 0
    ) {
        return false;
    }
    for (index = 0; index < MICROS_RAMFS_TEST_SERVICE_COUNT; ++index) {
        if (
            micros_bootstrap_service_config.services[index].service_id
                != index + 1
            || !vfs_endpoint_is_canonical(
                micros_bootstrap_service_config.services[index].endpoint,
                index
            )
        ) {
            return false;
        }
        for (other = 0; other < index; ++other) {
            if (
                micros_bootstrap_service_config.services[index].endpoint
                    == micros_bootstrap_service_config.services[other].endpoint
            ) {
                return false;
            }
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
    fixture->vfs_tty_endpoint = micros_bootstrap_service_config.services[
        MICROS_RAMFS_TEST_TTY_SERVICE_ID - 1
    ].endpoint;
    fixture->vfs_ramfs_endpoint = micros_bootstrap_service_config.services[
        MICROS_RAMFS_TEST_RAMFS_SERVICE_ID - 1
    ].endpoint;
    return true;
}

bool vfs_send_ready(void)
{
    struct micros_ipc_message message;

    vfs_clear_bytes(&message, sizeof(message));
    message.type = MICROS_BOOTSTRAP_MESSAGE_READY;
    vfs_write_u32_le(
        &message.payload[0],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    vfs_write_u32_le(
        &message.payload[4],
        MICROS_RAMFS_TEST_VFS_SERVICE_ID
    );
    vfs_write_u32_le(
        &message.payload[8],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    vfs_write_u32_le(
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
        && vfs_read_u32_le(&message.payload[0])
            == MICROS_BOOTSTRAP_MANIFEST_VERSION
        && vfs_read_u32_le(&message.payload[4])
            == MICROS_RAMFS_TEST_VFS_SERVICE_ID
        && vfs_read_u32_le(&message.payload[8])
            == MICROS_BOOTSTRAP_MANIFEST_VERSION
        && vfs_read_u32_le(&message.payload[12]) == 0
        && vfs_read_u32_le(&message.payload[16])
            == micros_bootstrap_service_config.self_endpoint
        && vfs_bytes_are_zero(
            &message.payload[20],
            sizeof(message.payload) - 20
        )
    );
}

struct vfs_service_fixture *vfs_service_context(void)
{
    return &vfs_fixture;
}

uint64_t vfs_initialized_data_value(void)
{
    return vfs_initialized_data;
}
