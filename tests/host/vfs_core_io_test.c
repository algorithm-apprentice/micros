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

static bool open_seeded_file(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected =
        vfs_core_test_path_request(
            context,
            MICROS_VFS_MESSAGE_OPEN,
            "/etc/motd",
            MICROS_VFS_OPEN_READ,
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
        && action.active
        && action.reply_token == 1
        && action.request_type == MICROS_VFS_MESSAGE_OPEN
        && action.result == MICROS_VFS_RESULT_OK
        && action.descriptor == 3
        && (
            action.mode & MICROS_RAMFS_MODE_TYPE_MASK
        ) == MICROS_RAMFS_MODE_REGULAR
        && action.transferred_count == 0
        && action.position == 0
        && context->state.processes[0].descriptors[3] != 0
        && context->fixture.backend_grant_count == 0
        && micros_vfs_state_validate(&context->state)
            == MICROS_VFS_CORE_OK
    );
    return true;
}

static bool read_capacity_data_grant_and_eof_are_exact(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    micros_grant_t destination;
    size_t position;
    size_t ramfs_calls;

    destination = vfs_test_fixture_add_application_grant(
        &context->fixture,
        context->application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        NULL,
        16
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_READ
    );
    selected.descriptor = 3;
    selected.grant = destination;
    selected.count = 6;
    position = context->state.open_files[
        context->state.processes[0].descriptors[3] - 1
    ].position;
    ramfs_calls = context->fixture.ramfs_calls;
    context->fixture.force_backend_grant_capacity = true;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_NO_SPACE
        && context->state.open_files[
            context->state.processes[0].descriptors[3] - 1
        ].position == position
        && context->fixture.ramfs_calls == ramfs_calls
        && context->fixture.backend_grant_count == 0
    );
    context->fixture.force_backend_grant_capacity = false;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.active
        && action.result == MICROS_VFS_RESULT_OK
        && action.transferred_count == 6
        && action.position == 6
        && memcmp(
            vfs_test_fixture_application_bytes(
                &context->fixture,
                destination
            ),
            "micros",
            6
        ) == 0
        && context->fixture.backend_grant_count == 0
    );
    position = context->state.open_files[
        context->state.processes[0].descriptors[3] - 1
    ].position;
    EXPECT_TRUE(
        vfs_test_fixture_revoke_application_grant(
            &context->fixture,
            destination
        )
        && micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_GRANT
        && context->state.open_files[
            context->state.processes[0].descriptors[3] - 1
        ].position == position
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_READ
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
        && action.transferred_count == 0
        && action.position == position
    );
    return true;
}

static bool access_range_and_descriptor_checks_are_exact(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;

    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_WRITE
    );
    selected.descriptor = 3;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_ACCESS
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_READ
    );
    selected.descriptor = MICROS_VFS_DESCRIPTOR_CAPACITY;
    selected.count = MICROS_VFS_TRANSFER_MAX + 1;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_RANGE
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_WRITE
    );
    selected.descriptor = MICROS_VFS_DESCRIPTOR_CAPACITY;
    selected.grant = 1;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_MALFORMED
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_GETDENTS
    );
    selected.descriptor = MICROS_VFS_DESCRIPTOR_CAPACITY;
    selected.count = 1;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_RANGE
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_READ
    );
    selected.descriptor = MICROS_VFS_DESCRIPTOR_CAPACITY;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_DESCRIPTOR
    );
    return true;
}

static bool close_releases_backend_reference(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_CLOSE
    );
    struct micros_vfs_result_action action;
    size_t putnode_calls = context->fixture.putnode_calls;

    selected.descriptor = 3;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && context->state.processes[0].descriptors[3] == 0
        && context->fixture.putnode_calls == putnode_calls + 1
        && context->fixture.last_putnode
            == UINT64_C(0x0000000100000002)
        && context->fixture.last_putnode_count == 1
        && context->fixture.nodes[2].backend_references == 0
        && context->fixture.backend_grant_count == 0
        && micros_vfs_state_validate(&context->state)
            == MICROS_VFS_CORE_OK
    );
    return true;
}

