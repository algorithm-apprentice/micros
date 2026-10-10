#include "servers/vfs/vfs_core.h"
#include "tests/host/vfs_test_fixture.h"

#include <stdbool.h>
#include <stdint.h>
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

static struct micros_vfs_state state;
static struct micros_vfs_state snapshot;
static struct vfs_test_fixture fixture;

struct test_backend {
    size_t mount_calls;
    size_t putnode_calls;
    uint64_t mount_node;
    uint64_t last_putnode;
    uint32_t last_putnode_count;
};

static struct test_backend backend;

static enum micros_vfs_backend_status test_ramfs_call(
    void *context,
    const struct micros_vfs_ramfs_request *request,
    struct micros_vfs_ramfs_response *response
)
{
    struct test_backend *selected = context;

    memset(response, 0, sizeof(*response));
    if (request->operation == MICROS_VFS_RAMFS_MOUNT) {
        ++selected->mount_calls;
        response->result = MICROS_RAMFS_RESULT_OK;
        response->node = selected->mount_node == 0
            ? UINT64_C(0x0000000100000000)
            : selected->mount_node;
        response->mode =
            MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755);
        return MICROS_VFS_BACKEND_OK;
    }
    if (request->operation == MICROS_VFS_RAMFS_PUTNODE) {
        ++selected->putnode_calls;
        selected->last_putnode = request->node;
        selected->last_putnode_count = request->count;
        response->result = MICROS_RAMFS_RESULT_OK;
        return MICROS_VFS_BACKEND_OK;
    }
    return MICROS_VFS_BACKEND_INVARIANT;
}

static const struct micros_vfs_io test_io = {
    .ramfs_call = test_ramfs_call,
    .context = &backend,
};

static const micros_endpoint_t vfs_endpoint =
    UINT32_C(0x00001006);
static const micros_endpoint_t ramfs_endpoint =
    UINT32_C(0x00001005);
static const micros_endpoint_t tty_endpoint =
    UINT32_C(0x00001004);
static const micros_endpoint_t application_endpoint =
    UINT32_C(0x00002007);

static bool initialize_full_fixture(void)
{
    enum micros_vfs_trusted_result result;

    vfs_test_fixture_initialize(
        &fixture,
        vfs_endpoint,
        ramfs_endpoint,
        tty_endpoint
    );
    if (
        micros_vfs_state_initialize(
            &state,
            vfs_endpoint,
            ramfs_endpoint,
            tty_endpoint
        ) != MICROS_VFS_CORE_OK
        || micros_vfs_mount(
            &state,
            &fixture.io
        ) != MICROS_VFS_CORE_OK
        || micros_vfs_attach_console(
            &state,
            application_endpoint,
            0,
            &result
        ) != MICROS_VFS_CORE_OK
        || result != MICROS_VFS_TRUSTED_OK
    ) {
        return false;
    }
    return true;
}

static struct micros_vfs_request request(uint32_t type)
{
    struct micros_vfs_request selected = {
        .type = type,
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .source = application_endpoint,
        .reply_token = 1,
    };

    return selected;
}

static struct micros_vfs_request path_request(
    uint32_t type,
    const char *path,
    uint32_t open_flags,
    uint32_t mode
)
{
    struct micros_vfs_request selected = request(type);
    size_t length = strlen(path) + 1;

    selected.grant = vfs_test_fixture_add_application_grant(
        &fixture,
        application_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        (const uint8_t *)path,
        length
    );
    selected.path_length = (uint32_t)length;
    selected.open_flags = open_flags;
    selected.mode = mode;
    return selected;
}

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

static bool constants_and_initial_state_are_exact(void)
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
        && MICROS_VFS_RESIDENT_PAGE_LIMIT == 64
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
    memset(&state, UINT8_C(0xa5), sizeof(state));
    EXPECT_TRUE(
        micros_vfs_state_initialize(
            &state,
            UINT32_C(0x00001006),
            UINT32_C(0x00001005),
            UINT32_C(0x00001004)
        ) == MICROS_VFS_CORE_OK
    );
    EXPECT_TRUE(
        state.phase == MICROS_VFS_PHASE_READY_UNMOUNTED
        && state.next_tty_request_id == 1
        && state.client_page_owner == MICROS_VFS_PAGE_FREE
        && state.backend_page_owner == MICROS_VFS_PAGE_FREE
        && micros_vfs_state_validate(&state) == MICROS_VFS_CORE_OK
    );
    return true;
}

