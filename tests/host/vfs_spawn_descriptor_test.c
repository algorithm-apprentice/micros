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

struct descriptor_prepare_case {
    struct micros_vfs_control_request descriptors;
    struct micros_vfs_control_request abort;
    struct micros_vfs_control_request release;
    struct micros_vfs_control_result_action action;
    uint16_t parent_read;
    uint16_t parent_write;
    uint32_t root_references;
    uint64_t image_token;
    uint64_t descriptor_token;
};

static bool prepare_descriptor_image(struct vfs_spawn_test_context *context,
                                     struct descriptor_prepare_case *test_case) {
    static const struct micros_spawn_descriptor_action actions[] = {
        {
            .source = 0,
            .destination = 0,
            .operation = MICROS_SPAWN_DESCRIPTOR_DUPLICATE,
        },
        {
            .source = 1,
            .destination = 1,
            .operation = MICROS_SPAWN_DESCRIPTOR_DUPLICATE,
        },
        {
            .source = 1,
            .destination = 2,
            .operation = MICROS_SPAWN_DESCRIPTOR_DUPLICATE,
        },
    };
    struct micros_vfs_control_request request;
    size_t request_size;

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    vfs_spawn_test_fill_executable(context, 64);
    EXPECT_TRUE(vfs_test_fixture_add_root_file_with_mode(&context->fixture, "child",
                                                         MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
                                                         context->executable_bytes, 64));
    request_size = vfs_spawn_test_build_request(context, "/child");
    vfs_spawn_test_set_descriptor_actions(context, actions, sizeof(actions) / sizeof(actions[0]));
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x111));
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &test_case->action) == MICROS_VFS_CORE_OK &&
                test_case->action.result == MICROS_VFS_CONTROL_RESULT_OK);
    test_case->image_token = test_case->action.image_token;
    test_case->parent_read = context->state.processes[0].descriptors[0];
    test_case->parent_write = context->state.processes[0].descriptors[1];
    test_case->root_references = context->state.vnodes[0].local_reference_count;
    test_case->descriptors = vfs_spawn_test_prepare_descriptors_request(context, UINT64_C(0x111),
                                                                        test_case->image_token);
    return true;
}

static bool reserve_descriptor_snapshot(struct vfs_spawn_test_context *context,
                                        struct descriptor_prepare_case *test_case) {
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &test_case->descriptors,
                                          &context->fixture.io,
                                          &test_case->action) == MICROS_VFS_CORE_OK &&
                test_case->action.active && test_case->action.reply_token == UINT64_C(0x73) &&
                test_case->action.command == MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS &&
                test_case->action.result == MICROS_VFS_CONTROL_RESULT_OK &&
                test_case->action.image_token == 0 && test_case->action.executable_grant == 0 &&
                test_case->action.executable_size == 0 && test_case->action.request_grant == 0 &&
                test_case->action.request_size == 0 && test_case->action.descriptor_token != 0 &&
                context->state.spawn.phase == MICROS_VFS_SPAWN_DESCRIPTORS_PREPARED &&
                vfs_spawn_test_active_process_count(context) == 1 &&
                context->state.vnodes[0].local_reference_count == test_case->root_references + 2 &&
                context->state.open_files[test_case->parent_read - 1].reference_count == 2 &&
                context->state.open_files[test_case->parent_write - 1].reference_count == 3 &&
                context->fixture.backend_grant_count == 2 &&
                micros_vfs_state_validate(&context->state) == MICROS_VFS_CORE_OK);
    test_case->descriptor_token = test_case->action.descriptor_token;
    return true;
}

