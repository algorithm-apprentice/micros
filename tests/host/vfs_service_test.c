#include "micros/spawn.h"
#include "micros/vfs.h"
#include "servers/vfs/vfs_service_core.h"
#include "tests/host/vfs_test_fixture.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define EXPECT_TRUE(expression) \
    do { \
        if (!(expression)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %s\n", \
                __FILE__, \
                __LINE__, \
                #expression \
            ); \
            return false; \
        } \
    } while (false)

static const micros_endpoint_t vfs_endpoint =
    UINT32_C(0x00001005);
static const micros_endpoint_t vm_endpoint =
    UINT32_C(0x00001001);
static const micros_endpoint_t ramfs_endpoint =
    UINT32_C(0x00001004);
static const micros_endpoint_t tty_endpoint =
    UINT32_C(0x00001003);
static const micros_endpoint_t pm_endpoint =
    UINT32_C(0x00001002);
static const micros_endpoint_t application_endpoint =
    UINT32_C(0x00002006);

static struct micros_vfs_state state;
static struct vfs_test_fixture fixture;

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

static uint64_t read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static size_t build_control_spawn_request(
    uint8_t *bytes,
    const char *path
)
{
    static const char argument[] = "child";
    size_t path_size = strlen(path) + 1;
    size_t argument_size = sizeof(argument);
    size_t cursor = MICROS_SPAWN_REQUEST_HEADER_SIZE;

    memset(bytes, 0, MICROS_SPAWN_REQUEST_SIZE_MAX);
    write_u32_le(&bytes[0], MICROS_SPAWN_REQUEST_VERSION);
    write_u32_le(&bytes[4], MICROS_SPAWN_REQUEST_HEADER_SIZE);
    bytes[MICROS_SPAWN_ARGUMENT_COUNT_OFFSET] = 1;
    bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET] =
        (uint8_t)cursor;
    bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET + 1] =
        (uint8_t)(cursor >> 8);
    bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET + 2] =
        (uint8_t)path_size;
    bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET + 3] =
        (uint8_t)(path_size >> 8);
    memcpy(&bytes[cursor], path, path_size);
    cursor += path_size;
    bytes[MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET] =
        (uint8_t)cursor;
    bytes[MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET + 1] =
        (uint8_t)(cursor >> 8);
    bytes[MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET + 2] =
        (uint8_t)argument_size;
    bytes[MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET + 3] =
        (uint8_t)(argument_size >> 8);
    memcpy(&bytes[cursor], argument, argument_size);
    cursor += argument_size;
    write_u32_le(&bytes[8], (uint32_t)cursor);
    return cursor;
}

static int32_t read_i32_le(const uint8_t *bytes)
{
    return (int32_t)read_u32_le(bytes);
}

static struct micros_ipc_message request_message(uint32_t type)
{
    struct micros_ipc_message message = {
        .source = application_endpoint,
        .type = type,
        .reply_token = UINT64_C(0x55),
    };

    write_u32_le(
        &message.payload[0],
        MICROS_VFS_PROTOCOL_VERSION
    );
    return message;
}

static struct micros_ipc_message pm_control_message(
    enum micros_vfs_control_command command
)
{
    struct micros_ipc_message message = {
        .source = pm_endpoint,
        .type = MICROS_VFS_MESSAGE_PM_CONTROL,
        .reply_token = UINT64_C(0x66),
    };

    write_u32_le(
        &message.payload[0],
        MICROS_VFS_PROTOCOL_VERSION
    );
    write_u32_le(&message.payload[4], (uint32_t)command);
    return message;
}

static struct micros_bootstrap_service_config production_config(void)
{
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
        config.services[index].endpoint =
            UINT32_C(0x00001000) + (uint32_t)index;
    }
    return config;
}

static struct micros_bootstrap_service_config test_config(void)
{
    struct micros_bootstrap_service_config config =
        production_config();

    config.service_count = MICROS_BOOTSTRAP_SERVICE_CAPACITY;
    config.services[6].service_id = 7;
    config.services[6].endpoint = application_endpoint;
    return config;
}

static bool initialize_service_fixture(void)
{
    enum micros_vfs_trusted_result result;

    vfs_test_fixture_initialize(
        &fixture,
        vfs_endpoint,
        vm_endpoint,
        pm_endpoint,
        ramfs_endpoint,
        tty_endpoint
    );
    return (
        micros_vfs_state_initialize(
            &state,
            vfs_endpoint,
            vm_endpoint,
            pm_endpoint,
            ramfs_endpoint,
            tty_endpoint
        ) == MICROS_VFS_CORE_OK
        && micros_vfs_mount(&state, &fixture.io)
            == MICROS_VFS_CORE_OK
        && micros_vfs_attach_console(
            &state,
            application_endpoint,
            0,
            &result
        ) == MICROS_VFS_CORE_OK
        && result == MICROS_VFS_TRUSTED_OK
    );
}

static bool public_constants_are_exact(void)
{
    EXPECT_TRUE(
        MICROS_VFS_PROTOCOL_VERSION == 1
        && MICROS_VFS_SERVICE_ID == 6
        && MICROS_VFS_PROCESS_SLOT == 5
        && MICROS_VFS_RESIDENT_PAGE_LIMIT == 80
        && MICROS_VFS_TRANSFER_MAX == 4096
        && MICROS_VFS_PATH_MAX == 4096
        && MICROS_VFS_NAME_MAX == 60
        && MICROS_VFS_DIRECTORY_RECORD_SIZE == 80
        && MICROS_VFS_MESSAGE_OPEN == UINT32_C(0x00040001)
        && MICROS_VFS_MESSAGE_CLOSE == UINT32_C(0x00040002)
        && MICROS_VFS_MESSAGE_READ == UINT32_C(0x00040003)
        && MICROS_VFS_MESSAGE_WRITE == UINT32_C(0x00040004)
        && MICROS_VFS_MESSAGE_GETDENTS == UINT32_C(0x00040005)
        && MICROS_VFS_MESSAGE_MKDIR == UINT32_C(0x00040006)
        && MICROS_VFS_MESSAGE_CHDIR == UINT32_C(0x00040007)
        && MICROS_VFS_MESSAGE_RESULT == UINT32_C(0x00040008)
        && MICROS_VFS_MESSAGE_PM_CONTROL
            == UINT32_C(0x00040009)
        && MICROS_VFS_MESSAGE_PM_CONTROL_RESULT
            == UINT32_C(0x0004000a)
        && MICROS_VFS_RESULT_RANGE == -15
        && MICROS_VFS_RESULT_OK == 0
        && MICROS_VFS_CONTROL_PREPARE_IMAGE == 1
        && MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS == 2
        && MICROS_VFS_CONTROL_COMMIT_HIDDEN == 3
        && MICROS_VFS_CONTROL_REVERT_HIDDEN == 4
        && MICROS_VFS_CONTROL_ABORT_DESCRIPTORS == 5
        && MICROS_VFS_CONTROL_RELEASE_IMAGE == 6
        && MICROS_VFS_CONTROL_EXIT_PROCESS == 7
        && MICROS_VFS_CONTROL_RESULT_MALFORMED == -1
        && MICROS_VFS_CONTROL_RESULT_CALLER == -2
        && MICROS_VFS_CONTROL_RESULT_STATE == -3
        && MICROS_VFS_CONTROL_RESULT_NO_SPACE == -4
        && MICROS_VFS_CONTROL_RESULT_NOT_FOUND == -5
        && MICROS_VFS_CONTROL_RESULT_ACCESS == -6
        && MICROS_VFS_CONTROL_RESULT_TOO_BIG == -7
        && MICROS_VFS_CONTROL_RESULT_IO == -8
        && MICROS_VFS_CONTROL_RESULT_DESCRIPTOR == -9
        && MICROS_VFS_CONTROL_RESULT_EXECUTABLE == -10
        && MICROS_VFS_CONTROL_RESULT_OK == 0
    );
    return true;
}

