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

struct public_hidden_case {
    struct micros_vfs_control_request commit;
    struct micros_vfs_control_request revert;
    struct micros_vfs_control_request abort;
    struct micros_vfs_control_request release;
    struct micros_vfs_process_record expected;
    uint16_t process_reference;
    uint16_t parent_read;
    uint16_t parent_write;
    uint32_t root_references;
    uint64_t descriptor_token;
};

static bool prepare_public_hidden_case(struct vfs_spawn_test_context *context,
                                       struct public_hidden_case *selected) {
    static const struct micros_spawn_descriptor_action actions[] = {
        {
            .source = 0,
            .destination = 4,
            .operation = MICROS_SPAWN_DESCRIPTOR_DUPLICATE,
        },
        {
            .source = 1,
            .destination = 5,
            .operation = MICROS_SPAWN_DESCRIPTOR_DUPLICATE,
        },
    };
    static const micros_endpoint_t child_endpoint = UINT32_C(0x00003008);
    struct micros_vfs_control_request request;
    struct micros_vfs_control_request descriptors;
    struct micros_vfs_control_result_action action;
    uint64_t image_token;
    size_t request_size;

    memset(selected, 0, sizeof(*selected));
    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    vfs_spawn_test_fill_executable(context, 64);
    EXPECT_TRUE(vfs_test_fixture_add_root_file_with_mode(&context->fixture, "child",
                                                         MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
                                                         context->executable_bytes, 64));
    request_size = vfs_spawn_test_build_request(context, "/child");
    vfs_spawn_test_set_descriptor_actions(context, actions, sizeof(actions) / sizeof(actions[0]));
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x117));
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK);
    image_token = action.image_token;
    selected->parent_read = context->state.processes[0].descriptors[0];
    selected->parent_write = context->state.processes[0].descriptors[1];
    selected->root_references = context->state.vnodes[0].local_reference_count;
    descriptors = vfs_spawn_test_prepare_descriptors_request(context, UINT64_C(0x117), image_token);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &descriptors, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK);
    selected->descriptor_token = action.descriptor_token;
    selected->process_reference = context->state.spawn.child_process_reference;
    selected->expected.state = MICROS_VFS_PROCESS_ACTIVE;
    selected->expected.endpoint = child_endpoint;
    selected->expected.root_vnode = context->state.spawn.child_root_vnode;
    selected->expected.working_directory_vnode = context->state.spawn.child_working_directory_vnode;
    memcpy(selected->expected.descriptors, context->state.spawn.child_descriptors,
           sizeof(selected->expected.descriptors));
    selected->commit =
        vfs_spawn_test_hidden_request(context, MICROS_VFS_CONTROL_COMMIT_HIDDEN, UINT64_C(0x117),
                                      image_token, selected->descriptor_token, child_endpoint);
    selected->revert =
        vfs_spawn_test_hidden_request(context, MICROS_VFS_CONTROL_REVERT_HIDDEN, UINT64_C(0x117),
                                      image_token, selected->descriptor_token, child_endpoint);
    selected->abort = vfs_spawn_test_abort_descriptors_request(
        context, UINT64_C(0x117), image_token, selected->descriptor_token);
    selected->release = vfs_spawn_test_release_request(context, UINT64_C(0x117), image_token);
    return true;
}

static bool hidden_commit_rejections_are_atomic(struct vfs_spawn_test_context *context,
                                                const struct public_hidden_case *selected) {
    struct micros_vfs_control_request request = selected->commit;
    struct micros_vfs_control_result_action action;

    context->snapshot = context->state;
    request.child_endpoint = context->parent_endpoint;
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_STATE &&
                memcmp(&context->state, &context->snapshot, sizeof(context->state)) == 0);
    request.child_endpoint = context->vfs_endpoint;
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_STATE &&
                memcmp(&context->state, &context->snapshot, sizeof(context->state)) == 0);
    request = selected->commit;
    request.descriptor_token ^= UINT64_C(1);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_STATE &&
                memcmp(&context->state, &context->snapshot, sizeof(context->state)) == 0);
    return true;
}

static bool hidden_commit_is_exact(struct vfs_spawn_test_context *context,
                                   const struct public_hidden_case *selected) {
    struct micros_vfs_control_result_action action;

    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &selected->commit, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.active && action.reply_token == UINT64_C(0x75) &&
                action.command == MICROS_VFS_CONTROL_COMMIT_HIDDEN &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK && action.image_token == 0 &&
                action.executable_grant == 0 && action.executable_size == 0 &&
                action.request_grant == 0 && action.request_size == 0 &&
                action.descriptor_token == 0 &&
                context->state.spawn.phase == MICROS_VFS_SPAWN_HIDDEN_COMMITTED &&
                vfs_spawn_test_active_process_count(context) == 2 &&
                memcmp(&context->state.processes[selected->process_reference - 1],
                       &selected->expected, sizeof(selected->expected)) == 0 &&
                context->state.vnodes[0].local_reference_count == selected->root_references + 2 &&
                context->state.open_files[selected->parent_read - 1].reference_count == 2 &&
                context->state.open_files[selected->parent_write - 1].reference_count == 2 &&
                micros_vfs_state_validate(&context->state) == MICROS_VFS_CORE_OK);
    return true;
}

