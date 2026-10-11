#include "tests/host/vfs_spawn_test_fixture.h"

#include "lib/spawn/spawn_request.h"

#include <string.h>

void vfs_spawn_test_write_u16_le(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

void vfs_spawn_test_write_u32_le(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

void vfs_spawn_test_context_initialize(struct vfs_spawn_test_context *context) {
    memset(context, 0, sizeof(*context));
    context->vfs_endpoint = UINT32_C(0x00001006);
    context->vm_endpoint = UINT32_C(0x00001002);
    context->pm_endpoint = UINT32_C(0x00001003);
    context->ramfs_endpoint = UINT32_C(0x00001005);
    context->tty_endpoint = UINT32_C(0x00001004);
    context->parent_endpoint = UINT32_C(0x00002007);
}

bool vfs_spawn_test_initialize_fixture(struct vfs_spawn_test_context *context) {
    enum micros_vfs_trusted_result result;

    vfs_test_fixture_initialize(&context->fixture, context->vfs_endpoint, context->vm_endpoint,
                                context->pm_endpoint, context->ramfs_endpoint,
                                context->tty_endpoint);
    return (micros_vfs_state_initialize(
                &context->state, context->vfs_endpoint, context->vm_endpoint, context->pm_endpoint,
                context->ramfs_endpoint, context->tty_endpoint) == MICROS_VFS_CORE_OK &&
            micros_vfs_mount(&context->state, &context->fixture.io) == MICROS_VFS_CORE_OK &&
            micros_vfs_attach_console(&context->state, context->parent_endpoint, 0, &result) ==
                MICROS_VFS_CORE_OK &&
            result == MICROS_VFS_TRUSTED_OK);
}

bool vfs_spawn_test_bytes_are_zero(const void *storage, size_t size) {
    const uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

size_t vfs_spawn_test_build_request(struct vfs_spawn_test_context *context, const char *path) {
    static const char argument[] = "child";
    size_t path_size = strlen(path) + 1;
    size_t argument_size = sizeof(argument);
    size_t cursor = MICROS_SPAWN_REQUEST_HEADER_SIZE;

    memset(context->request_bytes, 0, sizeof(context->request_bytes));
    vfs_spawn_test_write_u32_le(&context->request_bytes[0], MICROS_SPAWN_REQUEST_VERSION);
    vfs_spawn_test_write_u32_le(&context->request_bytes[4], MICROS_SPAWN_REQUEST_HEADER_SIZE);
    vfs_spawn_test_write_u16_le(&context->request_bytes[MICROS_SPAWN_ARGUMENT_COUNT_OFFSET], 1);
    vfs_spawn_test_write_u16_le(&context->request_bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET],
                                (uint16_t)cursor);
    vfs_spawn_test_write_u16_le(&context->request_bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET + 2],
                                (uint16_t)path_size);
    memcpy(&context->request_bytes[cursor], path, path_size);
    cursor += path_size;
    vfs_spawn_test_write_u16_le(&context->request_bytes[MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET],
                                (uint16_t)cursor);
    vfs_spawn_test_write_u16_le(
        &context->request_bytes[MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET + 2],
        (uint16_t)argument_size);
    memcpy(&context->request_bytes[cursor], argument, argument_size);
    cursor += argument_size;
    vfs_spawn_test_write_u32_le(&context->request_bytes[8], (uint32_t)cursor);
    return cursor;
}

void vfs_spawn_test_set_descriptor_actions(struct vfs_spawn_test_context *context,
                                           const struct micros_spawn_descriptor_action *actions,
                                           size_t action_count) {
    size_t index;

    vfs_spawn_test_write_u16_le(
        &context->request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTION_COUNT_OFFSET],
        (uint16_t)action_count);
    for (index = 0; index < action_count; ++index) {
        size_t offset =
            MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET + index * MICROS_SPAWN_DESCRIPTOR_ACTION_SIZE;

        vfs_spawn_test_write_u16_le(&context->request_bytes[offset], actions[index].source);
        vfs_spawn_test_write_u16_le(&context->request_bytes[offset + 2],
                                    actions[index].destination);
        vfs_spawn_test_write_u32_le(&context->request_bytes[offset + 4], actions[index].operation);
    }
}

