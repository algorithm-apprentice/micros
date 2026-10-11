#include "tests/host/vfs_spawn_test_fixture.h"

#include "lib/spawn/spawn_request.h"

#include <stdio.h>
#include <string.h>

#define EXPECT_TRUE(expression)                                                                    \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            fprintf(stderr, "%s:%d: expected %s\n", __FILE__, __LINE__, #expression);              \
            return false;                                                                          \
        }                                                                                          \
    } while (false)

struct successful_prepare_case {
    struct micros_vfs_control_request request;
    struct micros_vfs_control_result_action action;
    size_t request_size;
    size_t executable_size;
    uint64_t image_token;
};

static bool prepare_image_and_validate_transaction(struct vfs_spawn_test_context *context,
                                                   struct successful_prepare_case *test_case) {
    test_case->executable_size = MICROS_VFS_TRANSFER_MAX + 17;
    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    vfs_spawn_test_fill_executable(context, test_case->executable_size);
    EXPECT_TRUE(vfs_test_fixture_add_root_file_with_mode(
        &context->fixture, "child", MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
        context->executable_bytes, test_case->executable_size));
    test_case->request_size = vfs_spawn_test_build_request(context, "/child");
    test_case->request =
        vfs_spawn_test_prepare_request(context, test_case->request_size, UINT64_C(0x101));
    EXPECT_TRUE(test_case->request.request_grant != MICROS_GRANT_NONE &&
                micros_vfs_handle_control(&context->state, &test_case->request,
                                          &context->fixture.io,
                                          &test_case->action) == MICROS_VFS_CORE_OK &&
                test_case->action.active && test_case->action.reply_token == UINT64_C(0x71) &&
                test_case->action.command == MICROS_VFS_CONTROL_PREPARE_IMAGE &&
                test_case->action.result == MICROS_VFS_CONTROL_RESULT_OK &&
                test_case->action.image_token != 0 &&
                test_case->action.executable_grant != MICROS_GRANT_NONE &&
                test_case->action.executable_size == test_case->executable_size &&
                test_case->action.request_grant != MICROS_GRANT_NONE &&
                test_case->action.request_size == test_case->request_size &&
                test_case->action.descriptor_token == 0 &&
                context->state.spawn.phase == MICROS_VFS_SPAWN_IMAGE_PREPARED &&
                context->state.spawn.pm_spawn_token == UINT64_C(0x101) &&
                context->state.spawn.image_token == test_case->action.image_token &&
                context->state.spawn.semantic_pid == UINT64_C(0x0000000100000007) &&
                context->state.spawn.parent_endpoint == context->parent_endpoint &&
                !context->state.spawn.trusted_init &&
                context->state.spawn.request_size == test_case->request_size &&
                context->state.spawn.executable_size == test_case->executable_size &&
                context->state.last_image_token == test_case->action.image_token &&
                memcmp(context->state.spawn_request_bytes, context->request_bytes,
                       test_case->request_size) == 0 &&
                memcmp(context->state.spawn_executable_bytes, context->executable_bytes,
                       test_case->executable_size) == 0 &&
                context->fixture.backend_grant_count == 2 &&
                micros_vfs_state_validate(&context->state) == MICROS_VFS_CORE_OK);
    test_case->image_token = test_case->action.image_token;
    return true;
}

static bool prepared_image_grants_are_exact(struct vfs_spawn_test_context *context,
                                            const struct successful_prepare_case *test_case) {
    const struct vfs_test_backend_grant *executable_grant =
        vfs_test_fixture_backend_grant(&context->fixture, test_case->action.executable_grant);
    const struct vfs_test_backend_grant *request_grant =
        vfs_test_fixture_backend_grant(&context->fixture, test_case->action.request_grant);

    EXPECT_TRUE(executable_grant != NULL && executable_grant->endpoint == context->vm_endpoint &&
                executable_grant->local == context->state.spawn_executable_bytes &&
                executable_grant->length == test_case->executable_size &&
                executable_grant->permission == MICROS_GRANT_PERMISSION_READ &&
                request_grant != NULL && request_grant->endpoint == context->vm_endpoint &&
                request_grant->local == context->state.spawn_request_bytes &&
                request_grant->length == test_case->request_size &&
                request_grant->permission == MICROS_GRANT_PERMISSION_READ &&
                vfs_test_fixture_application_bytes(&context->fixture,
                                                   test_case->request.request_grant) != NULL);
    return true;
}

