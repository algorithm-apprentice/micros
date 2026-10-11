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

static bool service_dispatches_calls_and_notifications(struct vfs_service_test_context *context) {
    struct micros_ipc_message message =
        vfs_service_test_request_message(context, MICROS_VFS_MESSAGE_CLOSE);
    struct micros_vfs_service_reply_action action;
    micros_grant_t grant;
    static const uint8_t input[] = "ok\n";

    EXPECT_TRUE(vfs_service_test_initialize_fixture(context));
    vfs_service_test_write_u32_le(&message.payload[8], MICROS_VFS_DESCRIPTOR_CAPACITY);
    EXPECT_TRUE(micros_vfs_service_handle_message(&context->state, &message, &context->fixture.io,
                                                  &action) == MICROS_VFS_SERVICE_OK &&
                action.active && action.reply_token == UINT64_C(0x55) &&
                vfs_service_test_read_i32_le(&action.message.payload[8]) ==
                    MICROS_VFS_RESULT_DESCRIPTOR);
    grant = vfs_test_fixture_add_application_grant(&context->fixture, context->application_endpoint,
                                                   MICROS_GRANT_PERMISSION_WRITE, NULL, 8);
    message = vfs_service_test_request_message(context, MICROS_VFS_MESSAGE_READ);
    vfs_service_test_write_u32_le(&message.payload[8], 0);
    vfs_service_test_write_u32_le(&message.payload[12], grant);
    vfs_service_test_write_u32_le(&message.payload[24], 8);
    EXPECT_TRUE(grant != MICROS_GRANT_NONE &&
                micros_vfs_service_handle_message(&context->state, &message, &context->fixture.io,
                                                  &action) == MICROS_VFS_SERVICE_OK &&
                !action.active &&
                vfs_test_fixture_complete_read(&context->fixture, input, sizeof(input) - 1));
    memset(&message, 0, sizeof(message));
    message.source = context->tty_endpoint;
    message.type = MICROS_IPC_TYPE_KERNEL_NOTIFICATION;
    vfs_service_test_write_u64_le(&message.payload[0], MICROS_TTY_EVENT_COMPLETION);
    EXPECT_TRUE(micros_vfs_service_handle_message(&context->state, &message, &context->fixture.io,
                                                  &action) == MICROS_VFS_SERVICE_OK &&
                action.active && action.reply_token == UINT64_C(0x55) &&
                vfs_service_test_read_i32_le(&action.message.payload[8]) == MICROS_VFS_RESULT_OK &&
                vfs_service_test_read_u64_le(&action.message.payload[24]) == sizeof(input) - 1 &&
                memcmp(vfs_test_fixture_application_bytes(&context->fixture, grant), input,
                       sizeof(input) - 1) == 0);
    message.source = context->ramfs_endpoint;
    EXPECT_TRUE(micros_vfs_service_handle_message(&context->state, &message, &context->fixture.io,
                                                  &action) == MICROS_VFS_SERVICE_ERROR_INVARIANT);
    return true;
}

static bool
service_dispatches_test_drain_only_for_application(struct vfs_service_test_context *context) {
    struct micros_ipc_message message =
        vfs_service_test_request_message(context, MICROS_VFS_TEST_MESSAGE_DRAIN);
    struct micros_vfs_service_reply_action action;

    EXPECT_TRUE(vfs_service_test_initialize_fixture(context));
    EXPECT_TRUE(micros_vfs_service_decode_request(&message, &(struct micros_vfs_request){0}) ==
                MICROS_VFS_PROTOCOL_BAD_TYPE);
    EXPECT_TRUE(micros_vfs_service_handle_message(&context->state, &message, &context->fixture.io,
                                                  &action) == MICROS_VFS_SERVICE_OK &&
                action.active && action.reply_token == UINT64_C(0x55) &&
                vfs_service_test_read_u32_le(&action.message.payload[4]) ==
                    MICROS_VFS_TEST_MESSAGE_DRAIN &&
                vfs_service_test_read_i32_le(&action.message.payload[8]) == MICROS_VFS_RESULT_OK);

    EXPECT_TRUE(vfs_service_test_initialize_fixture(context));
    message = vfs_service_test_request_message(context, MICROS_VFS_TEST_MESSAGE_DRAIN);
    message.source = UINT32_C(0x00001002);
    EXPECT_TRUE(micros_vfs_service_handle_message(&context->state, &message, &context->fixture.io,
                                                  &action) == MICROS_VFS_SERVICE_ERROR_INVARIANT);

    EXPECT_TRUE(vfs_service_test_initialize_fixture(context));
    message = vfs_service_test_request_message(context, MICROS_VFS_TEST_MESSAGE_DRAIN);
    message.payload[4] = 1;
    EXPECT_TRUE(micros_vfs_service_handle_message(&context->state, &message, &context->fixture.io,
                                                  &action) == MICROS_VFS_SERVICE_ERROR_INVARIANT);
    return true;
}

