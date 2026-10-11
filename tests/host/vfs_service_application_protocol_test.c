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

static bool public_constants_are_exact(void) {
    EXPECT_TRUE(
        MICROS_VFS_PROTOCOL_VERSION == 1 && MICROS_VFS_SERVICE_ID == 6 &&
        MICROS_VFS_PROCESS_SLOT == 5 && MICROS_VFS_RESIDENT_PAGE_LIMIT == 80 &&
        MICROS_VFS_TRANSFER_MAX == 4096 && MICROS_VFS_PATH_MAX == 4096 &&
        MICROS_VFS_NAME_MAX == 60 && MICROS_VFS_DIRECTORY_RECORD_SIZE == 80 &&
        MICROS_VFS_MESSAGE_OPEN == UINT32_C(0x00040001) &&
        MICROS_VFS_MESSAGE_CLOSE == UINT32_C(0x00040002) &&
        MICROS_VFS_MESSAGE_READ == UINT32_C(0x00040003) &&
        MICROS_VFS_MESSAGE_WRITE == UINT32_C(0x00040004) &&
        MICROS_VFS_MESSAGE_GETDENTS == UINT32_C(0x00040005) &&
        MICROS_VFS_MESSAGE_MKDIR == UINT32_C(0x00040006) &&
        MICROS_VFS_MESSAGE_CHDIR == UINT32_C(0x00040007) &&
        MICROS_VFS_MESSAGE_RESULT == UINT32_C(0x00040008) &&
        MICROS_VFS_MESSAGE_PM_CONTROL == UINT32_C(0x00040009) &&
        MICROS_VFS_MESSAGE_PM_CONTROL_RESULT == UINT32_C(0x0004000a) &&
        MICROS_VFS_RESULT_RANGE == -15 && MICROS_VFS_RESULT_OK == 0 &&
        MICROS_VFS_CONTROL_PREPARE_IMAGE == 1 && MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS == 2 &&
        MICROS_VFS_CONTROL_COMMIT_HIDDEN == 3 && MICROS_VFS_CONTROL_REVERT_HIDDEN == 4 &&
        MICROS_VFS_CONTROL_ABORT_DESCRIPTORS == 5 && MICROS_VFS_CONTROL_RELEASE_IMAGE == 6 &&
        MICROS_VFS_CONTROL_EXIT_PROCESS == 7 && MICROS_VFS_CONTROL_RESULT_MALFORMED == -1 &&
        MICROS_VFS_CONTROL_RESULT_CALLER == -2 && MICROS_VFS_CONTROL_RESULT_STATE == -3 &&
        MICROS_VFS_CONTROL_RESULT_NO_SPACE == -4 && MICROS_VFS_CONTROL_RESULT_NOT_FOUND == -5 &&
        MICROS_VFS_CONTROL_RESULT_ACCESS == -6 && MICROS_VFS_CONTROL_RESULT_TOO_BIG == -7 &&
        MICROS_VFS_CONTROL_RESULT_IO == -8 && MICROS_VFS_CONTROL_RESULT_DESCRIPTOR == -9 &&
        MICROS_VFS_CONTROL_RESULT_EXECUTABLE == -10 && MICROS_VFS_CONTROL_RESULT_OK == 0);
    return true;
}