static bool pm_control_requests_are_exact(void)
{
    struct micros_ipc_message message = pm_control_message(
        MICROS_VFS_CONTROL_PREPARE_IMAGE
    );
    struct micros_vfs_control_request decoded;
    struct micros_vfs_control_request preserved;

    write_u64_le(&message.payload[8], UINT64_C(0x101));
    write_u64_le(
        &message.payload[16],
        UINT64_C(0x0000000100000007)
    );
    write_u32_le(&message.payload[24], application_endpoint);
    write_u32_le(&message.payload[32], 9);
    write_u32_le(
        &message.payload[36],
        MICROS_SPAWN_REQUEST_SIZE_MIN
    );
    EXPECT_TRUE(
        micros_vfs_service_decode_pm_control(
            pm_endpoint,
            &message,
            &decoded
        ) == MICROS_VFS_CONTROL_PROTOCOL_OK
        && decoded.version == MICROS_VFS_PROTOCOL_VERSION
        && decoded.command == MICROS_VFS_CONTROL_PREPARE_IMAGE
        && decoded.source == pm_endpoint
        && decoded.reply_token == UINT64_C(0x66)
        && decoded.lifecycle_token == UINT64_C(0x101)
        && decoded.image_token == 0
        && decoded.descriptor_token == 0
        && decoded.semantic_pid
            == UINT64_C(0x0000000100000007)
        && decoded.parent_endpoint == application_endpoint
        && decoded.request_grant == 9
        && decoded.request_size == MICROS_SPAWN_REQUEST_SIZE_MIN
        && !decoded.trusted_init
        && decoded.child_endpoint == 0
    );

    message = pm_control_message(
        MICROS_VFS_CONTROL_PREPARE_IMAGE
    );
    write_u64_le(&message.payload[8], UINT64_C(0x102));
    write_u64_le(
        &message.payload[16],
        UINT64_C(0x0000000200000001)
    );
    write_u32_le(&message.payload[32], 10);
    write_u32_le(
        &message.payload[36],
        MICROS_SPAWN_REQUEST_SIZE_MAX
    );
    write_u64_le(&message.payload[40], 1);
    EXPECT_TRUE(
        micros_vfs_service_decode_pm_control(
            pm_endpoint,
            &message,
            &decoded
        ) == MICROS_VFS_CONTROL_PROTOCOL_OK
        && decoded.lifecycle_token == UINT64_C(0x102)
        && decoded.semantic_pid
            == UINT64_C(0x0000000200000001)
        && decoded.parent_endpoint == 0
        && decoded.request_grant == 10
        && decoded.request_size == MICROS_SPAWN_REQUEST_SIZE_MAX
        && decoded.trusted_init
    );

    message = pm_control_message(
        MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS
    );
    write_u64_le(&message.payload[8], UINT64_C(0x103));
    write_u64_le(&message.payload[16], UINT64_C(0x201));
    write_u64_le(
        &message.payload[24],
        UINT64_C(0x0000000300000008)
    );
    EXPECT_TRUE(
        micros_vfs_service_decode_pm_control(
            pm_endpoint,
            &message,
            &decoded
        ) == MICROS_VFS_CONTROL_PROTOCOL_OK
        && decoded.command
            == MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS
        && decoded.lifecycle_token == UINT64_C(0x103)
        && decoded.image_token == UINT64_C(0x201)
        && decoded.semantic_pid
            == UINT64_C(0x0000000300000008)
    );

    message = pm_control_message(
        MICROS_VFS_CONTROL_COMMIT_HIDDEN
    );
    write_u64_le(&message.payload[8], UINT64_C(0x104));
    write_u64_le(&message.payload[16], UINT64_C(0x202));
    write_u64_le(&message.payload[24], UINT64_C(0x301));
    write_u32_le(&message.payload[32], application_endpoint);
    EXPECT_TRUE(
        micros_vfs_service_decode_pm_control(
            pm_endpoint,
            &message,
            &decoded
        ) == MICROS_VFS_CONTROL_PROTOCOL_OK
        && decoded.command == MICROS_VFS_CONTROL_COMMIT_HIDDEN
        && decoded.lifecycle_token == UINT64_C(0x104)
        && decoded.image_token == UINT64_C(0x202)
        && decoded.descriptor_token == UINT64_C(0x301)
        && decoded.child_endpoint == application_endpoint
    );
    write_u32_le(
        &message.payload[4],
        MICROS_VFS_CONTROL_REVERT_HIDDEN
    );
    EXPECT_TRUE(
        micros_vfs_service_decode_pm_control(
            pm_endpoint,
            &message,
            &decoded
        ) == MICROS_VFS_CONTROL_PROTOCOL_OK
        && decoded.command == MICROS_VFS_CONTROL_REVERT_HIDDEN
        && decoded.child_endpoint == application_endpoint
    );

    message = pm_control_message(
        MICROS_VFS_CONTROL_ABORT_DESCRIPTORS
    );
    write_u64_le(&message.payload[8], UINT64_C(0x105));
    write_u64_le(&message.payload[16], UINT64_C(0x203));
    write_u64_le(&message.payload[24], UINT64_C(0x302));
    EXPECT_TRUE(
        micros_vfs_service_decode_pm_control(
            pm_endpoint,
            &message,
            &decoded
        ) == MICROS_VFS_CONTROL_PROTOCOL_OK
        && decoded.command
            == MICROS_VFS_CONTROL_ABORT_DESCRIPTORS
        && decoded.lifecycle_token == UINT64_C(0x105)
        && decoded.image_token == UINT64_C(0x203)
        && decoded.descriptor_token == UINT64_C(0x302)
    );

    message = pm_control_message(
        MICROS_VFS_CONTROL_RELEASE_IMAGE
    );
    write_u64_le(&message.payload[8], UINT64_C(0x106));
    write_u64_le(&message.payload[16], UINT64_C(0x204));
    EXPECT_TRUE(
        micros_vfs_service_decode_pm_control(
            pm_endpoint,
            &message,
            &decoded
        ) == MICROS_VFS_CONTROL_PROTOCOL_OK
        && decoded.command == MICROS_VFS_CONTROL_RELEASE_IMAGE
        && decoded.lifecycle_token == UINT64_C(0x106)
        && decoded.image_token == UINT64_C(0x204)
    );

    message = pm_control_message(
        MICROS_VFS_CONTROL_EXIT_PROCESS
    );
    write_u64_le(&message.payload[8], UINT64_C(0x401));
    write_u32_le(&message.payload[16], application_endpoint);
    write_u64_le(
        &message.payload[24],
        UINT64_C(0x0000000400000009)
    );
    EXPECT_TRUE(
        micros_vfs_service_decode_pm_control(
            pm_endpoint,
            &message,
            &decoded
        ) == MICROS_VFS_CONTROL_PROTOCOL_OK
        && decoded.command == MICROS_VFS_CONTROL_EXIT_PROCESS
        && decoded.lifecycle_token == UINT64_C(0x401)
        && decoded.child_endpoint == application_endpoint
        && decoded.semantic_pid
            == UINT64_C(0x0000000400000009)
    );

    memset(&decoded, 0xa5, sizeof(decoded));
    memcpy(&preserved, &decoded, sizeof(preserved));
    message.source = application_endpoint;
    EXPECT_TRUE(
        micros_vfs_service_decode_pm_control(
            pm_endpoint,
            &message,
            &decoded
        ) == MICROS_VFS_CONTROL_PROTOCOL_CALLER
        && memcmp(&decoded, &preserved, sizeof(decoded)) == 0
    );
    message.source = pm_endpoint;
    message.type = MICROS_VFS_MESSAGE_OPEN;
    EXPECT_TRUE(
        micros_vfs_service_decode_pm_control(
            pm_endpoint,
            &message,
            &decoded
        ) == MICROS_VFS_CONTROL_PROTOCOL_BAD_TYPE
        && memcmp(&decoded, &preserved, sizeof(decoded)) == 0
    );
    message.type = MICROS_VFS_MESSAGE_PM_CONTROL;
    write_u32_le(&message.payload[0], 2);
    EXPECT_TRUE(
        micros_vfs_service_decode_pm_control(
            pm_endpoint,
            &message,
            &decoded
        ) == MICROS_VFS_CONTROL_PROTOCOL_BAD_VERSION
        && memcmp(&decoded, &preserved, sizeof(decoded)) == 0
    );
    write_u32_le(
        &message.payload[0],
        MICROS_VFS_PROTOCOL_VERSION
    );
    write_u32_le(&message.payload[4], 8);
    EXPECT_TRUE(
        micros_vfs_service_decode_pm_control(
            pm_endpoint,
            &message,
            &decoded
        ) == MICROS_VFS_CONTROL_PROTOCOL_MALFORMED
        && memcmp(&decoded, &preserved, sizeof(decoded)) == 0
    );
    write_u32_le(
        &message.payload[4],
        MICROS_VFS_CONTROL_EXIT_PROCESS
    );
    write_u32_le(&message.payload[20], 1);
    EXPECT_TRUE(
        micros_vfs_service_decode_pm_control(
            pm_endpoint,
            &message,
            &decoded
        ) == MICROS_VFS_CONTROL_PROTOCOL_MALFORMED
        && memcmp(&decoded, &preserved, sizeof(decoded)) == 0
    );
    write_u32_le(&message.payload[20], 0);
    message.reply_token = 0;
    EXPECT_TRUE(
        micros_vfs_service_decode_pm_control(
            pm_endpoint,
            &message,
            &decoded
        ) == MICROS_VFS_CONTROL_PROTOCOL_INVARIANT
        && memcmp(&decoded, &preserved, sizeof(decoded)) == 0
    );
    return true;
}

