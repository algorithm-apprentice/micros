#include "tests/host/ramfs_test_internal.h"

#include <string.h>

struct ramfs_creation_case {
    struct micros_ramfs_reply_action action;
    struct micros_ipc_message message;
    uint8_t malformed_name[4];
    uint64_t root;
    uint64_t created;
    size_t index;
};

struct ramfs_lookup_case {
    struct micros_ramfs_reply_action action;
    struct micros_ipc_message message;
    uint8_t overlong[MICROS_RAMFS_NAME_MAX + 2];
    uint8_t embedded[6];
    uint8_t missing_nul[4];
    uint8_t missing_then_overlong[sizeof("/missing/") + MICROS_RAMFS_NAME_MAX + 1];
    uint64_t root;
    uint64_t etc;
    uint64_t motd;
};

static const uint8_t absolute_motd[] = "/etc/motd";
static const uint8_t relative_motd[] = "etc//./motd";
static const uint8_t empty[] = "";
static const uint8_t parent_path[] = "../etc/motd";
static const uint8_t confined_parent[] = "../motd";
static const uint8_t trailing[] = "/etc/motd/";
static const uint8_t missing[] = "/missing";
static const uint8_t dot[] = ".";
static const uint8_t dotdot[] = "..";
static const uint8_t parent_name[] = "../name";

static bool lookup_absolute_relative_references(
    struct ramfs_test_fixture *fixture,
    struct ramfs_lookup_case *work
)
{
EXPECT_TRUE(ramfs_test_initialize_mounted(fixture));
    EXPECT_TRUE(
        ramfs_test_lookup_path(
            fixture,
            work->root,
            work->root,
            absolute_motd,
            sizeof(absolute_motd),
            MICROS_RAMFS_RESULT_OK,
            &work->action
        )
    );
    EXPECT_TRUE(ramfs_test_read_u64_le(&work->action.message.payload[16]) == work->motd);
    EXPECT_TRUE(ramfs_test_read_u64_le(&work->action.message.payload[24]) == 13);
    EXPECT_TRUE(fixture->state.nodes[2].reference_count == 1);

    EXPECT_TRUE(
        ramfs_test_lookup_path(
            fixture,
            work->root,
            work->root,
            relative_motd,
            sizeof(relative_motd),
            MICROS_RAMFS_RESULT_OK,
            &work->action
        )
    );
    EXPECT_TRUE(fixture->state.nodes[2].reference_count == 2);
    EXPECT_TRUE(
        ramfs_test_lookup_path(
            fixture,
            work->root,
            work->root,
            empty,
            sizeof(empty),
            MICROS_RAMFS_RESULT_OK,
            &work->action
        )
    );
    EXPECT_TRUE(ramfs_test_read_u64_le(&work->action.message.payload[16]) == work->root);
    EXPECT_TRUE(fixture->state.nodes[0].reference_count == 2);
    EXPECT_TRUE(
        ramfs_test_lookup_path(
            fixture,
            work->root,
            work->root,
            parent_path,
            sizeof(parent_path),
            MICROS_RAMFS_RESULT_OK,
            &work->action
        )
    );
    return true;
}

static bool lookup_confined_root(
    struct ramfs_test_fixture *fixture,
    struct ramfs_lookup_case *work
)
{
    EXPECT_TRUE(
        ramfs_test_lookup_path(
            fixture,
            work->root,
            work->root,
            (const uint8_t *)"/etc",
            5,
            MICROS_RAMFS_RESULT_OK,
            &work->action
        )
    );
    EXPECT_TRUE(ramfs_test_read_u64_le(&work->action.message.payload[16]) == work->etc);
    EXPECT_TRUE(
        ramfs_test_lookup_path(
            fixture,
            work->etc,
            work->etc,
            confined_parent,
            sizeof(confined_parent),
            MICROS_RAMFS_RESULT_OK,
            &work->action
        )
    );
    EXPECT_TRUE(ramfs_test_read_u64_le(&work->action.message.payload[16]) == work->motd);

    ramfs_test_reset_bus(fixture);
    work->message = ramfs_test_lookup_request(work->root, work->etc, 1, 0);
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );
    EXPECT_TRUE(fixture->bus.calls == 0);
    return true;
}