static bool mount_attach_share_and_detach_are_atomic(void)
{
    enum micros_vfs_trusted_result result;
    micros_endpoint_t first = UINT32_C(0x00002007);
    micros_endpoint_t second = UINT32_C(0x00002008);

    memset(&backend, 0, sizeof(backend));
    snapshot = state;
    backend.mount_node = UINT64_C(0x0000000100000001);
    EXPECT_TRUE(
        micros_vfs_mount(&state, &test_io)
            == MICROS_VFS_CORE_ERROR_INVARIANT
        && memcmp(&state, &snapshot, sizeof(state)) == 0
        && backend.mount_calls == 1
    );
    memset(&backend, 0, sizeof(backend));
    EXPECT_TRUE(
        micros_vfs_mount(&state, &test_io) == MICROS_VFS_CORE_OK
        && backend.mount_calls == 1
        && state.phase == MICROS_VFS_PHASE_MOUNTED
        && state.vnodes[0].state == MICROS_VFS_VNODE_ACTIVE
        && state.vnodes[0].mount_root
        && state.vnodes[0].local_reference_count == 1
        && state.vnodes[0].backend_reference_count == 1
        && micros_vfs_state_validate(&state) == MICROS_VFS_CORE_OK
    );
    EXPECT_TRUE(
        micros_vfs_attach_console(
            &state,
            first,
            0,
            &result
        ) == MICROS_VFS_CORE_OK
        && result == MICROS_VFS_TRUSTED_OK
        && state.processes[0].endpoint == first
        && state.processes[0].root_vnode == 1
        && state.processes[0].working_directory_vnode == 1
        && state.processes[0].descriptors[0] == 1
        && state.processes[0].descriptors[1] == 2
        && state.processes[0].descriptors[2] == 3
        && state.open_files[0].kind == MICROS_VFS_OBJECT_CONSOLE
        && state.open_files[0].access == MICROS_VFS_ACCESS_READ
        && state.open_files[1].access == MICROS_VFS_ACCESS_WRITE
        && state.open_files[2].access == MICROS_VFS_ACCESS_WRITE
        && state.vnodes[0].local_reference_count == 3
    );
    snapshot = state;
    EXPECT_TRUE(
        micros_vfs_attach_console(
            &state,
            first,
            0,
            &result
        ) == MICROS_VFS_CORE_OK
        && result == MICROS_VFS_TRUSTED_EXISTS
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );
    EXPECT_TRUE(
        micros_vfs_attach_console(
            &state,
            second,
            0,
            &result
        ) == MICROS_VFS_CORE_OK
        && result == MICROS_VFS_TRUSTED_OK
        && state.processes[1].endpoint == second
        && state.vnodes[0].local_reference_count == 5
    );
    EXPECT_TRUE(
        micros_vfs_share_descriptor(
            &state,
            first,
            1,
            second,
            3,
            &result
        ) == MICROS_VFS_CORE_OK
        && result == MICROS_VFS_TRUSTED_OK
        && state.processes[1].descriptors[3] == 2
        && state.open_files[1].reference_count == 2
        && micros_vfs_state_validate(&state) == MICROS_VFS_CORE_OK
    );
    snapshot = state;
    EXPECT_TRUE(
        micros_vfs_share_descriptor(
            &state,
            first,
            1,
            second,
            2,
            &result
        ) == MICROS_VFS_CORE_OK
        && result == MICROS_VFS_TRUSTED_EXISTS
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );
    EXPECT_TRUE(
        micros_vfs_detach(
            &state,
            first,
            &test_io,
            &result
        ) == MICROS_VFS_CORE_OK
        && result == MICROS_VFS_TRUSTED_OK
        && state.processes[0].state == MICROS_VFS_PROCESS_FREE
        && state.open_files[0].state == MICROS_VFS_OPEN_FILE_FREE
        && state.open_files[1].reference_count == 1
        && state.open_files[2].state == MICROS_VFS_OPEN_FILE_FREE
        && state.vnodes[0].local_reference_count == 3
        && backend.putnode_calls == 0
        && micros_vfs_state_validate(&state) == MICROS_VFS_CORE_OK
    );
    EXPECT_TRUE(
        micros_vfs_detach(
            &state,
            second,
            &test_io,
            &result
        ) == MICROS_VFS_CORE_OK
        && result == MICROS_VFS_TRUSTED_OK
        && state.vnodes[0].local_reference_count == 1
        && micros_vfs_state_validate(&state) == MICROS_VFS_CORE_OK
    );
    snapshot = state;
    EXPECT_TRUE(
        micros_vfs_detach(
            &state,
            second,
            &test_io,
            &result
        ) == MICROS_VFS_CORE_OK
        && result == MICROS_VFS_TRUSTED_NOT_FOUND
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );
    return true;
}

static bool complete_state_validation_rejects_dangling_state(void)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    micros_grant_t destination;

    EXPECT_TRUE(initialize_full_fixture());
    snapshot = state;
    state.vnodes[0].node = UINT64_C(0x0000000100000001);
    EXPECT_TRUE(
        micros_vfs_state_validate(&state)
            == MICROS_VFS_CORE_ERROR_INVARIANT
    );
    state = snapshot;
    state.processes[0].working_directory_vnode = 2;
    --state.vnodes[0].local_reference_count;
    EXPECT_TRUE(
        micros_vfs_state_validate(&state)
            == MICROS_VFS_CORE_ERROR_INVARIANT
    );
    state = snapshot;
    state.processes[0].descriptors[3] = 4;
    EXPECT_TRUE(
        micros_vfs_state_validate(&state)
            == MICROS_VFS_CORE_ERROR_INVARIANT
    );
    state = snapshot;
    state.open_files[0].open_flags = MICROS_VFS_OPEN_WRITE;
    EXPECT_TRUE(
        micros_vfs_state_validate(&state)
            == MICROS_VFS_CORE_ERROR_INVARIANT
    );

    state = snapshot;
    destination = vfs_test_fixture_add_application_grant(
        &fixture,
        application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        NULL,
        8
    );
    selected = request(MICROS_VFS_MESSAGE_READ);
    selected.descriptor = 0;
    selected.grant = destination;
    selected.count = 8;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && state.pending.state
            == MICROS_VFS_PENDING_TTY_READ_WAIT_COMPLETION
    );
    snapshot = state;
    state.open_files[state.pending.open_file].access =
        MICROS_VFS_ACCESS_WRITE;
    state.open_files[state.pending.open_file].open_flags =
        MICROS_VFS_OPEN_WRITE;
    EXPECT_TRUE(
        micros_vfs_state_validate(&state)
            == MICROS_VFS_CORE_ERROR_INVARIANT
    );
    state = snapshot;
    ++state.next_tty_request_id;
    EXPECT_TRUE(
        micros_vfs_state_validate(&state)
            == MICROS_VFS_CORE_ERROR_INVARIANT
    );
    return true;
}

