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

static bool find_process_slot(
    const struct micros_vfs_state *state,
    micros_endpoint_t endpoint,
    size_t *process_slot
)
{
    size_t index;

    for (
        index = 0;
        index < MICROS_VFS_PROCESS_CAPACITY;
        ++index
    ) {
        if (
            state->processes[index].state
                == MICROS_VFS_PROCESS_ACTIVE
            && state->processes[index].endpoint == endpoint
        ) {
            *process_slot = index;
            return true;
        }
    }
    return false;
}

static bool open_path_for_process(
    struct vfs_core_test_context *context,
    micros_endpoint_t endpoint,
    const char *path,
    uint32_t expected_descriptor,
    uint16_t *open_file_reference
)
{
    struct micros_vfs_request request = {
        .type = MICROS_VFS_MESSAGE_OPEN,
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .source = endpoint,
        .reply_token = UINT64_C(0x81),
        .path_length = (uint32_t)(strlen(path) + 1),
        .open_flags = MICROS_VFS_OPEN_READ,
    };
    struct micros_vfs_result_action action;
    size_t process_slot;

    request.grant = vfs_test_fixture_add_application_grant(
        &context->fixture,
        endpoint,
        MICROS_GRANT_PERMISSION_READ,
        (const uint8_t *)path,
        request.path_length
    );
    if (
        request.grant == MICROS_GRANT_NONE
        || micros_vfs_handle_request(
            &context->state,
            &request,
            &context->fixture.io,
            &action
        ) != MICROS_VFS_CORE_OK
        || !action.active
        || action.result != MICROS_VFS_RESULT_OK
        || action.descriptor != expected_descriptor
        || !find_process_slot(
            &context->state,
            endpoint,
            &process_slot
        )
    ) {
        return false;
    }
    *open_file_reference = context->state.processes[
        process_slot
    ].descriptors[expected_descriptor];
    return *open_file_reference != 0;
}

