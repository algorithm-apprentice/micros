#include "tests/host/vfs_service_test_fixture.h"
#include "tests/host/vfs_service_test_suites.h"

#include "micros/spawn.h"
#include "micros/vfs.h"
#include "servers/vfs/vfs_service_core.h"

#include <stdio.h>
#include <string.h>

#define EXPECT_TRUE(expression)                                                                    \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            fprintf(stderr, "%s:%d: expected %s\n", __FILE__, __LINE__, #expression);              \
            return false;                                                                          \
        }                                                                                          \
    } while (false)

static bool ramfs_mount_and_lookup_calls_encode_exactly(void) {
    struct micros_vfs_ramfs_request ramfs_request;
    struct micros_ipc_message message;
    memset(&ramfs_request, 0, sizeof(ramfs_request));
    ramfs_request.operation = MICROS_VFS_RAMFS_MOUNT;
    EXPECT_TRUE(
        micros_vfs_service_build_ramfs_call(&ramfs_request, &message) == MICROS_VFS_SERVICE_OK &&
        message.type == MICROS_RAMFS_MESSAGE_MOUNT &&
        vfs_service_test_read_u32_le(&message.payload[0]) == MICROS_RAMFS_PROTOCOL_VERSION &&
        vfs_service_test_read_u64_le(&message.payload[4]) == 0 &&
        vfs_service_test_read_u64_le(&message.payload[12]) == 0 &&
        vfs_service_test_read_u64_le(&message.payload[20]) == 0 &&
        vfs_service_test_read_u64_le(&message.payload[28]) == 0 &&
        vfs_service_test_read_u64_le(&message.payload[36]) == 0 &&
        vfs_service_test_read_u32_le(&message.payload[44]) == 0);
    ramfs_request.node = 1;
    EXPECT_TRUE(micros_vfs_service_build_ramfs_call(&ramfs_request, &message) ==
                MICROS_VFS_SERVICE_ERROR_INVARIANT);
    ramfs_request = (struct micros_vfs_ramfs_request){
        .operation = MICROS_VFS_RAMFS_LOOKUP,
        .start = UINT64_C(0x100000000),
        .root = UINT64_C(0x100000000),
        .grant = 9,
        .length = 10,
        .grant_offset = 7,
    };
    EXPECT_TRUE(
        micros_vfs_service_build_ramfs_call(&ramfs_request, &message) == MICROS_VFS_SERVICE_OK &&
        message.type == MICROS_RAMFS_MESSAGE_LOOKUP &&
        vfs_service_test_read_u32_le(&message.payload[0]) == MICROS_RAMFS_PROTOCOL_VERSION &&
        vfs_service_test_read_u64_le(&message.payload[8]) == ramfs_request.start &&
        vfs_service_test_read_u64_le(&message.payload[16]) == ramfs_request.root &&
        vfs_service_test_read_u32_le(&message.payload[24]) == 9 &&
        vfs_service_test_read_u32_le(&message.payload[28]) == 10 &&
        vfs_service_test_read_u64_le(&message.payload[32]) == 7 &&
        vfs_service_test_read_u64_le(&message.payload[40]) == 0);
    return true;
}

static bool ramfs_create_calls_encode_exactly(void) {
    struct micros_vfs_ramfs_request ramfs_request;
    struct micros_ipc_message message;
    ramfs_request = (struct micros_vfs_ramfs_request){
        .operation = MICROS_VFS_RAMFS_CREATE,
        .node = UINT64_C(0x100000001),
        .mode = MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        .grant = 10,
        .grant_offset = 11,
        .length = 5,
    };
    EXPECT_TRUE(micros_vfs_service_build_ramfs_call(&ramfs_request, &message) ==
                    MICROS_VFS_SERVICE_OK &&
                message.type == MICROS_RAMFS_MESSAGE_CREATE &&
                vfs_service_test_read_u64_le(&message.payload[8]) == ramfs_request.node &&
                vfs_service_test_read_u32_le(&message.payload[16]) == ramfs_request.mode &&
                vfs_service_test_read_u32_le(&message.payload[20]) == 10 &&
                vfs_service_test_read_u64_le(&message.payload[24]) == 11 &&
                vfs_service_test_read_u32_le(&message.payload[32]) == 5 &&
                vfs_service_test_read_u32_le(&message.payload[36]) == 0 &&
                vfs_service_test_read_u64_le(&message.payload[40]) == 0);
    ramfs_request.operation = MICROS_VFS_RAMFS_MKDIR;
    ramfs_request.mode = MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755);
    EXPECT_TRUE(micros_vfs_service_build_ramfs_call(&ramfs_request, &message) ==
                    MICROS_VFS_SERVICE_OK &&
                message.type == MICROS_RAMFS_MESSAGE_MKDIR);
    return true;
}