static bool release_image_and_validate_cleanup(struct vfs_spawn_test_context *context,
                                               struct successful_prepare_case *test_case) {
    struct micros_vfs_control_request release =
        vfs_spawn_test_release_request(context, UINT64_C(0x101), test_case->image_token);

    EXPECT_TRUE(
        micros_vfs_handle_control(&context->state, &release, &context->fixture.io,
                                  &test_case->action) == MICROS_VFS_CORE_OK &&
        test_case->action.active && test_case->action.reply_token == UINT64_C(0x72) &&
        test_case->action.command == MICROS_VFS_CONTROL_RELEASE_IMAGE &&
        test_case->action.result == MICROS_VFS_CONTROL_RESULT_OK &&
        context->state.spawn.phase == MICROS_VFS_SPAWN_IDLE &&
        context->state.last_image_token == test_case->image_token &&
        vfs_spawn_test_bytes_are_zero(&context->state.spawn, sizeof(context->state.spawn)) &&
        vfs_spawn_test_bytes_are_zero(context->state.spawn_request_bytes,
                                      sizeof(context->state.spawn_request_bytes)) &&
        vfs_spawn_test_bytes_are_zero(context->state.spawn_executable_bytes,
                                      sizeof(context->state.spawn_executable_bytes)) &&
        context->fixture.backend_grant_count == 0 && context->fixture.putnode_count == 1 &&
        micros_vfs_state_validate(&context->state) == MICROS_VFS_CORE_OK);
    return true;
}

bool vfs_spawn_image_successful_prepare_release_run(struct vfs_spawn_test_context *context) {
    struct successful_prepare_case test_case;

    return prepare_image_and_validate_transaction(context, &test_case) &&
           prepared_image_grants_are_exact(context, &test_case) &&
           release_image_and_validate_cleanup(context, &test_case);
}

static bool missing_executable_rolls_back(struct vfs_spawn_test_context *context) {
    struct micros_vfs_control_request request;
    struct micros_vfs_control_result_action action;
    size_t request_size;

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    request_size = vfs_spawn_test_build_request(context, "/missing");
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x201));
    EXPECT_TRUE(
        micros_vfs_handle_control(&context->state, &request, &context->fixture.io, &action) ==
            MICROS_VFS_CORE_OK &&
        action.result == MICROS_VFS_CONTROL_RESULT_NOT_FOUND &&
        vfs_spawn_test_bytes_are_zero(&context->state.spawn, sizeof(context->state.spawn)) &&
        context->fixture.backend_grant_count == 0);
    return true;
}

static bool backend_no_space_rolls_back(struct vfs_spawn_test_context *context) {
    struct micros_vfs_control_request request;
    struct micros_vfs_control_result_action action;
    size_t request_size;

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    request_size = vfs_spawn_test_build_request(context, "/capacity");
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x206));
    context->fixture.inject_ramfs_result = true;
    context->fixture.injected_ramfs_operation = MICROS_VFS_RAMFS_LOOKUP;
    context->fixture.injected_ramfs_result = MICROS_RAMFS_RESULT_NO_SPACE;
    EXPECT_TRUE(
        micros_vfs_handle_control(&context->state, &request, &context->fixture.io, &action) ==
            MICROS_VFS_CORE_OK &&
        action.result == MICROS_VFS_CONTROL_RESULT_NO_SPACE &&
        vfs_spawn_test_bytes_are_zero(&context->state.spawn, sizeof(context->state.spawn)) &&
        context->fixture.backend_grant_count == 0 && context->fixture.putnode_count == 0);
    return true;
}