static bool open_read_close_and_validation_are_exact(void)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    micros_grant_t destination;
    size_t position;
    size_t putnode_calls;
    size_t ramfs_calls;

    EXPECT_TRUE(initialize_full_fixture());
    selected = path_request(
        MICROS_VFS_MESSAGE_OPEN,
        "/etc/motd",
        MICROS_VFS_OPEN_READ,
        0
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
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
        && state.processes[0].descriptors[3] != 0
        && fixture.backend_grant_count == 0
        && micros_vfs_state_validate(&state) == MICROS_VFS_CORE_OK
    );
    destination = vfs_test_fixture_add_application_grant(
        &fixture,
        application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        NULL,
        16
    );
    selected = request(MICROS_VFS_MESSAGE_READ);
    selected.descriptor = 3;
    selected.grant = destination;
    selected.count = 6;
    position = state.open_files[
        state.processes[0].descriptors[3] - 1
    ].position;
    ramfs_calls = fixture.ramfs_calls;
    fixture.force_backend_grant_capacity = true;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_NO_SPACE
        && state.open_files[
            state.processes[0].descriptors[3] - 1
        ].position == position
        && fixture.ramfs_calls == ramfs_calls
        && fixture.backend_grant_count == 0
    );
    fixture.force_backend_grant_capacity = false;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.active
        && action.result == MICROS_VFS_RESULT_OK
        && action.transferred_count == 6
        && action.position == 6
        && memcmp(
            vfs_test_fixture_application_bytes(
                &fixture,
                destination
            ),
            "micros",
            6
        ) == 0
        && fixture.backend_grant_count == 0
    );
    position = state.open_files[
        state.processes[0].descriptors[3] - 1
    ].position;
    EXPECT_TRUE(
        vfs_test_fixture_revoke_application_grant(
            &fixture,
            destination
        )
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_GRANT
        && state.open_files[
            state.processes[0].descriptors[3] - 1
        ].position == position
    );
    selected = request(MICROS_VFS_MESSAGE_READ);
    selected.descriptor = 3;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && action.transferred_count == 0
        && action.position == position
    );
    selected = request(MICROS_VFS_MESSAGE_WRITE);
    selected.descriptor = 3;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_ACCESS
    );
    selected = request(MICROS_VFS_MESSAGE_READ);
    selected.descriptor = MICROS_VFS_DESCRIPTOR_CAPACITY;
    selected.count = MICROS_VFS_TRANSFER_MAX + 1;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_RANGE
    );
    selected = request(MICROS_VFS_MESSAGE_WRITE);
    selected.descriptor = MICROS_VFS_DESCRIPTOR_CAPACITY;
    selected.grant = 1;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_MALFORMED
    );
    selected = request(MICROS_VFS_MESSAGE_GETDENTS);
    selected.descriptor = MICROS_VFS_DESCRIPTOR_CAPACITY;
    selected.count = 1;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_RANGE
    );
    selected = request(MICROS_VFS_MESSAGE_READ);
    selected.descriptor = MICROS_VFS_DESCRIPTOR_CAPACITY;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_DESCRIPTOR
    );
    selected = request(MICROS_VFS_MESSAGE_CLOSE);
    selected.descriptor = 3;
    putnode_calls = fixture.putnode_calls;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && state.processes[0].descriptors[3] == 0
        && fixture.putnode_calls == putnode_calls + 1
        && fixture.last_putnode
            == UINT64_C(0x0000000100000002)
        && fixture.last_putnode_count == 1
        && fixture.nodes[2].backend_references == 0
        && fixture.backend_grant_count == 0
        && micros_vfs_state_validate(&state) == MICROS_VFS_CORE_OK
    );
    selected = request(MICROS_VFS_MESSAGE_CLOSE);
    selected.descriptor = 3;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_DESCRIPTOR
    );
    selected = request(MICROS_VFS_MESSAGE_CLOSE);
    selected.version = 2;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_BAD_VERSION
    );
    selected = request(UINT32_C(0x0004ffff));
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_BAD_TYPE
    );
    selected = request(MICROS_VFS_MESSAGE_CLOSE);
    selected.source = UINT32_C(0x00003008);
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_CALLER
    );
    return true;
}

