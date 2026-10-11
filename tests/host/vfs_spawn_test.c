#include "servers/vfs/vfs_core.h"
#include "tests/host/vfs_test_fixture.h"
#include "lib/spawn/spawn_request.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/spawn.h"

bool micros_vfs_spawn_test_run(void);

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

static const micros_endpoint_t vfs_endpoint =
    UINT32_C(0x00001006);
static const micros_endpoint_t vm_endpoint =
    UINT32_C(0x00001002);
static const micros_endpoint_t pm_endpoint =
    UINT32_C(0x00001003);
static const micros_endpoint_t ramfs_endpoint =
    UINT32_C(0x00001005);
static const micros_endpoint_t tty_endpoint =
    UINT32_C(0x00001004);
static const micros_endpoint_t parent_endpoint =
    UINT32_C(0x00002007);

static struct micros_vfs_state state;
static struct micros_vfs_state snapshot;
static struct vfs_test_fixture fixture;
static uint8_t request_bytes[MICROS_SPAWN_REQUEST_SIZE_MAX];
static uint8_t executable_bytes[
    MICROS_APPLICATION_EXECUTABLE_SIZE_MAX + 1
];

static void write_u16_le(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static bool bytes_are_zero(const void *storage, size_t size)
{
    const uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static size_t build_spawn_request(const char *path)
{
    static const char argument[] = "child";
    size_t path_size = strlen(path) + 1;
    size_t argument_size = sizeof(argument);
    size_t cursor = MICROS_SPAWN_REQUEST_HEADER_SIZE;

    memset(request_bytes, 0, sizeof(request_bytes));
    write_u32_le(&request_bytes[0], MICROS_SPAWN_REQUEST_VERSION);
    write_u32_le(
        &request_bytes[4],
        MICROS_SPAWN_REQUEST_HEADER_SIZE
    );
    write_u16_le(
        &request_bytes[MICROS_SPAWN_ARGUMENT_COUNT_OFFSET],
        1
    );
    write_u16_le(
        &request_bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET],
        (uint16_t)cursor
    );
    write_u16_le(
        &request_bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET + 2],
        (uint16_t)path_size
    );
    memcpy(&request_bytes[cursor], path, path_size);
    cursor += path_size;
    write_u16_le(
        &request_bytes[MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET],
        (uint16_t)cursor
    );
    write_u16_le(
        &request_bytes[
            MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET + 2
        ],
        (uint16_t)argument_size
    );
    memcpy(&request_bytes[cursor], argument, argument_size);
    cursor += argument_size;
    write_u32_le(&request_bytes[8], (uint32_t)cursor);
    return cursor;
}

static void set_descriptor_actions(
    const struct micros_spawn_descriptor_action *actions,
    size_t action_count
)
{
    size_t index;

    write_u16_le(
        &request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTION_COUNT_OFFSET],
        (uint16_t)action_count
    );
    for (index = 0; index < action_count; ++index) {
        size_t offset = MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET
            + index * MICROS_SPAWN_DESCRIPTOR_ACTION_SIZE;

        write_u16_le(&request_bytes[offset], actions[index].source);
        write_u16_le(
            &request_bytes[offset + 2],
            actions[index].destination
        );
        write_u32_le(
            &request_bytes[offset + 4],
            actions[index].operation
        );
    }
}

static size_t build_oversized_stack_request(const char *path)
{
    enum {
        STRING_SIZE = 120,
    };
    size_t path_size = strlen(path) + 1;
    size_t cursor = MICROS_SPAWN_REQUEST_HEADER_SIZE;
    size_t index;

    memset(request_bytes, 0, sizeof(request_bytes));
    write_u32_le(&request_bytes[0], MICROS_SPAWN_REQUEST_VERSION);
    write_u32_le(
        &request_bytes[4],
        MICROS_SPAWN_REQUEST_HEADER_SIZE
    );
    write_u16_le(
        &request_bytes[MICROS_SPAWN_ARGUMENT_COUNT_OFFSET],
        MICROS_SPAWN_ARGUMENT_CAPACITY
    );
    write_u16_le(
        &request_bytes[MICROS_SPAWN_ENVIRONMENT_COUNT_OFFSET],
        MICROS_SPAWN_ENVIRONMENT_CAPACITY
    );
    write_u16_le(
        &request_bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET],
        (uint16_t)cursor
    );
    write_u16_le(
        &request_bytes[
            MICROS_SPAWN_PATH_REFERENCE_OFFSET + 2
        ],
        (uint16_t)path_size
    );
    memcpy(&request_bytes[cursor], path, path_size);
    cursor += path_size;
    for (index = 0; index < MICROS_SPAWN_ARGUMENT_CAPACITY; ++index) {
        size_t reference =
            MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET
            + index * MICROS_SPAWN_STRING_REFERENCE_SIZE;

        write_u16_le(
            &request_bytes[reference],
            (uint16_t)cursor
        );
        write_u16_le(
            &request_bytes[reference + 2],
            STRING_SIZE
        );
        memset(&request_bytes[cursor], 'a', STRING_SIZE - 1);
        request_bytes[cursor + STRING_SIZE - 1] = 0;
        cursor += STRING_SIZE;
    }
    for (
        index = 0;
        index < MICROS_SPAWN_ENVIRONMENT_CAPACITY;
        ++index
    ) {
        size_t reference =
            MICROS_SPAWN_ENVIRONMENT_REFERENCES_OFFSET
            + index * MICROS_SPAWN_STRING_REFERENCE_SIZE;

        write_u16_le(
            &request_bytes[reference],
            (uint16_t)cursor
        );
        write_u16_le(
            &request_bytes[reference + 2],
            STRING_SIZE
        );
        request_bytes[cursor] = 'A';
        request_bytes[cursor + 1] = '=';
        memset(
            &request_bytes[cursor + 2],
            'v',
            STRING_SIZE - 3
        );
        request_bytes[cursor + STRING_SIZE - 1] = 0;
        cursor += STRING_SIZE;
    }
    write_u32_le(&request_bytes[8], (uint32_t)cursor);
    return cursor;
}

