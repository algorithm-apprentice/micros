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

static bool decode_prepare_image_variants(struct vfs_service_test_context *context) {
    struct micros_ipc_message message =
        vfs_service_test_pm_control_message(context, MICROS_VFS_CONTROL_PREPARE_IMAGE);
    struct micros_vfs_control_request decoded;

    vfs_service_test_write_u64_le(&message.payload[8], UINT64_C(0x101));
    vfs_service_test_write_u64_le(&message.payload[16], UINT64_C(0x0000000100000007));
    vfs_service_test_write_u32_le(&message.payload[24], context->application_endpoint);
    vfs_service_test_write_u32_le(&message.payload[32], 9);
    vfs_service_test_write_u32_le(&message.payload[36], MICROS_SPAWN_REQUEST_SIZE_MIN);
    EXPECT_TRUE(
        micros_vfs_service_decode_pm_control(context->pm_endpoint, &message, &decoded) ==
            MICROS_VFS_CONTROL_PROTOCOL_OK &&
        decoded.version == MICROS_VFS_PROTOCOL_VERSION &&
        decoded.command == MICROS_VFS_CONTROL_PREPARE_IMAGE &&
        decoded.source == context->pm_endpoint && decoded.reply_token == UINT64_C(0x66) &&
        decoded.lifecycle_token == UINT64_C(0x101) && decoded.image_token == 0 &&
        decoded.descriptor_token == 0 && decoded.semantic_pid == UINT64_C(0x0000000100000007) &&
        decoded.parent_endpoint == context->application_endpoint && decoded.request_grant == 9 &&
        decoded.request_size == MICROS_SPAWN_REQUEST_SIZE_MIN && !decoded.trusted_init &&
        decoded.child_endpoint == 0);

    message = vfs_service_test_pm_control_message(context, MICROS_VFS_CONTROL_PREPARE_IMAGE);
    vfs_service_test_write_u64_le(&message.payload[8], UINT64_C(0x102));
    vfs_service_test_write_u64_le(&message.payload[16], UINT64_C(0x0000000200000001));
    vfs_service_test_write_u32_le(&message.payload[32], 10);
    vfs_service_test_write_u32_le(&message.payload[36], MICROS_SPAWN_REQUEST_SIZE_MAX);
    vfs_service_test_write_u64_le(&message.payload[40], 1);
    EXPECT_TRUE(micros_vfs_service_decode_pm_control(context->pm_endpoint, &message, &decoded) ==
                    MICROS_VFS_CONTROL_PROTOCOL_OK &&
                decoded.lifecycle_token == UINT64_C(0x102) &&
                decoded.semantic_pid == UINT64_C(0x0000000200000001) &&
                decoded.parent_endpoint == 0 && decoded.request_grant == 10 &&
                decoded.request_size == MICROS_SPAWN_REQUEST_SIZE_MAX && decoded.trusted_init);
    return true;
}

static bool decode_descriptor_prepare(struct vfs_service_test_context *context) {
    struct micros_ipc_message message =
        vfs_service_test_pm_control_message(context, MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS);
    struct micros_vfs_control_request decoded;

    vfs_service_test_write_u64_le(&message.payload[8], UINT64_C(0x103));
    vfs_service_test_write_u64_le(&message.payload[16], UINT64_C(0x201));
    vfs_service_test_write_u64_le(&message.payload[24], UINT64_C(0x0000000300000008));
    EXPECT_TRUE(micros_vfs_service_decode_pm_control(context->pm_endpoint, &message, &decoded) ==
                    MICROS_VFS_CONTROL_PROTOCOL_OK &&
                decoded.command == MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS &&
                decoded.lifecycle_token == UINT64_C(0x103) &&
                decoded.image_token == UINT64_C(0x201) &&
                decoded.semantic_pid == UINT64_C(0x0000000300000008));
    return true;
}

