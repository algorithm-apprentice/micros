#include "tests/host/vfs_core_test_fixture.h"
#include "tests/host/vfs_test_suites.h"

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

bool vfs_core_namespace_mount_run(
    struct vfs_core_test_context *context
)
{
    memset(&context->backend, 0, sizeof(context->backend));
    context->snapshot = context->state;
    context->backend.mount_node =
        UINT64_C(0x0000000100000001);
    EXPECT_TRUE(
        micros_vfs_mount(
            &context->state,
            &context->backend_io
        ) == MICROS_VFS_CORE_ERROR_INVARIANT
        && memcmp(
            &context->state,
            &context->snapshot,
            sizeof(context->state)
        ) == 0
        && context->backend.mount_calls == 1
    );
    memset(&context->backend, 0, sizeof(context->backend));
    EXPECT_TRUE(
        micros_vfs_mount(
            &context->state,
            &context->backend_io
        ) == MICROS_VFS_CORE_OK
        && context->backend.mount_calls == 1
        && context->state.phase == MICROS_VFS_PHASE_MOUNTED
        && context->state.vnodes[0].state
            == MICROS_VFS_VNODE_ACTIVE
        && context->state.vnodes[0].mount_root
        && context->state.vnodes[0].local_reference_count == 1
        && context->state.vnodes[0].backend_reference_count == 1
        && micros_vfs_state_validate(&context->state)
            == MICROS_VFS_CORE_OK
    );
    return true;
}

bool vfs_core_namespace_path_grant_precedence_run(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    micros_grant_t stale_path;
    size_t descriptor;
    static const uint8_t root_path[] = "/";

    EXPECT_TRUE(vfs_core_test_initialize_full(context));
    for (
        descriptor = 3;
        descriptor < MICROS_VFS_DESCRIPTOR_CAPACITY;
        ++descriptor
    ) {
        selected = vfs_core_test_path_request(
            context,
            MICROS_VFS_MESSAGE_OPEN,
            "/etc/motd",
            MICROS_VFS_OPEN_READ,
            0
        );
        EXPECT_TRUE(
            micros_vfs_handle_request(
                &context->state,
                &selected,
                &context->fixture.io,
                &action
            ) == MICROS_VFS_CORE_OK
            && action.result == MICROS_VFS_RESULT_OK
            && action.descriptor == descriptor
        );
    }
    stale_path = vfs_test_fixture_add_application_grant(
        &context->fixture,
        context->application_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        root_path,
        sizeof(root_path)
    );
    EXPECT_TRUE(
        stale_path != MICROS_GRANT_NONE
        && vfs_test_fixture_revoke_application_grant(
            &context->fixture,
            stale_path
        )
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_OPEN
    );
    selected.grant = stale_path;
    selected.path_length = sizeof(root_path);
    selected.open_flags = MICROS_VFS_OPEN_READ;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_GRANT
    );
    return true;
}

bool vfs_core_namespace_mutation_begin(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;

    EXPECT_TRUE(vfs_core_test_initialize_full(context));
    selected = vfs_core_test_path_request(
        context,
        MICROS_VFS_MESSAGE_MKDIR,
        "tmp",
        0,
        UINT32_C(0755)
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && vfs_test_fixture_find_path(
            &context->fixture,
            "/tmp"
        ) != 0
        && context->state.vnodes[0].local_reference_count == 3
        && context->fixture.backend_grant_count == 0
    );
    selected = vfs_core_test_path_request(
        context,
        MICROS_VFS_MESSAGE_MKDIR,
        "/tmp/",
        0,
        UINT32_C(0700)
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_EXISTS
    );
    selected = vfs_core_test_path_request(
        context,
        MICROS_VFS_MESSAGE_OPEN,
        "tmp/note",
        MICROS_VFS_OPEN_READ
            | MICROS_VFS_OPEN_WRITE
            | MICROS_VFS_OPEN_CREATE,
        UINT32_C(0640)
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && action.descriptor == 3
        && action.mode
            == (
                MICROS_RAMFS_MODE_REGULAR
                | UINT32_C(0640)
            )
        && (
            context->note = vfs_test_fixture_find_path(
                &context->fixture,
                "/tmp/note"
            )
        ) != 0
    );
    return true;
}

bool vfs_core_namespace_mutation_continue(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;

    selected = vfs_core_test_path_request(
        context,
        MICROS_VFS_MESSAGE_OPEN,
        "/tmp/note",
        MICROS_VFS_OPEN_READ | MICROS_VFS_OPEN_EXCLUSIVE
            | MICROS_VFS_OPEN_CREATE,
        UINT32_C(0600)
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_EXISTS
    );
    selected = vfs_core_test_path_request(
        context,
        MICROS_VFS_MESSAGE_OPEN,
        "/tmp/exclusive",
        MICROS_VFS_OPEN_READ | MICROS_VFS_OPEN_EXCLUSIVE
            | MICROS_VFS_OPEN_CREATE,
        UINT32_C(0600)
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && action.descriptor == 3
        && vfs_test_fixture_find_path(
            &context->fixture,
            "/tmp/exclusive"
        ) != 0
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_CLOSE
    );
    selected.descriptor = 3;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
    );
    selected = vfs_core_test_path_request(
        context,
        MICROS_VFS_MESSAGE_OPEN,
        "/tmp/missing/",
        MICROS_VFS_OPEN_READ | MICROS_VFS_OPEN_CREATE,
        UINT32_C(0600)
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_NOT_DIRECTORY
        && vfs_test_fixture_find_path(
            &context->fixture,
            "/tmp/missing"
        ) == 0
    );
    selected = vfs_core_test_path_request(
        context,
        MICROS_VFS_MESSAGE_CHDIR,
        "/tmp",
        0,
        0
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && context->state.processes[0].working_directory_vnode != 1
    );
    selected = vfs_core_test_path_request(
        context,
        MICROS_VFS_MESSAGE_OPEN,
        "note",
        MICROS_VFS_OPEN_READ,
        0
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && action.descriptor == 3
    );
    return true;
}

bool vfs_core_namespace_mutation_finish(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected =
        vfs_core_test_path_request(
            context,
            MICROS_VFS_MESSAGE_CHDIR,
            "/",
            0,
            0
        );
    struct micros_vfs_result_action action;

    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && context->state.processes[0].working_directory_vnode == 1
        && micros_vfs_state_validate(&context->state)
            == MICROS_VFS_CORE_OK
        && context->note
            == vfs_test_fixture_find_path(
                &context->fixture,
                "/tmp/note"
            )
    );
    return true;
}