static bool initialize_fixture(void)
{
    enum micros_vfs_trusted_result result;

    vfs_test_fixture_initialize(
        &fixture,
        vfs_endpoint,
        vm_endpoint,
        pm_endpoint,
        ramfs_endpoint,
        tty_endpoint
    );
    return (
        micros_vfs_state_initialize(
            &state,
            vfs_endpoint,
            vm_endpoint,
            pm_endpoint,
            ramfs_endpoint,
            tty_endpoint
        ) == MICROS_VFS_CORE_OK
        && micros_vfs_mount(&state, &fixture.io)
            == MICROS_VFS_CORE_OK
        && micros_vfs_attach_console(
            &state,
            parent_endpoint,
            0,
            &result
        ) == MICROS_VFS_CORE_OK
        && result == MICROS_VFS_TRUSTED_OK
    );
}

static struct micros_vfs_control_request prepare_request(
    size_t request_size,
    uint64_t spawn_token
)
{
    struct micros_vfs_control_request request = {
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .command = MICROS_VFS_CONTROL_PREPARE_IMAGE,
        .source = pm_endpoint,
        .reply_token = UINT64_C(0x71),
        .lifecycle_token = spawn_token,
        .semantic_pid = UINT64_C(0x0000000100000007),
        .parent_endpoint = parent_endpoint,
        .request_size = (uint32_t)request_size,
    };

    request.request_grant =
        vfs_test_fixture_add_application_grant(
            &fixture,
            pm_endpoint,
            MICROS_GRANT_PERMISSION_READ,
            request_bytes,
            request_size
        );
    return request;
}

static struct micros_vfs_control_request release_request(
    uint64_t spawn_token,
    uint64_t image_token
)
{
    return (struct micros_vfs_control_request){
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .command = MICROS_VFS_CONTROL_RELEASE_IMAGE,
        .source = pm_endpoint,
        .reply_token = UINT64_C(0x72),
        .lifecycle_token = spawn_token,
        .image_token = image_token,
    };
}

static struct micros_vfs_control_request prepare_descriptors_request(
    uint64_t spawn_token,
    uint64_t image_token
)
{
    return (struct micros_vfs_control_request){
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .command = MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS,
        .source = pm_endpoint,
        .reply_token = UINT64_C(0x73),
        .lifecycle_token = spawn_token,
        .image_token = image_token,
        .semantic_pid = UINT64_C(0x0000000100000007),
    };
}

static struct micros_vfs_control_request abort_descriptors_request(
    uint64_t spawn_token,
    uint64_t image_token,
    uint64_t descriptor_token
)
{
    return (struct micros_vfs_control_request){
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .command = MICROS_VFS_CONTROL_ABORT_DESCRIPTORS,
        .source = pm_endpoint,
        .reply_token = UINT64_C(0x74),
        .lifecycle_token = spawn_token,
        .image_token = image_token,
        .descriptor_token = descriptor_token,
    };
}

static void fill_executable(size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        executable_bytes[index] = (uint8_t)(index * 37 + 11);
    }
}

static size_t active_process_count(void)
{
    size_t count = 0;
    size_t index;

    for (index = 0; index < MICROS_VFS_PROCESS_CAPACITY; ++index) {
        if (
            state.processes[index].state
                == MICROS_VFS_PROCESS_ACTIVE
        ) {
            ++count;
        }
    }
    return count;
}

static size_t active_console_open_file_count(uint32_t access)
{
    size_t count = 0;
    size_t index;

    for (index = 0; index < MICROS_VFS_OPEN_FILE_CAPACITY; ++index) {
        if (
            state.open_files[index].state
                == MICROS_VFS_OPEN_FILE_ACTIVE
            && state.open_files[index].kind
                == MICROS_VFS_OBJECT_CONSOLE
            && state.open_files[index].access == access
        ) {
            ++count;
        }
    }
    return count;
}

static bool successful_prepare_and_release_are_exact(void)
{
    struct micros_vfs_control_request request;
    struct micros_vfs_control_request release;
    struct micros_vfs_control_result_action action;
    const struct vfs_test_backend_grant *executable_grant;
    const struct vfs_test_backend_grant *request_grant;
    size_t request_size;
    size_t executable_size = MICROS_VFS_TRANSFER_MAX + 17;
    uint64_t image_token;

    EXPECT_TRUE(initialize_fixture());
    fill_executable(executable_size);
    EXPECT_TRUE(
        vfs_test_fixture_add_root_file_with_mode(
            &fixture,
            "child",
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
            executable_bytes,
            executable_size
        )
    );
    request_size = build_spawn_request("/child");
    request = prepare_request(request_size, UINT64_C(0x101));
    EXPECT_TRUE(
        request.request_grant != MICROS_GRANT_NONE
        && micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.active
        && action.reply_token == UINT64_C(0x71)
        && action.command == MICROS_VFS_CONTROL_PREPARE_IMAGE
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
        && action.image_token != 0
        && action.executable_grant != MICROS_GRANT_NONE
        && action.executable_size == executable_size
        && action.request_grant != MICROS_GRANT_NONE
        && action.request_size == request_size
        && action.descriptor_token == 0
        && state.spawn.phase == MICROS_VFS_SPAWN_IMAGE_PREPARED
        && state.spawn.pm_spawn_token == UINT64_C(0x101)
        && state.spawn.image_token == action.image_token
        && state.spawn.semantic_pid
            == UINT64_C(0x0000000100000007)
        && state.spawn.parent_endpoint == parent_endpoint
        && !state.spawn.trusted_init
        && state.spawn.request_size == request_size
        && state.spawn.executable_size == executable_size
        && state.last_image_token == action.image_token
        && memcmp(
            state.spawn_request_bytes,
            request_bytes,
            request_size
        ) == 0
        && memcmp(
            state.spawn_executable_bytes,
            executable_bytes,
            executable_size
        ) == 0
        && fixture.backend_grant_count == 2
        && micros_vfs_state_validate(&state) == MICROS_VFS_CORE_OK
    );
    executable_grant =
        vfs_test_fixture_backend_grant(
            &fixture,
            action.executable_grant
        );
    request_grant = vfs_test_fixture_backend_grant(
        &fixture,
        action.request_grant
    );
    EXPECT_TRUE(
        executable_grant != NULL
        && executable_grant->endpoint == vm_endpoint
        && executable_grant->local
            == state.spawn_executable_bytes
        && executable_grant->length == executable_size
        && executable_grant->permission
            == MICROS_GRANT_PERMISSION_READ
        && request_grant != NULL
        && request_grant->endpoint == vm_endpoint
        && request_grant->local == state.spawn_request_bytes
        && request_grant->length == request_size
        && request_grant->permission
            == MICROS_GRANT_PERMISSION_READ
        && vfs_test_fixture_application_bytes(
            &fixture,
            request.request_grant
        ) != NULL
    );

    image_token = action.image_token;
    release = release_request(UINT64_C(0x101), image_token);
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &release,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.active
        && action.reply_token == UINT64_C(0x72)
        && action.command == MICROS_VFS_CONTROL_RELEASE_IMAGE
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
        && state.spawn.phase == MICROS_VFS_SPAWN_IDLE
        && state.last_image_token == image_token
        && bytes_are_zero(&state.spawn, sizeof(state.spawn))
        && bytes_are_zero(
            state.spawn_request_bytes,
            sizeof(state.spawn_request_bytes)
        )
        && bytes_are_zero(
            state.spawn_executable_bytes,
            sizeof(state.spawn_executable_bytes)
        )
        && fixture.backend_grant_count == 0
        && fixture.putnode_count == 1
        && micros_vfs_state_validate(&state) == MICROS_VFS_CORE_OK
    );
    return true;
}