static bool decode_hidden_commit_and_revert(struct vfs_service_test_context *context) {
    struct micros_ipc_message message =
        vfs_service_test_pm_control_message(context, MICROS_VFS_CONTROL_COMMIT_HIDDEN);
    struct micros_vfs_control_request decoded;

    vfs_service_test_write_u64_le(&message.payload[8], UINT64_C(0x104));
    vfs_service_test_write_u64_le(&message.payload[16], UINT64_C(0x202));
    vfs_service_test_write_u64_le(&message.payload[24], UINT64_C(0x301));
    vfs_service_test_write_u32_le(&message.payload[32], context->application_endpoint);
    EXPECT_TRUE(micros_vfs_service_decode_pm_control(context->pm_endpoint, &message, &decoded) ==
                    MICROS_VFS_CONTROL_PROTOCOL_OK &&
                decoded.command == MICROS_VFS_CONTROL_COMMIT_HIDDEN &&
                decoded.lifecycle_token == UINT64_C(0x104) &&
                decoded.image_token == UINT64_C(0x202) &&
                decoded.descriptor_token == UINT64_C(0x301) &&
                decoded.child_endpoint == context->application_endpoint);
    vfs_service_test_write_u32_le(&message.payload[4], MICROS_VFS_CONTROL_REVERT_HIDDEN);
    EXPECT_TRUE(micros_vfs_service_decode_pm_control(context->pm_endpoint, &message, &decoded) ==
                    MICROS_VFS_CONTROL_PROTOCOL_OK &&
                decoded.command == MICROS_VFS_CONTROL_REVERT_HIDDEN &&
                decoded.child_endpoint == context->application_endpoint);
    return true;
}

static bool decode_abort_and_release(struct vfs_service_test_context *context) {
    struct micros_ipc_message message =
        vfs_service_test_pm_control_message(context, MICROS_VFS_CONTROL_ABORT_DESCRIPTORS);
    struct micros_vfs_control_request decoded;

    vfs_service_test_write_u64_le(&message.payload[8], UINT64_C(0x105));
    vfs_service_test_write_u64_le(&message.payload[16], UINT64_C(0x203));
    vfs_service_test_write_u64_le(&message.payload[24], UINT64_C(0x302));
    EXPECT_TRUE(micros_vfs_service_decode_pm_control(context->pm_endpoint, &message, &decoded) ==
                    MICROS_VFS_CONTROL_PROTOCOL_OK &&
                decoded.command == MICROS_VFS_CONTROL_ABORT_DESCRIPTORS &&
                decoded.lifecycle_token == UINT64_C(0x105) &&
                decoded.image_token == UINT64_C(0x203) &&
                decoded.descriptor_token == UINT64_C(0x302));

    message = vfs_service_test_pm_control_message(context, MICROS_VFS_CONTROL_RELEASE_IMAGE);
    vfs_service_test_write_u64_le(&message.payload[8], UINT64_C(0x106));
    vfs_service_test_write_u64_le(&message.payload[16], UINT64_C(0x204));
    EXPECT_TRUE(micros_vfs_service_decode_pm_control(context->pm_endpoint, &message, &decoded) ==
                    MICROS_VFS_CONTROL_PROTOCOL_OK &&
                decoded.command == MICROS_VFS_CONTROL_RELEASE_IMAGE &&
                decoded.lifecycle_token == UINT64_C(0x106) &&
                decoded.image_token == UINT64_C(0x204));
    return true;
}

static bool decode_exit_process(struct vfs_service_test_context *context) {
    struct micros_ipc_message message =
        vfs_service_test_pm_control_message(context, MICROS_VFS_CONTROL_EXIT_PROCESS);
    struct micros_vfs_control_request decoded;

    vfs_service_test_write_u64_le(&message.payload[8], UINT64_C(0x401));
    vfs_service_test_write_u32_le(&message.payload[16], context->application_endpoint);
    vfs_service_test_write_u64_le(&message.payload[24], UINT64_C(0x0000000400000009));
    EXPECT_TRUE(micros_vfs_service_decode_pm_control(context->pm_endpoint, &message, &decoded) ==
                    MICROS_VFS_CONTROL_PROTOCOL_OK &&
                decoded.command == MICROS_VFS_CONTROL_EXIT_PROCESS &&
                decoded.lifecycle_token == UINT64_C(0x401) &&
                decoded.child_endpoint == context->application_endpoint &&
                decoded.semantic_pid == UINT64_C(0x0000000400000009));
    return true;
}