static bool path_grant_precedes_local_capacity(void)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    micros_grant_t stale_path;
    size_t descriptor;
    static const uint8_t root_path[] = "/";

    EXPECT_TRUE(initialize_full_fixture());
    for (
        descriptor = 3;
        descriptor < MICROS_VFS_DESCRIPTOR_CAPACITY;
        ++descriptor
    ) {
        selected = path_request(
            MICROS_VFS_MESSAGE_OPEN,
            "/etc/motd",
            MICROS_VFS_OPEN_READ,
            0
        );
        EXPECT_TRUE(
            micros_vfs_handle_request(
                &state,
                &selected,
                &fixture.io,
                &action
            ) == MICROS_VFS_CORE_OK
            && action.result == MICROS_VFS_RESULT_OK
            && action.descriptor == descriptor
        );
    }
    stale_path = vfs_test_fixture_add_application_grant(
        &fixture,
        application_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        root_path,
        sizeof(root_path)
    );
    EXPECT_TRUE(
        stale_path != MICROS_GRANT_NONE
        && vfs_test_fixture_revoke_application_grant(
            &fixture,
            stale_path
        )
    );
    selected = request(MICROS_VFS_MESSAGE_OPEN);
    selected.grant = stale_path;
    selected.path_length = sizeof(root_path);
    selected.open_flags = MICROS_VFS_OPEN_READ;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_GRANT
    );
    return true;
}

static bool backend_reply_validation_is_operation_specific(void)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    micros_grant_t grant;
    static const uint8_t byte = UINT8_C(0x5a);

    EXPECT_TRUE(initialize_full_fixture());
    selected = path_request(
        MICROS_VFS_MESSAGE_OPEN,
        "/etc/motd",
        MICROS_VFS_OPEN_READ,
        0
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
    );
    grant = vfs_test_fixture_add_application_grant(
        &fixture,
        application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        NULL,
        4
    );
    selected = request(MICROS_VFS_MESSAGE_READ);
    selected.descriptor = 3;
    selected.grant = grant;
    selected.count = 4;
    fixture.inject_ramfs_result = true;
    fixture.injected_ramfs_operation = MICROS_VFS_RAMFS_READ;
    fixture.injected_ramfs_result = MICROS_RAMFS_RESULT_EXISTS;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_ERROR_INVARIANT
    );

    EXPECT_TRUE(initialize_full_fixture());
    selected = path_request(
        MICROS_VFS_MESSAGE_OPEN,
        "/etc/motd",
        MICROS_VFS_OPEN_READ,
        0
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
    );
    grant = vfs_test_fixture_add_application_grant(
        &fixture,
        application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        NULL,
        4
    );
    selected = request(MICROS_VFS_MESSAGE_READ);
    selected.descriptor = 3;
    selected.grant = grant;
    selected.count = 4;
    fixture.corrupt_read_count = true;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_ERROR_INVARIANT
    );

    EXPECT_TRUE(initialize_full_fixture());
    selected = path_request(
        MICROS_VFS_MESSAGE_OPEN,
        "/etc/motd",
        MICROS_VFS_OPEN_WRITE,
        0
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
    );
    grant = vfs_test_fixture_add_application_grant(
        &fixture,
        application_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        &byte,
        1
    );
    selected = request(MICROS_VFS_MESSAGE_WRITE);
    selected.descriptor = 3;
    selected.grant = grant;
    selected.count = 1;
    fixture.inject_ramfs_result = true;
    fixture.injected_ramfs_operation = MICROS_VFS_RAMFS_WRITE;
    fixture.injected_ramfs_result = MICROS_RAMFS_RESULT_NOT_FOUND;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_ERROR_INVARIANT
    );

    EXPECT_TRUE(initialize_full_fixture());
    selected = path_request(
        MICROS_VFS_MESSAGE_OPEN,
        "/",
        MICROS_VFS_OPEN_READ | MICROS_VFS_OPEN_DIRECTORY,
        0
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
    );
    grant = vfs_test_fixture_add_application_grant(
        &fixture,
        application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        NULL,
        MICROS_VFS_DIRECTORY_RECORD_SIZE
    );
    selected = request(MICROS_VFS_MESSAGE_GETDENTS);
    selected.descriptor = 3;
    selected.grant = grant;
    selected.count = MICROS_VFS_DIRECTORY_RECORD_SIZE;
    fixture.inject_ramfs_result = true;
    fixture.injected_ramfs_operation = MICROS_VFS_RAMFS_GETDENTS;
    fixture.injected_ramfs_result = MICROS_RAMFS_RESULT_NO_SPACE;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_ERROR_INVARIANT
    );
    return true;
}