static bool public_descriptor_prepare_and_abort_are_exact(void)
{
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
    struct micros_vfs_control_request descriptors;
    struct micros_vfs_control_request abort;
    struct micros_vfs_control_request release;
    struct micros_vfs_control_result_action action;
    enum micros_vfs_trusted_result trusted_result;
    uint16_t parent_read;
    uint16_t parent_write;
    uint32_t root_references;
    uint64_t image_token;
    uint64_t descriptor_token;
    size_t request_size;

    EXPECT_TRUE(initialize_fixture());
    fill_executable(64);
    EXPECT_TRUE(
        vfs_test_fixture_add_root_file_with_mode(
            &fixture,
            "child",
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
            executable_bytes,
            64
        )
    );
    request_size = build_spawn_request("/child");
    set_descriptor_actions(
        actions,
        sizeof(actions) / sizeof(actions[0])
    );
    request = prepare_request(request_size, UINT64_C(0x111));
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
    );
    image_token = action.image_token;
    parent_read = state.processes[0].descriptors[0];
    parent_write = state.processes[0].descriptors[1];
    root_references = state.vnodes[0].local_reference_count;
    descriptors = prepare_descriptors_request(
        UINT64_C(0x111),
        image_token
    );
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &descriptors,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.active
        && action.reply_token == UINT64_C(0x73)
        && action.command
            == MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
        && action.image_token == 0
        && action.executable_grant == 0
        && action.executable_size == 0
        && action.request_grant == 0
        && action.request_size == 0
        && action.descriptor_token != 0
        && state.spawn.phase
            == MICROS_VFS_SPAWN_DESCRIPTORS_PREPARED
        && active_process_count() == 1
        && state.vnodes[0].local_reference_count
            == root_references + 2
        && state.open_files[parent_read - 1].reference_count == 2
        && state.open_files[parent_write - 1].reference_count == 3
        && fixture.backend_grant_count == 2
        && micros_vfs_state_validate(&state) == MICROS_VFS_CORE_OK
    );
    descriptor_token = action.descriptor_token;

    snapshot = state;
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &descriptors,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_STATE
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );
    release = release_request(UINT64_C(0x111), image_token);
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &release,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_STATE
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );
    EXPECT_TRUE(
        micros_vfs_detach(
            &state,
            parent_endpoint,
            &fixture.io,
            &trusted_result
        ) == MICROS_VFS_CORE_OK
        && trusted_result == MICROS_VFS_TRUSTED_BUSY
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );

    abort = abort_descriptors_request(
        UINT64_C(0x111),
        image_token,
        descriptor_token
    );
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &abort,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.active
        && action.reply_token == UINT64_C(0x74)
        && action.command
            == MICROS_VFS_CONTROL_ABORT_DESCRIPTORS
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
        && action.descriptor_token == 0
        && state.spawn.phase == MICROS_VFS_SPAWN_IMAGE_PREPARED
        && state.vnodes[0].local_reference_count
            == root_references
        && state.open_files[parent_read - 1].reference_count == 1
        && state.open_files[parent_write - 1].reference_count == 1
        && fixture.backend_grant_count == 2
        && micros_vfs_state_validate(&state) == MICROS_VFS_CORE_OK
    );

    snapshot = state;
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &abort,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_STATE
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &descriptors,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
        && action.descriptor_token == descriptor_token + 1
    );
    abort.descriptor_token = action.descriptor_token;
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &abort,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
    );
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &release,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
    );
    return true;
}

static bool reserved_process_slot_and_capacity_are_exact(void)
{
    struct micros_vfs_control_request request;
    struct micros_vfs_control_request descriptors;
    struct micros_vfs_control_request abort;
    struct micros_vfs_control_request release;
    struct micros_vfs_control_result_action action;
    enum micros_vfs_trusted_result trusted_result;
    size_t request_size;
    size_t index;
    uint64_t image_token;
    micros_endpoint_t final_endpoint =
        UINT32_C(0x0000303e);

    EXPECT_TRUE(initialize_fixture());
    fill_executable(64);
    EXPECT_TRUE(
        vfs_test_fixture_add_root_file_with_mode(
            &fixture,
            "child",
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
            executable_bytes,
            64
        )
    );
    request_size = build_spawn_request("/child");
    request = prepare_request(request_size, UINT64_C(0x114));
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
    );
    image_token = action.image_token;
    for (index = 0; index < 62; ++index) {
        EXPECT_TRUE(
            micros_vfs_attach_console(
                &state,
                UINT32_C(0x00003000) + (uint32_t)index,
                0,
                &trusted_result
            ) == MICROS_VFS_CORE_OK
            && trusted_result == MICROS_VFS_TRUSTED_OK
        );
    }
    EXPECT_TRUE(active_process_count() == 63);

    descriptors = prepare_descriptors_request(
        UINT64_C(0x114),
        image_token
    );
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &descriptors,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
        && state.processes[
            state.spawn.child_process_reference - 1
        ].state
            == MICROS_VFS_PROCESS_FREE
        && active_process_count() == 63
    );
    abort = abort_descriptors_request(
        UINT64_C(0x114),
        image_token,
        action.descriptor_token
    );
    snapshot = state;
    EXPECT_TRUE(
        micros_vfs_attach_console(
            &state,
            final_endpoint,
            0,
            &trusted_result
        ) == MICROS_VFS_CORE_OK
        && trusted_result == MICROS_VFS_TRUSTED_NO_SPACE
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &abort,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
    );
    EXPECT_TRUE(
        micros_vfs_attach_console(
            &state,
            final_endpoint,
            0,
            &trusted_result
        ) == MICROS_VFS_CORE_OK
        && trusted_result == MICROS_VFS_TRUSTED_OK
        && active_process_count() == 64
    );

    snapshot = state;
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &descriptors,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_NO_SPACE
        && action.descriptor_token == 0
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );
    release = release_request(UINT64_C(0x114), image_token);
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &release,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
    );
    return true;
}