static bool pm_control_reserved_words_are_rejected(void)
{
    static const struct {
        enum micros_vfs_control_command command;
        size_t reserved_offset;
    } cases[] = {
        {MICROS_VFS_CONTROL_PREPARE_IMAGE, 28},
        {MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS, 32},
        {MICROS_VFS_CONTROL_COMMIT_HIDDEN, 36},
        {MICROS_VFS_CONTROL_REVERT_HIDDEN, 40},
        {MICROS_VFS_CONTROL_ABORT_DESCRIPTORS, 32},
        {MICROS_VFS_CONTROL_RELEASE_IMAGE, 24},
        {MICROS_VFS_CONTROL_EXIT_PROCESS, 20},
    };
    struct micros_vfs_control_request decoded;
    struct micros_vfs_control_request preserved;
    size_t index;

    memset(&decoded, 0x5a, sizeof(decoded));
    memcpy(&preserved, &decoded, sizeof(preserved));
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        struct micros_ipc_message message =
            pm_control_message(cases[index].command);

        message.payload[cases[index].reserved_offset] = 1;
        EXPECT_TRUE(
            micros_vfs_service_decode_pm_control(
                pm_endpoint,
                &message,
                &decoded
            ) == MICROS_VFS_CONTROL_PROTOCOL_MALFORMED
            && memcmp(&decoded, &preserved, sizeof(decoded)) == 0
        );
    }
    return true;
}