static bool create_write_mkdir_and_chdir_are_exact(void)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    micros_grant_t source;
    micros_grant_t destination;
    uint64_t note;
    size_t open_file_slot;
    uint64_t original_position;
    uint64_t original_size;
    static const uint8_t contents[] = "hello";

    EXPECT_TRUE(initialize_full_fixture());
    selected = path_request(
        MICROS_VFS_MESSAGE_MKDIR,
        "/tmp",
        0,
        UINT32_C(0755)
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && vfs_test_fixture_find_path(&fixture, "/tmp") != 0
        && state.vnodes[0].local_reference_count == 3
        && fixture.backend_grant_count == 0
    );
    selected = path_request(
        MICROS_VFS_MESSAGE_MKDIR,
        "/tmp/",
        0,
        UINT32_C(0700)
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_EXISTS
    );
    selected = path_request(
        MICROS_VFS_MESSAGE_OPEN,
        "tmp/note",
        MICROS_VFS_OPEN_READ
            | MICROS_VFS_OPEN_WRITE
            | MICROS_VFS_OPEN_CREATE,
        UINT32_C(0640)
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && action.descriptor == 3
        && action.mode
            == (
                MICROS_RAMFS_MODE_REGULAR
                | UINT32_C(0640)
            )
        && (note = vfs_test_fixture_find_path(
            &fixture,
            "/tmp/note"
        )) != 0
    );
    source = vfs_test_fixture_add_application_grant(
        &fixture,
        application_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        contents,
        sizeof(contents) - 1
    );
    selected = request(MICROS_VFS_MESSAGE_WRITE);
    selected.descriptor = 3;
    selected.grant = source;
    selected.count = sizeof(contents) - 1;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && action.transferred_count == sizeof(contents) - 1
        && action.position == sizeof(contents) - 1
        && fixture.backend_grant_count == 0
    );
    open_file_slot = state.processes[0].descriptors[3] - 1;
    original_position = state.open_files[open_file_slot].position;
    original_size = state.vnodes[
        state.open_files[open_file_slot].vnode - 1
    ].size;
    fixture.inject_ramfs_result = true;
    fixture.injected_ramfs_operation = MICROS_VFS_RAMFS_WRITE;
    fixture.injected_ramfs_result = MICROS_RAMFS_RESULT_NO_SPACE;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_NO_SPACE
        && state.open_files[open_file_slot].position
            == original_position
        && state.vnodes[
            state.open_files[open_file_slot].vnode - 1
        ].size == original_size
        && fixture.backend_grant_count == 0
    );
    selected = request(MICROS_VFS_MESSAGE_CLOSE);
    selected.descriptor = 3;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
    );
    selected = path_request(
        MICROS_VFS_MESSAGE_OPEN,
        "/tmp/note",
        MICROS_VFS_OPEN_READ | MICROS_VFS_OPEN_EXCLUSIVE
            | MICROS_VFS_OPEN_CREATE,
        UINT32_C(0600)
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_EXISTS
    );
    selected = path_request(
        MICROS_VFS_MESSAGE_OPEN,
        "/tmp/exclusive",
        MICROS_VFS_OPEN_READ | MICROS_VFS_OPEN_EXCLUSIVE
            | MICROS_VFS_OPEN_CREATE,
        UINT32_C(0600)
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && action.descriptor == 3
        && vfs_test_fixture_find_path(
            &fixture,
            "/tmp/exclusive"
        ) != 0
    );
    selected = request(MICROS_VFS_MESSAGE_CLOSE);
    selected.descriptor = 3;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
    );
    selected = path_request(
        MICROS_VFS_MESSAGE_OPEN,
        "/tmp/missing/",
        MICROS_VFS_OPEN_READ | MICROS_VFS_OPEN_CREATE,
        UINT32_C(0600)
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_NOT_DIRECTORY
        && vfs_test_fixture_find_path(
            &fixture,
            "/tmp/missing"
        ) == 0
    );
    selected = path_request(
        MICROS_VFS_MESSAGE_CHDIR,
        "/tmp",
        0,
        0
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && state.processes[0].working_directory_vnode != 1
    );
    selected = path_request(
        MICROS_VFS_MESSAGE_OPEN,
        "note",
        MICROS_VFS_OPEN_READ,
        0
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && action.descriptor == 3
    );
    destination = vfs_test_fixture_add_application_grant(
        &fixture,
        application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        NULL,
        sizeof(contents) - 1
    );
    selected = request(MICROS_VFS_MESSAGE_READ);
    selected.descriptor = 3;
    selected.grant = destination;
    selected.count = sizeof(contents) - 1;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && action.transferred_count == sizeof(contents) - 1
        && memcmp(
            vfs_test_fixture_application_bytes(
                &fixture,
                destination
            ),
            contents,
            sizeof(contents) - 1
        ) == 0
    );
    selected = request(MICROS_VFS_MESSAGE_CLOSE);
    selected.descriptor = 3;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
    );
    selected = path_request(
        MICROS_VFS_MESSAGE_CHDIR,
        "/",
        0,
        0
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && state.processes[0].working_directory_vnode == 1
        && micros_vfs_state_validate(&state) == MICROS_VFS_CORE_OK
        && note
            == vfs_test_fixture_find_path(
                &fixture,
                "/tmp/note"
            )
    );
    return true;
}