struct pm_control_dispatch_scenario {
    uint64_t image_token;
    uint64_t descriptor_token;
};

static bool prepare_pm_control_image(struct vfs_service_test_context *context,
                                     struct pm_control_dispatch_scenario *scenario) {
    uint8_t spawn_request[MICROS_SPAWN_REQUEST_SIZE_MAX];
    uint8_t executable[MICROS_VFS_TRANSFER_MAX + 1];
    struct micros_ipc_message message;
    struct micros_vfs_service_reply_action action;
    micros_grant_t grant;
    size_t request_size;
    size_t index;

    EXPECT_TRUE(vfs_service_test_initialize_fixture(context));
    for (index = 0; index < sizeof(executable); ++index) {
        executable[index] = (uint8_t)(index * 29 + 7);
    }
    EXPECT_TRUE(vfs_test_fixture_add_root_file_with_mode(&context->fixture, "child",
                                                         MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
                                                         executable, sizeof(executable)));
    request_size = vfs_service_test_build_control_spawn_request(spawn_request, "/child");
    grant = vfs_test_fixture_add_application_grant(&context->fixture, context->pm_endpoint,
                                                   MICROS_GRANT_PERMISSION_READ, spawn_request,
                                                   request_size);
    message = vfs_service_test_pm_control_message(context, MICROS_VFS_CONTROL_PREPARE_IMAGE);
    vfs_service_test_write_u64_le(&message.payload[8], UINT64_C(0x501));
    vfs_service_test_write_u64_le(&message.payload[16], UINT64_C(0x0000000500000007));
    vfs_service_test_write_u32_le(&message.payload[24], context->application_endpoint);
    vfs_service_test_write_u32_le(&message.payload[32], grant);
    vfs_service_test_write_u32_le(&message.payload[36], (uint32_t)request_size);
    EXPECT_TRUE(grant != MICROS_GRANT_NONE &&
                micros_vfs_service_handle_message(&context->state, &message, &context->fixture.io,
                                                  &action) == MICROS_VFS_SERVICE_OK &&
                action.active && action.reply_token == UINT64_C(0x66) &&
                action.message.type == MICROS_VFS_MESSAGE_PM_CONTROL_RESULT &&
                vfs_service_test_read_u32_le(&action.message.payload[4]) ==
                    MICROS_VFS_CONTROL_PREPARE_IMAGE &&
                vfs_service_test_read_i32_le(&action.message.payload[8]) ==
                    MICROS_VFS_CONTROL_RESULT_OK &&
                vfs_service_test_read_u64_le(&action.message.payload[16]) != 0 &&
                vfs_service_test_read_u32_le(&action.message.payload[24]) != 0 &&
                vfs_service_test_read_u32_le(&action.message.payload[28]) == sizeof(executable) &&
                vfs_service_test_read_u32_le(&action.message.payload[32]) != 0 &&
                vfs_service_test_read_u32_le(&action.message.payload[36]) == request_size);
    scenario->image_token = vfs_service_test_read_u64_le(&action.message.payload[16]);

    return true;
}