static bool pm_control_results_are_exact(void)
{
    struct micros_vfs_control_result_action result = {
        .active = true,
        .reply_token = UINT64_C(0x77),
        .command = MICROS_VFS_CONTROL_PREPARE_IMAGE,
        .result = MICROS_VFS_CONTROL_RESULT_OK,
        .image_token = UINT64_C(0x201),
        .executable_grant = 11,
        .executable_size = 4097,
        .request_grant = 12,
        .request_size = MICROS_SPAWN_REQUEST_SIZE_MIN,
    };
    struct micros_ipc_message message;
    struct micros_ipc_message preserved;

    EXPECT_TRUE(
        micros_vfs_service_build_pm_control_result(
            &result,
            &message
        ) == MICROS_VFS_SERVICE_OK
        && message.source == 0
        && message.type == MICROS_VFS_MESSAGE_PM_CONTROL_RESULT
        && message.reply_token == 0
        && read_u32_le(&message.payload[0])
            == MICROS_VFS_PROTOCOL_VERSION
        && read_u32_le(&message.payload[4])
            == MICROS_VFS_CONTROL_PREPARE_IMAGE
        && read_i32_le(&message.payload[8])
            == MICROS_VFS_CONTROL_RESULT_OK
        && read_u32_le(&message.payload[12]) == 0
        && read_u64_le(&message.payload[16])
            == UINT64_C(0x201)
        && read_u32_le(&message.payload[24]) == 11
        && read_u32_le(&message.payload[28]) == 4097
        && read_u32_le(&message.payload[32]) == 12
        && read_u32_le(&message.payload[36])
            == MICROS_SPAWN_REQUEST_SIZE_MIN
        && read_u64_le(&message.payload[40]) == 0
    );

    result = (struct micros_vfs_control_result_action){
        .active = true,
        .reply_token = UINT64_C(0x78),
        .command = MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS,
        .result = MICROS_VFS_CONTROL_RESULT_OK,
        .descriptor_token = UINT64_C(0x301),
    };
    EXPECT_TRUE(
        micros_vfs_service_build_pm_control_result(
            &result,
            &message
        ) == MICROS_VFS_SERVICE_OK
        && read_u64_le(&message.payload[16])
            == UINT64_C(0x301)
        && read_u64_le(&message.payload[24]) == 0
        && read_u64_le(&message.payload[32]) == 0
        && read_u64_le(&message.payload[40]) == 0
    );

    result = (struct micros_vfs_control_result_action){
        .active = true,
        .reply_token = UINT64_C(0x79),
        .command = MICROS_VFS_CONTROL_EXIT_PROCESS,
        .result = MICROS_VFS_CONTROL_RESULT_OK,
    };
    EXPECT_TRUE(
        micros_vfs_service_build_pm_control_result(
            &result,
            &message
        ) == MICROS_VFS_SERVICE_OK
        && read_u64_le(&message.payload[16]) == 0
        && read_u64_le(&message.payload[24]) == 0
        && read_u64_le(&message.payload[32]) == 0
        && read_u64_le(&message.payload[40]) == 0
    );

    result.result = MICROS_VFS_CONTROL_RESULT_NOT_FOUND;
    EXPECT_TRUE(
        micros_vfs_service_build_pm_control_result(
            &result,
            &message
        ) == MICROS_VFS_SERVICE_OK
        && read_i32_le(&message.payload[8])
            == MICROS_VFS_CONTROL_RESULT_NOT_FOUND
        && read_u32_le(&message.payload[12]) == 0
        && read_u64_le(&message.payload[16]) == 0
        && read_u64_le(&message.payload[24]) == 0
        && read_u64_le(&message.payload[32]) == 0
        && read_u64_le(&message.payload[40]) == 0
    );

    memset(&message, 0xa5, sizeof(message));
    memcpy(&preserved, &message, sizeof(preserved));
    result.result = (enum micros_vfs_control_result)-11;
    EXPECT_TRUE(
        micros_vfs_service_build_pm_control_result(
            &result,
            &message
        ) == MICROS_VFS_SERVICE_ERROR_ARGUMENT
        && memcmp(&message, &preserved, sizeof(message)) == 0
    );
    result.result = MICROS_VFS_CONTROL_RESULT_OK;
    result.command = (enum micros_vfs_control_command)8;
    EXPECT_TRUE(
        micros_vfs_service_build_pm_control_result(
            &result,
            &message
        ) == MICROS_VFS_SERVICE_ERROR_ARGUMENT
        && memcmp(&message, &preserved, sizeof(message)) == 0
    );
    result.command = MICROS_VFS_CONTROL_RELEASE_IMAGE;
    result.image_token = 1;
    EXPECT_TRUE(
        micros_vfs_service_build_pm_control_result(
            &result,
            &message
        ) == MICROS_VFS_SERVICE_ERROR_INVARIANT
        && memcmp(&message, &preserved, sizeof(message)) == 0
    );
    result.image_token = 0;
    result.result = MICROS_VFS_CONTROL_RESULT_IO;
    result.request_size = 1;
    EXPECT_TRUE(
        micros_vfs_service_build_pm_control_result(
            &result,
            &message
        ) == MICROS_VFS_SERVICE_ERROR_INVARIANT
        && memcmp(&message, &preserved, sizeof(message)) == 0
    );
    result.result = MICROS_VFS_CONTROL_RESULT_OK;
    result.request_size = 0;
    result.reply_token = 0;
    EXPECT_TRUE(
        micros_vfs_service_build_pm_control_result(
            &result,
            &message
        ) == MICROS_VFS_SERVICE_ERROR_ARGUMENT
        && memcmp(&message, &preserved, sizeof(message)) == 0
    );
    return true;
}

static bool application_protocol_is_exact(void)
{
    struct micros_ipc_message message =
        request_message(MICROS_VFS_MESSAGE_OPEN);
    struct micros_vfs_request decoded;
    struct micros_vfs_result_action result = {
        .active = true,
        .reply_token = UINT64_C(0x99),
        .request_type = MICROS_VFS_MESSAGE_OPEN,
        .result = MICROS_VFS_RESULT_OK,
        .descriptor = 3,
        .mode =
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
    };
    struct micros_ipc_message reply;

    write_u32_le(&message.payload[8], 7);
    write_u32_le(&message.payload[12], 10);
    write_u64_le(&message.payload[16], 4);
    write_u32_le(
        &message.payload[24],
        MICROS_VFS_OPEN_READ | MICROS_VFS_OPEN_CREATE
    );
    write_u32_le(&message.payload[28], UINT32_C(0644));
    EXPECT_TRUE(
        micros_vfs_service_decode_request(
            &message,
            &decoded
        ) == MICROS_VFS_PROTOCOL_OK
        && decoded.type == MICROS_VFS_MESSAGE_OPEN
        && decoded.version == MICROS_VFS_PROTOCOL_VERSION
        && decoded.flags == 0
        && decoded.source == application_endpoint
        && decoded.reply_token == UINT64_C(0x55)
        && decoded.grant == 7
        && decoded.path_length == 10
        && decoded.grant_offset == 4
        && decoded.open_flags
            == (
                MICROS_VFS_OPEN_READ
                | MICROS_VFS_OPEN_CREATE
            )
        && decoded.mode == UINT32_C(0644)
    );
    message.payload[47] = 1;
    EXPECT_TRUE(
        micros_vfs_service_decode_request(
            &message,
            &decoded
        ) == MICROS_VFS_PROTOCOL_MALFORMED
    );
    message.payload[47] = 0;
    message.reply_token = 0;
    EXPECT_TRUE(
        micros_vfs_service_decode_request(
            &message,
            &decoded
        ) == MICROS_VFS_PROTOCOL_INVARIANT
    );
    message = request_message(UINT32_C(0x0004ffff));
    EXPECT_TRUE(
        micros_vfs_service_decode_request(
            &message,
            &decoded
        ) == MICROS_VFS_PROTOCOL_BAD_TYPE
    );
    message = request_message(MICROS_VFS_MESSAGE_CLOSE);
    write_u32_le(&message.payload[0], 2);
    EXPECT_TRUE(
        micros_vfs_service_decode_request(
            &message,
            &decoded
        ) == MICROS_VFS_PROTOCOL_BAD_VERSION
    );
    EXPECT_TRUE(
        micros_vfs_service_build_result(&result, &reply)
            == MICROS_VFS_SERVICE_OK
        && reply.source == 0
        && reply.type == MICROS_VFS_MESSAGE_RESULT
        && reply.reply_token == 0
        && read_u32_le(&reply.payload[0])
            == MICROS_VFS_PROTOCOL_VERSION
        && read_u32_le(&reply.payload[4])
            == MICROS_VFS_MESSAGE_OPEN
        && read_i32_le(&reply.payload[8])
            == MICROS_VFS_RESULT_OK
        && read_u32_le(&reply.payload[12]) == 0
        && read_u32_le(&reply.payload[16]) == 3
        && read_u32_le(&reply.payload[20])
            == (
                MICROS_RAMFS_MODE_REGULAR
                | UINT32_C(0644)
            )
        && read_u64_le(&reply.payload[24]) == 0
        && read_u64_le(&reply.payload[32]) == 0
        && read_u64_le(&reply.payload[40]) == 0
    );
    result.result = MICROS_VFS_RESULT_DESCRIPTOR;
    result.descriptor = 0;
    result.mode = 0;
    EXPECT_TRUE(
        micros_vfs_service_build_result(&result, &reply)
            == MICROS_VFS_SERVICE_OK
        && read_i32_le(&reply.payload[8])
            == MICROS_VFS_RESULT_DESCRIPTOR
        && read_u64_le(&reply.payload[16]) == 0
        && read_u64_le(&reply.payload[24]) == 0
        && read_u64_le(&reply.payload[32]) == 0
        && read_u64_le(&reply.payload[40]) == 0
    );
    result.descriptor = 3;
    EXPECT_TRUE(
        micros_vfs_service_build_result(&result, &reply)
            == MICROS_VFS_SERVICE_ERROR_INVARIANT
    );
    return true;
}