static bool ramfs_data_calls_encode_exactly(void) {
    struct micros_vfs_ramfs_request ramfs_request;
    struct micros_ipc_message message;
    ramfs_request = (struct micros_vfs_ramfs_request){
        .operation = MICROS_VFS_RAMFS_READ,
        .node = UINT64_C(0x100000002),
        .file_offset = 13,
        .grant = 12,
        .count = 14,
        .grant_offset = 15,
    };
    EXPECT_TRUE(micros_vfs_service_build_ramfs_call(&ramfs_request, &message) ==
                    MICROS_VFS_SERVICE_OK &&
                message.type == MICROS_RAMFS_MESSAGE_READ &&
                vfs_service_test_read_u64_le(&message.payload[8]) == ramfs_request.node &&
                vfs_service_test_read_u64_le(&message.payload[16]) == 13 &&
                vfs_service_test_read_u32_le(&message.payload[24]) == 12 &&
                vfs_service_test_read_u32_le(&message.payload[28]) == 14 &&
                vfs_service_test_read_u64_le(&message.payload[32]) == 15 &&
                vfs_service_test_read_u64_le(&message.payload[40]) == 0);
    ramfs_request.operation = MICROS_VFS_RAMFS_WRITE;
    EXPECT_TRUE(micros_vfs_service_build_ramfs_call(&ramfs_request, &message) ==
                    MICROS_VFS_SERVICE_OK &&
                message.type == MICROS_RAMFS_MESSAGE_WRITE);
    ramfs_request = (struct micros_vfs_ramfs_request){
        .operation = MICROS_VFS_RAMFS_GETDENTS,
        .node = UINT64_C(0x100000003),
        .cursor = 16,
        .grant = 13,
        .count = MICROS_RAMFS_DIRECTORY_RECORD_SIZE,
        .grant_offset = 17,
    };
    EXPECT_TRUE(
        micros_vfs_service_build_ramfs_call(&ramfs_request, &message) == MICROS_VFS_SERVICE_OK &&
        message.type == MICROS_RAMFS_MESSAGE_GETDENTS &&
        vfs_service_test_read_u64_le(&message.payload[8]) == ramfs_request.node &&
        vfs_service_test_read_u64_le(&message.payload[16]) == 16 &&
        vfs_service_test_read_u32_le(&message.payload[24]) == 13 &&
        vfs_service_test_read_u32_le(&message.payload[28]) == MICROS_RAMFS_DIRECTORY_RECORD_SIZE &&
        vfs_service_test_read_u64_le(&message.payload[32]) == 17 &&
        vfs_service_test_read_u64_le(&message.payload[40]) == 0);
    return true;
}

static bool ramfs_putnode_calls_encode_exactly(void) {
    struct micros_vfs_ramfs_request ramfs_request;
    struct micros_ipc_message message;
    ramfs_request = (struct micros_vfs_ramfs_request){
        .operation = MICROS_VFS_RAMFS_PUTNODE,
        .node = UINT64_C(0x100000004),
        .count = 18,
    };
    EXPECT_TRUE(micros_vfs_service_build_ramfs_call(&ramfs_request, &message) ==
                    MICROS_VFS_SERVICE_OK &&
                message.type == MICROS_RAMFS_MESSAGE_PUTNODE &&
                vfs_service_test_read_u64_le(&message.payload[8]) == ramfs_request.node &&
                vfs_service_test_read_u32_le(&message.payload[16]) == 18 &&
                vfs_service_test_read_u32_le(&message.payload[20]) == 0 &&
                vfs_service_test_read_u64_le(&message.payload[24]) == 0 &&
                vfs_service_test_read_u64_le(&message.payload[32]) == 0 &&
                vfs_service_test_read_u64_le(&message.payload[40]) == 0);
    ramfs_request.count = 0;
    EXPECT_TRUE(micros_vfs_service_build_ramfs_call(&ramfs_request, &message) ==
                MICROS_VFS_SERVICE_ERROR_INVARIANT);
    return true;
}