static bool duplicate_prepare_release_and_parent_detach_are_rejected(
    struct vfs_spawn_test_context *context, struct descriptor_prepare_case *test_case) {
    enum micros_vfs_trusted_result trusted_result;

    context->snapshot = context->state;
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &test_case->descriptors,
                                          &context->fixture.io,
                                          &test_case->action) == MICROS_VFS_CORE_OK &&
                test_case->action.result == MICROS_VFS_CONTROL_RESULT_STATE &&
                memcmp(&context->state, &context->snapshot, sizeof(context->state)) == 0);
    test_case->release =
        vfs_spawn_test_release_request(context, UINT64_C(0x111), test_case->image_token);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &test_case->release,
                                          &context->fixture.io,
                                          &test_case->action) == MICROS_VFS_CORE_OK &&
                test_case->action.result == MICROS_VFS_CONTROL_RESULT_STATE &&
                memcmp(&context->state, &context->snapshot, sizeof(context->state)) == 0);
    EXPECT_TRUE(micros_vfs_detach(&context->state, context->parent_endpoint, &context->fixture.io,
                                  &trusted_result) == MICROS_VFS_CORE_OK &&
                trusted_result == MICROS_VFS_TRUSTED_BUSY &&
                memcmp(&context->state, &context->snapshot, sizeof(context->state)) == 0);
    return true;
}

static bool
abort_descriptor_snapshot_and_verify_rollback(struct vfs_spawn_test_context *context,
                                              struct descriptor_prepare_case *test_case) {
    test_case->abort = vfs_spawn_test_abort_descriptors_request(
        context, UINT64_C(0x111), test_case->image_token, test_case->descriptor_token);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &test_case->abort, &context->fixture.io,
                                          &test_case->action) == MICROS_VFS_CORE_OK &&
                test_case->action.active && test_case->action.reply_token == UINT64_C(0x74) &&
                test_case->action.command == MICROS_VFS_CONTROL_ABORT_DESCRIPTORS &&
                test_case->action.result == MICROS_VFS_CONTROL_RESULT_OK &&
                test_case->action.descriptor_token == 0 &&
                context->state.spawn.phase == MICROS_VFS_SPAWN_IMAGE_PREPARED &&
                context->state.vnodes[0].local_reference_count == test_case->root_references &&
                context->state.open_files[test_case->parent_read - 1].reference_count == 1 &&
                context->state.open_files[test_case->parent_write - 1].reference_count == 1 &&
                context->fixture.backend_grant_count == 2 &&
                micros_vfs_state_validate(&context->state) == MICROS_VFS_CORE_OK);
    return true;
}

static bool reprepare_abort_and_release_are_exact(struct vfs_spawn_test_context *context,
                                                  struct descriptor_prepare_case *test_case) {
    context->snapshot = context->state;
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &test_case->abort, &context->fixture.io,
                                          &test_case->action) == MICROS_VFS_CORE_OK &&
                test_case->action.result == MICROS_VFS_CONTROL_RESULT_STATE &&
                memcmp(&context->state, &context->snapshot, sizeof(context->state)) == 0);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &test_case->descriptors,
                                          &context->fixture.io,
                                          &test_case->action) == MICROS_VFS_CORE_OK &&
                test_case->action.result == MICROS_VFS_CONTROL_RESULT_OK &&
                test_case->action.descriptor_token == test_case->descriptor_token + 1);
    test_case->abort.descriptor_token = test_case->action.descriptor_token;
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &test_case->abort, &context->fixture.io,
                                          &test_case->action) == MICROS_VFS_CORE_OK &&
                test_case->action.result == MICROS_VFS_CONTROL_RESULT_OK);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &test_case->release,
                                          &context->fixture.io,
                                          &test_case->action) == MICROS_VFS_CORE_OK &&
                test_case->action.result == MICROS_VFS_CONTROL_RESULT_OK);
    return true;
}

bool vfs_spawn_descriptor_prepare_abort_run(struct vfs_spawn_test_context *context) {
    struct descriptor_prepare_case test_case;

    return prepare_descriptor_image(context, &test_case) &&
           reserve_descriptor_snapshot(context, &test_case) &&
           duplicate_prepare_release_and_parent_detach_are_rejected(context, &test_case) &&
           abort_descriptor_snapshot_and_verify_rollback(context, &test_case) &&
           reprepare_abort_and_release_are_exact(context, &test_case);
}

