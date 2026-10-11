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

static const micros_endpoint_t first_endpoint =
    UINT32_C(0x00002007);
static const micros_endpoint_t second_endpoint =
    UINT32_C(0x00002008);

static bool attach_arguments_are_rejected(
    struct vfs_core_test_context *context
)
{
    enum micros_vfs_trusted_result result;

    context->snapshot = context->state;
    EXPECT_TRUE(
        micros_vfs_attach_console(
            &context->state,
            context->fixture.vm_endpoint,
            0,
            &result
        ) == MICROS_VFS_CORE_ERROR_ARGUMENT
        && memcmp(
            &context->state,
            &context->snapshot,
            sizeof(context->state)
        ) == 0
    );
    EXPECT_TRUE(
        micros_vfs_attach_console(
            &context->state,
            context->fixture.pm_endpoint,
            0,
            &result
        ) == MICROS_VFS_CORE_ERROR_ARGUMENT
        && memcmp(
            &context->state,
            &context->snapshot,
            sizeof(context->state)
        ) == 0
    );
    return true;
}

static bool attach_processes_and_idempotence(
    struct vfs_core_test_context *context
)
{
    enum micros_vfs_trusted_result result;

    EXPECT_TRUE(
        micros_vfs_attach_console(
            &context->state,
            first_endpoint,
            0,
            &result
        ) == MICROS_VFS_CORE_OK
        && result == MICROS_VFS_TRUSTED_OK
        && context->state.processes[0].endpoint == first_endpoint
        && context->state.processes[0].root_vnode == 1
        && context->state.processes[0].working_directory_vnode == 1
        && context->state.processes[0].descriptors[0] == 1
        && context->state.processes[0].descriptors[1] == 2
        && context->state.processes[0].descriptors[2] == 3
        && context->state.open_files[0].kind
            == MICROS_VFS_OBJECT_CONSOLE
        && context->state.open_files[0].access
            == MICROS_VFS_ACCESS_READ
        && context->state.open_files[1].access
            == MICROS_VFS_ACCESS_WRITE
        && context->state.open_files[2].access
            == MICROS_VFS_ACCESS_WRITE
        && context->state.vnodes[0].local_reference_count == 3
    );
    context->snapshot = context->state;
    EXPECT_TRUE(
        micros_vfs_attach_console(
            &context->state,
            first_endpoint,
            0,
            &result
        ) == MICROS_VFS_CORE_OK
        && result == MICROS_VFS_TRUSTED_EXISTS
        && memcmp(
            &context->state,
            &context->snapshot,
            sizeof(context->state)
        ) == 0
    );
    EXPECT_TRUE(
        micros_vfs_attach_console(
            &context->state,
            second_endpoint,
            0,
            &result
        ) == MICROS_VFS_CORE_OK
        && result == MICROS_VFS_TRUSTED_OK
        && context->state.processes[1].endpoint == second_endpoint
        && context->state.vnodes[0].local_reference_count == 5
    );
    return true;
}

static bool descriptor_sharing_and_collision_are_exact(
    struct vfs_core_test_context *context
)
{
    enum micros_vfs_trusted_result result;

    EXPECT_TRUE(
        micros_vfs_share_descriptor(
            &context->state,
            first_endpoint,
            1,
            second_endpoint,
            3,
            &result
        ) == MICROS_VFS_CORE_OK
        && result == MICROS_VFS_TRUSTED_OK
        && context->state.processes[1].descriptors[3] == 2
        && context->state.open_files[1].reference_count == 2
        && micros_vfs_state_validate(&context->state)
            == MICROS_VFS_CORE_OK
    );
    context->snapshot = context->state;
    EXPECT_TRUE(
        micros_vfs_share_descriptor(
            &context->state,
            first_endpoint,
            1,
            second_endpoint,
            2,
            &result
        ) == MICROS_VFS_CORE_OK
        && result == MICROS_VFS_TRUSTED_EXISTS
        && memcmp(
            &context->state,
            &context->snapshot,
            sizeof(context->state)
        ) == 0
    );
    return true;
}

static bool detach_processes_and_idempotence_are_exact(
    struct vfs_core_test_context *context
)
{
    enum micros_vfs_trusted_result result;

    EXPECT_TRUE(
        micros_vfs_detach(
            &context->state,
            first_endpoint,
            &context->backend_io,
            &result
        ) == MICROS_VFS_CORE_OK
        && result == MICROS_VFS_TRUSTED_OK
        && context->state.processes[0].state
            == MICROS_VFS_PROCESS_FREE
        && context->state.open_files[0].state
            == MICROS_VFS_OPEN_FILE_FREE
        && context->state.open_files[1].reference_count == 1
        && context->state.open_files[2].state
            == MICROS_VFS_OPEN_FILE_FREE
        && context->state.vnodes[0].local_reference_count == 3
        && context->backend.putnode_calls == 0
        && micros_vfs_state_validate(&context->state)
            == MICROS_VFS_CORE_OK
    );
    EXPECT_TRUE(
        micros_vfs_detach(
            &context->state,
            second_endpoint,
            &context->backend_io,
            &result
        ) == MICROS_VFS_CORE_OK
        && result == MICROS_VFS_TRUSTED_OK
        && context->state.vnodes[0].local_reference_count == 1
        && micros_vfs_state_validate(&context->state)
            == MICROS_VFS_CORE_OK
    );
    context->snapshot = context->state;
    EXPECT_TRUE(
        micros_vfs_detach(
            &context->state,
            second_endpoint,
            &context->backend_io,
            &result
        ) == MICROS_VFS_CORE_OK
        && result == MICROS_VFS_TRUSTED_NOT_FOUND
        && memcmp(
            &context->state,
            &context->snapshot,
            sizeof(context->state)
        ) == 0
    );
    return true;
}

bool vfs_core_process_attach_share_detach_run(
    struct vfs_core_test_context *context
)
{
    return (
        attach_arguments_are_rejected(context)
        && attach_processes_and_idempotence(context)
        && descriptor_sharing_and_collision_are_exact(context)
        && detach_processes_and_idempotence_are_exact(context)
    );
}

bool vfs_core_process_trusted_result_validation_run(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    enum micros_vfs_trusted_result result =
        MICROS_VFS_TRUSTED_EXISTS;

    EXPECT_TRUE(vfs_core_test_initialize_full(context));
    selected = vfs_core_test_path_request(
        context,
        MICROS_VFS_MESSAGE_OPEN,
        "note",
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
        && action.result == MICROS_VFS_RESULT_OK
    );
    EXPECT_TRUE(
        micros_vfs_detach(
            &context->state,
            context->application_endpoint,
            &context->corrupting_io,
            &result
        ) == MICROS_VFS_CORE_ERROR_INVARIANT
        && context->corrupting_ramfs.corrupted
        && result == MICROS_VFS_TRUSTED_EXISTS
    );
    return true;
}