static bool backend_protocol_is_exact(void)
{
    struct micros_vfs_ramfs_request ramfs_request = {
        .operation = MICROS_VFS_RAMFS_LOOKUP,
        .start = UINT64_C(0x100000000),
        .root = UINT64_C(0x100000000),
        .grant = 9,
        .length = 10,
        .grant_offset = 7,
    };
    struct micros_vfs_ramfs_response ramfs_response;
    struct micros_vfs_tty_request tty_request = {
        .operation = MICROS_VFS_TTY_SUBMIT_WRITE,
        .grant = 11,
        .request_id = 3,
        .count = 5,
    };
    struct micros_vfs_tty_response tty_response;
    struct micros_ipc_message message;

    memset(&ramfs_request, 0, sizeof(ramfs_request));
    ramfs_request.operation = MICROS_VFS_RAMFS_MOUNT;
    EXPECT_TRUE(
        micros_vfs_service_build_ramfs_call(
            &ramfs_request,
            &message
        ) == MICROS_VFS_SERVICE_OK
        && message.type == MICROS_RAMFS_MESSAGE_MOUNT
        && read_u32_le(&message.payload[0])
            == MICROS_RAMFS_PROTOCOL_VERSION
        && read_u64_le(&message.payload[4]) == 0
        && read_u64_le(&message.payload[12]) == 0
        && read_u64_le(&message.payload[20]) == 0
        && read_u64_le(&message.payload[28]) == 0
        && read_u64_le(&message.payload[36]) == 0
        && read_u32_le(&message.payload[44]) == 0
    );
    ramfs_request.node = 1;
    EXPECT_TRUE(
        micros_vfs_service_build_ramfs_call(
            &ramfs_request,
            &message
        ) == MICROS_VFS_SERVICE_ERROR_INVARIANT
    );
    ramfs_request = (struct micros_vfs_ramfs_request){
        .operation = MICROS_VFS_RAMFS_LOOKUP,
        .start = UINT64_C(0x100000000),
        .root = UINT64_C(0x100000000),
        .grant = 9,
        .length = 10,
        .grant_offset = 7,
    };
    EXPECT_TRUE(
        micros_vfs_service_build_ramfs_call(
            &ramfs_request,
            &message
        ) == MICROS_VFS_SERVICE_OK
        && message.type == MICROS_RAMFS_MESSAGE_LOOKUP
        && read_u32_le(&message.payload[0])
            == MICROS_RAMFS_PROTOCOL_VERSION
        && read_u64_le(&message.payload[8])
            == ramfs_request.start
        && read_u64_le(&message.payload[16])
            == ramfs_request.root
        && read_u32_le(&message.payload[24]) == 9
        && read_u32_le(&message.payload[28]) == 10
        && read_u64_le(&message.payload[32]) == 7
        && read_u64_le(&message.payload[40]) == 0
    );
    ramfs_request = (struct micros_vfs_ramfs_request){
        .operation = MICROS_VFS_RAMFS_CREATE,
        .node = UINT64_C(0x100000001),
        .mode = MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        .grant = 10,
        .grant_offset = 11,
        .length = 5,
    };
    EXPECT_TRUE(
        micros_vfs_service_build_ramfs_call(
            &ramfs_request,
            &message
        ) == MICROS_VFS_SERVICE_OK
        && message.type == MICROS_RAMFS_MESSAGE_CREATE
        && read_u64_le(&message.payload[8])
            == ramfs_request.node
        && read_u32_le(&message.payload[16])
            == ramfs_request.mode
        && read_u32_le(&message.payload[20]) == 10
        && read_u64_le(&message.payload[24]) == 11
        && read_u32_le(&message.payload[32]) == 5
        && read_u32_le(&message.payload[36]) == 0
        && read_u64_le(&message.payload[40]) == 0
    );
    ramfs_request.operation = MICROS_VFS_RAMFS_MKDIR;
    ramfs_request.mode =
        MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755);
    EXPECT_TRUE(
        micros_vfs_service_build_ramfs_call(
            &ramfs_request,
            &message
        ) == MICROS_VFS_SERVICE_OK
        && message.type == MICROS_RAMFS_MESSAGE_MKDIR
    );
    ramfs_request = (struct micros_vfs_ramfs_request){
        .operation = MICROS_VFS_RAMFS_READ,
        .node = UINT64_C(0x100000002),
        .file_offset = 13,
        .grant = 12,
        .count = 14,
        .grant_offset = 15,
    };
    EXPECT_TRUE(
        micros_vfs_service_build_ramfs_call(
            &ramfs_request,
            &message
        ) == MICROS_VFS_SERVICE_OK
        && message.type == MICROS_RAMFS_MESSAGE_READ
        && read_u64_le(&message.payload[8])
            == ramfs_request.node
        && read_u64_le(&message.payload[16]) == 13
        && read_u32_le(&message.payload[24]) == 12
        && read_u32_le(&message.payload[28]) == 14
        && read_u64_le(&message.payload[32]) == 15
        && read_u64_le(&message.payload[40]) == 0
    );
    ramfs_request.operation = MICROS_VFS_RAMFS_WRITE;
    EXPECT_TRUE(
        micros_vfs_service_build_ramfs_call(
            &ramfs_request,
            &message
        ) == MICROS_VFS_SERVICE_OK
        && message.type == MICROS_RAMFS_MESSAGE_WRITE
    );
    ramfs_request = (struct micros_vfs_ramfs_request){
        .operation = MICROS_VFS_RAMFS_GETDENTS,
        .node = UINT64_C(0x100000003),
        .cursor = 16,
        .grant = 13,
        .count = MICROS_RAMFS_DIRECTORY_RECORD_SIZE,
        .grant_offset = 17,
    };
    EXPECT_TRUE(
        micros_vfs_service_build_ramfs_call(
            &ramfs_request,
            &message
        ) == MICROS_VFS_SERVICE_OK
        && message.type == MICROS_RAMFS_MESSAGE_GETDENTS
        && read_u64_le(&message.payload[8])
            == ramfs_request.node
        && read_u64_le(&message.payload[16]) == 16
        && read_u32_le(&message.payload[24]) == 13
        && read_u32_le(&message.payload[28])
            == MICROS_RAMFS_DIRECTORY_RECORD_SIZE
        && read_u64_le(&message.payload[32]) == 17
        && read_u64_le(&message.payload[40]) == 0
    );
    ramfs_request = (struct micros_vfs_ramfs_request){
        .operation = MICROS_VFS_RAMFS_PUTNODE,
        .node = UINT64_C(0x100000004),
        .count = 18,
    };
    EXPECT_TRUE(
        micros_vfs_service_build_ramfs_call(
            &ramfs_request,
            &message
        ) == MICROS_VFS_SERVICE_OK
        && message.type == MICROS_RAMFS_MESSAGE_PUTNODE
        && read_u64_le(&message.payload[8])
            == ramfs_request.node
        && read_u32_le(&message.payload[16]) == 18
        && read_u32_le(&message.payload[20]) == 0
        && read_u64_le(&message.payload[24]) == 0
        && read_u64_le(&message.payload[32]) == 0
        && read_u64_le(&message.payload[40]) == 0
    );
    ramfs_request.count = 0;
    EXPECT_TRUE(
        micros_vfs_service_build_ramfs_call(
            &ramfs_request,
            &message
        ) == MICROS_VFS_SERVICE_ERROR_INVARIANT
    );
    ramfs_request = (struct micros_vfs_ramfs_request){
        .operation = MICROS_VFS_RAMFS_LOOKUP,
        .start = UINT64_C(0x100000000),
        .root = UINT64_C(0x100000000),
        .grant = 9,
        .length = 10,
        .grant_offset = 7,
    };
    memset(&message, 0, sizeof(message));
    message.source = ramfs_endpoint;
    message.type = MICROS_RAMFS_MESSAGE_RESULT;
    write_u32_le(
        &message.payload[0],
        MICROS_RAMFS_PROTOCOL_VERSION
    );
    write_u32_le(
        &message.payload[4],
        MICROS_RAMFS_MESSAGE_LOOKUP
    );
    write_u32_le(
        &message.payload[8],
        (uint32_t)(int32_t)MICROS_RAMFS_RESULT_OK
    );
    write_u64_le(
        &message.payload[16],
        UINT64_C(0x100000002)
    );
    write_u64_le(&message.payload[24], 19);
    write_u32_le(
        &message.payload[44],
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644)
    );
    EXPECT_TRUE(
        micros_vfs_service_decode_ramfs_result(
            ramfs_endpoint,
            &ramfs_request,
            &message,
            &ramfs_response
        ) == MICROS_VFS_SERVICE_OK
        && ramfs_response.result == MICROS_RAMFS_RESULT_OK
        && ramfs_response.node == UINT64_C(0x100000002)
        && ramfs_response.file_size == 19
        && ramfs_response.position == 0
        && ramfs_response.count == 0
        && ramfs_response.mode
            == (
                MICROS_RAMFS_MODE_REGULAR
                | UINT32_C(0644)
            )
    );
    message.source = tty_endpoint;
    EXPECT_TRUE(
        micros_vfs_service_decode_ramfs_result(
            ramfs_endpoint,
            &ramfs_request,
            &message,
            &ramfs_response
        ) == MICROS_VFS_SERVICE_ERROR_INVARIANT
    );
    message.source = ramfs_endpoint;
    ramfs_request.operation = MICROS_VFS_RAMFS_PUTNODE;
    write_u32_le(
        &message.payload[4],
        MICROS_RAMFS_MESSAGE_PUTNODE
    );
    EXPECT_TRUE(
        micros_vfs_service_decode_ramfs_result(
            ramfs_endpoint,
            &ramfs_request,
            &message,
            &ramfs_response
        ) == MICROS_VFS_SERVICE_ERROR_INVARIANT
    );
    tty_request.count = 0;
    EXPECT_TRUE(
        micros_vfs_service_build_tty_call(
            &tty_request,
            &message
        ) == MICROS_VFS_SERVICE_ERROR_INVARIANT
    );
    tty_request.grant = MICROS_GRANT_NONE;
    tty_request.count = 1;
    EXPECT_TRUE(
        micros_vfs_service_build_tty_call(
            &tty_request,
            &message
        ) == MICROS_VFS_SERVICE_OK
        && read_u32_le(&message.payload[16])
            == MICROS_GRANT_NONE
    );
    tty_request.grant = 11;
    tty_request.count = 5;
    EXPECT_TRUE(
        micros_vfs_service_build_tty_call(
            &tty_request,
            &message
        ) == MICROS_VFS_SERVICE_OK
        && message.type == MICROS_TTY_MESSAGE_SUBMIT_WRITE
        && read_u32_le(&message.payload[0])
            == MICROS_TTY_PROTOCOL_VERSION
        && read_u64_le(&message.payload[8]) == 3
        && read_u32_le(&message.payload[16]) == 11
        && read_u32_le(&message.payload[20]) == 0
        && read_u64_le(&message.payload[24]) == 0
        && read_u64_le(&message.payload[32]) == 5
        && read_u64_le(&message.payload[40]) == 0
    );
    tty_request = (struct micros_vfs_tty_request){
        .operation = MICROS_VFS_TTY_CANCEL,
        .request_id = 3,
    };
    EXPECT_TRUE(
        micros_vfs_service_build_tty_call(
            &tty_request,
            &message
        ) == MICROS_VFS_SERVICE_OK
        && message.type == MICROS_TTY_MESSAGE_CANCEL
        && read_u64_le(&message.payload[8]) == 3
        && read_u64_le(&message.payload[16]) == 0
        && read_u64_le(&message.payload[24]) == 0
        && read_u64_le(&message.payload[32]) == 0
        && read_u64_le(&message.payload[40]) == 0
    );
    tty_request.operation = MICROS_VFS_TTY_COLLECT;
    EXPECT_TRUE(
        micros_vfs_service_build_tty_call(
            &tty_request,
            &message
        ) == MICROS_VFS_SERVICE_OK
        && message.type == MICROS_TTY_MESSAGE_COLLECT
    );
    tty_request.grant = 1;
    EXPECT_TRUE(
        micros_vfs_service_build_tty_call(
            &tty_request,
            &message
        ) == MICROS_VFS_SERVICE_ERROR_INVARIANT
    );
    tty_request = (struct micros_vfs_tty_request){
        .operation = MICROS_VFS_TTY_SUBMIT_WRITE,
        .grant = 11,
        .request_id = 3,
        .count = 5,
    };
    memset(&message, 0, sizeof(message));
    message.source = tty_endpoint;
    message.type = MICROS_TTY_MESSAGE_RESULT;
    write_u32_le(
        &message.payload[0],
        MICROS_TTY_PROTOCOL_VERSION
    );
    write_u32_le(
        &message.payload[4],
        MICROS_TTY_MESSAGE_SUBMIT_WRITE
    );
    write_u32_le(
        &message.payload[8],
        (uint32_t)(int32_t)MICROS_TTY_RESULT_OK
    );
    write_u64_le(&message.payload[16], 3);
    EXPECT_TRUE(
        micros_vfs_service_decode_tty_result(
            tty_endpoint,
            &tty_request,
            &message,
            &tty_response
        ) == MICROS_VFS_SERVICE_OK
        && tty_response.result == MICROS_TTY_RESULT_OK
        && tty_response.request_id == 3
        && tty_response.transferred_count == 0
    );
    message.payload[32] = 1;
    EXPECT_TRUE(
        micros_vfs_service_decode_tty_result(
            tty_endpoint,
            &tty_request,
            &message,
            &tty_response
        ) == MICROS_VFS_SERVICE_ERROR_INVARIANT
    );
    message.payload[32] = 0;
    write_u64_le(&message.payload[24], 1);
    EXPECT_TRUE(
        micros_vfs_service_decode_tty_result(
            tty_endpoint,
            &tty_request,
            &message,
            &tty_response
        ) == MICROS_VFS_SERVICE_ERROR_INVARIANT
    );
    return true;
}