struct descriptor_capacity_case {
    struct micros_vfs_control_request descriptors;
    struct micros_vfs_control_request abort;
    struct micros_vfs_control_result_action action;
    uint64_t image_token;
};

static bool
prepare_capacity_image_and_fill_public_processes(struct vfs_spawn_test_context *context,
                                                 struct descriptor_capacity_case *test_case) {
    struct micros_vfs_control_request request;
    enum micros_vfs_trusted_result trusted_result;
    size_t request_size;
    size_t index;

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    vfs_spawn_test_fill_executable(context, 64);
    EXPECT_TRUE(vfs_test_fixture_add_root_file_with_mode(&context->fixture, "child",
                                                         MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
                                                         context->executable_bytes, 64));
    request_size = vfs_spawn_test_build_request(context, "/child");
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x114));
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &test_case->action) == MICROS_VFS_CORE_OK &&
                test_case->action.result == MICROS_VFS_CONTROL_RESULT_OK);
    test_case->image_token = test_case->action.image_token;
    for (index = 0; index < 62; ++index) {
        EXPECT_TRUE(micros_vfs_attach_console(&context->state,
                                              UINT32_C(0x00003000) + (uint32_t)index, 0,
                                              &trusted_result) == MICROS_VFS_CORE_OK &&
                    trusted_result == MICROS_VFS_TRUSTED_OK);
    }
    EXPECT_TRUE(vfs_spawn_test_active_process_count(context) == 63);
    test_case->descriptors = vfs_spawn_test_prepare_descriptors_request(context, UINT64_C(0x114),
                                                                        test_case->image_token);
    return true;
}

static bool
reserve_hidden_process_and_prove_public_capacity(struct vfs_spawn_test_context *context,
                                                 struct descriptor_capacity_case *test_case) {
    enum micros_vfs_trusted_result trusted_result;
    const micros_endpoint_t final_endpoint = UINT32_C(0x0000303e);

    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &test_case->descriptors,
                                          &context->fixture.io,
                                          &test_case->action) == MICROS_VFS_CORE_OK &&
                test_case->action.result == MICROS_VFS_CONTROL_RESULT_OK &&
                context->state.processes[context->state.spawn.child_process_reference - 1].state ==
                    MICROS_VFS_PROCESS_FREE &&
                vfs_spawn_test_active_process_count(context) == 63);
    test_case->abort = vfs_spawn_test_abort_descriptors_request(
        context, UINT64_C(0x114), test_case->image_token, test_case->action.descriptor_token);
    context->snapshot = context->state;
    EXPECT_TRUE(micros_vfs_attach_console(&context->state, final_endpoint, 0, &trusted_result) ==
                    MICROS_VFS_CORE_OK &&
                trusted_result == MICROS_VFS_TRUSTED_NO_SPACE &&
                memcmp(&context->state, &context->snapshot, sizeof(context->state)) == 0);
    return true;
}

static bool
abort_reservation_and_fill_final_process_slot(struct vfs_spawn_test_context *context,
                                              struct descriptor_capacity_case *test_case) {
    enum micros_vfs_trusted_result trusted_result;
    const micros_endpoint_t final_endpoint = UINT32_C(0x0000303e);

    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &test_case->abort, &context->fixture.io,
                                          &test_case->action) == MICROS_VFS_CORE_OK &&
                test_case->action.result == MICROS_VFS_CONTROL_RESULT_OK);
    EXPECT_TRUE(micros_vfs_attach_console(&context->state, final_endpoint, 0, &trusted_result) ==
                    MICROS_VFS_CORE_OK &&
                trusted_result == MICROS_VFS_TRUSTED_OK &&
                vfs_spawn_test_active_process_count(context) == 64);
    return true;
}