static bool non_executable_file_rolls_back(struct vfs_spawn_test_context *context) {
    struct micros_vfs_control_request request;
    struct micros_vfs_control_result_action action;
    size_t request_size;

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    vfs_spawn_test_fill_executable(context, 64);
    EXPECT_TRUE(vfs_test_fixture_add_root_file_with_mode(&context->fixture, "plain",
                                                         MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
                                                         context->executable_bytes, 64));
    request_size = vfs_spawn_test_build_request(context, "/plain");
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x202));
    EXPECT_TRUE(
        micros_vfs_handle_control(&context->state, &request, &context->fixture.io, &action) ==
            MICROS_VFS_CORE_OK &&
        action.result == MICROS_VFS_CONTROL_RESULT_ACCESS &&
        vfs_spawn_test_bytes_are_zero(&context->state.spawn, sizeof(context->state.spawn)) &&
        context->fixture.backend_grant_count == 0 && context->fixture.putnode_count == 1);
    return true;
}

static bool directory_executable_rolls_back(struct vfs_spawn_test_context *context) {
    struct micros_vfs_control_request request;
    struct micros_vfs_control_result_action action;
    size_t request_size;

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    request_size = vfs_spawn_test_build_request(context, "/etc");
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x203));
    EXPECT_TRUE(
        micros_vfs_handle_control(&context->state, &request, &context->fixture.io, &action) ==
            MICROS_VFS_CORE_OK &&
        action.result == MICROS_VFS_CONTROL_RESULT_ACCESS &&
        vfs_spawn_test_bytes_are_zero(&context->state.spawn, sizeof(context->state.spawn)) &&
        context->fixture.backend_grant_count == 0 && context->fixture.putnode_count == 1);
    return true;
}

static bool empty_executable_rolls_back(struct vfs_spawn_test_context *context) {
    struct micros_vfs_control_request request;
    struct micros_vfs_control_result_action action;
    size_t request_size;

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    EXPECT_TRUE(vfs_test_fixture_add_root_file_with_mode(
        &context->fixture, "empty", MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755), NULL, 0));
    request_size = vfs_spawn_test_build_request(context, "/empty");
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x204));
    EXPECT_TRUE(
        micros_vfs_handle_control(&context->state, &request, &context->fixture.io, &action) ==
            MICROS_VFS_CORE_OK &&
        action.result == MICROS_VFS_CONTROL_RESULT_EXECUTABLE &&
        vfs_spawn_test_bytes_are_zero(&context->state.spawn, sizeof(context->state.spawn)) &&
        context->fixture.backend_grant_count == 0 && context->fixture.putnode_count == 1);
    return true;
}

static bool oversized_executable_rolls_back(struct vfs_spawn_test_context *context) {
    struct micros_vfs_control_request request;
    struct micros_vfs_control_result_action action;
    size_t request_size;

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    vfs_spawn_test_fill_executable(context, MICROS_APPLICATION_EXECUTABLE_SIZE_MAX + 1);
    EXPECT_TRUE(vfs_test_fixture_add_root_file_with_mode(
        &context->fixture, "large", MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
        context->executable_bytes, MICROS_APPLICATION_EXECUTABLE_SIZE_MAX + 1));
    request_size = vfs_spawn_test_build_request(context, "/large");
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x205));
    EXPECT_TRUE(
        micros_vfs_handle_control(&context->state, &request, &context->fixture.io, &action) ==
            MICROS_VFS_CORE_OK &&
        action.result == MICROS_VFS_CONTROL_RESULT_TOO_BIG &&
        vfs_spawn_test_bytes_are_zero(&context->state.spawn, sizeof(context->state.spawn)) &&
        context->fixture.backend_grant_count == 0 && context->fixture.putnode_count == 1);
    return true;
}

bool vfs_spawn_image_expected_failures_run(struct vfs_spawn_test_context *context) {
    return missing_executable_rolls_back(context) && backend_no_space_rolls_back(context) &&
           non_executable_file_rolls_back(context) && directory_executable_rolls_back(context) &&
           empty_executable_rolls_back(context) && oversized_executable_rolls_back(context);
}