static bool close_process_descriptor(
    struct vfs_core_test_context *context,
    micros_endpoint_t endpoint,
    uint32_t descriptor
)
{
    struct micros_vfs_request request = {
        .type = MICROS_VFS_MESSAGE_CLOSE,
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .source = endpoint,
        .reply_token = UINT64_C(0x82),
        .descriptor = descriptor,
    };
    struct micros_vfs_result_action action;

    return (
        micros_vfs_handle_request(
            &context->state,
            &request,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.active
        && action.result == MICROS_VFS_RESULT_OK
    );
}

struct process_exit_case {
    struct micros_vfs_control_request request;
    uint16_t shared_open_file;
    uint16_t private_open_file;
    uint16_t private_vnode;
    uint32_t root_references;
    size_t process_slot;
    size_t putnode_calls;
};

static bool prepare_process_exit_case(
    struct vfs_core_test_context *context,
    struct process_exit_case *selected
)
{
    static const uint8_t shared_bytes[] = "shared";
    static const uint8_t private_bytes[] = "private";
    enum micros_vfs_trusted_result trusted_result;

    EXPECT_TRUE(vfs_core_test_initialize_full(context));
    EXPECT_TRUE(
        vfs_test_fixture_add_root_file(
            &context->fixture,
            "shared",
            shared_bytes,
            sizeof(shared_bytes) - 1
        )
        && vfs_test_fixture_add_root_file(
            &context->fixture,
            "private",
            private_bytes,
            sizeof(private_bytes) - 1
        )
        && open_path_for_process(
            context,
            context->application_endpoint,
            "/shared",
            3,
            &selected->shared_open_file
        )
    );
    selected->root_references =
        context->state.vnodes[0].local_reference_count;
    EXPECT_TRUE(
        micros_vfs_attach_console(
            &context->state,
            second_endpoint,
            0,
            &trusted_result
        ) == MICROS_VFS_CORE_OK
        && trusted_result == MICROS_VFS_TRUSTED_OK
        && micros_vfs_share_descriptor(
            &context->state,
            context->application_endpoint,
            3,
            second_endpoint,
            3,
            &trusted_result
        ) == MICROS_VFS_CORE_OK
        && trusted_result == MICROS_VFS_TRUSTED_OK
        && open_path_for_process(
            context,
            second_endpoint,
            "/private",
            4,
            &selected->private_open_file
        )
        && find_process_slot(
            &context->state,
            second_endpoint,
            &selected->process_slot
        )
    );
    selected->private_vnode = context->state.open_files[
        selected->private_open_file - 1
    ].vnode;
    selected->putnode_calls = context->fixture.putnode_calls;
    selected->request = vfs_core_test_exit_request(
        context,
        second_endpoint
    );
    return true;
}

static bool process_exit_case_is_released(
    struct vfs_core_test_context *context,
    const struct process_exit_case *selected
)
{
    struct micros_vfs_control_result_action action;
    struct micros_vfs_process_record free_process = {0};

    EXPECT_TRUE(
        micros_vfs_handle_control(
            &context->state,
            &selected->request,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && vfs_core_test_control_action_is_exact(
            &action,
            &selected->request,
            MICROS_VFS_CONTROL_RESULT_OK
        )
        && memcmp(
            &context->state.processes[selected->process_slot],
            &free_process,
            sizeof(context->state.processes[
                selected->process_slot
            ])
        ) == 0
        && context->state.open_files[
            selected->shared_open_file - 1
        ].state
            == MICROS_VFS_OPEN_FILE_ACTIVE
        && context->state.open_files[
            selected->shared_open_file - 1
        ].reference_count == 1
        && context->state.open_files[
            selected->private_open_file - 1
        ].state
            == MICROS_VFS_OPEN_FILE_FREE
        && context->state.vnodes[
            selected->private_vnode - 1
        ].state
            == MICROS_VFS_VNODE_FREE
        && context->state.vnodes[0].local_reference_count
            == selected->root_references
        && context->fixture.putnode_calls
            == selected->putnode_calls + 1
        && !vfs_core_test_state_names_endpoint(
            &context->state,
            second_endpoint
        )
        && micros_vfs_state_validate(&context->state)
            == MICROS_VFS_CORE_OK
    );
    return true;
}

static bool duplicate_exit_is_atomic(
    struct vfs_core_test_context *context,
    const struct process_exit_case *selected
)
{
    struct micros_vfs_control_result_action action;

    context->snapshot = context->state;
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &context->state,
            &selected->request,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && vfs_core_test_control_action_is_exact(
            &action,
            &selected->request,
            MICROS_VFS_CONTROL_RESULT_STATE
        )
        && memcmp(
            &context->state,
            &context->snapshot,
            sizeof(context->state)
        ) == 0
        && close_process_descriptor(
            context,
            context->application_endpoint,
            3
        )
        && context->state.open_files[
            selected->shared_open_file - 1
        ].state
            == MICROS_VFS_OPEN_FILE_FREE
    );
    return true;
}

static bool exit_releases_process_owned_resources(
    struct vfs_core_test_context *context
)
{
    struct process_exit_case selected;

    return (
        prepare_process_exit_case(context, &selected)
        && process_exit_case_is_released(context, &selected)
        && duplicate_exit_is_atomic(context, &selected)
    );
}

static bool rejected_exit_preserves_state(
    struct vfs_core_test_context *context,
    const struct micros_vfs_control_request *request
)
{
    struct micros_vfs_control_result_action action;

    context->snapshot = context->state;
    return (
        micros_vfs_handle_control(
            &context->state,
            request,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && vfs_core_test_control_action_is_exact(
            &action,
            request,
            MICROS_VFS_CONTROL_RESULT_STATE
        )
        && memcmp(
            &context->state,
            &context->snapshot,
            sizeof(context->state)
        ) == 0
    );
}

static bool invalid_exit_requests_are_atomic(
    struct vfs_core_test_context *context
)
{
    struct micros_vfs_control_request request;
    struct micros_vfs_control_request invalid[12];
    struct micros_vfs_control_result_action action;
    struct micros_vfs_control_result_action zero_action = {0};
    size_t index;

    EXPECT_TRUE(vfs_core_test_initialize_full(context));
    request = vfs_core_test_exit_request(
        context,
        context->application_endpoint
    );
    for (
        index = 0;
        index < sizeof(invalid) / sizeof(invalid[0]);
        ++index
    ) {
        invalid[index] = request;
    }
    invalid[0].version = 2;
    invalid[1].lifecycle_token = 0;
    invalid[2].semantic_pid = 0;
    invalid[3].child_endpoint ^= UINT32_C(1);
    invalid[4].child_endpoint = context->fixture.pm_endpoint;
    invalid[5].image_token = 1;
    invalid[6].descriptor_token = 1;
    invalid[7].parent_endpoint = context->application_endpoint;
    invalid[8].request_grant = 1;
    invalid[9].request_size = 1;
    invalid[10].trusted_init = true;
    invalid[11].child_endpoint = 0;
    for (
        index = 0;
        index < sizeof(invalid) / sizeof(invalid[0]);
        ++index
    ) {
        EXPECT_TRUE(
            rejected_exit_preserves_state(
                context,
                &invalid[index]
            )
        );
    }

    context->snapshot = context->state;
    request.source = context->application_endpoint;
    memset(&action, 0xa5, sizeof(action));
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &context->state,
            &request,
            &context->fixture.io,
            &action
        ) == MICROS_VFS_CORE_ERROR_INVARIANT
        && memcmp(
            &context->state,
            &context->snapshot,
            sizeof(context->state)
        ) == 0
        && memcmp(&action, &zero_action, sizeof(action)) == 0
    );

    context->state.reserved = 1;
    context->snapshot = context->state;
    request = vfs_core_test_exit_request(
        context,
        context->application_endpoint
    );
    memset(&action, 0xa5, sizeof(action));
    {
        struct micros_vfs_control_result_action preserved_action =
            action;

        EXPECT_TRUE(
            micros_vfs_handle_control(
                &context->state,
                &request,
                &context->fixture.io,
                &action
            ) == MICROS_VFS_CORE_ERROR_ARGUMENT
            && memcmp(
                &context->state,
                &context->snapshot,
                sizeof(context->state)
            ) == 0
            && memcmp(
                &action,
                &preserved_action,
                sizeof(action)
            ) == 0
        );
    }
    return true;
}

bool vfs_core_process_exit_run(
    struct vfs_core_test_context *context
)
{
    return (
        exit_releases_process_owned_resources(context)
        && invalid_exit_requests_are_atomic(context)
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