static bool
descriptor_capacity_failure_is_atomic_and_releasable(struct vfs_spawn_test_context *context,
                                                     struct descriptor_capacity_case *test_case) {
    struct micros_vfs_control_request release;

    context->snapshot = context->state;
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &test_case->descriptors,
                                          &context->fixture.io,
                                          &test_case->action) == MICROS_VFS_CORE_OK &&
                test_case->action.result == MICROS_VFS_CONTROL_RESULT_NO_SPACE &&
                test_case->action.descriptor_token == 0 &&
                memcmp(&context->state, &context->snapshot, sizeof(context->state)) == 0);
    release = vfs_spawn_test_release_request(context, UINT64_C(0x114), test_case->image_token);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &release, &context->fixture.io,
                                          &test_case->action) == MICROS_VFS_CORE_OK &&
                test_case->action.result == MICROS_VFS_CONTROL_RESULT_OK);
    return true;
}

bool vfs_spawn_descriptor_capacity_run(struct vfs_spawn_test_context *context) {
    struct descriptor_capacity_case test_case;

    return prepare_capacity_image_and_fill_public_processes(context, &test_case) &&
           reserve_hidden_process_and_prove_public_capacity(context, &test_case) &&
           abort_reservation_and_fill_final_process_slot(context, &test_case) &&
           descriptor_capacity_failure_is_atomic_and_releasable(context, &test_case);
}

bool vfs_spawn_descriptor_token_exhaustion_run(struct vfs_spawn_test_context *context) {
    struct micros_vfs_control_request request;
    struct micros_vfs_control_request descriptors;
    struct micros_vfs_control_request abort;
    struct micros_vfs_control_request release;
    struct micros_vfs_control_result_action action;
    size_t request_size;
    uint64_t image_token;

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    vfs_spawn_test_fill_executable(context, 64);
    EXPECT_TRUE(vfs_test_fixture_add_root_file_with_mode(&context->fixture, "child",
                                                         MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
                                                         context->executable_bytes, 64));
    request_size = vfs_spawn_test_build_request(context, "/child");
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x115));
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK);
    image_token = action.image_token;
    context->state.last_descriptor_token = UINT64_MAX - 1;
    EXPECT_TRUE(micros_vfs_state_validate(&context->state) == MICROS_VFS_CORE_OK);
    descriptors = vfs_spawn_test_prepare_descriptors_request(context, UINT64_C(0x115), image_token);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &descriptors, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK &&
                action.descriptor_token == UINT64_MAX);
    abort =
        vfs_spawn_test_abort_descriptors_request(context, UINT64_C(0x115), image_token, UINT64_MAX);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &abort, &context->fixture.io, &action) ==
                    MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK);
    context->snapshot = context->state;
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &descriptors, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_ERROR_INVARIANT &&
                !action.active &&
                memcmp(&context->state, &context->snapshot, sizeof(context->state)) == 0);
    release = vfs_spawn_test_release_request(context, UINT64_C(0x115), image_token);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &release, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK);
    return true;
}

