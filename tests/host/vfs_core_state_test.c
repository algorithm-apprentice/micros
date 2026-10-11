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

bool vfs_core_state_constants_and_initialization_run(
    struct vfs_core_test_context *context
)
{
    EXPECT_TRUE(
        MICROS_VFS_PROCESS_CAPACITY == 64
        && MICROS_VFS_DESCRIPTOR_CAPACITY == 16
        && MICROS_VFS_OPEN_FILE_CAPACITY == 1024
        && MICROS_VFS_VNODE_CAPACITY == 64
        && MICROS_VFS_TRANSFER_MAX == 4096
        && MICROS_VFS_PATH_MAX == 4096
        && MICROS_VFS_NAME_MAX == 60
        && MICROS_VFS_DIRECTORY_RECORD_SIZE == 80
        && MICROS_VFS_BACKEND_REFERENCE_THRESHOLD == 256
        && MICROS_VFS_PENDING_OPERATION_CAPACITY == 1
        && MICROS_VFS_RESIDENT_PAGE_LIMIT == 80
    );
    EXPECT_TRUE(
        MICROS_VFS_RESULT_BAD_TYPE == -1
        && MICROS_VFS_RESULT_BAD_VERSION == -2
        && MICROS_VFS_RESULT_MALFORMED == -3
        && MICROS_VFS_RESULT_CALLER == -4
        && MICROS_VFS_RESULT_STATE == -5
        && MICROS_VFS_RESULT_BUSY == -6
        && MICROS_VFS_RESULT_DESCRIPTOR == -7
        && MICROS_VFS_RESULT_ACCESS == -8
        && MICROS_VFS_RESULT_NOT_FOUND == -9
        && MICROS_VFS_RESULT_EXISTS == -10
        && MICROS_VFS_RESULT_NOT_DIRECTORY == -11
        && MICROS_VFS_RESULT_IS_DIRECTORY == -12
        && MICROS_VFS_RESULT_NO_SPACE == -13
        && MICROS_VFS_RESULT_GRANT == -14
        && MICROS_VFS_RESULT_RANGE == -15
    );
    memset(&context->state, UINT8_C(0xa5), sizeof(context->state));
    EXPECT_TRUE(
        micros_vfs_state_initialize(
            &context->state,
            UINT32_C(0x00001006),
            UINT32_C(0x00001002),
            UINT32_C(0x00001003),
            UINT32_C(0x00001005),
            UINT32_C(0x00001004)
        ) == MICROS_VFS_CORE_OK
    );
    EXPECT_TRUE(
        context->state.phase == MICROS_VFS_PHASE_READY_UNMOUNTED
        && context->state.next_tty_request_id == 1
        && context->state.client_page_owner == MICROS_VFS_PAGE_FREE
        && context->state.backend_page_owner == MICROS_VFS_PAGE_FREE
        && micros_vfs_state_validate(&context->state)
            == MICROS_VFS_CORE_OK
    );
    return true;
}

bool vfs_core_state_corruption_run(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    micros_grant_t destination;

    EXPECT_TRUE(vfs_core_test_initialize_full(context));
    context->snapshot = context->state;
    context->state.vnodes[0].node =
        UINT64_C(0x0000000100000001);
    EXPECT_TRUE(
        micros_vfs_state_validate(&context->state)
            == MICROS_VFS_CORE_ERROR_INVARIANT
    );
    context->state = context->snapshot;
    context->state.processes[0].working_directory_vnode = 2;
    --context->state.vnodes[0].local_reference_count;
    EXPECT_TRUE(
        micros_vfs_state_validate(&context->state)
            == MICROS_VFS_CORE_ERROR_INVARIANT
    );
    context->state = context->snapshot;
    context->state.processes[0].descriptors[3] = 4;
    EXPECT_TRUE(
        micros_vfs_state_validate(&context->state)
            == MICROS_VFS_CORE_ERROR_INVARIANT
    );
    context->state = context->snapshot;
    context->state.open_files[0].open_flags =
        MICROS_VFS_OPEN_WRITE;
    EXPECT_TRUE(
        micros_vfs_state_validate(&context->state)
            == MICROS_VFS_CORE_ERROR_INVARIANT
    );

    context->state = context->snapshot;
    destination = vfs_test_fixture_add_application_grant(
        &context->fixture,
        context->application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        NULL,
        8
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_READ
    );
    selected.descriptor = 0;
    selected.grant = destination;
    selected.count = 8;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && context->state.pending.state
            == MICROS_VFS_PENDING_TTY_READ_WAIT_COMPLETION
    );
    context->snapshot = context->state;
    context->state.open_files[
        context->state.pending.open_file
    ].access = MICROS_VFS_ACCESS_WRITE;
    context->state.open_files[
        context->state.pending.open_file
    ].open_flags = MICROS_VFS_OPEN_WRITE;
    EXPECT_TRUE(
        micros_vfs_state_validate(&context->state)
            == MICROS_VFS_CORE_ERROR_INVARIANT
    );
    context->state = context->snapshot;
    ++context->state.next_tty_request_id;
    EXPECT_TRUE(
        micros_vfs_state_validate(&context->state)
            == MICROS_VFS_CORE_ERROR_INVARIANT
    );
    return true;
}