static bool lookup_trailing_missing_bounds(
    struct ramfs_test_fixture *fixture,
    struct ramfs_lookup_case *work
)
{
    EXPECT_TRUE(
        ramfs_test_lookup_path(
            fixture,
            work->root,
            work->root,
            trailing,
            sizeof(trailing),
            MICROS_RAMFS_RESULT_NOT_DIRECTORY,
            &work->action
        )
    );
    EXPECT_TRUE(
        ramfs_test_lookup_path(
            fixture,
            work->root,
            work->root,
            missing,
            sizeof(missing),
            MICROS_RAMFS_RESULT_NOT_FOUND,
            &work->action
        )
    );
    memcpy(work->missing_then_overlong, "/missing/", sizeof("/missing/") - 1);
    memset(
        &work->missing_then_overlong[sizeof("/missing/") - 1],
        'a',
        MICROS_RAMFS_NAME_MAX + 1
    );
    work->missing_then_overlong[sizeof(work->missing_then_overlong) - 1] = 0;
    EXPECT_TRUE(
        ramfs_test_lookup_path(
            fixture,
            work->root,
            work->root,
            work->missing_then_overlong,
            sizeof(work->missing_then_overlong),
            MICROS_RAMFS_RESULT_RANGE,
            &work->action
        )
    );

    memset(work->overlong, 'a', sizeof(work->overlong));
    work->overlong[sizeof(work->overlong) - 1] = 0;
    EXPECT_TRUE(
        ramfs_test_lookup_path(
            fixture,
            work->root,
            work->root,
            work->overlong,
            sizeof(work->overlong),
            MICROS_RAMFS_RESULT_RANGE,
            &work->action
        )
    );
    return true;
}

static bool lookup_non_directory_starts(
    struct ramfs_test_fixture *fixture,
    struct ramfs_lookup_case *work
)
{
    EXPECT_TRUE(
        ramfs_test_lookup_path(
            fixture,
            work->motd,
            work->root,
            empty,
            sizeof(empty),
            MICROS_RAMFS_RESULT_NOT_DIRECTORY,
            &work->action
        )
    );
    EXPECT_TRUE(
        ramfs_test_lookup_path(
            fixture,
            work->motd,
            work->root,
            dot,
            sizeof(dot),
            MICROS_RAMFS_RESULT_NOT_DIRECTORY,
            &work->action
        )
    );
    EXPECT_TRUE(
        ramfs_test_lookup_path(
            fixture,
            work->motd,
            work->root,
            dotdot,
            sizeof(dotdot),
            MICROS_RAMFS_RESULT_NOT_DIRECTORY,
            &work->action
        )
    );
    EXPECT_TRUE(
        ramfs_test_lookup_path(
            fixture,
            work->motd,
            work->root,
            parent_name,
            sizeof(parent_name),
            MICROS_RAMFS_RESULT_NOT_DIRECTORY,
            &work->action
        )
    );
    EXPECT_TRUE(
        ramfs_test_lookup_path(
            fixture,
            work->motd,
            work->root,
            (const uint8_t *)"/etc",
            5,
            MICROS_RAMFS_RESULT_OK,
            &work->action
        )
    );
    EXPECT_TRUE(
        ramfs_test_lookup_path(
            fixture,
            work->motd,
            work->motd,
            dot,
            sizeof(dot),
            MICROS_RAMFS_RESULT_NOT_DIRECTORY,
            &work->action
        )
    );
    return true;
}

static bool lookup_malformed_and_ranges(
    struct ramfs_test_fixture *fixture,
    struct ramfs_lookup_case *work
)
{
    EXPECT_TRUE(
        ramfs_test_lookup_path(
            fixture,
            work->root,
            work->root,
            work->embedded,
            sizeof(work->embedded),
            MICROS_RAMFS_RESULT_MALFORMED,
            &work->action
        )
    );
    EXPECT_TRUE(
        ramfs_test_lookup_path(
            fixture,
            work->root,
            work->root,
            work->missing_nul,
            sizeof(work->missing_nul),
            MICROS_RAMFS_RESULT_MALFORMED,
            &work->action
        )
    );

    ramfs_test_reset_bus(fixture);
    work->message = ramfs_test_lookup_request(work->root, work->root, 0, 0);
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );
    work->message = ramfs_test_lookup_request(
        work->root,
        work->root,
        MICROS_RAMFS_PATH_MAX + 1,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );
    work->message = ramfs_test_lookup_request(work->root, work->root, 1, UINT64_MAX);
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );
    return true;
}

static bool lookup_grant_and_reference_overflow(
    struct ramfs_test_fixture *fixture,
    struct ramfs_lookup_case *work
)
{
    ramfs_test_reset_bus(fixture);
    fixture->bus.storage[0] = 0;
    fixture->bus.reject = true;
    work->message = ramfs_test_lookup_request(work->root, work->root, 1, 0);
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_GRANT
        )
    );

    fixture->state.nodes[2].reference_count = UINT32_MAX;
    ramfs_test_reset_bus(fixture);
    memcpy(fixture->bus.storage, absolute_motd, sizeof(absolute_motd));
    work->message = ramfs_test_lookup_request(
        work->root,
        work->root,
        sizeof(absolute_motd),
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_NO_SPACE
        )
    );
    return true;
}