bool vfs_spawn_descriptor_state_corruption_run(struct vfs_spawn_test_context *context) {
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
    struct micros_vfs_control_request request;
    struct micros_vfs_control_request descriptors;
    struct micros_vfs_control_result_action action;
    size_t request_size;

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    vfs_spawn_test_fill_executable(context, 64);
    EXPECT_TRUE(vfs_test_fixture_add_root_file_with_mode(&context->fixture, "child",
                                                         MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
                                                         context->executable_bytes, 64));
    request_size = vfs_spawn_test_build_request(context, "/child");
    vfs_spawn_test_set_descriptor_actions(context, actions, sizeof(actions) / sizeof(actions[0]));
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x116));
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK);
    descriptors =
        vfs_spawn_test_prepare_descriptors_request(context, UINT64_C(0x116), action.image_token);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &descriptors, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK);
    context->snapshot = context->state;

    context->state.spawn.descriptor_token ^= UINT64_C(1);
    EXPECT_TRUE(micros_vfs_state_validate(&context->state) == MICROS_VFS_CORE_ERROR_INVARIANT);
    context->state = context->snapshot;
    context->state.spawn.child_descriptors[4] = context->snapshot.spawn.child_descriptors[5];
    context->state.spawn.child_descriptors[5] = context->snapshot.spawn.child_descriptors[4];
    EXPECT_TRUE(micros_vfs_state_validate(&context->state) == MICROS_VFS_CORE_ERROR_INVARIANT);
    context->state = context->snapshot;
    context->state.spawn.child_descriptors[3] = context->state.spawn.child_descriptors[4];
    EXPECT_TRUE(micros_vfs_state_validate(&context->state) == MICROS_VFS_CORE_ERROR_INVARIANT);
    context->state = context->snapshot;
    context->state.processes[context->state.spawn.child_process_reference - 1].state =
        MICROS_VFS_PROCESS_ACTIVE;
    EXPECT_TRUE(micros_vfs_state_validate(&context->state) == MICROS_VFS_CORE_ERROR_INVARIANT);
    context->state = context->snapshot;
    return true;
}

bool vfs_spawn_descriptor_failures_run(struct vfs_spawn_test_context *context) {
    static const struct micros_spawn_descriptor_action missing[] = {
        {
            .source = 15,
            .destination = 0,
            .operation = MICROS_SPAWN_DESCRIPTOR_DUPLICATE,
        },
    };
    struct micros_vfs_control_request request;
    struct micros_vfs_control_request descriptors;
    struct micros_vfs_control_request release;
    struct micros_vfs_control_result_action action;
    size_t request_size;
    uint64_t image_token;

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    vfs_spawn_test_fill_executable(context, 64);
    EXPECT_TRUE(vfs_test_fixture_add_root_file_with_mode(&context->fixture, "child",
                                                         MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
                                                         context->executable_bytes, 64));
    request_size = vfs_spawn_test_build_request(context, "/child");
    vfs_spawn_test_set_descriptor_actions(context, missing, sizeof(missing) / sizeof(missing[0]));
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x112));
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK);
    image_token = action.image_token;
    descriptors = vfs_spawn_test_prepare_descriptors_request(context, UINT64_C(0x112), image_token);
    context->snapshot = context->state;
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &descriptors, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_DESCRIPTOR &&
                memcmp(&context->state, &context->snapshot, sizeof(context->state)) == 0);
    descriptors.semantic_pid ^= UINT64_C(1);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &descriptors, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_STATE &&
                memcmp(&context->state, &context->snapshot, sizeof(context->state)) == 0);

    release = vfs_spawn_test_release_request(context, UINT64_C(0x112), image_token);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &release, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK);
    return true;
}