bool vfs_spawn_image_transaction_failures_run(struct vfs_spawn_test_context *context) {
    struct micros_vfs_control_request request;
    struct micros_vfs_control_request release;
    struct micros_vfs_control_result_action action;
    size_t request_size;
    uint64_t image_token;

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    vfs_spawn_test_fill_executable(context, MICROS_VFS_TRANSFER_MAX + 1);
    EXPECT_TRUE(vfs_test_fixture_add_root_file_with_mode(
        &context->fixture, "child", MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
        context->executable_bytes, MICROS_VFS_TRANSFER_MAX + 1));
    request_size = vfs_spawn_test_build_request(context, "/child");
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x301));
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK);
    image_token = action.image_token;
    context->snapshot = context->state;
    request.lifecycle_token = UINT64_C(0x302);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_STATE &&
                memcmp(&context->state, &context->snapshot, sizeof(context->state)) == 0);
    release = vfs_spawn_test_release_request(context, UINT64_C(0x301), image_token + 1);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &release, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_STATE &&
                memcmp(&context->state, &context->snapshot, sizeof(context->state)) == 0);
    release.image_token = image_token;
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &release, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK);

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    vfs_spawn_test_fill_executable(context, MICROS_VFS_TRANSFER_MAX + 1);
    EXPECT_TRUE(vfs_test_fixture_add_root_file_with_mode(
        &context->fixture, "short", MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
        context->executable_bytes, MICROS_VFS_TRANSFER_MAX + 1));
    request_size = vfs_spawn_test_build_request(context, "/short");
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x303));
    context->fixture.corrupt_read_count = true;
    EXPECT_TRUE(
        micros_vfs_handle_control(&context->state, &request, &context->fixture.io, &action) ==
            MICROS_VFS_CORE_OK &&
        action.result == MICROS_VFS_CONTROL_RESULT_IO &&
        vfs_spawn_test_bytes_are_zero(&context->state.spawn, sizeof(context->state.spawn)) &&
        context->fixture.backend_grant_count == 0 && context->fixture.putnode_count == 1);

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    vfs_spawn_test_fill_executable(context, 64);
    EXPECT_TRUE(vfs_test_fixture_add_root_file_with_mode(&context->fixture, "capacity",
                                                         MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
                                                         context->executable_bytes, 64));
    request_size = vfs_spawn_test_build_request(context, "/capacity");
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x304));
    context->fixture.force_backend_grant_capacity = true;
    EXPECT_TRUE(
        micros_vfs_handle_control(&context->state, &request, &context->fixture.io, &action) ==
            MICROS_VFS_CORE_OK &&
        action.result == MICROS_VFS_CONTROL_RESULT_NO_SPACE &&
        vfs_spawn_test_bytes_are_zero(&context->state.spawn, sizeof(context->state.spawn)) &&
        context->fixture.backend_grant_count == 0);
    return true;
}

static bool trusted_init_prepare_and_release_are_exact(struct vfs_spawn_test_context *context) {
    struct micros_vfs_control_request request;
    struct micros_vfs_control_request release;
    struct micros_vfs_control_result_action action;
    size_t request_size;

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    vfs_spawn_test_fill_executable(context, 64);
    EXPECT_TRUE(vfs_test_fixture_add_root_file_with_mode(&context->fixture, "child",
                                                         MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
                                                         context->executable_bytes, 64));
    request_size = vfs_spawn_test_build_request(context, "/child");
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x401));
    request.parent_endpoint = 0;
    request.trusted_init = true;
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK &&
                context->state.spawn.trusted_init && context->state.spawn.parent_endpoint == 0);
    release = vfs_spawn_test_release_request(context, UINT64_C(0x401), action.image_token);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &release, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK);
    return true;
}

static bool trusted_init_relative_path_is_rejected(struct vfs_spawn_test_context *context) {
    struct micros_vfs_control_request request;
    struct micros_vfs_control_result_action action;
    size_t request_size;

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    request_size = vfs_spawn_test_build_request(context, "child");
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x402));
    request.parent_endpoint = 0;
    request.trusted_init = true;
    EXPECT_TRUE(
        micros_vfs_handle_control(&context->state, &request, &context->fixture.io, &action) ==
            MICROS_VFS_CORE_OK &&
        action.result == MICROS_VFS_CONTROL_RESULT_STATE && context->fixture.ramfs_calls == 1 &&
        vfs_spawn_test_bytes_are_zero(&context->state.spawn, sizeof(context->state.spawn)) &&
        vfs_spawn_test_bytes_are_zero(context->state.spawn_request_bytes,
                                      sizeof(context->state.spawn_request_bytes)));
    return true;
}