static bool creation_file_and_duplicate(
    struct ramfs_test_fixture *fixture,
    struct ramfs_creation_case *work
)
{
EXPECT_TRUE(ramfs_test_initialize_mounted(fixture));
    EXPECT_TRUE(
        ramfs_test_create_node(
            fixture,
            MICROS_RAMFS_MESSAGE_CREATE,
            work->root,
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0640),
            "file",
            &work->action
        )
    );
    work->created = ramfs_test_read_u64_le(&work->action.message.payload[16]);
    EXPECT_TRUE(work->created == micros_ramfs_handle(3, 1));
    EXPECT_TRUE(fixture->state.nodes[3].reference_count == 1);
    EXPECT_TRUE(fixture->state.nodes[3].link_count == 1);
    EXPECT_TRUE(
        ramfs_test_read_u32_le(&work->action.message.payload[44])
            == (MICROS_RAMFS_MODE_REGULAR | UINT32_C(0640))
    );

    ramfs_test_reset_bus(fixture);
    memcpy(fixture->bus.storage, "file", 5);
    work->message = ramfs_test_create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        work->root,
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        5,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_EXISTS
        )
    );
    return true;
}

static bool creation_mkdir_and_parent_authority(
    struct ramfs_test_fixture *fixture,
    struct ramfs_creation_case *work
)
{
    EXPECT_TRUE(
        ramfs_test_create_node(
            fixture,
            MICROS_RAMFS_MESSAGE_MKDIR,
            work->root,
            MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0750),
            "dir",
            &work->action
        )
    );
    EXPECT_TRUE(ramfs_test_read_u64_le(&work->action.message.payload[16]) == 0);
    EXPECT_TRUE(fixture->state.nodes[4].reference_count == 0);
    EXPECT_TRUE(fixture->state.nodes[4].link_count == 2);
    EXPECT_TRUE(fixture->state.nodes[0].link_count == 4);

    ramfs_test_reset_bus(fixture);
    memcpy(fixture->bus.storage, "child", 6);
    work->message = ramfs_test_create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        work->created,
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        6,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_NOT_DIRECTORY
        )
    );
    work->message = ramfs_test_create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        micros_ramfs_handle(1, 1),
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        6,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_REFERENCE
        )
    );
    return true;
}

static bool creation_mode_and_range_rejections(
    struct ramfs_test_fixture *fixture,
    struct ramfs_creation_case *work
)
{
    work->message = ramfs_test_create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        work->root,
        MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755),
        2,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );
    work->message = ramfs_test_create_request(
        MICROS_RAMFS_MESSAGE_MKDIR,
        work->root,
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        2,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );

    ramfs_test_reset_bus(fixture);
    work->message = ramfs_test_create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        work->root,
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        1,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );
    work->message = ramfs_test_create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        work->root,
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        MICROS_RAMFS_NAME_MAX + 2,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );
    return true;
}

static bool creation_name_and_copy_rejections(
    struct ramfs_test_fixture *fixture,
    struct ramfs_creation_case *work
)
{
    ramfs_test_reset_bus(fixture);
    memcpy(fixture->bus.storage, work->malformed_name, sizeof(work->malformed_name));
    work->message = ramfs_test_create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        work->root,
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        sizeof(work->malformed_name),
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );
    ramfs_test_reset_bus(fixture);
    memcpy(fixture->bus.storage, "bad/name", 9);
    work->message = ramfs_test_create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        work->root,
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        9,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );
    ramfs_test_reset_bus(fixture);
    memcpy(fixture->bus.storage, ".\0", 2);
    work->message = ramfs_test_create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        work->root,
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        2,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );
    ramfs_test_reset_bus(fixture);
    memcpy(fixture->bus.storage, "..\0", 3);
    work->message = ramfs_test_create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        work->root,
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        3,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );
    ramfs_test_reset_bus(fixture);
    memcpy(fixture->bus.storage, "x\0", 2);
    fixture->bus.reject = true;
    work->message = ramfs_test_create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        work->root,
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        2,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_GRANT
        )
    );
    return true;
}