bool vfs_spawn_descriptor_trusted_init_run(struct vfs_spawn_test_context *context) {
    struct micros_vfs_control_request request;
    struct micros_vfs_control_request descriptors;
    struct micros_vfs_control_request abort;
    struct micros_vfs_control_request release;
    struct micros_vfs_control_result_action action;
    size_t request_size;
    uint32_t root_references;
    uint64_t image_token;

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    vfs_spawn_test_fill_executable(context, 64);
    EXPECT_TRUE(vfs_test_fixture_add_root_file_with_mode(&context->fixture, "child",
                                                         MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
                                                         context->executable_bytes, 64));
    request_size = vfs_spawn_test_build_request(context, "/child");
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x113));
    request.parent_endpoint = 0;
    request.trusted_init = true;
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK);
    image_token = action.image_token;
    root_references = context->state.vnodes[0].local_reference_count;
    descriptors = vfs_spawn_test_prepare_descriptors_request(context, UINT64_C(0x113), image_token);
    EXPECT_TRUE(
        micros_vfs_handle_control(&context->state, &descriptors, &context->fixture.io, &action) ==
            MICROS_VFS_CORE_OK &&
        action.result == MICROS_VFS_CONTROL_RESULT_OK && action.descriptor_token != 0 &&
        context->state.spawn.phase == MICROS_VFS_SPAWN_DESCRIPTORS_PREPARED &&
        vfs_spawn_test_active_process_count(context) == 1 &&
        vfs_spawn_test_active_console_open_file_count(context, MICROS_VFS_ACCESS_READ) == 2 &&
        vfs_spawn_test_active_console_open_file_count(context, MICROS_VFS_ACCESS_WRITE) == 4 &&
        context->state.vnodes[0].local_reference_count == root_references + 2 &&
        micros_vfs_state_validate(&context->state) == MICROS_VFS_CORE_OK);

    abort = vfs_spawn_test_abort_descriptors_request(context, UINT64_C(0x113), image_token,
                                                     action.descriptor_token);
    EXPECT_TRUE(
        micros_vfs_handle_control(&context->state, &abort, &context->fixture.io, &action) ==
            MICROS_VFS_CORE_OK &&
        action.result == MICROS_VFS_CONTROL_RESULT_OK &&
        vfs_spawn_test_active_process_count(context) == 1 &&
        vfs_spawn_test_active_console_open_file_count(context, MICROS_VFS_ACCESS_READ) == 1 &&
        vfs_spawn_test_active_console_open_file_count(context, MICROS_VFS_ACCESS_WRITE) == 2 &&
        context->state.vnodes[0].local_reference_count == root_references &&
        micros_vfs_state_validate(&context->state) == MICROS_VFS_CORE_OK);

    release = vfs_spawn_test_release_request(context, UINT64_C(0x113), image_token);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &release, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK);
    return true;
}

bool vfs_spawn_descriptor_parent_detach_run(struct vfs_spawn_test_context *context) {
    struct micros_vfs_control_request request;
    struct micros_vfs_control_request release;
    struct micros_vfs_control_result_action action;
    enum micros_vfs_trusted_result trusted_result;
    size_t request_size;
    size_t ramfs_calls;
    size_t backend_revoke_calls;
    size_t putnode_calls;

    EXPECT_TRUE(vfs_spawn_test_initialize_fixture(context));
    vfs_spawn_test_fill_executable(context, 64);
    EXPECT_TRUE(vfs_test_fixture_add_root_file_with_mode(&context->fixture, "child",
                                                         MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
                                                         context->executable_bytes, 64));
    request_size = vfs_spawn_test_build_request(context, "/child");
    request = vfs_spawn_test_prepare_request(context, request_size, UINT64_C(0x102));
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &request, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK);

    context->snapshot = context->state;
    ramfs_calls = context->fixture.ramfs_calls;
    backend_revoke_calls = context->fixture.backend_revoke_calls;
    putnode_calls = context->fixture.putnode_calls;
    EXPECT_TRUE(micros_vfs_detach(&context->state, context->parent_endpoint, &context->fixture.io,
                                  &trusted_result) == MICROS_VFS_CORE_OK &&
                trusted_result == MICROS_VFS_TRUSTED_BUSY &&
                memcmp(&context->state, &context->snapshot, sizeof(context->state)) == 0 &&
                context->fixture.ramfs_calls == ramfs_calls &&
                context->fixture.backend_revoke_calls == backend_revoke_calls &&
                context->fixture.putnode_calls == putnode_calls &&
                context->fixture.backend_grant_count == 2);

    release = vfs_spawn_test_release_request(context, UINT64_C(0x102), action.image_token);
    EXPECT_TRUE(micros_vfs_handle_control(&context->state, &release, &context->fixture.io,
                                          &action) == MICROS_VFS_CORE_OK &&
                action.result == MICROS_VFS_CONTROL_RESULT_OK &&
                micros_vfs_state_validate(&context->state) == MICROS_VFS_CORE_OK);
    return true;
}