static bool invalid_pm_control_requests_preserve_output(struct vfs_service_test_context *context) {
    struct micros_ipc_message message =
        vfs_service_test_pm_control_message(context, MICROS_VFS_CONTROL_EXIT_PROCESS);
    struct micros_vfs_control_request decoded;
    struct micros_vfs_control_request preserved;

    vfs_service_test_write_u64_le(&message.payload[8], UINT64_C(0x401));
    vfs_service_test_write_u32_le(&message.payload[16], context->application_endpoint);
    vfs_service_test_write_u64_le(&message.payload[24], UINT64_C(0x0000000400000009));
    memset(&decoded, 0xa5, sizeof(decoded));
    memcpy(&preserved, &decoded, sizeof(preserved));
    message.source = context->application_endpoint;
    EXPECT_TRUE(micros_vfs_service_decode_pm_control(context->pm_endpoint, &message, &decoded) ==
                    MICROS_VFS_CONTROL_PROTOCOL_CALLER &&
                memcmp(&decoded, &preserved, sizeof(decoded)) == 0);
    message.source = context->pm_endpoint;
    message.type = MICROS_VFS_MESSAGE_OPEN;
    EXPECT_TRUE(micros_vfs_service_decode_pm_control(context->pm_endpoint, &message, &decoded) ==
                    MICROS_VFS_CONTROL_PROTOCOL_BAD_TYPE &&
                memcmp(&decoded, &preserved, sizeof(decoded)) == 0);
    message.type = MICROS_VFS_MESSAGE_PM_CONTROL;
    vfs_service_test_write_u32_le(&message.payload[0], 2);
    EXPECT_TRUE(micros_vfs_service_decode_pm_control(context->pm_endpoint, &message, &decoded) ==
                    MICROS_VFS_CONTROL_PROTOCOL_BAD_VERSION &&
                memcmp(&decoded, &preserved, sizeof(decoded)) == 0);
    vfs_service_test_write_u32_le(&message.payload[0], MICROS_VFS_PROTOCOL_VERSION);
    vfs_service_test_write_u32_le(&message.payload[4], 8);
    EXPECT_TRUE(micros_vfs_service_decode_pm_control(context->pm_endpoint, &message, &decoded) ==
                    MICROS_VFS_CONTROL_PROTOCOL_MALFORMED &&
                memcmp(&decoded, &preserved, sizeof(decoded)) == 0);
    vfs_service_test_write_u32_le(&message.payload[4], MICROS_VFS_CONTROL_EXIT_PROCESS);
    vfs_service_test_write_u32_le(&message.payload[20], 1);
    EXPECT_TRUE(micros_vfs_service_decode_pm_control(context->pm_endpoint, &message, &decoded) ==
                    MICROS_VFS_CONTROL_PROTOCOL_MALFORMED &&
                memcmp(&decoded, &preserved, sizeof(decoded)) == 0);
    vfs_service_test_write_u32_le(&message.payload[20], 0);
    message.reply_token = 0;
    EXPECT_TRUE(micros_vfs_service_decode_pm_control(context->pm_endpoint, &message, &decoded) ==
                    MICROS_VFS_CONTROL_PROTOCOL_INVARIANT &&
                memcmp(&decoded, &preserved, sizeof(decoded)) == 0);
    return true;
}

static bool pm_control_requests_are_exact(struct vfs_service_test_context *context) {
    return (decode_prepare_image_variants(context) && decode_descriptor_prepare(context) &&
            decode_hidden_commit_and_revert(context) && decode_abort_and_release(context) &&
            decode_exit_process(context) && invalid_pm_control_requests_preserve_output(context));
}