static bool trusted_init_descriptor_actions_are_rejected(struct vfs_spawn_test_context *context) {
    struct micros_vfs_control_request request;
    struct micros_vfs_control_result_action action;
    size_t request_size;

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    request_size = vfs_spawn_test_build_request(context, "/child");
    vfs_spawn_test_write_u16_le(
        &context->request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTION_COUNT_OFFSET], 1);
    vfs_spawn_test_write_u16_le(&context->request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET], 0);
    vfs_spawn_test_write_u16_le(&context->request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET + 2],
                                1);
    vfs_spawn_test_write_u32_le(&context->request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET + 4],
                                MICROS_SPAWN_DESCRIPTOR_DUPLICATE);
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x403));
    request.parent_endpoint = 0;
    request.trusted_init = true;
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_STATE &&
                context->fixture.ramfs_calls == 1 &&
                vfs_spawn_test_bytes_are_zero(&context->state.spawn, sizeof(context->state.spawn)));
    return true;
}

static bool
overlong_path_is_rejected_before_backend_lookup(struct vfs_spawn_test_context *context) {
    struct micros_vfs_control_request request;
    struct micros_vfs_control_result_action action;
    char overlong_path[MICROS_VFS_NAME_MAX + 3];
    size_t request_size;
    size_t index;

    overlong_path[0] = '/';
    for (index = 1; index < sizeof(overlong_path) - 1; ++index) {
        overlong_path[index] = 'a';
    }
    overlong_path[sizeof(overlong_path) - 1] = 0;
    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    request_size = vfs_spawn_test_build_request(context, overlong_path);
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x404));
    EXPECT_TRUE(
        micros_vfs_handle_control(&context->state, &request, &context->fixture.io, &action) ==
            MICROS_VFS_CORE_OK &&
        action.result == MICROS_VFS_CONTROL_RESULT_TOO_BIG && context->fixture.ramfs_calls == 1 &&
        context->fixture.putnode_count == 0 &&
        vfs_spawn_test_bytes_are_zero(&context->state.spawn, sizeof(context->state.spawn)) &&
        vfs_spawn_test_bytes_are_zero(context->state.spawn_request_bytes,
                                      sizeof(context->state.spawn_request_bytes)));
    return true;
}

bool vfs_spawn_image_trusted_init_bounds_run(struct vfs_spawn_test_context *context) {
    return trusted_init_prepare_and_release_are_exact(context) &&
           trusted_init_relative_path_is_rejected(context) &&
           trusted_init_descriptor_actions_are_rejected(context) &&
           overlong_path_is_rejected_before_backend_lookup(context);
}

bool vfs_spawn_image_trusted_input_failures_run(struct vfs_spawn_test_context *context) {
    struct micros_vfs_control_request request;
    struct micros_vfs_control_result_action action;
    size_t request_size;

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    request_size = vfs_spawn_test_build_request(context, "/child");
    context->request_bytes[8] ^= 1;
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x501));
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_ERROR_INVARIANT &&
                !action.active && context->fixture.ramfs_calls == 1);

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    request_size = vfs_spawn_test_build_oversized_stack_request(context, "/child");
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x505));
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_ERROR_INVARIANT &&
                !action.active && context->fixture.ramfs_calls == 1);

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    request_size = vfs_spawn_test_build_request(context, "/child");
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x502));
    context->fixture.reject_client_validate = true;
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_ERROR_INVARIANT &&
                context->fixture.client_validate_calls == 1 &&
                context->fixture.client_copy_calls == 0 &&
                micros_vfs_state_validate(&context->state) == MICROS_VFS_CORE_OK);

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    request_size = vfs_spawn_test_build_request(context, "/child");
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x503));
    context->fixture.reject_client_copy = true;
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_ERROR_INVARIANT &&
                context->fixture.client_validate_calls == 1 &&
                context->fixture.client_copy_calls == 1 &&
                micros_vfs_state_validate(&context->state) == MICROS_VFS_CORE_OK);

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    request_size = vfs_spawn_test_build_request(context, "/child");
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x504));
    context->state.last_image_token = UINT64_MAX;
    context->snapshot = context->state;
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_ERROR_INVARIANT &&
                memcmp(&context->state, &context->snapshot, sizeof(context->state)) == 0);
    return true;
}

