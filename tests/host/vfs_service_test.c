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
static const micros_endpoint_t ramfs_endpoint =
    UINT32_C(0x00001004);
static const micros_endpoint_t tty_endpoint =
    UINT32_C(0x00001003);
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

static bool initialize_service_fixture(void)
{
    enum micros_vfs_trusted_result result;

    vfs_test_fixture_initialize(
        &fixture,
        vfs_endpoint,
        ramfs_endpoint,
        tty_endpoint
    );
    return (
        micros_vfs_state_initialize(
            &state,
            vfs_endpoint,
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
        && MICROS_VFS_RESIDENT_PAGE_LIMIT == 64
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
        && MICROS_VFS_RESULT_RANGE == -15
        && MICROS_VFS_RESULT_OK == 0
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

int main(void)
{
    return (
        public_constants_are_exact()
        && application_protocol_is_exact()
        && backend_protocol_is_exact()
        && production_configuration_is_exact()
        && service_dispatches_calls_and_notifications()
    ) ? 0 : 1;
}