static bool pm_control_reserved_words_are_rejected(struct vfs_service_test_context *context) {
    static const struct {
        enum micros_vfs_control_command command;
        size_t reserved_offset;
    } cases[] = {
        {MICROS_VFS_CONTROL_PREPARE_IMAGE, 28},     {MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS, 32},
        {MICROS_VFS_CONTROL_COMMIT_HIDDEN, 36},     {MICROS_VFS_CONTROL_REVERT_HIDDEN, 40},
        {MICROS_VFS_CONTROL_ABORT_DESCRIPTORS, 32}, {MICROS_VFS_CONTROL_RELEASE_IMAGE, 24},
        {MICROS_VFS_CONTROL_EXIT_PROCESS, 20},
    };
    struct micros_vfs_control_request decoded;
    struct micros_vfs_control_request preserved;
    size_t index;

    memset(&decoded, 0x5a, sizeof(decoded));
    memcpy(&preserved, &decoded, sizeof(preserved));
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        struct micros_ipc_message message =
            vfs_service_test_pm_control_message(context, cases[index].command);

        message.payload[cases[index].reserved_offset] = 1;
        EXPECT_TRUE(
            micros_vfs_service_decode_pm_control(context->pm_endpoint, &message, &decoded) ==
                MICROS_VFS_CONTROL_PROTOCOL_MALFORMED &&
            memcmp(&decoded, &preserved, sizeof(decoded)) == 0);
    }
    return true;
}

static bool encode_prepare_image_result(void) {
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

    EXPECT_TRUE(
        micros_vfs_service_build_pm_control_result(&result, &message) == MICROS_VFS_SERVICE_OK &&
        message.source == 0 && message.type == MICROS_VFS_MESSAGE_PM_CONTROL_RESULT &&
        message.reply_token == 0 &&
        vfs_service_test_read_u32_le(&message.payload[0]) == MICROS_VFS_PROTOCOL_VERSION &&
        vfs_service_test_read_u32_le(&message.payload[4]) == MICROS_VFS_CONTROL_PREPARE_IMAGE &&
        vfs_service_test_read_i32_le(&message.payload[8]) == MICROS_VFS_CONTROL_RESULT_OK &&
        vfs_service_test_read_u32_le(&message.payload[12]) == 0 &&
        vfs_service_test_read_u64_le(&message.payload[16]) == UINT64_C(0x201) &&
        vfs_service_test_read_u32_le(&message.payload[24]) == 11 &&
        vfs_service_test_read_u32_le(&message.payload[28]) == 4097 &&
        vfs_service_test_read_u32_le(&message.payload[32]) == 12 &&
        vfs_service_test_read_u32_le(&message.payload[36]) == MICROS_SPAWN_REQUEST_SIZE_MIN &&
        vfs_service_test_read_u64_le(&message.payload[40]) == 0);
    return true;
}

static bool encode_descriptor_prepare_result(void) {
    struct micros_vfs_control_result_action result = {
        .active = true,
        .reply_token = UINT64_C(0x78),
        .command = MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS,
        .result = MICROS_VFS_CONTROL_RESULT_OK,
        .descriptor_token = UINT64_C(0x301),
    };
    struct micros_ipc_message message;

    EXPECT_TRUE(micros_vfs_service_build_pm_control_result(&result, &message) ==
                    MICROS_VFS_SERVICE_OK &&
                vfs_service_test_read_u64_le(&message.payload[16]) == UINT64_C(0x301) &&
                vfs_service_test_read_u64_le(&message.payload[24]) == 0 &&
                vfs_service_test_read_u64_le(&message.payload[32]) == 0 &&
                vfs_service_test_read_u64_le(&message.payload[40]) == 0);
    return true;
}