static bool prepare_pm_control_descriptors(struct vfs_service_test_context *context,
                                           struct pm_control_dispatch_scenario *scenario) {
    struct micros_ipc_message message;
    struct micros_vfs_service_reply_action action;

    message = vfs_service_test_pm_control_message(context, MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS);
    vfs_service_test_write_u64_le(&message.payload[8], UINT64_C(0x501));
    vfs_service_test_write_u64_le(&message.payload[16], scenario->image_token);
    vfs_service_test_write_u64_le(&message.payload[24], UINT64_C(0x0000000500000007));
    EXPECT_TRUE(micros_vfs_service_handle_message(&context->state, &message, &context->fixture.io,
                                                  &action) == MICROS_VFS_SERVICE_OK &&
                action.active &&
                vfs_service_test_read_u32_le(&action.message.payload[4]) ==
                    MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS &&
                vfs_service_test_read_i32_le(&action.message.payload[8]) ==
                    MICROS_VFS_CONTROL_RESULT_OK &&
                vfs_service_test_read_u64_le(&action.message.payload[16]) != 0);
    scenario->descriptor_token = vfs_service_test_read_u64_le(&action.message.payload[16]);

    return true;
}

static bool commit_and_revert_hidden_process(struct vfs_service_test_context *context,
                                             const struct pm_control_dispatch_scenario *scenario) {
    struct micros_ipc_message message;
    struct micros_vfs_service_reply_action action;

    message = vfs_service_test_pm_control_message(context, MICROS_VFS_CONTROL_COMMIT_HIDDEN);
    vfs_service_test_write_u64_le(&message.payload[8], UINT64_C(0x501));
    vfs_service_test_write_u64_le(&message.payload[16], scenario->image_token);
    vfs_service_test_write_u64_le(&message.payload[24], scenario->descriptor_token);
    vfs_service_test_write_u32_le(&message.payload[32], context->spawn_child_endpoint);
    EXPECT_TRUE(micros_vfs_service_handle_message(&context->state, &message, &context->fixture.io,
                                                  &action) == MICROS_VFS_SERVICE_OK &&
                action.active &&
                vfs_service_test_read_u32_le(&action.message.payload[4]) ==
                    MICROS_VFS_CONTROL_COMMIT_HIDDEN &&
                vfs_service_test_read_i32_le(&action.message.payload[8]) ==
                    MICROS_VFS_CONTROL_RESULT_OK &&
                vfs_service_test_read_u64_le(&action.message.payload[16]) == 0 &&
                vfs_service_test_read_u64_le(&action.message.payload[24]) == 0 &&
                vfs_service_test_read_u64_le(&action.message.payload[32]) == 0 &&
                vfs_service_test_read_u64_le(&action.message.payload[40]) == 0);

    vfs_service_test_write_u32_le(&message.payload[4], MICROS_VFS_CONTROL_REVERT_HIDDEN);
    EXPECT_TRUE(micros_vfs_service_handle_message(&context->state, &message, &context->fixture.io,
                                                  &action) == MICROS_VFS_SERVICE_OK &&
                action.active &&
                vfs_service_test_read_u32_le(&action.message.payload[4]) ==
                    MICROS_VFS_CONTROL_REVERT_HIDDEN &&
                vfs_service_test_read_i32_le(&action.message.payload[8]) ==
                    MICROS_VFS_CONTROL_RESULT_OK &&
                vfs_service_test_read_u64_le(&action.message.payload[16]) == 0 &&
                vfs_service_test_read_u64_le(&action.message.payload[24]) == 0 &&
                vfs_service_test_read_u64_le(&action.message.payload[32]) == 0 &&
                vfs_service_test_read_u64_le(&action.message.payload[40]) == 0);

    return true;
}