static bool descriptor_token_exhaustion_is_atomic(void)
{
    struct micros_vfs_control_request request;
    struct micros_vfs_control_request descriptors;
    struct micros_vfs_control_request abort;
    struct micros_vfs_control_request release;
    struct micros_vfs_control_result_action action;
    size_t request_size;
    uint64_t image_token;

    EXPECT_TRUE(initialize_fixture());
    fill_executable(64);
    EXPECT_TRUE(
        vfs_test_fixture_add_root_file_with_mode(
            &fixture,
            "child",
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
            executable_bytes,
            64
        )
    );
    request_size = build_spawn_request("/child");
    request = prepare_request(request_size, UINT64_C(0x115));
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
    );
    image_token = action.image_token;
    state.last_descriptor_token = UINT64_MAX - 1;
    EXPECT_TRUE(
        micros_vfs_state_validate(&state) == MICROS_VFS_CORE_OK
    );
    descriptors = prepare_descriptors_request(
        UINT64_C(0x115),
        image_token
    );
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &descriptors,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
        && action.descriptor_token == UINT64_MAX
    );
    abort = abort_descriptors_request(
        UINT64_C(0x115),
        image_token,
        UINT64_MAX
    );
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &abort,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
    );
    snapshot = state;
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &descriptors,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_ERROR_INVARIANT
        && !action.active
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );
    release = release_request(UINT64_C(0x115), image_token);
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &release,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
    );
    return true;
}

static bool descriptor_state_corruption_is_rejected(void)
{
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

    EXPECT_TRUE(initialize_fixture());
    fill_executable(64);
    EXPECT_TRUE(
        vfs_test_fixture_add_root_file_with_mode(
            &fixture,
            "child",
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
            executable_bytes,
            64
        )
    );
    request_size = build_spawn_request("/child");
    set_descriptor_actions(
        actions,
        sizeof(actions) / sizeof(actions[0])
    );
    request = prepare_request(request_size, UINT64_C(0x116));
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
    );
    descriptors = prepare_descriptors_request(
        UINT64_C(0x116),
        action.image_token
    );
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &descriptors,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
    );
    snapshot = state;

    state.spawn.descriptor_token ^= UINT64_C(1);
    EXPECT_TRUE(
        micros_vfs_state_validate(&state)
            == MICROS_VFS_CORE_ERROR_INVARIANT
    );
    state = snapshot;
    state.spawn.child_descriptors[4] =
        snapshot.spawn.child_descriptors[5];
    state.spawn.child_descriptors[5] =
        snapshot.spawn.child_descriptors[4];
    EXPECT_TRUE(
        micros_vfs_state_validate(&state)
            == MICROS_VFS_CORE_ERROR_INVARIANT
    );
    state = snapshot;
    state.spawn.child_descriptors[3] =
        state.spawn.child_descriptors[4];
    EXPECT_TRUE(
        micros_vfs_state_validate(&state)
            == MICROS_VFS_CORE_ERROR_INVARIANT
    );
    state = snapshot;
    state.processes[
        state.spawn.child_process_reference - 1
    ].state =
        MICROS_VFS_PROCESS_ACTIVE;
    EXPECT_TRUE(
        micros_vfs_state_validate(&state)
            == MICROS_VFS_CORE_ERROR_INVARIANT
    );
    state = snapshot;
    return true;
}

static bool descriptor_failures_are_atomic(void)
{
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

    EXPECT_TRUE(initialize_fixture());
    fill_executable(64);
    EXPECT_TRUE(
        vfs_test_fixture_add_root_file_with_mode(
            &fixture,
            "child",
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
            executable_bytes,
            64
        )
    );
    request_size = build_spawn_request("/child");
    set_descriptor_actions(
        missing,
        sizeof(missing) / sizeof(missing[0])
    );
    request = prepare_request(request_size, UINT64_C(0x112));
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
    );
    image_token = action.image_token;
    descriptors = prepare_descriptors_request(
        UINT64_C(0x112),
        image_token
    );
    snapshot = state;
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &descriptors,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_DESCRIPTOR
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );
    descriptors.semantic_pid ^= UINT64_C(1);
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &descriptors,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_STATE
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );

    release = release_request(UINT64_C(0x112), image_token);
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &release,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
    );
    return true;
}