bool vfs_spawn_image_capacity_tokens_run(struct vfs_spawn_test_context *context) {
    struct micros_vfs_control_request request;
    struct micros_vfs_control_request release;
    struct micros_vfs_control_result_action action;
    size_t request_size;
    uint64_t first_image_token;

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    vfs_spawn_test_fill_executable(context, 64);
    EXPECT_TRUE(vfs_test_fixture_add_root_file_with_mode(&context->fixture, "child",
                                                         MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
                                                         context->executable_bytes, 64));
    request_size = vfs_spawn_test_build_request(context, "/child");
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x601));
    context->fixture.backend_capacity_on_create_call = 4;
    EXPECT_TRUE(
        micros_vfs_handle_control(&context->state, &request, &context->fixture.io, &action) ==
            MICROS_VFS_CORE_OK &&
        action.result == MICROS_VFS_CONTROL_RESULT_NO_SPACE &&
        context->fixture.backend_create_calls == 4 && context->fixture.backend_grant_count == 0 &&
        context->fixture.putnode_count == 1 &&
        vfs_spawn_test_bytes_are_zero(&context->state.spawn, sizeof(context->state.spawn)) &&
        vfs_spawn_test_bytes_are_zero(context->state.spawn_request_bytes,
                                      sizeof(context->state.spawn_request_bytes)) &&
        vfs_spawn_test_bytes_are_zero(context->state.spawn_executable_bytes,
                                      sizeof(context->state.spawn_executable_bytes)));

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    vfs_spawn_test_fill_executable(context, 64);
    EXPECT_TRUE(vfs_test_fixture_add_root_file_with_mode(&context->fixture, "child",
                                                         MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
                                                         context->executable_bytes, 64));
    request_size = vfs_spawn_test_build_request(context, "/child");
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x602));
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK);
    first_image_token = action.image_token;
    release = vfs_spawn_test_release_request(context, UINT64_C(0x602), first_image_token);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &release, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK);
    request.lifecycle_token = UINT64_C(0x603);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK &&
                action.image_token == first_image_token + 1);
    release = vfs_spawn_test_release_request(context, UINT64_C(0x603), action.image_token);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &release, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK);
    return true;
}

struct prepared_snapshot_case {
    struct micros_vfs_control_result_action control_action;
    uint32_t descriptor;
};

static bool prepare_executable_snapshot(struct vfs_spawn_test_context *context,
                                        struct prepared_snapshot_case *test_case) {
    struct micros_vfs_control_request request;
    size_t request_size;

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    vfs_spawn_test_fill_executable(context, 64);
    EXPECT_TRUE(vfs_test_fixture_add_root_file_with_mode(&context->fixture, "child",
                                                         MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
                                                         context->executable_bytes, 64));
    request_size = vfs_spawn_test_build_request(context, "/child");
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x801));
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &test_case->control_action) == MICROS_VFS_CORE_OK &&
                test_case->control_action.result == MICROS_VFS_CONTROL_RESULT_OK);
    return true;
}