static bool production_configuration_is_exact(void)
{
    struct micros_bootstrap_service_config config =
        production_config();
    struct micros_vfs_service_endpoints endpoints;

    EXPECT_TRUE(
        micros_vfs_service_validate_configuration(
            &config,
            &endpoints
        )
        && endpoints.launcher == UINT32_C(0x00001000)
        && endpoints.vm == UINT32_C(0x00001001)
        && endpoints.pm == UINT32_C(0x00001002)
        && endpoints.tty == UINT32_C(0x00001003)
        && endpoints.ramfs == UINT32_C(0x00001004)
        && endpoints.self == UINT32_C(0x00001005)
    );
    config.service_count = MICROS_BOOTSTRAP_SERVICE_CAPACITY;
    EXPECT_TRUE(
        !micros_vfs_service_validate_configuration(
            &config,
            &endpoints
        )
    );
    config = production_config();
    config.services[6].service_id = 7;
    config.services[6].endpoint = UINT32_C(0x00001006);
    EXPECT_TRUE(
        !micros_vfs_service_validate_configuration(
            &config,
            &endpoints
        )
    );
    config = production_config();
    config.services[4].endpoint = config.services[3].endpoint;
    EXPECT_TRUE(
        !micros_vfs_service_validate_configuration(
            &config,
            &endpoints
        )
    );
    config = production_config();
    config.reserved[0] = 1;
    EXPECT_TRUE(
        !micros_vfs_service_validate_configuration(
            &config,
            &endpoints
        )
    );
    return true;
}