static bool trusted_init_descriptor_reservations_are_exact(void)
{
    struct micros_vfs_control_request request;
    struct micros_vfs_control_request descriptors;
    struct micros_vfs_control_request abort;
    struct micros_vfs_control_request release;
    struct micros_vfs_control_result_action action;
    size_t request_size;
    uint32_t root_references;
    uint64_t image_token;

    EXPECT_TRUE(initialize_fixture());
    fill_executable(64);
    EXPECT_TRUE(
        vfs_test_fixture_add_root_file_with_mode(
            &fixture,
            "child",
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
            executable_bytes,
            64
        )
    );
    request_size = build_spawn_request("/child");
    request = prepare_request(request_size, UINT64_C(0x113));
    request.parent_endpoint = 0;
    request.trusted_init = true;
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
    );
    image_token = action.image_token;
    root_references = state.vnodes[0].local_reference_count;
    descriptors = prepare_descriptors_request(
        UINT64_C(0x113),
        image_token
    );
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &descriptors,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
        && action.descriptor_token != 0
        && state.spawn.phase
            == MICROS_VFS_SPAWN_DESCRIPTORS_PREPARED
        && active_process_count() == 1
        && active_console_open_file_count(
            MICROS_VFS_ACCESS_READ
        ) == 2
        && active_console_open_file_count(
            MICROS_VFS_ACCESS_WRITE
        ) == 4
        && state.vnodes[0].local_reference_count
            == root_references + 2
        && micros_vfs_state_validate(&state) == MICROS_VFS_CORE_OK
    );

    abort = abort_descriptors_request(
        UINT64_C(0x113),
        image_token,
        action.descriptor_token
    );
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &abort,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
        && active_process_count() == 1
        && active_console_open_file_count(
            MICROS_VFS_ACCESS_READ
        ) == 1
        && active_console_open_file_count(
            MICROS_VFS_ACCESS_WRITE
        ) == 2
        && state.vnodes[0].local_reference_count
            == root_references
        && micros_vfs_state_validate(&state) == MICROS_VFS_CORE_OK
    );

    release = release_request(UINT64_C(0x113), image_token);
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &release,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
    );
    return true;
}

static bool prepared_parent_detach_is_busy_and_atomic(void)
{
    struct micros_vfs_control_request request;
    struct micros_vfs_control_request release;
    struct micros_vfs_control_result_action action;
    enum micros_vfs_trusted_result trusted_result;
    size_t request_size;
    size_t ramfs_calls;
    size_t backend_revoke_calls;
    size_t putnode_calls;

    EXPECT_TRUE(initialize_fixture());
    fill_executable(64);
    EXPECT_TRUE(
        vfs_test_fixture_add_root_file_with_mode(
            &fixture,
            "child",
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
            executable_bytes,
            64
        )
    );
    request_size = build_spawn_request("/child");
    request = prepare_request(request_size, UINT64_C(0x102));
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
    );

    snapshot = state;
    ramfs_calls = fixture.ramfs_calls;
    backend_revoke_calls = fixture.backend_revoke_calls;
    putnode_calls = fixture.putnode_calls;
    EXPECT_TRUE(
        micros_vfs_detach(
            &state,
            parent_endpoint,
            &fixture.io,
            &trusted_result
        ) == MICROS_VFS_CORE_OK
        && trusted_result == MICROS_VFS_TRUSTED_BUSY
        && memcmp(&state, &snapshot, sizeof(state)) == 0
        && fixture.ramfs_calls == ramfs_calls
        && fixture.backend_revoke_calls == backend_revoke_calls
        && fixture.putnode_calls == putnode_calls
        && fixture.backend_grant_count == 2
    );

    release = release_request(
        UINT64_C(0x102),
        action.image_token
    );
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &release,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
        && micros_vfs_state_validate(&state) == MICROS_VFS_CORE_OK
    );
    return true;
}

static bool expected_prepare_failures_roll_back(void)
{
    struct micros_vfs_control_request request;
    struct micros_vfs_control_result_action action;
    size_t request_size;

    EXPECT_TRUE(initialize_fixture());
    request_size = build_spawn_request("/missing");
    request = prepare_request(request_size, UINT64_C(0x201));
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_NOT_FOUND
        && bytes_are_zero(&state.spawn, sizeof(state.spawn))
        && fixture.backend_grant_count == 0
    );

    EXPECT_TRUE(initialize_fixture());
    request_size = build_spawn_request("/capacity");
    request = prepare_request(request_size, UINT64_C(0x206));
    fixture.inject_ramfs_result = true;
    fixture.injected_ramfs_operation = MICROS_VFS_RAMFS_LOOKUP;
    fixture.injected_ramfs_result = MICROS_RAMFS_RESULT_NO_SPACE;
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_NO_SPACE
        && bytes_are_zero(&state.spawn, sizeof(state.spawn))
        && fixture.backend_grant_count == 0
        && fixture.putnode_count == 0
    );

    EXPECT_TRUE(initialize_fixture());
    fill_executable(64);
    EXPECT_TRUE(
        vfs_test_fixture_add_root_file_with_mode(
            &fixture,
            "plain",
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
            executable_bytes,
            64
        )
    );
    request_size = build_spawn_request("/plain");
    request = prepare_request(request_size, UINT64_C(0x202));
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_ACCESS
        && bytes_are_zero(&state.spawn, sizeof(state.spawn))
        && fixture.backend_grant_count == 0
        && fixture.putnode_count == 1
    );

    EXPECT_TRUE(initialize_fixture());
    request_size = build_spawn_request("/etc");
    request = prepare_request(request_size, UINT64_C(0x203));
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_ACCESS
        && bytes_are_zero(&state.spawn, sizeof(state.spawn))
        && fixture.backend_grant_count == 0
        && fixture.putnode_count == 1
    );

    EXPECT_TRUE(initialize_fixture());
    EXPECT_TRUE(
        vfs_test_fixture_add_root_file_with_mode(
            &fixture,
            "empty",
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
            NULL,
            0
        )
    );
    request_size = build_spawn_request("/empty");
    request = prepare_request(request_size, UINT64_C(0x204));
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_EXECUTABLE
        && bytes_are_zero(&state.spawn, sizeof(state.spawn))
        && fixture.backend_grant_count == 0
        && fixture.putnode_count == 1
    );

    EXPECT_TRUE(initialize_fixture());
    fill_executable(
        MICROS_APPLICATION_EXECUTABLE_SIZE_MAX + 1
    );
    EXPECT_TRUE(
        vfs_test_fixture_add_root_file_with_mode(
            &fixture,
            "large",
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
            executable_bytes,
            MICROS_APPLICATION_EXECUTABLE_SIZE_MAX + 1
        )
    );
    request_size = build_spawn_request("/large");
    request = prepare_request(request_size, UINT64_C(0x205));
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_TOO_BIG
        && bytes_are_zero(&state.spawn, sizeof(state.spawn))
        && fixture.backend_grant_count == 0
        && fixture.putnode_count == 1
    );
    return true;
}