static bool grow_backing_vnode_without_mutating_snapshot(struct vfs_spawn_test_context *context,
                                                         struct prepared_snapshot_case *test_case) {
    static const uint8_t path[] = "/child";
    uint8_t write_bytes[65];
    struct micros_vfs_request application_request;
    struct micros_vfs_result_action application_action;
    micros_grant_t path_grant;
    micros_grant_t write_grant;

    memset(write_bytes, 0x5a, sizeof(write_bytes));
    path_grant =
        vfs_test_fixture_add_application_grant(&context->fixture, context->parent_endpoint,
                                               MICROS_GRANT_PERMISSION_READ, path, sizeof(path));
    application_request = (struct micros_vfs_request){
        .type = MICROS_VFS_MESSAGE_OPEN,
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .source = context->parent_endpoint,
        .reply_token = UINT64_C(0x81),
        .grant = path_grant,
        .path_length = sizeof(path),
        .open_flags = MICROS_VFS_OPEN_WRITE,
    };
    EXPECT_TRUE(path_grant != MICROS_GRANT_NONE &&
                micros_vfs_handle_request(&context->state, &application_request,
                                          &context->fixture.io,
                                          &application_action) == MICROS_VFS_CORE_OK &&
                application_action.active && application_action.result == MICROS_VFS_RESULT_OK);
    test_case->descriptor = application_action.descriptor;
    write_grant = vfs_test_fixture_add_application_grant(
        &context->fixture, context->parent_endpoint, MICROS_GRANT_PERMISSION_READ, write_bytes,
        sizeof(write_bytes));
    application_request = (struct micros_vfs_request){
        .type = MICROS_VFS_MESSAGE_WRITE,
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .source = context->parent_endpoint,
        .reply_token = UINT64_C(0x82),
        .descriptor = test_case->descriptor,
        .grant = write_grant,
        .count = sizeof(write_bytes),
    };
    EXPECT_TRUE(write_grant != MICROS_GRANT_NONE &&
                micros_vfs_handle_request(&context->state, &application_request,
                                          &context->fixture.io,
                                          &application_action) == MICROS_VFS_CORE_OK &&
                application_action.result == MICROS_VFS_RESULT_OK &&
                context->state.spawn.executable_size == 64 &&
                memcmp(context->state.spawn_executable_bytes, context->executable_bytes, 64) == 0 &&
                context->state.vnodes[context->state.spawn.executable_vnode - 1].size ==
                    sizeof(write_bytes) &&
                micros_vfs_state_validate(&context->state) == MICROS_VFS_CORE_OK);
    return true;
}

static bool release_snapshot_and_close_descriptor(struct vfs_spawn_test_context *context,
                                                  struct prepared_snapshot_case *test_case) {
    struct micros_vfs_control_request release = vfs_spawn_test_release_request(
        context, UINT64_C(0x801), test_case->control_action.image_token);
    struct micros_vfs_request application_request;
    struct micros_vfs_result_action application_action;

    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &release, &context->fixture.io,
                                          &test_case->control_action) == MICROS_VFS_CORE_OK &&
                test_case->control_action.result == MICROS_VFS_CONTROL_RESULT_OK);
    application_request = (struct micros_vfs_request){
        .type = MICROS_VFS_MESSAGE_CLOSE,
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .source = context->parent_endpoint,
        .reply_token = UINT64_C(0x83),
        .descriptor = test_case->descriptor,
    };
    EXPECT_TRUE(micros_vfs_handle_request(&context->state, &application_request,
                                          &context->fixture.io,
                                          &application_action) == MICROS_VFS_CORE_OK &&
                application_action.result == MICROS_VFS_RESULT_OK);
    return true;
}

bool vfs_spawn_image_snapshot_run(struct vfs_spawn_test_context *context) {
    struct prepared_snapshot_case test_case;

    return prepare_executable_snapshot(context, &test_case) &&
           grow_backing_vnode_without_mutating_snapshot(context, &test_case) &&
           release_snapshot_and_close_descriptor(context, &test_case);
}