static bool
abort_descriptors_and_release_image(struct vfs_service_test_context *context,
                                    const struct pm_control_dispatch_scenario *scenario) {
    struct micros_ipc_message message;
    struct micros_vfs_service_reply_action action;

    message = vfs_service_test_pm_control_message(context, MICROS_VFS_CONTROL_ABORT_DESCRIPTORS);
    vfs_service_test_write_u64_le(&message.payload[8], UINT64_C(0x501));
    vfs_service_test_write_u64_le(&message.payload[16], scenario->image_token);
    vfs_service_test_write_u64_le(&message.payload[24], scenario->descriptor_token);
    EXPECT_TRUE(micros_vfs_service_handle_message(&context->state, &message, &context->fixture.io,
                                                  &action) == MICROS_VFS_SERVICE_OK &&
                action.active &&
                vfs_service_test_read_u32_le(&action.message.payload[4]) ==
                    MICROS_VFS_CONTROL_ABORT_DESCRIPTORS &&
                vfs_service_test_read_i32_le(&action.message.payload[8]) ==
                    MICROS_VFS_CONTROL_RESULT_OK &&
                vfs_service_test_read_u64_le(&action.message.payload[16]) == 0 &&
                vfs_service_test_read_u64_le(&action.message.payload[24]) == 0 &&
                vfs_service_test_read_u64_le(&action.message.payload[32]) == 0 &&
                vfs_service_test_read_u64_le(&action.message.payload[40]) == 0);

    message = vfs_service_test_pm_control_message(context, MICROS_VFS_CONTROL_RELEASE_IMAGE);
    vfs_service_test_write_u64_le(&message.payload[8], UINT64_C(0x501));
    vfs_service_test_write_u64_le(&message.payload[16], scenario->image_token);
    EXPECT_TRUE(micros_vfs_service_handle_message(&context->state, &message, &context->fixture.io,
                                                  &action) == MICROS_VFS_SERVICE_OK &&
                action.active &&
                vfs_service_test_read_u32_le(&action.message.payload[4]) ==
                    MICROS_VFS_CONTROL_RELEASE_IMAGE &&
                vfs_service_test_read_i32_le(&action.message.payload[8]) ==
                    MICROS_VFS_CONTROL_RESULT_OK &&
                context->state.spawn.phase == MICROS_VFS_SPAWN_IDLE);

    return true;
}

static bool reject_invalid_pm_control_messages(struct vfs_service_test_context *context) {
    struct micros_ipc_message message;
    struct micros_vfs_service_reply_action action;

    message = vfs_service_test_pm_control_message(context, MICROS_VFS_CONTROL_RELEASE_IMAGE);
    message.source = context->application_endpoint;
    EXPECT_TRUE(micros_vfs_service_handle_message(&context->state, &message, &context->fixture.io,
                                                  &action) == MICROS_VFS_SERVICE_OK &&
                action.active &&
                vfs_service_test_read_i32_le(&action.message.payload[8]) ==
                    MICROS_VFS_CONTROL_RESULT_CALLER);

    message = vfs_service_test_pm_control_message(context, MICROS_VFS_CONTROL_PREPARE_IMAGE);
    message.payload[28] = 1;
    EXPECT_TRUE(micros_vfs_service_handle_message(&context->state, &message, &context->fixture.io,
                                                  &action) == MICROS_VFS_SERVICE_OK &&
                action.active &&
                vfs_service_test_read_i32_le(&action.message.payload[8]) ==
                    MICROS_VFS_CONTROL_RESULT_MALFORMED);

    message = vfs_service_test_pm_control_message(context, (enum micros_vfs_control_command)8);
    EXPECT_TRUE(micros_vfs_service_handle_message(&context->state, &message, &context->fixture.io,
                                                  &action) == MICROS_VFS_SERVICE_OK &&
                action.active && vfs_service_test_read_u32_le(&action.message.payload[4]) == 8 &&
                vfs_service_test_read_i32_le(&action.message.payload[8]) ==
                    MICROS_VFS_CONTROL_RESULT_MALFORMED);
    return true;
}

static bool service_dispatches_pm_control(struct vfs_service_test_context *context) {
    struct pm_control_dispatch_scenario scenario = {0};

    return (prepare_pm_control_image(context, &scenario) &&
            prepare_pm_control_descriptors(context, &scenario) &&
            commit_and_revert_hidden_process(context, &scenario) &&
            abort_descriptors_and_release_image(context, &scenario) &&
            reject_invalid_pm_control_messages(context));
}

bool vfs_service_dispatch_run(struct vfs_service_test_context *context) {
    return (service_dispatches_calls_and_notifications(context) &&
            service_dispatches_test_drain_only_for_application(context) &&
            service_dispatches_pm_control(context));
}