static bool test_configuration_is_exact(void)
{
    struct micros_bootstrap_service_config config = test_config();
    struct micros_vfs_service_endpoints endpoints;
    micros_endpoint_t selected_application;

    EXPECT_TRUE(
        micros_vfs_service_validate_test_configuration(
            &config,
            &endpoints,
            &selected_application
        )
        && endpoints.launcher == UINT32_C(0x00001000)
        && endpoints.vm == UINT32_C(0x00001001)
        && endpoints.pm == UINT32_C(0x00001002)
        && endpoints.tty == UINT32_C(0x00001003)
        && endpoints.ramfs == UINT32_C(0x00001004)
        && endpoints.self == UINT32_C(0x00001005)
        && selected_application == application_endpoint
        && !micros_vfs_service_validate_configuration(
            &config,
            &endpoints
        )
    );
    config = production_config();
    EXPECT_TRUE(
        !micros_vfs_service_validate_test_configuration(
            &config,
            &endpoints,
            &selected_application
        )
    );
    config = test_config();
    config.service_count = MICROS_BOOTSTRAP_SERVICE_CAPACITY + 1;
    EXPECT_TRUE(
        !micros_vfs_service_validate_test_configuration(
            &config,
            &endpoints,
            &selected_application
        )
    );
    config = test_config();
    config.services[6].service_id = MICROS_VFS_SERVICE_ID;
    EXPECT_TRUE(
        !micros_vfs_service_validate_test_configuration(
            &config,
            &endpoints,
            &selected_application
        )
    );
    config = test_config();
    config.services[6].endpoint = config.services[5].endpoint;
    EXPECT_TRUE(
        !micros_vfs_service_validate_test_configuration(
            &config,
            &endpoints,
            &selected_application
        )
    );
    config = test_config();
    config.self_endpoint = application_endpoint;
    EXPECT_TRUE(
        !micros_vfs_service_validate_test_configuration(
            &config,
            &endpoints,
            &selected_application
        )
    );
    return true;
}

static bool service_dispatches_calls_and_notifications(void)
{
    struct micros_ipc_message message =
        request_message(MICROS_VFS_MESSAGE_CLOSE);
    struct micros_vfs_service_reply_action action;
    micros_grant_t grant;
    static const uint8_t input[] = "ok\n";

    EXPECT_TRUE(initialize_service_fixture());
    write_u32_le(
        &message.payload[8],
        MICROS_VFS_DESCRIPTOR_CAPACITY
    );
    EXPECT_TRUE(
        micros_vfs_service_handle_message(
            &state,
            &message,
            &fixture.io,
            &action
        ) == MICROS_VFS_SERVICE_OK
        && action.active
        && action.reply_token == UINT64_C(0x55)
        && read_i32_le(&action.message.payload[8])
            == MICROS_VFS_RESULT_DESCRIPTOR
    );
    grant = vfs_test_fixture_add_application_grant(
        &fixture,
        application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        NULL,
        8
    );
    message = request_message(MICROS_VFS_MESSAGE_READ);
    write_u32_le(&message.payload[8], 0);
    write_u32_le(&message.payload[12], grant);
    write_u32_le(&message.payload[24], 8);
    EXPECT_TRUE(
        grant != MICROS_GRANT_NONE
        && micros_vfs_service_handle_message(
            &state,
            &message,
            &fixture.io,
            &action
        ) == MICROS_VFS_SERVICE_OK
        && !action.active
        && vfs_test_fixture_complete_read(
            &fixture,
            input,
            sizeof(input) - 1
        )
    );
    memset(&message, 0, sizeof(message));
    message.source = tty_endpoint;
    message.type = MICROS_IPC_TYPE_KERNEL_NOTIFICATION;
    write_u64_le(
        &message.payload[0],
        MICROS_TTY_EVENT_COMPLETION
    );
    EXPECT_TRUE(
        micros_vfs_service_handle_message(
            &state,
            &message,
            &fixture.io,
            &action
        ) == MICROS_VFS_SERVICE_OK
        && action.active
        && action.reply_token == UINT64_C(0x55)
        && read_i32_le(&action.message.payload[8])
            == MICROS_VFS_RESULT_OK
        && read_u64_le(&action.message.payload[24])
            == sizeof(input) - 1
        && memcmp(
            vfs_test_fixture_application_bytes(
                &fixture,
                grant
            ),
            input,
            sizeof(input) - 1
        ) == 0
    );
    message.source = ramfs_endpoint;
    EXPECT_TRUE(
        micros_vfs_service_handle_message(
            &state,
            &message,
            &fixture.io,
            &action
        ) == MICROS_VFS_SERVICE_ERROR_INVARIANT
    );
    return true;
}

static bool service_dispatches_test_drain_only_for_application(void)
{
    struct micros_ipc_message message =
        request_message(MICROS_VFS_TEST_MESSAGE_DRAIN);
    struct micros_vfs_service_reply_action action;

    EXPECT_TRUE(initialize_service_fixture());
    EXPECT_TRUE(
        micros_vfs_service_decode_request(
            &message,
            &(struct micros_vfs_request){0}
        ) == MICROS_VFS_PROTOCOL_BAD_TYPE
    );
    EXPECT_TRUE(
        micros_vfs_service_handle_message(
            &state,
            &message,
            &fixture.io,
            &action
        ) == MICROS_VFS_SERVICE_OK
        && action.active
        && action.reply_token == UINT64_C(0x55)
        && read_u32_le(&action.message.payload[4])
            == MICROS_VFS_TEST_MESSAGE_DRAIN
        && read_i32_le(&action.message.payload[8])
            == MICROS_VFS_RESULT_OK
    );

    EXPECT_TRUE(initialize_service_fixture());
    message = request_message(MICROS_VFS_TEST_MESSAGE_DRAIN);
    message.source = UINT32_C(0x00001002);
    EXPECT_TRUE(
        micros_vfs_service_handle_message(
            &state,
            &message,
            &fixture.io,
            &action
        ) == MICROS_VFS_SERVICE_ERROR_INVARIANT
    );

    EXPECT_TRUE(initialize_service_fixture());
    message = request_message(MICROS_VFS_TEST_MESSAGE_DRAIN);
    message.payload[4] = 1;
    EXPECT_TRUE(
        micros_vfs_service_handle_message(
            &state,
            &message,
            &fixture.io,
            &action
        ) == MICROS_VFS_SERVICE_ERROR_INVARIANT
    );
    return true;
}