bool vfs_spawn_image_grant_alias_run(struct vfs_spawn_test_context *context) {
    static const uint8_t completion[] = "x";
    struct micros_vfs_control_request request;
    struct micros_vfs_control_request release;
    struct micros_vfs_control_result_action control_action;
    struct micros_vfs_request read_request;
    struct micros_vfs_result_action application_action;
    micros_grant_t application_grant;
    size_t request_size;

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    vfs_spawn_test_fill_executable(context, 64);
    EXPECT_TRUE(vfs_test_fixture_add_root_file_with_mode(&context->fixture, "child",
                                                         MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
                                                         context->executable_bytes, 64));
    request_size = vfs_spawn_test_build_request(context, "/child");
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x901));
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &control_action) == MICROS_VFS_CORE_OK &&
                control_action.result == MICROS_VFS_CONTROL_RESULT_OK);
    application_grant = vfs_test_fixture_add_application_grant(
        &context->fixture, context->parent_endpoint, MICROS_GRANT_PERMISSION_WRITE, NULL, 8);
    read_request = (struct micros_vfs_request){
        .type = MICROS_VFS_MESSAGE_READ,
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .source = context->parent_endpoint,
        .reply_token = UINT64_C(0x91),
        .descriptor = 0,
        .grant = application_grant,
        .count = 8,
    };
    EXPECT_TRUE(application_grant != MICROS_GRANT_NONE &&
                micros_vfs_handle_request(&context->state, &read_request, &context->fixture.io,
                                          &application_action) == MICROS_VFS_CORE_OK &&
                !application_action.active &&
                context->state.pending.tty_grant != MICROS_GRANT_NONE &&
                micros_vfs_state_validate(&context->state) == MICROS_VFS_CORE_OK);

    context->snapshot = context->state;
    context->snapshot.spawn.executable_grant = context->snapshot.pending.tty_grant;
    EXPECT_TRUE(micros_vfs_state_validate(&context->snapshot) == MICROS_VFS_CORE_ERROR_INVARIANT);
    context->snapshot = context->state;
    context->snapshot.spawn.request_grant = context->snapshot.pending.tty_grant;
    EXPECT_TRUE(micros_vfs_state_validate(&context->snapshot) == MICROS_VFS_CORE_ERROR_INVARIANT);

    EXPECT_TRUE(
        vfs_test_fixture_complete_read(&context->fixture, completion, sizeof(completion) - 1) &&
        micros_vfs_handle_tty_notification(&context->state, MICROS_TTY_EVENT_COMPLETION,
                                           &context->fixture.io,
                                           &application_action) == MICROS_VFS_CORE_OK &&
        application_action.active && application_action.result == MICROS_VFS_RESULT_OK);
    release = vfs_spawn_test_release_request(context, UINT64_C(0x901), control_action.image_token);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &release, &context->fixture.io,
                                          &control_action) == MICROS_VFS_CORE_OK &&
                control_action.result == MICROS_VFS_CONTROL_RESULT_OK);
    return true;
}

bool vfs_spawn_image_state_corruption_run(struct vfs_spawn_test_context *context) {
    struct micros_vfs_control_request request;
    struct micros_vfs_control_request release;
    struct micros_vfs_control_result_action action;
    size_t request_size;

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    vfs_spawn_test_fill_executable(context, 64);
    EXPECT_TRUE(vfs_test_fixture_add_root_file_with_mode(&context->fixture, "child",
                                                         MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
                                                         context->executable_bytes, 64));
    request_size = vfs_spawn_test_build_request(context, "/child");
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x701));
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK);

    context->snapshot = context->state;
    ++context->snapshot.last_image_token;
    EXPECT_TRUE(micros_vfs_state_validate(&context->snapshot) == MICROS_VFS_CORE_ERROR_INVARIANT);
    context->snapshot = context->state;
    context->snapshot.spawn_request_bytes[request_size] = 1;
    EXPECT_TRUE(micros_vfs_state_validate(&context->snapshot) == MICROS_VFS_CORE_ERROR_INVARIANT);
    context->snapshot = context->state;
    context->snapshot.spawn_executable_bytes[64] = 1;
    EXPECT_TRUE(micros_vfs_state_validate(&context->snapshot) == MICROS_VFS_CORE_ERROR_INVARIANT);
    context->snapshot = context->state;
    context->snapshot.spawn.parent_endpoint = UINT32_C(0x00003008);
    EXPECT_TRUE(micros_vfs_state_validate(&context->snapshot) == MICROS_VFS_CORE_ERROR_INVARIANT);
    context->snapshot = context->state;
    context->snapshot.spawn.executable_size = 63;
    EXPECT_TRUE(micros_vfs_state_validate(&context->snapshot) == MICROS_VFS_CORE_ERROR_INVARIANT);

    release = vfs_spawn_test_release_request(context, UINT64_C(0x701), action.image_token);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &release, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK);
    return true;
}