static bool ramfs_calls_encode_exactly(void) {
    return (ramfs_mount_and_lookup_calls_encode_exactly() && ramfs_create_calls_encode_exactly() &&
            ramfs_data_calls_encode_exactly() && ramfs_putnode_calls_encode_exactly());
}

static bool ramfs_results_decode_exactly(struct vfs_service_test_context *context) {
    struct micros_vfs_ramfs_request ramfs_request;
    struct micros_vfs_ramfs_response ramfs_response;
    struct micros_ipc_message message;
    ramfs_request = (struct micros_vfs_ramfs_request){
        .operation = MICROS_VFS_RAMFS_LOOKUP,
        .start = UINT64_C(0x100000000),
        .root = UINT64_C(0x100000000),
        .grant = 9,
        .length = 10,
        .grant_offset = 7,
    };
    memset(&message, 0, sizeof(message));
    message.source = context->ramfs_endpoint;
    message.type = MICROS_RAMFS_MESSAGE_RESULT;
    vfs_service_test_write_u32_le(&message.payload[0], MICROS_RAMFS_PROTOCOL_VERSION);
    vfs_service_test_write_u32_le(&message.payload[4], MICROS_RAMFS_MESSAGE_LOOKUP);
    vfs_service_test_write_u32_le(&message.payload[8], (uint32_t)(int32_t)MICROS_RAMFS_RESULT_OK);
    vfs_service_test_write_u64_le(&message.payload[16], UINT64_C(0x100000002));
    vfs_service_test_write_u64_le(&message.payload[24], 19);
    vfs_service_test_write_u32_le(&message.payload[44], MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644));
    EXPECT_TRUE(micros_vfs_service_decode_ramfs_result(context->ramfs_endpoint, &ramfs_request,
                                                       &message,
                                                       &ramfs_response) == MICROS_VFS_SERVICE_OK &&
                ramfs_response.result == MICROS_RAMFS_RESULT_OK &&
                ramfs_response.node == UINT64_C(0x100000002) && ramfs_response.file_size == 19 &&
                ramfs_response.position == 0 && ramfs_response.count == 0 &&
                ramfs_response.mode == (MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644)));
    message.source = context->tty_endpoint;
    EXPECT_TRUE(micros_vfs_service_decode_ramfs_result(context->ramfs_endpoint, &ramfs_request,
                                                       &message, &ramfs_response) ==
                MICROS_VFS_SERVICE_ERROR_INVARIANT);
    message.source = context->ramfs_endpoint;
    ramfs_request.operation = MICROS_VFS_RAMFS_PUTNODE;
    vfs_service_test_write_u32_le(&message.payload[4], MICROS_RAMFS_MESSAGE_PUTNODE);
    EXPECT_TRUE(micros_vfs_service_decode_ramfs_result(context->ramfs_endpoint, &ramfs_request,
                                                       &message, &ramfs_response) ==
                MICROS_VFS_SERVICE_ERROR_INVARIANT);
    return true;
}

