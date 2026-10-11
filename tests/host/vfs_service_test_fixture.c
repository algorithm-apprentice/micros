#include "tests/host/vfs_service_test_fixture.h"

#include "micros/spawn.h"
#include "micros/vfs.h"

#include <string.h>

void vfs_service_test_context_initialize(struct vfs_service_test_context *context) {
    memset(context, 0, sizeof(*context));
    context->vfs_endpoint = UINT32_C(0x00001005);
    context->vm_endpoint = UINT32_C(0x00001001);
    context->ramfs_endpoint = UINT32_C(0x00001004);
    context->tty_endpoint = UINT32_C(0x00001003);
    context->pm_endpoint = UINT32_C(0x00001002);
    context->application_endpoint = UINT32_C(0x00002006);
    context->spawn_child_endpoint = UINT32_C(0x00003007);
}

void vfs_service_test_write_u32_le(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

void vfs_service_test_write_u64_le(uint8_t *bytes, uint64_t value) {
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

uint32_t vfs_service_test_read_u32_le(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
            (uint32_t)bytes[3] << 24);
}

uint64_t vfs_service_test_read_u64_le(const uint8_t *bytes) {
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

int32_t vfs_service_test_read_i32_le(const uint8_t *bytes) {
    return (int32_t)vfs_service_test_read_u32_le(bytes);
}

size_t vfs_service_test_build_control_spawn_request(uint8_t *bytes, const char *path) {
    static const char argument[] = "child";
    size_t path_size = strlen(path) + 1;
    size_t argument_size = sizeof(argument);
    size_t cursor = MICROS_SPAWN_REQUEST_HEADER_SIZE;

    memset(bytes, 0, MICROS_SPAWN_REQUEST_SIZE_MAX);
    vfs_service_test_write_u32_le(&bytes[0], MICROS_SPAWN_REQUEST_VERSION);
    vfs_service_test_write_u32_le(&bytes[4], MICROS_SPAWN_REQUEST_HEADER_SIZE);
    bytes[MICROS_SPAWN_ARGUMENT_COUNT_OFFSET] = 1;
    bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET] = (uint8_t)cursor;
    bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET + 1] = (uint8_t)(cursor >> 8);
    bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET + 2] = (uint8_t)path_size;
    bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET + 3] = (uint8_t)(path_size >> 8);
    memcpy(&bytes[cursor], path, path_size);
    cursor += path_size;
    bytes[MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET] = (uint8_t)cursor;
    bytes[MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET + 1] = (uint8_t)(cursor >> 8);
    bytes[MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET + 2] = (uint8_t)argument_size;
    bytes[MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET + 3] = (uint8_t)(argument_size >> 8);
    memcpy(&bytes[cursor], argument, argument_size);
    cursor += argument_size;
    vfs_service_test_write_u32_le(&bytes[8], (uint32_t)cursor);
    return cursor;
}

struct micros_ipc_message
vfs_service_test_request_message(const struct vfs_service_test_context *context, uint32_t type) {
    struct micros_ipc_message message = {
        .source = context->application_endpoint,
        .type = type,
        .reply_token = UINT64_C(0x55),
    };

    vfs_service_test_write_u32_le(&message.payload[0], MICROS_VFS_PROTOCOL_VERSION);
    return message;
}

struct micros_ipc_message
vfs_service_test_pm_control_message(const struct vfs_service_test_context *context,
                                    enum micros_vfs_control_command command) {
    struct micros_ipc_message message = {
        .source = context->pm_endpoint,
        .type = MICROS_VFS_MESSAGE_PM_CONTROL,
        .reply_token = UINT64_C(0x66),
    };

    vfs_service_test_write_u32_le(&message.payload[0], MICROS_VFS_PROTOCOL_VERSION);
    vfs_service_test_write_u32_le(&message.payload[4], (uint32_t)command);
    return message;
}

struct micros_bootstrap_service_config vfs_service_test_production_config(void) {
    struct micros_bootstrap_service_config config = {
        .version = MICROS_BOOTSTRAP_MANIFEST_VERSION,
        .manifest_version = MICROS_BOOTSTRAP_MANIFEST_VERSION,
        .service_id = MICROS_VFS_SERVICE_ID,
        .self_endpoint = UINT32_C(0x00001005),
        .launcher_endpoint = UINT32_C(0x00001000),
        .service_count = MICROS_VFS_SERVICE_ID,
    };
    size_t index;

    for (index = 0; index < MICROS_VFS_SERVICE_ID; ++index) {
        config.services[index].service_id = (uint32_t)index + 1;
        config.services[index].endpoint = UINT32_C(0x00001000) + (uint32_t)index;
    }
    return config;
}

struct micros_bootstrap_service_config
vfs_service_test_config(const struct vfs_service_test_context *context) {
    struct micros_bootstrap_service_config config = vfs_service_test_production_config();

    config.service_count = MICROS_BOOTSTRAP_SERVICE_CAPACITY;
    config.services[6].service_id = 7;
    config.services[6].endpoint = context->application_endpoint;
    return config;
}

bool vfs_service_test_initialize_fixture(struct vfs_service_test_context *context) {
    enum micros_vfs_trusted_result result;

    vfs_test_fixture_initialize(&context->fixture, context->vfs_endpoint, context->vm_endpoint,
                                context->pm_endpoint, context->ramfs_endpoint,
                                context->tty_endpoint);
    return (micros_vfs_state_initialize(
                &context->state, context->vfs_endpoint, context->vm_endpoint, context->pm_endpoint,
                context->ramfs_endpoint, context->tty_endpoint) == MICROS_VFS_CORE_OK &&
            micros_vfs_mount(&context->state, &context->fixture.io) == MICROS_VFS_CORE_OK &&
            micros_vfs_attach_console(&context->state, context->application_endpoint, 0, &result) ==
                MICROS_VFS_CORE_OK &&
            result == MICROS_VFS_TRUSTED_OK);
}