static bool application_protocol_is_exact(struct vfs_service_test_context *context) {
    struct micros_ipc_message message =
        vfs_service_test_request_message(context, MICROS_VFS_MESSAGE_OPEN);
    struct micros_vfs_request decoded;
    struct micros_vfs_result_action result = {
        .active = true,
        .reply_token = UINT64_C(0x99),
        .request_type = MICROS_VFS_MESSAGE_OPEN,
        .result = MICROS_VFS_RESULT_OK,
        .descriptor = 3,
        .mode = MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
    };
    struct micros_ipc_message reply;

    vfs_service_test_write_u32_le(&message.payload[8], 7);
    vfs_service_test_write_u32_le(&message.payload[12], 10);
    vfs_service_test_write_u64_le(&message.payload[16], 4);
    vfs_service_test_write_u32_le(&message.payload[24],
                                  MICROS_VFS_OPEN_READ | MICROS_VFS_OPEN_CREATE);
    vfs_service_test_write_u32_le(&message.payload[28], UINT32_C(0644));
    EXPECT_TRUE(micros_vfs_service_decode_request(&message, &decoded) == MICROS_VFS_PROTOCOL_OK &&
                decoded.type == MICROS_VFS_MESSAGE_OPEN &&
                decoded.version == MICROS_VFS_PROTOCOL_VERSION && decoded.flags == 0 &&
                decoded.source == context->application_endpoint &&
                decoded.reply_token == UINT64_C(0x55) && decoded.grant == 7 &&
                decoded.path_length == 10 && decoded.grant_offset == 4 &&
                decoded.open_flags == (MICROS_VFS_OPEN_READ | MICROS_VFS_OPEN_CREATE) &&
                decoded.mode == UINT32_C(0644));
    message.payload[47] = 1;
    EXPECT_TRUE(micros_vfs_service_decode_request(&message, &decoded) ==
                MICROS_VFS_PROTOCOL_MALFORMED);
    message.payload[47] = 0;
    message.reply_token = 0;
    EXPECT_TRUE(micros_vfs_service_decode_request(&message, &decoded) ==
                MICROS_VFS_PROTOCOL_INVARIANT);
    message = vfs_service_test_request_message(context, UINT32_C(0x0004ffff));
    EXPECT_TRUE(micros_vfs_service_decode_request(&message, &decoded) ==
                MICROS_VFS_PROTOCOL_BAD_TYPE);
    message = vfs_service_test_request_message(context, MICROS_VFS_MESSAGE_CLOSE);
    vfs_service_test_write_u32_le(&message.payload[0], 2);
    EXPECT_TRUE(micros_vfs_service_decode_request(&message, &decoded) ==
                MICROS_VFS_PROTOCOL_BAD_VERSION);
    EXPECT_TRUE(micros_vfs_service_build_result(&result, &reply) == MICROS_VFS_SERVICE_OK &&
                reply.source == 0 && reply.type == MICROS_VFS_MESSAGE_RESULT &&
                reply.reply_token == 0 &&
                vfs_service_test_read_u32_le(&reply.payload[0]) == MICROS_VFS_PROTOCOL_VERSION &&
                vfs_service_test_read_u32_le(&reply.payload[4]) == MICROS_VFS_MESSAGE_OPEN &&
                vfs_service_test_read_i32_le(&reply.payload[8]) == MICROS_VFS_RESULT_OK &&
                vfs_service_test_read_u32_le(&reply.payload[12]) == 0 &&
                vfs_service_test_read_u32_le(&reply.payload[16]) == 3 &&
                vfs_service_test_read_u32_le(&reply.payload[20]) ==
                    (MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644)) &&
                vfs_service_test_read_u64_le(&reply.payload[24]) == 0 &&
                vfs_service_test_read_u64_le(&reply.payload[32]) == 0 &&
                vfs_service_test_read_u64_le(&reply.payload[40]) == 0);
    result.result = MICROS_VFS_RESULT_DESCRIPTOR;
    result.descriptor = 0;
    result.mode = 0;
    EXPECT_TRUE(micros_vfs_service_build_result(&result, &reply) == MICROS_VFS_SERVICE_OK &&
                vfs_service_test_read_i32_le(&reply.payload[8]) == MICROS_VFS_RESULT_DESCRIPTOR &&
                vfs_service_test_read_u64_le(&reply.payload[16]) == 0 &&
                vfs_service_test_read_u64_le(&reply.payload[24]) == 0 &&
                vfs_service_test_read_u64_le(&reply.payload[32]) == 0 &&
                vfs_service_test_read_u64_le(&reply.payload[40]) == 0);
    result.descriptor = 3;
    EXPECT_TRUE(micros_vfs_service_build_result(&result, &reply) ==
                MICROS_VFS_SERVICE_ERROR_INVARIANT);
    return true;
}

bool vfs_service_public_constants_run(struct vfs_service_test_context *context) {
    (void)context;
    return public_constants_are_exact();
}

bool vfs_service_application_protocol_run(struct vfs_service_test_context *context) {
    return application_protocol_is_exact(context);
}