static bool transaction_state_and_io_failures_are_exact(void)
{
    struct micros_vfs_control_request request;
    struct micros_vfs_control_request release;
    struct micros_vfs_control_result_action action;
    size_t request_size;
    uint64_t image_token;

    EXPECT_TRUE(initialize_fixture());
    fill_executable(MICROS_VFS_TRANSFER_MAX + 1);
    EXPECT_TRUE(
        vfs_test_fixture_add_root_file_with_mode(
            &fixture,
            "child",
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
            executable_bytes,
            MICROS_VFS_TRANSFER_MAX + 1
        )
    );
    request_size = build_spawn_request("/child");
    request = prepare_request(request_size, UINT64_C(0x301));
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
    );
    image_token = action.image_token;
    snapshot = state;
    request.lifecycle_token = UINT64_C(0x302);
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_STATE
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );
    release = release_request(UINT64_C(0x301), image_token + 1);
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &release,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_STATE
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );
    release.image_token = image_token;
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &release,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
    );

    EXPECT_TRUE(initialize_fixture());
    fill_executable(MICROS_VFS_TRANSFER_MAX + 1);
    EXPECT_TRUE(
        vfs_test_fixture_add_root_file_with_mode(
            &fixture,
            "short",
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
            executable_bytes,
            MICROS_VFS_TRANSFER_MAX + 1
        )
    );
    request_size = build_spawn_request("/short");
    request = prepare_request(request_size, UINT64_C(0x303));
    fixture.corrupt_read_count = true;
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_IO
        && bytes_are_zero(&state.spawn, sizeof(state.spawn))
        && fixture.backend_grant_count == 0
        && fixture.putnode_count == 1
    );

    EXPECT_TRUE(initialize_fixture());
    fill_executable(64);
    EXPECT_TRUE(
        vfs_test_fixture_add_root_file_with_mode(
            &fixture,
            "capacity",
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
            executable_bytes,
            64
        )
    );
    request_size = build_spawn_request("/capacity");
    request = prepare_request(request_size, UINT64_C(0x304));
    fixture.force_backend_grant_capacity = true;
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_NO_SPACE
        && bytes_are_zero(&state.spawn, sizeof(state.spawn))
        && fixture.backend_grant_count == 0
    );
    return true;
}

static bool trusted_init_and_path_bounds_are_exact(void)
{
    struct micros_vfs_control_request request;
    struct micros_vfs_control_request release;
    struct micros_vfs_control_result_action action;
    char overlong_path[MICROS_VFS_NAME_MAX + 3];
    size_t request_size;
    size_t index;

    EXPECT_TRUE(initialize_fixture());
    fill_executable(64);
    EXPECT_TRUE(
        vfs_test_fixture_add_root_file_with_mode(
            &fixture,
            "child",
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
            executable_bytes,
            64
        )
    );
    request_size = build_spawn_request("/child");
    request = prepare_request(request_size, UINT64_C(0x401));
    request.parent_endpoint = 0;
    request.trusted_init = true;
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
        && state.spawn.trusted_init
        && state.spawn.parent_endpoint == 0
    );
    release = release_request(
        UINT64_C(0x401),
        action.image_token
    );
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &release,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
    );

    EXPECT_TRUE(initialize_fixture());
    request_size = build_spawn_request("child");
    request = prepare_request(request_size, UINT64_C(0x402));
    request.parent_endpoint = 0;
    request.trusted_init = true;
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_STATE
        && fixture.ramfs_calls == 1
        && bytes_are_zero(&state.spawn, sizeof(state.spawn))
        && bytes_are_zero(
            state.spawn_request_bytes,
            sizeof(state.spawn_request_bytes)
        )
    );

    EXPECT_TRUE(initialize_fixture());
    request_size = build_spawn_request("/child");
    write_u16_le(
        &request_bytes[
            MICROS_SPAWN_DESCRIPTOR_ACTION_COUNT_OFFSET
        ],
        1
    );
    write_u16_le(
        &request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET],
        0
    );
    write_u16_le(
        &request_bytes[
            MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET + 2
        ],
        1
    );
    write_u32_le(
        &request_bytes[
            MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET + 4
        ],
        MICROS_SPAWN_DESCRIPTOR_DUPLICATE
    );
    request = prepare_request(request_size, UINT64_C(0x403));
    request.parent_endpoint = 0;
    request.trusted_init = true;
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_STATE
        && fixture.ramfs_calls == 1
        && bytes_are_zero(&state.spawn, sizeof(state.spawn))
    );

    overlong_path[0] = '/';
    for (index = 1; index < sizeof(overlong_path) - 1; ++index) {
        overlong_path[index] = 'a';
    }
    overlong_path[sizeof(overlong_path) - 1] = 0;
    EXPECT_TRUE(initialize_fixture());
    request_size = build_spawn_request(overlong_path);
    request = prepare_request(request_size, UINT64_C(0x404));
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_TOO_BIG
        && fixture.ramfs_calls == 1
        && fixture.putnode_count == 0
        && bytes_are_zero(&state.spawn, sizeof(state.spawn))
        && bytes_are_zero(
            state.spawn_request_bytes,
            sizeof(state.spawn_request_bytes)
        )
    );
    return true;
}

static bool trusted_input_failures_are_fatal(void)
{
    struct micros_vfs_control_request request;
    struct micros_vfs_control_result_action action;
    size_t request_size;

    EXPECT_TRUE(initialize_fixture());
    request_size = build_spawn_request("/child");
    request_bytes[8] ^= 1;
    request = prepare_request(request_size, UINT64_C(0x501));
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_ERROR_INVARIANT
        && !action.active
        && fixture.ramfs_calls == 1
    );

    EXPECT_TRUE(initialize_fixture());
    request_size = build_oversized_stack_request("/child");
    request = prepare_request(request_size, UINT64_C(0x505));
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_ERROR_INVARIANT
        && !action.active
        && fixture.ramfs_calls == 1
    );

    EXPECT_TRUE(initialize_fixture());
    request_size = build_spawn_request("/child");
    request = prepare_request(request_size, UINT64_C(0x502));
    fixture.reject_client_validate = true;
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_ERROR_INVARIANT
        && fixture.client_validate_calls == 1
        && fixture.client_copy_calls == 0
        && micros_vfs_state_validate(&state)
            == MICROS_VFS_CORE_OK
    );

    EXPECT_TRUE(initialize_fixture());
    request_size = build_spawn_request("/child");
    request = prepare_request(request_size, UINT64_C(0x503));
    fixture.reject_client_copy = true;
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_ERROR_INVARIANT
        && fixture.client_validate_calls == 1
        && fixture.client_copy_calls == 1
        && micros_vfs_state_validate(&state)
            == MICROS_VFS_CORE_OK
    );

    EXPECT_TRUE(initialize_fixture());
    request_size = build_spawn_request("/child");
    request = prepare_request(request_size, UINT64_C(0x504));
    state.last_image_token = UINT64_MAX;
    snapshot = state;
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_ERROR_INVARIANT
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );
    return true;
}