static bool request_envelope_validation_is_exact(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;

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
        && action.result == MICROS_VFS_RESULT_DESCRIPTOR
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_CLOSE
    );
    selected.version = 2;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_BAD_VERSION
    );
    selected = vfs_core_test_request(
        context,
        UINT32_C(0x0004ffff)
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_BAD_TYPE
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_CLOSE
    );
    selected.source = UINT32_C(0x00003008);
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_CALLER
    );
    return true;
}

bool vfs_core_io_open_read_close_run(
    struct vfs_core_test_context *context
)
{
    return (
        vfs_core_test_initialize_full(context)
        && open_seeded_file(context)
        && read_capacity_data_grant_and_eof_are_exact(context)
        && access_range_and_descriptor_checks_are_exact(context)
        && close_releases_backend_reference(context)
        && request_envelope_validation_is_exact(context)
    );
}

static bool invalid_read_result_is_rejected(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    micros_grant_t grant;

    EXPECT_TRUE(vfs_core_test_initialize_full(context));
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
    );
    grant = vfs_test_fixture_add_application_grant(
        &context->fixture,
        context->application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        NULL,
        4
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_READ
    );
    selected.descriptor = 3;
    selected.grant = grant;
    selected.count = 4;
    context->fixture.inject_ramfs_result = true;
    context->fixture.injected_ramfs_operation =
        MICROS_VFS_RAMFS_READ;
    context->fixture.injected_ramfs_result =
        MICROS_RAMFS_RESULT_EXISTS;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_ERROR_INVARIANT
    );
    return true;
}

static bool corrupt_read_count_is_rejected(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    micros_grant_t grant;

    EXPECT_TRUE(vfs_core_test_initialize_full(context));
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
    );
    grant = vfs_test_fixture_add_application_grant(
        &context->fixture,
        context->application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        NULL,
        4
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_READ
    );
    selected.descriptor = 3;
    selected.grant = grant;
    selected.count = 4;
    context->fixture.corrupt_read_count = true;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_ERROR_INVARIANT
    );
    return true;
}

static bool invalid_write_result_is_rejected(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    micros_grant_t grant;
    static const uint8_t byte = UINT8_C(0x5a);

    EXPECT_TRUE(vfs_core_test_initialize_full(context));
    selected = vfs_core_test_path_request(
        context,
        MICROS_VFS_MESSAGE_OPEN,
        "/etc/motd",
        MICROS_VFS_OPEN_WRITE,
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
    );
    grant = vfs_test_fixture_add_application_grant(
        &context->fixture,
        context->application_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        &byte,
        1
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_WRITE
    );
    selected.descriptor = 3;
    selected.grant = grant;
    selected.count = 1;
    context->fixture.inject_ramfs_result = true;
    context->fixture.injected_ramfs_operation =
        MICROS_VFS_RAMFS_WRITE;
    context->fixture.injected_ramfs_result =
        MICROS_RAMFS_RESULT_NOT_FOUND;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_ERROR_INVARIANT
    );
    return true;
}

static bool invalid_getdents_result_is_rejected(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    micros_grant_t grant;

    EXPECT_TRUE(vfs_core_test_initialize_full(context));
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
    );
    grant = vfs_test_fixture_add_application_grant(
        &context->fixture,
        context->application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        NULL,
        MICROS_VFS_DIRECTORY_RECORD_SIZE
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_GETDENTS
    );
    selected.descriptor = 3;
    selected.grant = grant;
    selected.count = MICROS_VFS_DIRECTORY_RECORD_SIZE;
    context->fixture.inject_ramfs_result = true;
    context->fixture.injected_ramfs_operation =
        MICROS_VFS_RAMFS_GETDENTS;
    context->fixture.injected_ramfs_result =
        MICROS_RAMFS_RESULT_NO_SPACE;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_ERROR_INVARIANT
    );
    return true;
}