static bool encode_zero_payload_and_non_ok_results(void) {
    struct micros_vfs_control_result_action result = {
        .active = true,
        .reply_token = UINT64_C(0x79),
        .command = MICROS_VFS_CONTROL_EXIT_PROCESS,
        .result = MICROS_VFS_CONTROL_RESULT_OK,
    };
    struct micros_ipc_message message;

    EXPECT_TRUE(micros_vfs_service_build_pm_control_result(&result, &message) ==
                    MICROS_VFS_SERVICE_OK &&
                vfs_service_test_read_u64_le(&message.payload[16]) == 0 &&
                vfs_service_test_read_u64_le(&message.payload[24]) == 0 &&
                vfs_service_test_read_u64_le(&message.payload[32]) == 0 &&
                vfs_service_test_read_u64_le(&message.payload[40]) == 0);
    result.result = MICROS_VFS_CONTROL_RESULT_NOT_FOUND;
    EXPECT_TRUE(
        micros_vfs_service_build_pm_control_result(&result, &message) == MICROS_VFS_SERVICE_OK &&
        vfs_service_test_read_i32_le(&message.payload[8]) == MICROS_VFS_CONTROL_RESULT_NOT_FOUND &&
        vfs_service_test_read_u32_le(&message.payload[12]) == 0 &&
        vfs_service_test_read_u64_le(&message.payload[16]) == 0 &&
        vfs_service_test_read_u64_le(&message.payload[24]) == 0 &&
        vfs_service_test_read_u64_le(&message.payload[32]) == 0 &&
        vfs_service_test_read_u64_le(&message.payload[40]) == 0);
    return true;
}

static bool invalid_pm_control_results_preserve_output(void) {
    struct micros_vfs_control_result_action result = {
        .active = true,
        .reply_token = UINT64_C(0x79),
        .command = MICROS_VFS_CONTROL_EXIT_PROCESS,
        .result = (enum micros_vfs_control_result) - 11,
    };
    struct micros_ipc_message message;
    struct micros_ipc_message preserved;

    memset(&message, 0xa5, sizeof(message));
    memcpy(&preserved, &message, sizeof(preserved));
    EXPECT_TRUE(micros_vfs_service_build_pm_control_result(&result, &message) ==
                    MICROS_VFS_SERVICE_ERROR_ARGUMENT &&
                memcmp(&message, &preserved, sizeof(message)) == 0);
    result.result = MICROS_VFS_CONTROL_RESULT_OK;
    result.command = (enum micros_vfs_control_command)8;
    EXPECT_TRUE(micros_vfs_service_build_pm_control_result(&result, &message) ==
                    MICROS_VFS_SERVICE_ERROR_ARGUMENT &&
                memcmp(&message, &preserved, sizeof(message)) == 0);
    result.command = MICROS_VFS_CONTROL_RELEASE_IMAGE;
    result.image_token = 1;
    EXPECT_TRUE(micros_vfs_service_build_pm_control_result(&result, &message) ==
                    MICROS_VFS_SERVICE_ERROR_INVARIANT &&
                memcmp(&message, &preserved, sizeof(message)) == 0);
    result.image_token = 0;
    result.result = MICROS_VFS_CONTROL_RESULT_IO;
    result.request_size = 1;
    EXPECT_TRUE(micros_vfs_service_build_pm_control_result(&result, &message) ==
                    MICROS_VFS_SERVICE_ERROR_INVARIANT &&
                memcmp(&message, &preserved, sizeof(message)) == 0);
    result.result = MICROS_VFS_CONTROL_RESULT_OK;
    result.request_size = 0;
    result.reply_token = 0;
    EXPECT_TRUE(micros_vfs_service_build_pm_control_result(&result, &message) ==
                    MICROS_VFS_SERVICE_ERROR_ARGUMENT &&
                memcmp(&message, &preserved, sizeof(message)) == 0);
    return true;
}

static bool pm_control_results_are_exact(void) {
    return (encode_prepare_image_result() && encode_descriptor_prepare_result() &&
            encode_zero_payload_and_non_ok_results() &&
            invalid_pm_control_results_preserve_output());
}

bool vfs_service_pm_control_protocol_run(struct vfs_service_test_context *context) {
    return (pm_control_requests_are_exact(context) &&
            pm_control_reserved_words_are_rejected(context) && pm_control_results_are_exact());
}