static bool hidden_process_is_immutable(struct vfs_spawn_test_context *context,
                                        const struct public_hidden_case *selected) {
    struct micros_vfs_control_request wrong_revert = selected->revert;
    struct micros_vfs_control_result_action action;
    enum micros_vfs_trusted_result trusted_result;

    context->snapshot = context->state;
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &selected->commit, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_STATE &&
                memcmp(&context->state, &context->snapshot, sizeof(context->state)) == 0);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &selected->abort, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_STATE &&
                memcmp(&context->state, &context->snapshot, sizeof(context->state)) == 0);
    EXPECT_TRUE(micros_vfs_detach(&context->state, selected->commit.child_endpoint,
                                  &context->fixture.io, &trusted_result) == MICROS_VFS_CORE_OK &&
                trusted_result == MICROS_VFS_TRUSTED_BUSY &&
                memcmp(&context->state, &context->snapshot, sizeof(context->state)) == 0);
    EXPECT_TRUE(micros_vfs_share_descriptor(&context->state, selected->commit.child_endpoint, 4,
                                            context->parent_endpoint, 3,
                                            &trusted_result) == MICROS_VFS_CORE_OK &&
                trusted_result == MICROS_VFS_TRUSTED_BUSY &&
                memcmp(&context->state, &context->snapshot, sizeof(context->state)) == 0);
    EXPECT_TRUE(micros_vfs_share_descriptor(&context->state, context->parent_endpoint, 0,
                                            selected->commit.child_endpoint, 3,
                                            &trusted_result) == MICROS_VFS_CORE_OK &&
                trusted_result == MICROS_VFS_TRUSTED_BUSY &&
                memcmp(&context->state, &context->snapshot, sizeof(context->state)) == 0);
    wrong_revert.child_endpoint ^= UINT32_C(1);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &wrong_revert, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_STATE &&
                memcmp(&context->state, &context->snapshot, sizeof(context->state)) == 0);
    context->state.spawn.child_endpoint ^= UINT32_C(1);
    EXPECT_TRUE(micros_vfs_state_validate(&context->state) == MICROS_VFS_CORE_ERROR_INVARIANT);
    context->state = context->snapshot;
    context->state.processes[selected->process_reference - 1].descriptors[4] =
        context->state.processes[selected->process_reference - 1].descriptors[5];
    context->state.processes[selected->process_reference - 1].descriptors[5] =
        context->snapshot.processes[selected->process_reference - 1].descriptors[4];
    EXPECT_TRUE(micros_vfs_state_validate(&context->state) == MICROS_VFS_CORE_ERROR_INVARIANT);
    context->state = context->snapshot;
    return true;
}

static bool hidden_revert_is_exact(struct vfs_spawn_test_context *context,
                                   const struct public_hidden_case *selected) {
    struct micros_vfs_control_result_action action;
    struct micros_vfs_process_record free_process = {0};

    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &selected->revert, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.active && action.command == MICROS_VFS_CONTROL_REVERT_HIDDEN &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK &&
                context->state.spawn.phase == MICROS_VFS_SPAWN_DESCRIPTORS_PREPARED &&
                vfs_spawn_test_active_process_count(context) == 1 &&
                memcmp(&context->state.processes[selected->process_reference - 1], &free_process,
                       sizeof(free_process)) == 0 &&
                context->state.vnodes[0].local_reference_count == selected->root_references + 2 &&
                context->state.open_files[selected->parent_read - 1].reference_count == 2 &&
                context->state.open_files[selected->parent_write - 1].reference_count == 2 &&
                micros_vfs_state_validate(&context->state) == MICROS_VFS_CORE_OK);
    context->snapshot = context->state;
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &selected->revert, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_STATE &&
                memcmp(&context->state, &context->snapshot, sizeof(context->state)) == 0);
    return true;
}