static bool creation_node_capacity(
    struct ramfs_test_fixture *fixture,
    struct ramfs_creation_case *work
)
{
    EXPECT_TRUE(ramfs_test_initialize_mounted(fixture));
    for (work->index = 0; work->index < 61; ++work->index) {
        char name[4];

        name[0] = 'd';
        name[1] = (char)('0' + (work->index / 10) % 10);
        name[2] = (char)('0' + work->index % 10);
        name[3] = '\0';
        EXPECT_TRUE(
            ramfs_test_create_node(
                fixture,
                MICROS_RAMFS_MESSAGE_MKDIR,
                work->root,
                MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755),
                name,
                &work->action
            )
        );
    }
    EXPECT_TRUE(fixture->state.node_count == MICROS_RAMFS_NODE_CAPACITY);
    EXPECT_TRUE(fixture->state.nodes[0].link_count == 64);
    ramfs_test_reset_bus(fixture);
    memcpy(fixture->bus.storage, "full\0", 5);
    work->message = ramfs_test_create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        work->root,
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        5,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_NO_SPACE
        )
    );
    EXPECT_TRUE(fixture->bus.calls == 0);
    return true;
}

bool ramfs_test_lookup_semantics_and_reference_limits_are_exact(
    struct ramfs_test_fixture *fixture
)
{
    struct ramfs_lookup_case work;
    work.embedded[0] = '/';
    work.embedded[1] = 'e';
    work.embedded[2] = 't';
    work.embedded[3] = '\0';
    work.embedded[4] = 'c';
    work.embedded[5] = '\0';
    work.missing_nul[0] = '/';
    work.missing_nul[1] = 'e';
    work.missing_nul[2] = 't';
    work.missing_nul[3] = 'c';
    work.root = micros_ramfs_handle(0, 1);
    work.etc = micros_ramfs_handle(1, 1);
    work.motd = micros_ramfs_handle(2, 1);

    return lookup_absolute_relative_references(fixture, &work)
        && lookup_confined_root(fixture, &work)
        && lookup_trailing_missing_bounds(fixture, &work)
        && lookup_non_directory_starts(fixture, &work)
        && lookup_malformed_and_ranges(fixture, &work)
        && lookup_grant_and_reference_overflow(fixture, &work);
}

bool ramfs_test_handles_create_mkdir_and_capacity_are_exact(
    struct ramfs_test_fixture *fixture
)
{
    struct ramfs_creation_case work;
    work.malformed_name[0] = 'b';
    work.malformed_name[1] = '\0';
    work.malformed_name[2] = 'd';
    work.malformed_name[3] = '\0';
    work.root = micros_ramfs_handle(0, 1);

    return creation_file_and_duplicate(fixture, &work)
        && creation_mkdir_and_parent_authority(fixture, &work)
        && creation_mode_and_range_rejections(fixture, &work)
        && creation_name_and_copy_rejections(fixture, &work)
        && creation_node_capacity(fixture, &work);
}

bool ramfs_test_putnode_and_handle_validation_are_exact(
    struct ramfs_test_fixture *fixture
)
{
    struct micros_ramfs_reply_action action;
    struct micros_ipc_message message;
    static const uint8_t motd_path[] = "/etc/motd";
    uint64_t root = micros_ramfs_handle(0, 1);
    uint64_t motd = micros_ramfs_handle(2, 1);
    uint64_t invalid_handles[] = {
        0,
        UINT64_C(1) << 16 | 1,
        micros_ramfs_handle(63, 1),
        micros_ramfs_handle(2, 2),
    };
    size_t index;

    EXPECT_TRUE(ramfs_test_initialize_mounted(fixture));
    for (index = 0; index < 2; ++index) {
        EXPECT_TRUE(
            ramfs_test_lookup_path(
                fixture,
                root,
                root,
                motd_path,
                sizeof(motd_path),
                MICROS_RAMFS_RESULT_OK,
                &action
            )
        );
    }
    EXPECT_TRUE(fixture->state.nodes[2].reference_count == 2);
    message = ramfs_test_putnode_request(motd, 2);
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &message, MICROS_RAMFS_RESULT_OK, &action)
    );
    EXPECT_TRUE(fixture->state.nodes[2].reference_count == 0);

    message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        motd,
        0,
        1,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_REFERENCE
        )
    );
    message = ramfs_test_putnode_request(motd, 1);
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_REFERENCE
        )
    );
    message = ramfs_test_putnode_request(root, 1);
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_REFERENCE
        )
    );
    message = ramfs_test_putnode_request(root, 0);
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );

    for (
        index = 0;
        index < sizeof(invalid_handles) / sizeof(invalid_handles[0]);
        ++index
    ) {
        message = ramfs_test_putnode_request(invalid_handles[index], 1);
        EXPECT_TRUE(
            ramfs_test_failure_preserves_state(
                fixture,
                &message,
                MICROS_RAMFS_RESULT_NODE
            )
        );
    }
    return true;
}