static bool capacity_rollback_and_tokens_are_exact(void)
{
    struct micros_vfs_control_request request;
    struct micros_vfs_control_request release;
    struct micros_vfs_control_result_action action;
    size_t request_size;
    uint64_t first_image_token;

    EXPECT_TRUE(initialize_fixture());
    fill_executable(64);
    EXPECT_TRUE(
        vfs_test_fixture_add_root_file_with_mode(
            &fixture,
            "child",
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
            executable_bytes,
            64
        )
    );
    request_size = build_spawn_request("/child");
    request = prepare_request(request_size, UINT64_C(0x601));
    fixture.backend_capacity_on_create_call = 4;
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_NO_SPACE
        && fixture.backend_create_calls == 4
        && fixture.backend_grant_count == 0
        && fixture.putnode_count == 1
        && bytes_are_zero(&state.spawn, sizeof(state.spawn))
        && bytes_are_zero(
            state.spawn_request_bytes,
            sizeof(state.spawn_request_bytes)
        )
        && bytes_are_zero(
            state.spawn_executable_bytes,
            sizeof(state.spawn_executable_bytes)
        )
    );

    EXPECT_TRUE(initialize_fixture());
    fill_executable(64);
    EXPECT_TRUE(
        vfs_test_fixture_add_root_file_with_mode(
            &fixture,
            "child",
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
            executable_bytes,
            64
        )
    );
    request_size = build_spawn_request("/child");
    request = prepare_request(request_size, UINT64_C(0x602));
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
    );
    first_image_token = action.image_token;
    release = release_request(
        UINT64_C(0x602),
        first_image_token
    );
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &release,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
    );
    request.lifecycle_token = UINT64_C(0x603);
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
        && action.image_token == first_image_token + 1
    );
    release = release_request(
        UINT64_C(0x603),
        action.image_token
    );
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &release,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
    );
    return true;
}

static bool prepared_snapshot_is_independent_from_vnode_size(void)
{
    static const uint8_t path[] = "/child";
    uint8_t write_bytes[65];
    struct micros_vfs_control_request request;
    struct micros_vfs_control_request release;
    struct micros_vfs_control_result_action control_action;
    struct micros_vfs_request application_request;
    struct micros_vfs_result_action application_action;
    micros_grant_t path_grant;
    micros_grant_t write_grant;
    size_t request_size;
    uint32_t descriptor;

    EXPECT_TRUE(initialize_fixture());
    fill_executable(64);
    memset(write_bytes, 0x5a, sizeof(write_bytes));
    EXPECT_TRUE(
        vfs_test_fixture_add_root_file_with_mode(
            &fixture,
            "child",
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
            executable_bytes,
            64
        )
    );
    request_size = build_spawn_request("/child");
    request = prepare_request(request_size, UINT64_C(0x801));
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &control_action
        ) == MICROS_VFS_CORE_OK
        && control_action.result == MICROS_VFS_CONTROL_RESULT_OK
    );

    path_grant = vfs_test_fixture_add_application_grant(
        &fixture,
        parent_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        path,
        sizeof(path)
    );
    application_request = (struct micros_vfs_request){
        .type = MICROS_VFS_MESSAGE_OPEN,
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .source = parent_endpoint,
        .reply_token = UINT64_C(0x81),
        .grant = path_grant,
        .path_length = sizeof(path),
        .open_flags = MICROS_VFS_OPEN_WRITE,
    };
    EXPECT_TRUE(
        path_grant != MICROS_GRANT_NONE
        && micros_vfs_handle_request(
            &state,
            &application_request,
            &fixture.io,
            &application_action
        ) == MICROS_VFS_CORE_OK
        && application_action.active
        && application_action.result == MICROS_VFS_RESULT_OK
    );
    descriptor = application_action.descriptor;
    write_grant = vfs_test_fixture_add_application_grant(
        &fixture,
        parent_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        write_bytes,
        sizeof(write_bytes)
    );
    application_request = (struct micros_vfs_request){
        .type = MICROS_VFS_MESSAGE_WRITE,
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .source = parent_endpoint,
        .reply_token = UINT64_C(0x82),
        .descriptor = descriptor,
        .grant = write_grant,
        .count = sizeof(write_bytes),
    };
    EXPECT_TRUE(
        write_grant != MICROS_GRANT_NONE
        && micros_vfs_handle_request(
            &state,
            &application_request,
            &fixture.io,
            &application_action
        ) == MICROS_VFS_CORE_OK
        && application_action.result == MICROS_VFS_RESULT_OK
        && state.spawn.executable_size == 64
        && memcmp(
            state.spawn_executable_bytes,
            executable_bytes,
            64
        ) == 0
        && state.vnodes[state.spawn.executable_vnode - 1].size
            == sizeof(write_bytes)
        && micros_vfs_state_validate(&state) == MICROS_VFS_CORE_OK
    );

    release = release_request(
        UINT64_C(0x801),
        control_action.image_token
    );
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &release,
            &fixture.io,
            &control_action
        ) == MICROS_VFS_CORE_OK
        && control_action.result == MICROS_VFS_CONTROL_RESULT_OK
    );
    application_request = (struct micros_vfs_request){
        .type = MICROS_VFS_MESSAGE_CLOSE,
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .source = parent_endpoint,
        .reply_token = UINT64_C(0x83),
        .descriptor = descriptor,
    };
    EXPECT_TRUE(
        micros_vfs_handle_request(
            &state,
            &application_request,
            &fixture.io,
            &application_action
        ) == MICROS_VFS_CORE_OK
        && application_action.result == MICROS_VFS_RESULT_OK
    );
    return true;
}