static bool service_dispatches_pm_control(void)
{
    uint8_t spawn_request[MICROS_SPAWN_REQUEST_SIZE_MAX];
    uint8_t executable[MICROS_VFS_TRANSFER_MAX + 1];
    struct micros_ipc_message message;
    struct micros_vfs_service_reply_action action;
    micros_grant_t grant;
    size_t request_size;
    size_t index;
    uint64_t image_token;
    uint64_t descriptor_token;

    EXPECT_TRUE(initialize_service_fixture());
    for (index = 0; index < sizeof(executable); ++index) {
        executable[index] = (uint8_t)(index * 29 + 7);
    }
    EXPECT_TRUE(
        vfs_test_fixture_add_root_file_with_mode(
            &fixture,
            "child",
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
            executable,
            sizeof(executable)
        )
    );
    request_size = build_control_spawn_request(
        spawn_request,
        "/child"
    );
    grant = vfs_test_fixture_add_application_grant(
        &fixture,
        pm_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        spawn_request,
        request_size
    );
    message = pm_control_message(
        MICROS_VFS_CONTROL_PREPARE_IMAGE
    );
    write_u64_le(&message.payload[8], UINT64_C(0x501));
    write_u64_le(
        &message.payload[16],
        UINT64_C(0x0000000500000007)
    );
    write_u32_le(&message.payload[24], application_endpoint);
    write_u32_le(&message.payload[32], grant);
    write_u32_le(
        &message.payload[36],
        (uint32_t)request_size
    );
    EXPECT_TRUE(
        grant != MICROS_GRANT_NONE
        && micros_vfs_service_handle_message(
            &state,
            &message,
            &fixture.io,
            &action
        ) == MICROS_VFS_SERVICE_OK
        && action.active
        && action.reply_token == UINT64_C(0x66)
        && action.message.type
            == MICROS_VFS_MESSAGE_PM_CONTROL_RESULT
        && read_u32_le(&action.message.payload[4])
            == MICROS_VFS_CONTROL_PREPARE_IMAGE
        && read_i32_le(&action.message.payload[8])
            == MICROS_VFS_CONTROL_RESULT_OK
        && read_u64_le(&action.message.payload[16]) != 0
        && read_u32_le(&action.message.payload[24]) != 0
        && read_u32_le(&action.message.payload[28])
            == sizeof(executable)
        && read_u32_le(&action.message.payload[32]) != 0
        && read_u32_le(&action.message.payload[36])
            == request_size
    );
    image_token = read_u64_le(&action.message.payload[16]);

    message = pm_control_message(
        MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS
    );
    write_u64_le(&message.payload[8], UINT64_C(0x501));
    write_u64_le(&message.payload[16], image_token);
    write_u64_le(
        &message.payload[24],
        UINT64_C(0x0000000500000007)
    );
    EXPECT_TRUE(
        micros_vfs_service_handle_message(
            &state,
            &message,
            &fixture.io,
            &action
        ) == MICROS_VFS_SERVICE_OK
        && action.active
        && read_u32_le(&action.message.payload[4])
            == MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS
        && read_i32_le(&action.message.payload[8])
            == MICROS_VFS_CONTROL_RESULT_OK
        && read_u64_le(&action.message.payload[16]) != 0
    );
    descriptor_token = read_u64_le(&action.message.payload[16]);

    message = pm_control_message(
        MICROS_VFS_CONTROL_ABORT_DESCRIPTORS
    );
    write_u64_le(&message.payload[8], UINT64_C(0x501));
    write_u64_le(&message.payload[16], image_token);
    write_u64_le(&message.payload[24], descriptor_token);
    EXPECT_TRUE(
        micros_vfs_service_handle_message(
            &state,
            &message,
            &fixture.io,
            &action
        ) == MICROS_VFS_SERVICE_OK
        && action.active
        && read_u32_le(&action.message.payload[4])
            == MICROS_VFS_CONTROL_ABORT_DESCRIPTORS
        && read_i32_le(&action.message.payload[8])
            == MICROS_VFS_CONTROL_RESULT_OK
        && read_u64_le(&action.message.payload[16]) == 0
        && read_u64_le(&action.message.payload[24]) == 0
        && read_u64_le(&action.message.payload[32]) == 0
        && read_u64_le(&action.message.payload[40]) == 0
    );

    message = pm_control_message(
        MICROS_VFS_CONTROL_RELEASE_IMAGE
    );
    write_u64_le(&message.payload[8], UINT64_C(0x501));
    write_u64_le(&message.payload[16], image_token);
    EXPECT_TRUE(
        micros_vfs_service_handle_message(
            &state,
            &message,
            &fixture.io,
            &action
        ) == MICROS_VFS_SERVICE_OK
        && action.active
        && read_u32_le(&action.message.payload[4])
            == MICROS_VFS_CONTROL_RELEASE_IMAGE
        && read_i32_le(&action.message.payload[8])
            == MICROS_VFS_CONTROL_RESULT_OK
        && state.spawn.phase == MICROS_VFS_SPAWN_IDLE
    );

    message = pm_control_message(
        MICROS_VFS_CONTROL_RELEASE_IMAGE
    );
    message.source = application_endpoint;
    EXPECT_TRUE(
        micros_vfs_service_handle_message(
            &state,
            &message,
            &fixture.io,
            &action
        ) == MICROS_VFS_SERVICE_OK
        && action.active
        && read_i32_le(&action.message.payload[8])
            == MICROS_VFS_CONTROL_RESULT_CALLER
    );

    message = pm_control_message(
        MICROS_VFS_CONTROL_PREPARE_IMAGE
    );
    message.payload[28] = 1;
    EXPECT_TRUE(
        micros_vfs_service_handle_message(
            &state,
            &message,
            &fixture.io,
            &action
        ) == MICROS_VFS_SERVICE_OK
        && action.active
        && read_i32_le(&action.message.payload[8])
            == MICROS_VFS_CONTROL_RESULT_MALFORMED
    );

    message = pm_control_message(
        (enum micros_vfs_control_command)8
    );
    EXPECT_TRUE(
        micros_vfs_service_handle_message(
            &state,
            &message,
            &fixture.io,
            &action
        ) == MICROS_VFS_SERVICE_OK
        && action.active
        && read_u32_le(&action.message.payload[4]) == 8
        && read_i32_le(&action.message.payload[8])
            == MICROS_VFS_CONTROL_RESULT_MALFORMED
    );
    return true;
}

int main(void)
{
    return (
        public_constants_are_exact()
        && pm_control_requests_are_exact()
        && pm_control_reserved_words_are_rejected()
        && pm_control_results_are_exact()
        && application_protocol_is_exact()
        && backend_protocol_is_exact()
        && production_configuration_is_exact()
        && test_configuration_is_exact()
        && service_dispatches_calls_and_notifications()
        && service_dispatches_test_drain_only_for_application()
        && service_dispatches_pm_control()
    ) ? 0 : 1;
}