static bool getdents_translation_and_cursor_are_exact(void)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    micros_grant_t destination;
    uint8_t *records;
    size_t validate_calls;

    EXPECT_TRUE(initialize_full_fixture());
    selected = path_request(
        MICROS_VFS_MESSAGE_MKDIR,
        "/tmp",
        0,
        UINT32_C(0755)
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
    );
    selected = path_request(
        MICROS_VFS_MESSAGE_OPEN,
        "/",
        MICROS_VFS_OPEN_READ | MICROS_VFS_OPEN_DIRECTORY,
        0
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && action.descriptor == 3
    );
    destination = vfs_test_fixture_add_application_grant(
        &fixture,
        application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        NULL,
        320
    );
    selected = request(MICROS_VFS_MESSAGE_GETDENTS);
    selected.descriptor = 3;
    selected.grant = destination;
    selected.count = 160;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && action.transferred_count == 160
        && action.position == 3
    );
    records = vfs_test_fixture_application_bytes(
        &fixture,
        destination
    );
    EXPECT_TRUE(
        read_u32_le(&records[0]) == 80
        && read_u32_le(&records[4])
            == (
                MICROS_RAMFS_MODE_DIRECTORY
                | UINT32_C(0755)
            )
        && read_u32_le(&records[8]) == 1
        && read_u32_le(&records[12]) == 0
        && records[16] == '.'
        && records[17] == 0
        && read_u32_le(&records[80]) == 80
        && read_u32_le(&records[88]) == 2
        && records[96] == '.'
        && records[97] == '.'
        && records[98] == 0
    );
    fixture.reject_client_copy = true;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_GRANT
        && state.open_files[
            state.processes[0].descriptors[3] - 1
        ].position == 3
    );
    fixture.reject_client_copy = false;
    selected.grant_offset = 160;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && action.transferred_count == 160
        && action.position == MICROS_RAMFS_DIRECTORY_CURSOR_END
        && read_u32_le(&records[160]) == 80
        && read_u32_le(&records[168]) == 3
        && memcmp(&records[176], "etc", 3) == 0
        && read_u32_le(&records[240]) == 80
        && read_u32_le(&records[248]) == 3
        && memcmp(&records[256], "tmp", 3) == 0
    );
    EXPECT_TRUE(
        vfs_test_fixture_revoke_application_grant(
            &fixture,
            destination
        )
    );
    validate_calls = fixture.client_validate_calls;
    selected.grant_offset = 0;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && action.transferred_count == 0
        && action.position == MICROS_RAMFS_DIRECTORY_CURSOR_END
        && fixture.client_validate_calls == validate_calls
    );
    selected = request(MICROS_VFS_MESSAGE_GETDENTS);
    selected.descriptor = 3;
    selected.count = 81;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_RANGE
    );
    return true;
}