bool vfs_core_io_backend_reply_validation_run(
    struct vfs_core_test_context *context
)
{
    return (
        invalid_read_result_is_rejected(context)
        && corrupt_read_count_is_rejected(context)
        && invalid_write_result_is_rejected(context)
        && invalid_getdents_result_is_rejected(context)
    );
}

bool vfs_core_io_namespace_mutation_write(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    micros_grant_t source;
    size_t open_file_slot;
    uint64_t original_position;
    uint64_t original_size;
    static const uint8_t contents[] = "hello";

    source = vfs_test_fixture_add_application_grant(
        &context->fixture,
        context->application_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        contents,
        sizeof(contents) - 1
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_WRITE
    );
    selected.descriptor = 3;
    selected.grant = source;
    selected.count = sizeof(contents) - 1;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && action.transferred_count == sizeof(contents) - 1
        && action.position == sizeof(contents) - 1
        && context->fixture.backend_grant_count == 0
    );
    open_file_slot =
        context->state.processes[0].descriptors[3] - 1;
    original_position =
        context->state.open_files[open_file_slot].position;
    original_size = context->state.vnodes[
        context->state.open_files[open_file_slot].vnode - 1
    ].size;
    context->fixture.inject_ramfs_result = true;
    context->fixture.injected_ramfs_operation =
        MICROS_VFS_RAMFS_WRITE;
    context->fixture.injected_ramfs_result =
        MICROS_RAMFS_RESULT_NO_SPACE;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_NO_SPACE
        && context->state.open_files[open_file_slot].position
            == original_position
        && context->state.vnodes[
            context->state.open_files[open_file_slot].vnode - 1
        ].size == original_size
        && context->fixture.backend_grant_count == 0
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
    return true;
}

bool vfs_core_io_namespace_mutation_read(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    micros_grant_t destination;
    static const uint8_t contents[] = "hello";

    destination = vfs_test_fixture_add_application_grant(
        &context->fixture,
        context->application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        NULL,
        sizeof(contents) - 1
    );
    selected = vfs_core_test_request(
        context,
        MICROS_VFS_MESSAGE_READ
    );
    selected.descriptor = 3;
    selected.grant = destination;
    selected.count = sizeof(contents) - 1;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &context->state,
            &selected,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && action.transferred_count == sizeof(contents) - 1
        && memcmp(
            vfs_test_fixture_application_bytes(
                &context->fixture,
                destination
            ),
            contents,
            sizeof(contents) - 1
        ) == 0
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
    return true;
}

bool vfs_core_io_backend_reference_cleanup_run(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    size_t index;

    EXPECT_TRUE(vfs_core_test_initialize_full(context));
    for (
        index = 0;
        index < MICROS_VFS_BACKEND_REFERENCE_THRESHOLD - 1;
        ++index
    ) {
        selected = vfs_core_test_path_request(
            context,
            MICROS_VFS_MESSAGE_CHDIR,
            "/",
            0,
            0
        );
        EXPECT_TRUE(
            selected.grant != MICROS_GRANT_NONE
            && micros_vfs_handle_request(
                &context->state,
                &selected,
                &context->fixture.io,
                &action
            ) == MICROS_VFS_CORE_OK
            && action.result == MICROS_VFS_RESULT_OK
            && vfs_test_fixture_revoke_application_grant(
                &context->fixture,
                selected.grant
            )
        );
    }
    EXPECT_TRUE(
        context->fixture.putnode_calls == 1
        && context->fixture.putnode_count
            == MICROS_VFS_BACKEND_REFERENCE_THRESHOLD - 1
        && context->fixture.last_putnode
            == UINT64_C(0x0000000100000000)
        && context->fixture.last_putnode_count
            == MICROS_VFS_BACKEND_REFERENCE_THRESHOLD - 1
        && context->fixture.nodes[0].backend_references == 1
        && context->state.vnodes[0].backend_reference_count == 1
        && micros_vfs_state_validate(&context->state)
            == MICROS_VFS_CORE_OK
    );
    return true;
}
