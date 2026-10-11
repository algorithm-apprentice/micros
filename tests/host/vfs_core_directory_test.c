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

struct directory_test_case {
    struct micros_vfs_request request;
    micros_grant_t destination;
    uint8_t *records;
};

static bool build_open_and_translate_first_records(
    struct vfs_core_test_context *context,
    struct directory_test_case *test_case
)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;

    EXPECT_TRUE(vfs_core_test_initialize_full(context));
    selected = vfs_core_test_path_request(
        context,
        MICROS_VFS_MESSAGE_MKDIR,
        "/tmp",
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
    );
    selected = vfs_core_test_path_request(
        context,
        MICROS_VFS_MESSAGE_OPEN,
        "/",
        MICROS_VFS_OPEN_READ | MICROS_VFS_OPEN_DIRECTORY,
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
    test_case->destination =
        vfs_test_fixture_add_application_grant(
            &context->fixture,
            context->application_endpoint,
            MICROS_GRANT_PERMISSION_WRITE,
            NULL,
            320
        );
    test_case->request = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_GETDENTS
    );
    test_case->request.descriptor = 3;
    test_case->request.grant = test_case->destination;
    test_case->request.count = 160;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &test_case->request,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && action.transferred_count == 160
        && action.position == 3
    );
    test_case->records = vfs_test_fixture_application_bytes(
        &context->fixture,
        test_case->destination
    );
    EXPECT_TRUE(
        vfs_core_test_read_u32_le(&test_case->records[0]) == 80
        && vfs_core_test_read_u32_le(&test_case->records[4])
            == (
                MICROS_RAMFS_MODE_DIRECTORY
                | UINT32_C(0755)
            )
        && vfs_core_test_read_u32_le(&test_case->records[8]) == 1
        && vfs_core_test_read_u32_le(&test_case->records[12]) == 0
        && test_case->records[16] == '.'
        && test_case->records[17] == 0
        && vfs_core_test_read_u32_le(&test_case->records[80]) == 80
        && vfs_core_test_read_u32_le(&test_case->records[88]) == 2
        && test_case->records[96] == '.'
        && test_case->records[97] == '.'
        && test_case->records[98] == 0
    );
    return true;
}

static bool grant_failure_preserves_directory_cursor(
    struct vfs_core_test_context *context,
    const struct directory_test_case *test_case
)
{
    struct micros_vfs_result_action action;

    context->fixture.reject_client_copy = true;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &test_case->request,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_GRANT
        && context->state.open_files[
            context->state.processes[0].descriptors[3] - 1
        ].position == 3
    );
    context->fixture.reject_client_copy = false;
    return true;
}

static bool translate_remaining_records_and_eof(
    struct vfs_core_test_context *context,
    struct directory_test_case *test_case
)
{
    struct micros_vfs_result_action action;
    size_t validate_calls;

    test_case->request.grant_offset = 160;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &test_case->request,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && action.transferred_count == 160
        && action.position == MICROS_RAMFS_DIRECTORY_CURSOR_END
        && vfs_core_test_read_u32_le(
            &test_case->records[160]
        ) == 80
        && vfs_core_test_read_u32_le(
            &test_case->records[168]
        ) == 3
        && memcmp(&test_case->records[176], "etc", 3) == 0
        && vfs_core_test_read_u32_le(
            &test_case->records[240]
        ) == 80
        && vfs_core_test_read_u32_le(
            &test_case->records[248]
        ) == 3
        && memcmp(&test_case->records[256], "tmp", 3) == 0
    );
    EXPECT_TRUE(
        vfs_test_fixture_revoke_application_grant(
            &context->fixture,
            test_case->destination
        )
    );
    validate_calls = context->fixture.client_validate_calls;
    test_case->request.grant_offset = 0;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &test_case->request,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && action.transferred_count == 0
        && action.position == MICROS_RAMFS_DIRECTORY_CURSOR_END
        && context->fixture.client_validate_calls == validate_calls
    );
    return true;
}

static bool directory_range_is_rejected(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_GETDENTS
    );
    struct micros_vfs_result_action action;

    selected.descriptor = 3;
    selected.count = 81;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_RANGE
    );
    return true;
}

bool vfs_core_directory_translation_run(
    struct vfs_core_test_context *context
)
{
    struct directory_test_case test_case;

    return (
        build_open_and_translate_first_records(
            context,
            &test_case
        )
        && grant_failure_preserves_directory_cursor(
            context,
            &test_case
        )
        && translate_remaining_records_and_eof(
            context,
            &test_case
        )
        && directory_range_is_rejected(context)
    );
}