static bool console_async_and_cleanup_are_exact(void)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    enum micros_vfs_trusted_result trusted_result;
    micros_grant_t destination;
    micros_grant_t source;
    uint64_t events;
    size_t tty_calls;
    static const uint8_t input[] = "ok\n";
    static const uint8_t output[] = "hello";

    EXPECT_TRUE(initialize_full_fixture());
    destination = vfs_test_fixture_add_application_grant(
        &fixture,
        application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        NULL,
        16
    );
    selected = request(MICROS_VFS_MESSAGE_READ);
    selected.descriptor = 0;
    selected.grant = destination;
    selected.count = 16;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && !action.active
        && state.pending.state
            == MICROS_VFS_PENDING_TTY_READ_WAIT_COMPLETION
        && state.backend_page_owner == MICROS_VFS_PAGE_TTY_READ
        && fixture.backend_grant_count == 1
        && state.next_tty_request_id == 2
    );
    selected = request(MICROS_VFS_MESSAGE_CLOSE);
    selected.descriptor = 2;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_BUSY
    );
    selected.descriptor = MICROS_VFS_DESCRIPTOR_CAPACITY;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_DESCRIPTOR
    );
    EXPECT_TRUE(
        vfs_test_fixture_complete_read(
            &fixture,
            input,
            sizeof(input) - 1
        )
        && micros_vfs_handle_tty_notification(
            &state,
            MICROS_TTY_EVENT_COMPLETION,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.active
        && action.request_type == MICROS_VFS_MESSAGE_READ
        && action.result == MICROS_VFS_RESULT_OK
        && action.transferred_count == sizeof(input) - 1
        && action.position == 0
        && memcmp(
            vfs_test_fixture_application_bytes(
                &fixture,
                destination
            ),
            input,
            sizeof(input) - 1
        ) == 0
        && state.pending.state == MICROS_VFS_PENDING_NONE
        && fixture.backend_grant_count == 0
    );

    EXPECT_TRUE(
        initialize_full_fixture()
        && vfs_test_fixture_queue_input(
            &fixture,
            input,
            sizeof(input) - 1
        )
    );
    destination = vfs_test_fixture_add_application_grant(
        &fixture,
        application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        NULL,
        16
    );
    selected = request(MICROS_VFS_MESSAGE_READ);
    selected.descriptor = 0;
    selected.grant = destination;
    selected.count = 16;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && !action.active
        && state.pending.state
            == MICROS_VFS_PENDING_TTY_READ_WAIT_COMPLETION
        && !fixture.pending_read.active
        && fixture.read_completion.active
        && micros_vfs_handle_tty_notification(
            &state,
            MICROS_TTY_EVENT_COMPLETION,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.active
        && action.transferred_count == sizeof(input) - 1
        && memcmp(
            vfs_test_fixture_application_bytes(
                &fixture,
                destination
            ),
            input,
            sizeof(input) - 1
        ) == 0
        && state.pending.state == MICROS_VFS_PENDING_NONE
        && fixture.backend_grant_count == 0
    );

    source = vfs_test_fixture_add_application_grant(
        &fixture,
        application_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        output,
        sizeof(output) - 1
    );
    selected = request(MICROS_VFS_MESSAGE_WRITE);
    selected.descriptor = 1;
    selected.grant = source;
    selected.count = sizeof(output) - 1;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && !action.active
        && state.pending.state
            == MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION
        && fixture.backend_grant_count == 0
        && state.next_tty_request_id == 3
        && fixture.tty_output_length == sizeof(output) - 1
        && memcmp(
            fixture.tty_output,
            output,
            sizeof(output) - 1
        ) == 0
    );
    snapshot = state;
    EXPECT_TRUE(
        micros_vfs_handle_tty_notification(
            &state,
            MICROS_TTY_EVENT_COMPLETION
                | MICROS_TTY_EVENT_WRITABLE,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_ERROR_INVARIANT
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );
    EXPECT_TRUE(
        micros_vfs_handle_tty_notification(
            &state,
            MICROS_TTY_EVENT_COMPLETION,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.active
        && action.request_type == MICROS_VFS_MESSAGE_WRITE
        && action.result == MICROS_VFS_RESULT_OK
        && action.transferred_count == sizeof(output) - 1
        && state.pending.state == MICROS_VFS_PENDING_NONE
    );
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && !action.active
        && state.pending.state
            == MICROS_VFS_PENDING_TTY_WRITE_WAIT_WRITABLE
        && fixture.backend_grant_count == 1
        && state.next_tty_request_id == 3
    );
    events = vfs_test_fixture_drain_output(&fixture);
    snapshot = state;
    EXPECT_TRUE(
        events == MICROS_TTY_EVENT_WRITABLE
        && micros_vfs_handle_tty_notification(
            &state,
            events | MICROS_TTY_EVENT_COMPLETION,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_ERROR_INVARIANT
        && memcmp(&state, &snapshot, sizeof(state)) == 0
        && fixture.backend_grant_count == 1
        && state.next_tty_request_id == 3
        && micros_vfs_handle_tty_notification(
            &state,
            events,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && !action.active
        && state.pending.state
            == MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION
        && fixture.backend_grant_count == 0
        && state.next_tty_request_id == 4
        && micros_vfs_handle_tty_notification(
            &state,
            MICROS_TTY_EVENT_COMPLETION,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.active
        && action.transferred_count == sizeof(output) - 1
        && state.pending.state == MICROS_VFS_PENDING_NONE
    );

    EXPECT_TRUE(initialize_full_fixture());
    destination = vfs_test_fixture_add_application_grant(
        &fixture,
        application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        NULL,
        8
    );
    selected = request(MICROS_VFS_MESSAGE_READ);
    selected.descriptor = 0;
    selected.grant = destination;
    selected.count = 8;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && micros_vfs_detach(
            &state,
            application_endpoint,
            &fixture.io,
            &trusted_result
        ) == MICROS_VFS_CORE_OK
        && trusted_result == MICROS_VFS_TRUSTED_OK
        && state.pending.state == MICROS_VFS_PENDING_NONE
        && fixture.backend_grant_count == 0
        && state.vnodes[0].local_reference_count == 1
    );

    EXPECT_TRUE(initialize_full_fixture());
    destination = vfs_test_fixture_add_application_grant(
        &fixture,
        application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        NULL,
        8
    );
    selected = request(MICROS_VFS_MESSAGE_READ);
    selected.descriptor = 0;
    selected.grant = destination;
    selected.count = 8;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && vfs_test_fixture_complete_read(
            &fixture,
            input,
            sizeof(input) - 1
        )
        && micros_vfs_detach(
            &state,
            application_endpoint,
            &fixture.io,
            &trusted_result
        ) == MICROS_VFS_CORE_OK
        && trusted_result == MICROS_VFS_TRUSTED_PENDING
        && state.pending.state
            == MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT
        && fixture.backend_grant_count == 0
        && micros_vfs_handle_tty_notification(
            &state,
            MICROS_TTY_EVENT_COMPLETION,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && !action.active
        && state.pending.state == MICROS_VFS_PENDING_NONE
        && micros_vfs_state_validate(&state) == MICROS_VFS_CORE_OK
    );

    EXPECT_TRUE(initialize_full_fixture());
    source = vfs_test_fixture_add_application_grant(
        &fixture,
        application_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        output,
        sizeof(output) - 1
    );
    selected = request(MICROS_VFS_MESSAGE_WRITE);
    selected.descriptor = 1;
    selected.grant = source;
    selected.count = sizeof(output) - 1;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && state.pending.state
            == MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION
        && micros_vfs_detach(
            &state,
            application_endpoint,
            &fixture.io,
            &trusted_result
        ) == MICROS_VFS_CORE_OK
        && trusted_result == MICROS_VFS_TRUSTED_PENDING
        && state.pending.state
            == MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT
        && fixture.backend_grant_count == 0
        && state.vnodes[0].local_reference_count == 1
        && micros_vfs_handle_tty_notification(
            &state,
            MICROS_TTY_EVENT_COMPLETION,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && !action.active
        && state.pending.state == MICROS_VFS_PENDING_NONE
    );

    EXPECT_TRUE(initialize_full_fixture());
    fixture.tty_output_busy = true;
    source = vfs_test_fixture_add_application_grant(
        &fixture,
        application_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        output,
        sizeof(output) - 1
    );
    selected = request(MICROS_VFS_MESSAGE_WRITE);
    selected.descriptor = 1;
    selected.grant = source;
    selected.count = sizeof(output) - 1;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && state.pending.state
            == MICROS_VFS_PENDING_TTY_WRITE_WAIT_WRITABLE
        && micros_vfs_detach(
            &state,
            application_endpoint,
            &fixture.io,
            &trusted_result
        ) == MICROS_VFS_CORE_OK
        && trusted_result == MICROS_VFS_TRUSTED_PENDING
        && state.pending.state
            == MICROS_VFS_PENDING_TTY_WRITABLE_NOTICE_DEBT
        && fixture.backend_grant_count == 0
        && state.next_tty_request_id == 2
    );
    fixture.tty_output_busy = false;
    EXPECT_TRUE(
        micros_vfs_handle_tty_notification(
            &state,
            MICROS_TTY_EVENT_WRITABLE,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && state.pending.state == MICROS_VFS_PENDING_NONE
    );

    EXPECT_TRUE(initialize_full_fixture());
    destination = vfs_test_fixture_add_application_grant(
        &fixture,
        application_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        NULL,
        8
    );
    selected = request(MICROS_VFS_MESSAGE_READ);
    selected.descriptor = 0;
    selected.grant = destination;
    selected.count = 8;
    tty_calls = fixture.tty_calls;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_GRANT
        && fixture.tty_calls == tty_calls
    );
    state.next_tty_request_id = 0;
    source = vfs_test_fixture_add_application_grant(
        &fixture,
        application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        output,
        sizeof(output) - 1
    );
    selected = request(MICROS_VFS_MESSAGE_WRITE);
    selected.descriptor = 1;
    selected.grant = source;
    selected.count = sizeof(output) - 1;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_GRANT
        && fixture.tty_calls == tty_calls
    );
    source = vfs_test_fixture_add_application_grant(
        &fixture,
        application_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        output,
        sizeof(output) - 1
    );
    selected.grant = source;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_NO_SPACE
        && fixture.tty_calls == tty_calls
    );
    destination = vfs_test_fixture_add_application_grant(
        &fixture,
        application_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        NULL,
        8
    );
    selected = request(MICROS_VFS_MESSAGE_READ);
    selected.descriptor = 0;
    selected.grant = destination;
    selected.count = 8;
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &selected,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_NO_SPACE
        && fixture.tty_calls == tty_calls
    );
    return true;
}