static bool image_grants_cannot_alias_pending_tty_grant(void)
{
    static const uint8_t completion[] = "x";
    struct micros_vfs_control_request request;
    struct micros_vfs_control_request release;
    struct micros_vfs_control_result_action control_action;
    struct micros_vfs_request read_request;
    struct micros_vfs_result_action application_action;
    micros_grant_t application_grant;
    size_t request_size;

    EXPECT_TRUE(initialize_fixture());
    fill_executable(64);
    EXPECT_TRUE(
        vfs_test_fixture_add_root_file_with_mode(
            &fixture,
            "child",
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
            executable_bytes,
            64
        )
    );
    request_size = build_spawn_request("/child");
    request = prepare_request(request_size, UINT64_C(0x901));
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &control_action
        ) == MICROS_VFS_CORE_OK
        && control_action.result == MICROS_VFS_CONTROL_RESULT_OK
    );
    application_grant =
        vfs_test_fixture_add_application_grant(
            &fixture,
            parent_endpoint,
            MICROS_GRANT_PERMISSION_WRITE,
            NULL,
            8
        );
    read_request = (struct micros_vfs_request){
        .type = MICROS_VFS_MESSAGE_READ,
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .source = parent_endpoint,
        .reply_token = UINT64_C(0x91),
        .descriptor = 0,
        .grant = application_grant,
        .count = 8,
    };
    EXPECT_TRUE(
        application_grant != MICROS_GRANT_NONE
        && micros_vfs_handle_request(
            &state,
            &read_request,
            &fixture.io,
            &application_action
        ) == MICROS_VFS_CORE_OK
        && !application_action.active
        && state.pending.tty_grant != MICROS_GRANT_NONE
        && micros_vfs_state_validate(&state) == MICROS_VFS_CORE_OK
    );

    snapshot = state;
    snapshot.spawn.executable_grant =
        snapshot.pending.tty_grant;
    EXPECT_TRUE(
        micros_vfs_state_validate(&snapshot)
            == MICROS_VFS_CORE_ERROR_INVARIANT
    );
    snapshot = state;
    snapshot.spawn.request_grant = snapshot.pending.tty_grant;
    EXPECT_TRUE(
        micros_vfs_state_validate(&snapshot)
            == MICROS_VFS_CORE_ERROR_INVARIANT
    );

    EXPECT_TRUE(
        vfs_test_fixture_complete_read(
            &fixture,
            completion,
            sizeof(completion) - 1
        )
        && micros_vfs_handle_tty_notification(
            &state,
            MICROS_TTY_EVENT_COMPLETION,
            &fixture.io,
            &application_action
        ) == MICROS_VFS_CORE_OK
        && application_action.active
        && application_action.result == MICROS_VFS_RESULT_OK
    );
    release = release_request(
        UINT64_C(0x901),
        control_action.image_token
    );
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &release,
            &fixture.io,
            &control_action
        ) == MICROS_VFS_CORE_OK
        && control_action.result == MICROS_VFS_CONTROL_RESULT_OK
    );
    return true;
}

static bool prepared_state_corruption_is_rejected(void)
{
    struct micros_vfs_control_request request;
    struct micros_vfs_control_request release;
    struct micros_vfs_control_result_action action;
    size_t request_size;

    EXPECT_TRUE(initialize_fixture());
    fill_executable(64);
    EXPECT_TRUE(
        vfs_test_fixture_add_root_file_with_mode(
            &fixture,
            "child",
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755),
            executable_bytes,
            64
        )
    );
    request_size = build_spawn_request("/child");
    request = prepare_request(request_size, UINT64_C(0x701));
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &request,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
    );

    snapshot = state;
    ++snapshot.last_image_token;
    EXPECT_TRUE(
        micros_vfs_state_validate(&snapshot)
            == MICROS_VFS_CORE_ERROR_INVARIANT
    );
    snapshot = state;
    snapshot.spawn_request_bytes[request_size] = 1;
    EXPECT_TRUE(
        micros_vfs_state_validate(&snapshot)
            == MICROS_VFS_CORE_ERROR_INVARIANT
    );
    snapshot = state;
    snapshot.spawn_executable_bytes[64] = 1;
    EXPECT_TRUE(
        micros_vfs_state_validate(&snapshot)
            == MICROS_VFS_CORE_ERROR_INVARIANT
    );
    snapshot = state;
    snapshot.spawn.parent_endpoint = UINT32_C(0x00003008);
    EXPECT_TRUE(
        micros_vfs_state_validate(&snapshot)
            == MICROS_VFS_CORE_ERROR_INVARIANT
    );
    snapshot = state;
    snapshot.spawn.executable_size = 63;
    EXPECT_TRUE(
        micros_vfs_state_validate(&snapshot)
            == MICROS_VFS_CORE_ERROR_INVARIANT
    );

    release = release_request(
        UINT64_C(0x701),
        action.image_token
    );
    EXPECT_TRUE(
        micros_vfs_handle_control(
            &state,
            &release,
            &fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_CONTROL_RESULT_OK
    );
    return true;
}

bool micros_vfs_spawn_test_run(void)
{
    return (
        successful_prepare_and_release_are_exact()
        && public_descriptor_prepare_and_abort_are_exact()
        && reserved_process_slot_and_capacity_are_exact()
        && descriptor_token_exhaustion_is_atomic()
        && descriptor_state_corruption_is_rejected()
        && descriptor_failures_are_atomic()
        && trusted_init_descriptor_reservations_are_exact()
        && prepared_parent_detach_is_busy_and_atomic()
        && expected_prepare_failures_roll_back()
        && transaction_state_and_io_failures_are_exact()
        && trusted_init_and_path_bounds_are_exact()
        && trusted_input_failures_are_fatal()
        && capacity_rollback_and_tokens_are_exact()
        && prepared_snapshot_is_independent_from_vnode_size()
        && image_grants_cannot_alias_pending_tty_grant()
        && prepared_state_corruption_is_rejected()
    );
}