static bool hidden_release_retains_child(struct vfs_spawn_test_context *context,
                                         const struct public_hidden_case *selected) {
    struct micros_vfs_control_result_action action;
    enum micros_vfs_trusted_result trusted_result;

    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &selected->commit, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &selected->release, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK &&
                context->state.spawn.phase == MICROS_VFS_SPAWN_IDLE &&
                vfs_spawn_test_active_process_count(context) == 2 &&
                memcmp(&context->state.processes[selected->process_reference - 1],
                       &selected->expected, sizeof(selected->expected)) == 0 &&
                context->state.last_descriptor_token == selected->descriptor_token &&
                context->fixture.backend_grant_count == 0 &&
                micros_vfs_state_validate(&context->state) == MICROS_VFS_CORE_OK);
    EXPECT_TRUE(micros_vfs_detach(&context->state, selected->commit.child_endpoint,
                                  &context->fixture.io, &trusted_result) == MICROS_VFS_CORE_OK &&
                trusted_result == MICROS_VFS_TRUSTED_OK &&
                vfs_spawn_test_active_process_count(context) == 1 &&
                context->state.vnodes[0].local_reference_count == selected->root_references &&
                context->state.open_files[selected->parent_read - 1].reference_count == 1 &&
                context->state.open_files[selected->parent_write - 1].reference_count == 1 &&
                micros_vfs_state_validate(&context->state) == MICROS_VFS_CORE_OK);
    return true;
}

bool vfs_spawn_publication_commit_revert_release_run(struct vfs_spawn_test_context *context) {
    struct public_hidden_case selected;

    return (prepare_public_hidden_case(context, &selected) &&
            hidden_commit_rejections_are_atomic(context, &selected) &&
            hidden_commit_is_exact(context, &selected) &&
            hidden_process_is_immutable(context, &selected) &&
            hidden_revert_is_exact(context, &selected) &&
            hidden_release_retains_child(context, &selected));
}

bool vfs_spawn_publication_trusted_init_release_run(struct vfs_spawn_test_context *context) {
    static const micros_endpoint_t child_endpoint = UINT32_C(0x00003009);
    struct micros_vfs_control_request request;
    struct micros_vfs_control_request descriptors;
    struct micros_vfs_control_request commit;
    struct micros_vfs_control_request release;
    struct micros_vfs_control_result_action action;
    enum micros_vfs_trusted_result trusted_result;
    uint16_t process_reference;
    uint32_t root_references;
    uint64_t descriptor_token;
    size_t request_size;

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    vfs_spawn_test_fill_executable(context, 64);
    EXPECT_TRUE(vfs_test_fixture_add_root_file_with_mode(&context->fixture, "child",
                                                         MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
                                                         context->executable_bytes, 64));
    request_size = vfs_spawn_test_build_request(context, "/child");
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x118));
    request.parent_endpoint = 0;
    request.trusted_init = true;
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK);
    release = vfs_spawn_test_release_request(context, UINT64_C(0x118), action.image_token);
    root_references = context->state.vnodes[0].local_reference_count;
    descriptors =
        vfs_spawn_test_prepare_descriptors_request(context, UINT64_C(0x118), action.image_token);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &descriptors, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK);
    descriptor_token = action.descriptor_token;
    process_reference = context->state.spawn.child_process_reference;
    commit =
        vfs_spawn_test_hidden_request(context, MICROS_VFS_CONTROL_COMMIT_HIDDEN, UINT64_C(0x118),
                                      release.image_token, descriptor_token, child_endpoint);
    EXPECT_TRUE(
        micros_vfs_handle_control(&context->state, &commit, &context->fixture.io, &action) ==
            MICROS_VFS_CORE_OK &&
        action.result == MICROS_VFS_CONTROL_RESULT_OK &&
        context->state.processes[process_reference - 1].root_vnode == 1 &&
        context->state.processes[process_reference - 1].working_directory_vnode == 1 &&
        context->state.processes[process_reference - 1].descriptors[0] != 0 &&
        context->state.processes[process_reference - 1].descriptors[1] != 0 &&
        context->state.processes[process_reference - 1].descriptors[2] != 0 &&
        vfs_spawn_test_active_console_open_file_count(context, MICROS_VFS_ACCESS_READ) == 2 &&
        vfs_spawn_test_active_console_open_file_count(context, MICROS_VFS_ACCESS_WRITE) == 4 &&
        micros_vfs_state_validate(&context->state) == MICROS_VFS_CORE_OK);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &release, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK &&
                context->state.spawn.phase == MICROS_VFS_SPAWN_IDLE &&
                context->state.last_descriptor_token == descriptor_token &&
                micros_vfs_state_validate(&context->state) == MICROS_VFS_CORE_OK);
    EXPECT_TRUE(
        micros_vfs_detach(&context->state, child_endpoint, &context->fixture.io, &trusted_result) ==
            MICROS_VFS_CORE_OK &&
        trusted_result == MICROS_VFS_TRUSTED_OK &&
        context->state.vnodes[0].local_reference_count == root_references &&
        vfs_spawn_test_active_console_open_file_count(context, MICROS_VFS_ACCESS_READ) == 1 &&
        vfs_spawn_test_active_console_open_file_count(context, MICROS_VFS_ACCESS_WRITE) == 2 &&
        micros_vfs_state_validate(&context->state) == MICROS_VFS_CORE_OK);
    return true;
}