static bool backend_reference_cleanup_is_exact(void)
{
    struct micros_vfs_request selected;
    struct micros_vfs_result_action action;
    size_t index;

    EXPECT_TRUE(initialize_full_fixture());
    for (
        index = 0;
        index < MICROS_VFS_BACKEND_REFERENCE_THRESHOLD - 1;
        ++index
    ) {
        selected = path_request(
            MICROS_VFS_MESSAGE_CHDIR,
            "/",
            0,
            0
        );
        EXPECT_TRUE(
            selected.grant != MICROS_GRANT_NONE
            && micros_vfs_handle_request(
                &state,
                &selected,
                &fixture.io,
                &action
            ) == MICROS_VFS_CORE_OK
            && action.result == MICROS_VFS_RESULT_OK
            && vfs_test_fixture_revoke_application_grant(
                &fixture,
                selected.grant
            )
        );
    }
    EXPECT_TRUE(
        fixture.putnode_calls == 1
        && fixture.putnode_count
            == MICROS_VFS_BACKEND_REFERENCE_THRESHOLD - 1
        && fixture.last_putnode
            == UINT64_C(0x0000000100000000)
        && fixture.last_putnode_count
            == MICROS_VFS_BACKEND_REFERENCE_THRESHOLD - 1
        && fixture.nodes[0].backend_references == 1
        && state.vnodes[0].backend_reference_count == 1
        && micros_vfs_state_validate(&state) == MICROS_VFS_CORE_OK
    );
    return true;
}

static bool test_drain_barrier_is_no_authority(void)
{
    struct micros_vfs_result_action action;
    uint64_t events;

    EXPECT_TRUE(initialize_full_fixture());
    fixture.tty_output_busy = true;
    EXPECT_TRUE(
        micros_vfs_begin_test_drain(
            &state,
            application_endpoint,
            99,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && !action.active
        && state.pending.state
            == MICROS_VFS_PENDING_TTY_TEST_DRAIN_WAIT_WRITABLE
        && state.pending.reply_token == 99
        && state.pending.request_id == 1
        && state.pending.tty_grant == MICROS_GRANT_NONE
        && fixture.backend_grant_count == 0
        && fixture.tty_output_length == 0
    );
    events = vfs_test_fixture_drain_output(&fixture);
    EXPECT_TRUE(
        events == MICROS_TTY_EVENT_WRITABLE
        && micros_vfs_handle_tty_notification(
            &state,
            events,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.active
        && action.reply_token == 99
        && action.request_type == MICROS_VFS_TEST_MESSAGE_DRAIN
        && action.result == MICROS_VFS_RESULT_OK
        && action.transferred_count == 0
        && state.pending.state == MICROS_VFS_PENDING_NONE
        && state.next_tty_request_id == 2
        && fixture.backend_grant_count == 0
        && fixture.tty_output_length == 0
    );
    EXPECT_TRUE(
        micros_vfs_begin_test_drain(
            &state,
            application_endpoint,
            100,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.active
        && action.reply_token == 100
        && action.result == MICROS_VFS_RESULT_OK
        && state.pending.state == MICROS_VFS_PENDING_NONE
        && state.next_tty_request_id == 3
    );
    return true;
}

int main(void)
{
    return (
        constants_and_initial_state_are_exact()
        && mount_attach_share_and_detach_are_atomic()
        && complete_state_validation_rejects_dangling_state()
        && open_read_close_and_validation_are_exact()
        && path_grant_precedes_local_capacity()
        && backend_reply_validation_is_operation_specific()
        && create_write_mkdir_and_chdir_are_exact()
        && getdents_translation_and_cursor_are_exact()
        && console_async_and_cleanup_are_exact()
        && backend_reference_cleanup_is_exact()
        && test_drain_barrier_is_no_authority()
    ) ? 0 : 1;
}