static bool tty_calls_encode_exactly(void) {
    struct micros_vfs_tty_request tty_request = {
        .operation = MICROS_VFS_TTY_SUBMIT_WRITE,
        .grant = 11,
        .request_id = 3,
        .count = 5,
    };
    struct micros_ipc_message message;
    tty_request.count = 0;
    EXPECT_TRUE(micros_vfs_service_build_tty_call(&tty_request, &message) ==
                MICROS_VFS_SERVICE_ERROR_INVARIANT);
    tty_request.grant = MICROS_GRANT_NONE;
    tty_request.count = 1;
    EXPECT_TRUE(micros_vfs_service_build_tty_call(&tty_request, &message) ==
                    MICROS_VFS_SERVICE_OK &&
                vfs_service_test_read_u32_le(&message.payload[16]) == MICROS_GRANT_NONE);
    tty_request.grant = 11;
    tty_request.count = 5;
    EXPECT_TRUE(micros_vfs_service_build_tty_call(&tty_request, &message) ==
                    MICROS_VFS_SERVICE_OK &&
                message.type == MICROS_TTY_MESSAGE_SUBMIT_WRITE &&
                vfs_service_test_read_u32_le(&message.payload[0]) == MICROS_TTY_PROTOCOL_VERSION &&
                vfs_service_test_read_u64_le(&message.payload[8]) == 3 &&
                vfs_service_test_read_u32_le(&message.payload[16]) == 11 &&
                vfs_service_test_read_u32_le(&message.payload[20]) == 0 &&
                vfs_service_test_read_u64_le(&message.payload[24]) == 0 &&
                vfs_service_test_read_u64_le(&message.payload[32]) == 5 &&
                vfs_service_test_read_u64_le(&message.payload[40]) == 0);
    tty_request = (struct micros_vfs_tty_request){
        .operation = MICROS_VFS_TTY_CANCEL,
        .request_id = 3,
    };
    EXPECT_TRUE(micros_vfs_service_build_tty_call(&tty_request, &message) ==
                    MICROS_VFS_SERVICE_OK &&
                message.type == MICROS_TTY_MESSAGE_CANCEL &&
                vfs_service_test_read_u64_le(&message.payload[8]) == 3 &&
                vfs_service_test_read_u64_le(&message.payload[16]) == 0 &&
                vfs_service_test_read_u64_le(&message.payload[24]) == 0 &&
                vfs_service_test_read_u64_le(&message.payload[32]) == 0 &&
                vfs_service_test_read_u64_le(&message.payload[40]) == 0);
    tty_request.operation = MICROS_VFS_TTY_COLLECT;
    EXPECT_TRUE(micros_vfs_service_build_tty_call(&tty_request, &message) ==
                    MICROS_VFS_SERVICE_OK &&
                message.type == MICROS_TTY_MESSAGE_COLLECT);
    tty_request.grant = 1;
    EXPECT_TRUE(micros_vfs_service_build_tty_call(&tty_request, &message) ==
                MICROS_VFS_SERVICE_ERROR_INVARIANT);
    return true;
}

static bool tty_results_decode_exactly(struct vfs_service_test_context *context) {
    struct micros_vfs_tty_request tty_request;
    struct micros_vfs_tty_response tty_response;
    struct micros_ipc_message message;
    tty_request = (struct micros_vfs_tty_request){
        .operation = MICROS_VFS_TTY_SUBMIT_WRITE,
        .grant = 11,
        .request_id = 3,
        .count = 5,
    };
    memset(&message, 0, sizeof(message));
    message.source = context->tty_endpoint;
    message.type = MICROS_TTY_MESSAGE_RESULT;
    vfs_service_test_write_u32_le(&message.payload[0], MICROS_TTY_PROTOCOL_VERSION);
    vfs_service_test_write_u32_le(&message.payload[4], MICROS_TTY_MESSAGE_SUBMIT_WRITE);
    vfs_service_test_write_u32_le(&message.payload[8], (uint32_t)(int32_t)MICROS_TTY_RESULT_OK);
    vfs_service_test_write_u64_le(&message.payload[16], 3);
    EXPECT_TRUE(micros_vfs_service_decode_tty_result(context->tty_endpoint, &tty_request, &message,
                                                     &tty_response) == MICROS_VFS_SERVICE_OK &&
                tty_response.result == MICROS_TTY_RESULT_OK && tty_response.request_id == 3 &&
                tty_response.transferred_count == 0);
    message.payload[32] = 1;
    EXPECT_TRUE(micros_vfs_service_decode_tty_result(context->tty_endpoint, &tty_request, &message,
                                                     &tty_response) ==
                MICROS_VFS_SERVICE_ERROR_INVARIANT);
    message.payload[32] = 0;
    vfs_service_test_write_u64_le(&message.payload[24], 1);
    EXPECT_TRUE(micros_vfs_service_decode_tty_result(context->tty_endpoint, &tty_request, &message,
                                                     &tty_response) ==
                MICROS_VFS_SERVICE_ERROR_INVARIANT);
    return true;
}

static bool backend_protocol_is_exact(struct vfs_service_test_context *context) {
    return (ramfs_calls_encode_exactly() && ramfs_results_decode_exactly(context) &&
            tty_calls_encode_exactly() && tty_results_decode_exactly(context));
}

bool vfs_service_backend_protocol_run(struct vfs_service_test_context *context) {
    return backend_protocol_is_exact(context);
}