size_t vfs_spawn_test_build_oversized_stack_request(struct vfs_spawn_test_context *context,
                                                    const char *path) {
    enum {
        STRING_SIZE = 120,
    };
    size_t path_size = strlen(path) + 1;
    size_t cursor = MICROS_SPAWN_REQUEST_HEADER_SIZE;
    size_t index;

    memset(context->request_bytes, 0, sizeof(context->request_bytes));
    vfs_spawn_test_write_u32_le(&context->request_bytes[0], MICROS_SPAWN_REQUEST_VERSION);
    vfs_spawn_test_write_u32_le(&context->request_bytes[4], MICROS_SPAWN_REQUEST_HEADER_SIZE);
    vfs_spawn_test_write_u16_le(&context->request_bytes[MICROS_SPAWN_ARGUMENT_COUNT_OFFSET],
                                MICROS_SPAWN_ARGUMENT_CAPACITY);
    vfs_spawn_test_write_u16_le(&context->request_bytes[MICROS_SPAWN_ENVIRONMENT_COUNT_OFFSET],
                                MICROS_SPAWN_ENVIRONMENT_CAPACITY);
    vfs_spawn_test_write_u16_le(&context->request_bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET],
                                (uint16_t)cursor);
    vfs_spawn_test_write_u16_le(&context->request_bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET + 2],
                                (uint16_t)path_size);
    memcpy(&context->request_bytes[cursor], path, path_size);
    cursor += path_size;
    for (index = 0; index < MICROS_SPAWN_ARGUMENT_CAPACITY; ++index) {
        size_t reference =
            MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET + index * MICROS_SPAWN_STRING_REFERENCE_SIZE;

        vfs_spawn_test_write_u16_le(&context->request_bytes[reference], (uint16_t)cursor);
        vfs_spawn_test_write_u16_le(&context->request_bytes[reference + 2], STRING_SIZE);
        memset(&context->request_bytes[cursor], 'a', STRING_SIZE - 1);
        context->request_bytes[cursor + STRING_SIZE - 1] = 0;
        cursor += STRING_SIZE;
    }
    for (index = 0; index < MICROS_SPAWN_ENVIRONMENT_CAPACITY; ++index) {
        size_t reference =
            MICROS_SPAWN_ENVIRONMENT_REFERENCES_OFFSET + index * MICROS_SPAWN_STRING_REFERENCE_SIZE;

        vfs_spawn_test_write_u16_le(&context->request_bytes[reference], (uint16_t)cursor);
        vfs_spawn_test_write_u16_le(&context->request_bytes[reference + 2], STRING_SIZE);
        context->request_bytes[cursor] = 'A';
        context->request_bytes[cursor + 1] = '=';
        memset(&context->request_bytes[cursor + 2], 'v', STRING_SIZE - 3);
        context->request_bytes[cursor + STRING_SIZE - 1] = 0;
        cursor += STRING_SIZE;
    }
    vfs_spawn_test_write_u32_le(&context->request_bytes[8], (uint32_t)cursor);
    return cursor;
}

struct micros_vfs_control_request
vfs_spawn_test_prepare_request(struct vfs_spawn_test_context *context, size_t request_size,
                               uint64_t spawn_token) {
    struct micros_vfs_control_request request = {
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .command = MICROS_VFS_CONTROL_PREPARE_IMAGE,
        .source = context->pm_endpoint,
        .reply_token = UINT64_C(0x71),
        .lifecycle_token = spawn_token,
        .semantic_pid = UINT64_C(0x0000000100000007),
        .parent_endpoint = context->parent_endpoint,
        .request_size = (uint32_t)request_size,
    };

    request.request_grant = vfs_test_fixture_add_application_grant(
        &context->fixture, context->pm_endpoint, MICROS_GRANT_PERMISSION_READ,
        context->request_bytes, request_size);
    return request;
}

struct micros_vfs_control_request
vfs_spawn_test_release_request(const struct vfs_spawn_test_context *context, uint64_t spawn_token,
                               uint64_t image_token) {
    return (struct micros_vfs_control_request){
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .command = MICROS_VFS_CONTROL_RELEASE_IMAGE,
        .source = context->pm_endpoint,
        .reply_token = UINT64_C(0x72),
        .lifecycle_token = spawn_token,
        .image_token = image_token,
    };
}

struct micros_vfs_control_request
vfs_spawn_test_prepare_descriptors_request(const struct vfs_spawn_test_context *context,
                                           uint64_t spawn_token, uint64_t image_token) {
    return (struct micros_vfs_control_request){
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .command = MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS,
        .source = context->pm_endpoint,
        .reply_token = UINT64_C(0x73),
        .lifecycle_token = spawn_token,
        .image_token = image_token,
        .semantic_pid = UINT64_C(0x0000000100000007),
    };
}

struct micros_vfs_control_request
vfs_spawn_test_abort_descriptors_request(const struct vfs_spawn_test_context *context,
                                         uint64_t spawn_token, uint64_t image_token,
                                         uint64_t descriptor_token) {
    return (struct micros_vfs_control_request){
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .command = MICROS_VFS_CONTROL_ABORT_DESCRIPTORS,
        .source = context->pm_endpoint,
        .reply_token = UINT64_C(0x74),
        .lifecycle_token = spawn_token,
        .image_token = image_token,
        .descriptor_token = descriptor_token,
    };
}

struct micros_vfs_control_request
vfs_spawn_test_hidden_request(const struct vfs_spawn_test_context *context,
                              enum micros_vfs_control_command command, uint64_t spawn_token,
                              uint64_t image_token, uint64_t descriptor_token,
                              micros_endpoint_t child_endpoint) {
    return (struct micros_vfs_control_request){
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .command = command,
        .source = context->pm_endpoint,
        .reply_token = UINT64_C(0x75),
        .lifecycle_token = spawn_token,
        .image_token = image_token,
        .descriptor_token = descriptor_token,
        .child_endpoint = child_endpoint,
    };
}

void vfs_spawn_test_fill_executable(struct vfs_spawn_test_context *context, size_t size) {
    size_t index;

    for (index = 0; index < size; ++index) {
        context->executable_bytes[index] = (uint8_t)(index * 37 + 11);
    }
}

size_t vfs_spawn_test_active_process_count(const struct vfs_spawn_test_context *context) {
    size_t count = 0;
    size_t index;

    for (index = 0; index < MICROS_VFS_PROCESS_CAPACITY; ++index) {
        if (context->state.processes[index].state == MICROS_VFS_PROCESS_ACTIVE) {
            ++count;
        }
    }
    return count;
}

size_t vfs_spawn_test_active_console_open_file_count(const struct vfs_spawn_test_context *context,
                                                     uint32_t access) {
    size_t count = 0;
    size_t index;

    for (index = 0; index < MICROS_VFS_OPEN_FILE_CAPACITY; ++index) {
        if (context->state.open_files[index].state == MICROS_VFS_OPEN_FILE_ACTIVE &&
            context->state.open_files[index].kind == MICROS_VFS_OBJECT_CONSOLE &&
            context->state.open_files[index].access == access) {
            ++count;
        }
    }
    return count;
}
