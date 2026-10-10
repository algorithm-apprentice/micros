#include "servers/ramfs/ramfs_core.h"

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

enum {
    TEST_GRANT = 1,
    TEST_GRANT_CAPACITY = 8192,
};

_Static_assert(
    MICROS_RAMFS_PROTOCOL_VERSION == 1,
    "RAMFS protocol version drift"
);
_Static_assert(
    MICROS_RAMFS_MESSAGE_MOUNT == UINT32_C(0x00030001)
        && MICROS_RAMFS_MESSAGE_LOOKUP == UINT32_C(0x00030002)
        && MICROS_RAMFS_MESSAGE_CREATE == UINT32_C(0x00030003)
        && MICROS_RAMFS_MESSAGE_MKDIR == UINT32_C(0x00030004)
        && MICROS_RAMFS_MESSAGE_READ == UINT32_C(0x00030005)
        && MICROS_RAMFS_MESSAGE_WRITE == UINT32_C(0x00030006)
        && MICROS_RAMFS_MESSAGE_GETDENTS == UINT32_C(0x00030007)
        && MICROS_RAMFS_MESSAGE_PUTNODE == UINT32_C(0x00030008)
        && MICROS_RAMFS_MESSAGE_RESULT == UINT32_C(0x00030009),
    "RAMFS message identity drift"
);
_Static_assert(
    MICROS_RAMFS_DIRECTORY_RECORD_SIZE == 128,
    "RAMFS directory record size drift"
);

static const micros_endpoint_t vfs_endpoint = UINT32_C(0x00001005);
static uint8_t seed_image[MICROS_RAMFS_SEED_IMAGE_MAX];
static size_t seed_image_size;
static struct micros_ramfs_state state;
static struct micros_ramfs_state state_snapshot;
static struct micros_ramfs_state corrupt_state;
static uint8_t storage_snapshot[TEST_GRANT_CAPACITY];

struct test_bus {
    uint8_t storage[TEST_GRANT_CAPACITY];
    bool reject;
    bool invariant;
    size_t calls;
    enum micros_ramfs_copy_direction last_direction;
    uint64_t last_offset;
    size_t last_length;
};

static struct test_bus bus;

static void write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
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

static uint64_t read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static enum micros_ramfs_copy_result test_copy(
    void *context,
    enum micros_ramfs_copy_direction direction,
    micros_endpoint_t endpoint,
    micros_grant_t grant,
    uint64_t grant_offset,
    uint8_t *local,
    size_t length
)
{
    struct test_bus *selected = context;

    ++selected->calls;
    selected->last_direction = direction;
    selected->last_offset = grant_offset;
    selected->last_length = length;
    if (selected->invariant || endpoint != vfs_endpoint) {
        return MICROS_RAMFS_COPY_INVARIANT;
    }
    if (
        selected->reject
        || grant != TEST_GRANT
        || grant_offset > TEST_GRANT_CAPACITY
        || length > TEST_GRANT_CAPACITY - (size_t)grant_offset
        || local == NULL
    ) {
        return MICROS_RAMFS_COPY_REJECTED;
    }
    if (direction == MICROS_RAMFS_COPY_FROM_VFS) {
        memcpy(local, &selected->storage[grant_offset], length);
    } else {
        memcpy(&selected->storage[grant_offset], local, length);
    }
    return MICROS_RAMFS_COPY_OK;
}

static const struct micros_ramfs_io test_io = {
    .copy = test_copy,
    .context = &bus,
};

static bool load_seed(const char *path)
{
    FILE *stream;
    long length;

    stream = fopen(path, "rb");
    if (stream == NULL) {
        return false;
    }
    if (
        fseek(stream, 0, SEEK_END) != 0
        || (length = ftell(stream)) < 0
        || (uint64_t)length > MICROS_RAMFS_SEED_IMAGE_MAX
        || fseek(stream, 0, SEEK_SET) != 0
        || fread(seed_image, 1, (size_t)length, stream) != (size_t)length
        || fclose(stream) != 0
    ) {
        return false;
    }
    seed_image_size = (size_t)length;
    return true;
}

static void reset_bus(void)
{
    memset(&bus, 0, sizeof(bus));
}

static struct micros_ipc_message request(uint32_t type)
{
    struct micros_ipc_message message = {0};

    message.type = type;
    message.source = vfs_endpoint;
    message.reply_token = 1;
    write_u32_le(
        &message.payload[0],
        MICROS_RAMFS_PROTOCOL_VERSION
    );
    return message;
}

static struct micros_ipc_message lookup_request(
    uint64_t start,
    uint64_t root,
    uint32_t length,
    uint64_t grant_offset
)
{
    struct micros_ipc_message message =
        request(MICROS_RAMFS_MESSAGE_LOOKUP);

    write_u64_le(&message.payload[8], start);
    write_u64_le(&message.payload[16], root);
    write_u32_le(&message.payload[24], TEST_GRANT);
    write_u32_le(&message.payload[28], length);
    write_u64_le(&message.payload[32], grant_offset);
    return message;
}

static struct micros_ipc_message create_request(
    uint32_t type,
    uint64_t parent,
    uint32_t mode,
    uint32_t length,
    uint64_t grant_offset
)
{
    struct micros_ipc_message message = request(type);

    write_u64_le(&message.payload[8], parent);
    write_u32_le(&message.payload[16], mode);
    write_u32_le(&message.payload[20], TEST_GRANT);
    write_u64_le(&message.payload[24], grant_offset);
    write_u32_le(&message.payload[32], length);
    return message;
}

static struct micros_ipc_message transfer_request(
    uint32_t type,
    uint64_t node,
    uint64_t file_offset,
    uint32_t count,
    uint64_t grant_offset
)
{
    struct micros_ipc_message message = request(type);

    write_u64_le(&message.payload[8], node);
    write_u64_le(&message.payload[16], file_offset);
    write_u32_le(&message.payload[24], TEST_GRANT);
    write_u32_le(&message.payload[28], count);
    write_u64_le(&message.payload[32], grant_offset);
    return message;
}

static struct micros_ipc_message getdents_request(
    uint64_t node,
    uint64_t cursor,
    uint32_t count,
    uint64_t grant_offset
)
{
    struct micros_ipc_message message =
        request(MICROS_RAMFS_MESSAGE_GETDENTS);

    write_u64_le(&message.payload[8], node);
    write_u64_le(&message.payload[16], cursor);
    write_u32_le(&message.payload[24], TEST_GRANT);
    write_u32_le(&message.payload[28], count);
    write_u64_le(&message.payload[32], grant_offset);
    return message;
}

static struct micros_ipc_message putnode_request(
    uint64_t node,
    uint32_t count
)
{
    struct micros_ipc_message message =
        request(MICROS_RAMFS_MESSAGE_PUTNODE);

    write_u64_le(&message.payload[8], node);
    write_u32_le(&message.payload[16], count);
    return message;
}

static enum micros_ramfs_result reply_result(
    const struct micros_ramfs_reply_action *action
)
{
    return (enum micros_ramfs_result)(int32_t)read_u32_le(
        &action->message.payload[8]
    );
}

static bool reply_error_fields_are_zero(
    const struct micros_ramfs_reply_action *action
)
{
    size_t index;

    for (index = 12; index < sizeof(action->message.payload); ++index) {
        if (action->message.payload[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool call_expect(
    const struct micros_ipc_message *message,
    enum micros_ramfs_result expected,
    struct micros_ramfs_reply_action *action
)
{
    if (
        micros_ramfs_handle_call(
            &state,
            message,
            &test_io,
            action
        ) != MICROS_RAMFS_CORE_OK
        || !action->active
        || action->reply_token != message->reply_token
        || action->message.type != MICROS_RAMFS_MESSAGE_RESULT
        || read_u32_le(&action->message.payload[0])
            != MICROS_RAMFS_PROTOCOL_VERSION
        || read_u32_le(&action->message.payload[4])
            != message->type
        || reply_result(action) != expected
        || (
            expected != MICROS_RAMFS_RESULT_OK
            && !reply_error_fields_are_zero(action)
        )
    ) {
        return false;
    }
    return true;
}

static bool failure_preserves_state(
    const struct micros_ipc_message *message,
    enum micros_ramfs_result expected
)
{
    struct micros_ramfs_reply_action action;

    state_snapshot = state;
    memcpy(storage_snapshot, bus.storage, sizeof(storage_snapshot));
    if (!call_expect(message, expected, &action)) {
        return false;
    }
    return (
        memcmp(&state, &state_snapshot, sizeof(state)) == 0
        && memcmp(
            bus.storage,
            storage_snapshot,
            sizeof(storage_snapshot)
        ) == 0
    );
}

static bool initialize_unmounted(void)
{
    reset_bus();
    return micros_ramfs_state_initialize(
        &state,
        vfs_endpoint,
        seed_image,
        seed_image_size
    ) == MICROS_RAMFS_CORE_OK;
}

static bool initialize_mounted(void)
{
    struct micros_ramfs_reply_action action;
    struct micros_ipc_message message =
        request(MICROS_RAMFS_MESSAGE_MOUNT);

    return (
        initialize_unmounted()
        && call_expect(
            &message,
            MICROS_RAMFS_RESULT_OK,
            &action
        )
    );
}

static bool write_grant_bytes(
    const uint8_t *bytes,
    size_t length,
    size_t offset
)
{
    if (offset > sizeof(bus.storage) || length > sizeof(bus.storage) - offset) {
        return false;
    }
    memcpy(&bus.storage[offset], bytes, length);
    return true;
}

static bool lookup_path(
    uint64_t start,
    uint64_t root,
    const uint8_t *path,
    uint32_t length,
    enum micros_ramfs_result expected,
    struct micros_ramfs_reply_action *action
)
{
    struct micros_ipc_message message;

    reset_bus();
    if (!write_grant_bytes(path, length, 0)) {
        return false;
    }
    message = lookup_request(start, root, length, 0);
    return call_expect(&message, expected, action);
}

static bool create_node(
    uint32_t type,
    uint64_t parent,
    uint32_t mode,
    const char *name,
    struct micros_ramfs_reply_action *action
)
{
    struct micros_ipc_message message;
    size_t length = strlen(name) + 1;

    reset_bus();
    if (!write_grant_bytes((const uint8_t *)name, length, 0)) {
        return false;
    }
    message = create_request(
        type,
        parent,
        mode,
        (uint32_t)length,
        0
    );
    return call_expect(&message, MICROS_RAMFS_RESULT_OK, action);
}

static bool seed_state_is_exact_and_invariants_fail_closed(void)
{
    uint64_t root = micros_ramfs_handle(0, 1);

    EXPECT_TRUE(
        micros_ramfs_state_initialize(
            &corrupt_state,
            5,
            seed_image,
            seed_image_size
        ) == MICROS_RAMFS_CORE_ERROR_ARGUMENT
    );
    EXPECT_TRUE(initialize_unmounted());
    EXPECT_TRUE(root == UINT64_C(0x0000000100000000));
    EXPECT_TRUE(
        micros_ramfs_handle(63, 7)
            == UINT64_C(0x000000070000003f)
    );
    EXPECT_TRUE(state.phase == MICROS_RAMFS_PHASE_READY_UNMOUNTED);
    EXPECT_TRUE(state.node_count == 3);
    EXPECT_TRUE(state.allocated_block_count == 1);
    EXPECT_TRUE(state.nodes[0].parent == root);
    EXPECT_TRUE(state.nodes[0].link_count == 3);
    EXPECT_TRUE(state.nodes[1].parent == root);
    EXPECT_TRUE(state.nodes[1].link_count == 2);
    EXPECT_TRUE(state.nodes[2].parent == micros_ramfs_handle(1, 1));
    EXPECT_TRUE(state.nodes[2].link_count == 1);
    EXPECT_TRUE(state.nodes[2].size == 13);
    EXPECT_TRUE(state.nodes[2].blocks[0] == 0);
    EXPECT_TRUE(
        memcmp(state.data[0], "micros ramfs\n", 13) == 0
    );
    EXPECT_TRUE(micros_ramfs_handle(64, 1) == 0);
    EXPECT_TRUE(micros_ramfs_handle(0, 0) == 0);
    EXPECT_TRUE(
        micros_ramfs_state_validate(&state) == MICROS_RAMFS_CORE_OK
    );

    corrupt_state = state;
    corrupt_state.scratch[0] = 1;
    EXPECT_TRUE(
        micros_ramfs_state_validate(&corrupt_state)
            == MICROS_RAMFS_CORE_ERROR_INVARIANT
    );
    corrupt_state = state;
    corrupt_state.vfs_endpoint = 5;
    EXPECT_TRUE(
        micros_ramfs_state_validate(&corrupt_state)
            == MICROS_RAMFS_CORE_ERROR_INVARIANT
    );
    corrupt_state = state;
    corrupt_state.nodes[2].blocks[0] = 1;
    EXPECT_TRUE(
        micros_ramfs_state_validate(&corrupt_state)
            == MICROS_RAMFS_CORE_ERROR_INVARIANT
    );
    corrupt_state = state;
    corrupt_state.nodes[1].link_count = 3;
    EXPECT_TRUE(
        micros_ramfs_state_validate(&corrupt_state)
            == MICROS_RAMFS_CORE_ERROR_INVARIANT
    );
    return true;
}

static bool protocol_mount_and_result_layout_are_exact(void)
{
    struct micros_ramfs_reply_action action;
    struct micros_ipc_message message;
    struct micros_ipc_message built;
    enum micros_ramfs_result result;
    uint64_t root = micros_ramfs_handle(0, 1);

    EXPECT_TRUE(initialize_unmounted());
    message = create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        root,
        MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755),
        2,
        0
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_STATE
        )
    );
    EXPECT_TRUE(bus.calls == 0);

    message = lookup_request(0, 0, 1, 0);
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_STATE
        )
    );
    EXPECT_TRUE(bus.calls == 0);

    message = request(UINT32_C(0x000300ff));
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_BAD_TYPE
        )
    );
    message = request(MICROS_RAMFS_MESSAGE_MOUNT);
    write_u32_le(&message.payload[0], 2);
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_BAD_VERSION
        )
    );
    message = request(MICROS_RAMFS_MESSAGE_MOUNT);
    write_u32_le(&message.payload[4], 1);
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );
    message = request(MICROS_RAMFS_MESSAGE_MOUNT);
    message.payload[8] = 1;
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );
    message = request(MICROS_RAMFS_MESSAGE_MOUNT);
    message.source ^= 1;
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_CALLER
        )
    );
    message = request(MICROS_RAMFS_MESSAGE_MOUNT);
    message.reply_token = 0;
    state_snapshot = state;
    memset(&action, 0xa5, sizeof(action));
    EXPECT_TRUE(
        micros_ramfs_handle_call(
            &state,
            &message,
            &test_io,
            &action
        ) == MICROS_RAMFS_CORE_ERROR_INVARIANT
    );
    EXPECT_TRUE(memcmp(&state, &state_snapshot, sizeof(state)) == 0);

    message = request(MICROS_RAMFS_MESSAGE_MOUNT);
    EXPECT_TRUE(
        call_expect(&message, MICROS_RAMFS_RESULT_OK, &action)
    );
    EXPECT_TRUE(state.phase == MICROS_RAMFS_PHASE_MOUNTED);
    EXPECT_TRUE(state.nodes[0].reference_count == 1);
    EXPECT_TRUE(
        read_u64_le(&action.message.payload[16])
            == micros_ramfs_handle(0, 1)
    );
    EXPECT_TRUE(read_u64_le(&action.message.payload[24]) == 0);
    EXPECT_TRUE(read_u64_le(&action.message.payload[32]) == 0);
    EXPECT_TRUE(read_u32_le(&action.message.payload[40]) == 0);
    EXPECT_TRUE(
        read_u32_le(&action.message.payload[44])
            == (MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755))
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_STATE
        )
    );

    message = create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        root,
        MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755),
        2,
        0
    );
    message.source ^= 1;
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_CALLER
        )
    );
    message.source = vfs_endpoint;
    write_u64_le(
        &message.payload[8],
        micros_ramfs_handle(63, 1)
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_NODE
        )
    );
    write_u64_le(
        &message.payload[8],
        micros_ramfs_handle(1, 1)
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_REFERENCE
        )
    );
    write_u64_le(&message.payload[8], root);
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );
    EXPECT_TRUE(bus.calls == 0);

    message = lookup_request(0, 0, 1, 0);
    message.payload[40] = 1;
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );
    message = create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        0,
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        2,
        0
    );
    message.payload[36] = 1;
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );
    message = transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        0,
        0,
        1,
        0
    );
    message.payload[40] = 1;
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );
    message = getdents_request(0, 0, 128, 0);
    message.payload[40] = 1;
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );
    message = putnode_request(0, 1);
    message.payload[20] = 1;
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );

    for (
        result = MICROS_RAMFS_RESULT_REFERENCE;
        result <= MICROS_RAMFS_RESULT_OK;
        result = (enum micros_ramfs_result)((int)result + 1)
    ) {
        EXPECT_TRUE(
            micros_ramfs_build_result(
                MICROS_RAMFS_MESSAGE_READ,
                result,
                0,
                0,
                0,
                0,
                0,
                &built
            ) == MICROS_RAMFS_CORE_OK
        );
        EXPECT_TRUE(
            (int32_t)read_u32_le(&built.payload[8]) == result
        );
    }
    return true;
}

static bool lookup_semantics_and_reference_limits_are_exact(void)
{
    struct micros_ramfs_reply_action action;
    struct micros_ipc_message message;
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
    uint8_t overlong[MICROS_RAMFS_NAME_MAX + 2];
    uint8_t embedded[] = {'/', 'e', 't', '\0', 'c', '\0'};
    uint8_t missing_nul[] = {'/', 'e', 't', 'c'};
    uint8_t missing_then_overlong[
        sizeof("/missing/") + MICROS_RAMFS_NAME_MAX + 1
    ];
    uint64_t root = micros_ramfs_handle(0, 1);
    uint64_t etc = micros_ramfs_handle(1, 1);
    uint64_t motd = micros_ramfs_handle(2, 1);

    EXPECT_TRUE(initialize_mounted());
    EXPECT_TRUE(
        lookup_path(
            root,
            root,
            absolute_motd,
            sizeof(absolute_motd),
            MICROS_RAMFS_RESULT_OK,
            &action
        )
    );
    EXPECT_TRUE(read_u64_le(&action.message.payload[16]) == motd);
    EXPECT_TRUE(read_u64_le(&action.message.payload[24]) == 13);
    EXPECT_TRUE(state.nodes[2].reference_count == 1);

    EXPECT_TRUE(
        lookup_path(
            root,
            root,
            relative_motd,
            sizeof(relative_motd),
            MICROS_RAMFS_RESULT_OK,
            &action
        )
    );
    EXPECT_TRUE(state.nodes[2].reference_count == 2);
    EXPECT_TRUE(
        lookup_path(
            root,
            root,
            empty,
            sizeof(empty),
            MICROS_RAMFS_RESULT_OK,
            &action
        )
    );
    EXPECT_TRUE(read_u64_le(&action.message.payload[16]) == root);
    EXPECT_TRUE(state.nodes[0].reference_count == 2);
    EXPECT_TRUE(
        lookup_path(
            root,
            root,
            parent_path,
            sizeof(parent_path),
            MICROS_RAMFS_RESULT_OK,
            &action
        )
    );

    EXPECT_TRUE(
        lookup_path(
            root,
            root,
            (const uint8_t *)"/etc",
            5,
            MICROS_RAMFS_RESULT_OK,
            &action
        )
    );
    EXPECT_TRUE(read_u64_le(&action.message.payload[16]) == etc);
    EXPECT_TRUE(
        lookup_path(
            etc,
            etc,
            confined_parent,
            sizeof(confined_parent),
            MICROS_RAMFS_RESULT_OK,
            &action
        )
    );
    EXPECT_TRUE(read_u64_le(&action.message.payload[16]) == motd);

    reset_bus();
    message = lookup_request(root, etc, 1, 0);
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );
    EXPECT_TRUE(bus.calls == 0);

    EXPECT_TRUE(
        lookup_path(
            root,
            root,
            trailing,
            sizeof(trailing),
            MICROS_RAMFS_RESULT_NOT_DIRECTORY,
            &action
        )
    );
    EXPECT_TRUE(
        lookup_path(
            root,
            root,
            missing,
            sizeof(missing),
            MICROS_RAMFS_RESULT_NOT_FOUND,
            &action
        )
    );
    memcpy(missing_then_overlong, "/missing/", sizeof("/missing/") - 1);
    memset(
        &missing_then_overlong[sizeof("/missing/") - 1],
        'a',
        MICROS_RAMFS_NAME_MAX + 1
    );
    missing_then_overlong[sizeof(missing_then_overlong) - 1] = 0;
    EXPECT_TRUE(
        lookup_path(
            root,
            root,
            missing_then_overlong,
            sizeof(missing_then_overlong),
            MICROS_RAMFS_RESULT_RANGE,
            &action
        )
    );

    memset(overlong, 'a', sizeof(overlong));
    overlong[sizeof(overlong) - 1] = 0;
    EXPECT_TRUE(
        lookup_path(
            root,
            root,
            overlong,
            sizeof(overlong),
            MICROS_RAMFS_RESULT_RANGE,
            &action
        )
    );
    EXPECT_TRUE(
        lookup_path(
            motd,
            root,
            empty,
            sizeof(empty),
            MICROS_RAMFS_RESULT_NOT_DIRECTORY,
            &action
        )
    );
    EXPECT_TRUE(
        lookup_path(
            motd,
            root,
            dot,
            sizeof(dot),
            MICROS_RAMFS_RESULT_NOT_DIRECTORY,
            &action
        )
    );
    EXPECT_TRUE(
        lookup_path(
            motd,
            root,
            dotdot,
            sizeof(dotdot),
            MICROS_RAMFS_RESULT_NOT_DIRECTORY,
            &action
        )
    );
    EXPECT_TRUE(
        lookup_path(
            motd,
            root,
            parent_name,
            sizeof(parent_name),
            MICROS_RAMFS_RESULT_NOT_DIRECTORY,
            &action
        )
    );
    EXPECT_TRUE(
        lookup_path(
            motd,
            root,
            (const uint8_t *)"/etc",
            5,
            MICROS_RAMFS_RESULT_OK,
            &action
        )
    );
    EXPECT_TRUE(
        lookup_path(
            motd,
            motd,
            dot,
            sizeof(dot),
            MICROS_RAMFS_RESULT_NOT_DIRECTORY,
            &action
        )
    );
    EXPECT_TRUE(
        lookup_path(
            root,
            root,
            embedded,
            sizeof(embedded),
            MICROS_RAMFS_RESULT_MALFORMED,
            &action
        )
    );
    EXPECT_TRUE(
        lookup_path(
            root,
            root,
            missing_nul,
            sizeof(missing_nul),
            MICROS_RAMFS_RESULT_MALFORMED,
            &action
        )
    );

    reset_bus();
    message = lookup_request(root, root, 0, 0);
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );
    message = lookup_request(
        root,
        root,
        MICROS_RAMFS_PATH_MAX + 1,
        0
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );
    message = lookup_request(root, root, 1, UINT64_MAX);
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );

    reset_bus();
    bus.storage[0] = 0;
    bus.reject = true;
    message = lookup_request(root, root, 1, 0);
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_GRANT
        )
    );

    state.nodes[2].reference_count = UINT32_MAX;
    reset_bus();
    memcpy(bus.storage, absolute_motd, sizeof(absolute_motd));
    message = lookup_request(
        root,
        root,
        sizeof(absolute_motd),
        0
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_NO_SPACE
        )
    );
    return true;
}

static bool handles_create_mkdir_and_capacity_are_exact(void)
{
    struct micros_ramfs_reply_action action;
    struct micros_ipc_message message;
    uint8_t malformed_name[] = {'b', '\0', 'd', '\0'};
    uint64_t root = micros_ramfs_handle(0, 1);
    uint64_t created;
    size_t index;

    EXPECT_TRUE(initialize_mounted());
    EXPECT_TRUE(
        create_node(
            MICROS_RAMFS_MESSAGE_CREATE,
            root,
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0640),
            "file",
            &action
        )
    );
    created = read_u64_le(&action.message.payload[16]);
    EXPECT_TRUE(created == micros_ramfs_handle(3, 1));
    EXPECT_TRUE(state.nodes[3].reference_count == 1);
    EXPECT_TRUE(state.nodes[3].link_count == 1);
    EXPECT_TRUE(
        read_u32_le(&action.message.payload[44])
            == (MICROS_RAMFS_MODE_REGULAR | UINT32_C(0640))
    );

    reset_bus();
    memcpy(bus.storage, "file", 5);
    message = create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        root,
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        5,
        0
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_EXISTS
        )
    );

    EXPECT_TRUE(
        create_node(
            MICROS_RAMFS_MESSAGE_MKDIR,
            root,
            MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0750),
            "dir",
            &action
        )
    );
    EXPECT_TRUE(read_u64_le(&action.message.payload[16]) == 0);
    EXPECT_TRUE(state.nodes[4].reference_count == 0);
    EXPECT_TRUE(state.nodes[4].link_count == 2);
    EXPECT_TRUE(state.nodes[0].link_count == 4);

    reset_bus();
    memcpy(bus.storage, "child", 6);
    message = create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        created,
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        6,
        0
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_NOT_DIRECTORY
        )
    );
    message = create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        micros_ramfs_handle(1, 1),
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        6,
        0
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_REFERENCE
        )
    );

    message = create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        root,
        MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755),
        2,
        0
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );
    message = create_request(
        MICROS_RAMFS_MESSAGE_MKDIR,
        root,
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        2,
        0
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );

    reset_bus();
    message = create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        root,
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        1,
        0
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );
    message = create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        root,
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        MICROS_RAMFS_NAME_MAX + 2,
        0
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );

    reset_bus();
    memcpy(bus.storage, malformed_name, sizeof(malformed_name));
    message = create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        root,
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        sizeof(malformed_name),
        0
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );
    reset_bus();
    memcpy(bus.storage, "bad/name", 9);
    message = create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        root,
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        9,
        0
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );
    reset_bus();
    memcpy(bus.storage, ".\0", 2);
    message = create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        root,
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        2,
        0
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );
    reset_bus();
    memcpy(bus.storage, "..\0", 3);
    message = create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        root,
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        3,
        0
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );
    reset_bus();
    memcpy(bus.storage, "x\0", 2);
    bus.reject = true;
    message = create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        root,
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        2,
        0
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_GRANT
        )
    );

    EXPECT_TRUE(initialize_mounted());
    for (index = 0; index < 61; ++index) {
        char name[4];

        name[0] = 'd';
        name[1] = (char)('0' + (index / 10) % 10);
        name[2] = (char)('0' + index % 10);
        name[3] = '\0';
        EXPECT_TRUE(
            create_node(
                MICROS_RAMFS_MESSAGE_MKDIR,
                root,
                MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755),
                name,
                &action
            )
        );
    }
    EXPECT_TRUE(state.node_count == MICROS_RAMFS_NODE_CAPACITY);
    EXPECT_TRUE(state.nodes[0].link_count == 64);
    reset_bus();
    memcpy(bus.storage, "full\0", 5);
    message = create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        root,
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        5,
        0
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_NO_SPACE
        )
    );
    EXPECT_TRUE(bus.calls == 0);
    return true;
}

static bool read_write_sparse_and_block_capacity_are_exact(void)
{
    struct micros_ramfs_reply_action action;
    struct micros_ipc_message message;
    uint64_t root = micros_ramfs_handle(0, 1);
    uint64_t file;
    uint64_t second_file;
    size_t index;

    EXPECT_TRUE(initialize_mounted());
    EXPECT_TRUE(
        create_node(
            MICROS_RAMFS_MESSAGE_CREATE,
            root,
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
            "data",
            &action
        )
    );
    file = read_u64_le(&action.message.payload[16]);

    reset_bus();
    memcpy(bus.storage, "abc", 3);
    message = transfer_request(
        MICROS_RAMFS_MESSAGE_WRITE,
        file,
        0,
        3,
        0
    );
    EXPECT_TRUE(
        call_expect(&message, MICROS_RAMFS_RESULT_OK, &action)
    );
    EXPECT_TRUE(read_u64_le(&action.message.payload[24]) == 3);
    EXPECT_TRUE(read_u64_le(&action.message.payload[32]) == 3);
    EXPECT_TRUE(read_u32_le(&action.message.payload[40]) == 3);

    reset_bus();
    message = transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        file,
        0,
        5,
        16
    );
    EXPECT_TRUE(
        call_expect(&message, MICROS_RAMFS_RESULT_OK, &action)
    );
    EXPECT_TRUE(read_u32_le(&action.message.payload[40]) == 3);
    EXPECT_TRUE(memcmp(&bus.storage[16], "abc", 3) == 0);

    reset_bus();
    memset(bus.storage, 0xa5, sizeof(bus.storage));
    message = transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        file,
        3,
        4,
        0
    );
    EXPECT_TRUE(
        call_expect(&message, MICROS_RAMFS_RESULT_OK, &action)
    );
    EXPECT_TRUE(read_u32_le(&action.message.payload[40]) == 0);
    EXPECT_TRUE(read_u64_le(&action.message.payload[32]) == 3);
    EXPECT_TRUE(bus.calls == 0);
    EXPECT_TRUE(bus.storage[0] == 0xa5);

    reset_bus();
    memcpy(bus.storage, "XY", 2);
    message = transfer_request(
        MICROS_RAMFS_MESSAGE_WRITE,
        file,
        8194,
        2,
        0
    );
    EXPECT_TRUE(
        call_expect(&message, MICROS_RAMFS_RESULT_OK, &action)
    );
    EXPECT_TRUE(state.nodes[3].size == 8196);
    EXPECT_TRUE(
        state.nodes[3].blocks[1] == MICROS_RAMFS_BLOCK_NONE
    );

    reset_bus();
    memset(bus.storage, 0xa5, sizeof(bus.storage));
    message = transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        file,
        4090,
        4106 > MICROS_RAMFS_TRANSFER_MAX
            ? MICROS_RAMFS_TRANSFER_MAX
            : 4106,
        0
    );
    EXPECT_TRUE(
        call_expect(&message, MICROS_RAMFS_RESULT_OK, &action)
    );
    for (index = 0; index < MICROS_RAMFS_TRANSFER_MAX; ++index) {
        EXPECT_TRUE(bus.storage[index] == 0);
    }
    reset_bus();
    message = transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        file,
        8188,
        8,
        0
    );
    EXPECT_TRUE(
        call_expect(&message, MICROS_RAMFS_RESULT_OK, &action)
    );
    EXPECT_TRUE(memcmp(bus.storage, "\0\0\0\0\0\0XY", 8) == 0);

    reset_bus();
    memcpy(bus.storage, "WXYZ", 4);
    message = transfer_request(
        MICROS_RAMFS_MESSAGE_WRITE,
        file,
        4094,
        4,
        0
    );
    EXPECT_TRUE(
        call_expect(&message, MICROS_RAMFS_RESULT_OK, &action)
    );
    reset_bus();
    message = transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        file,
        4094,
        4,
        0
    );
    EXPECT_TRUE(
        call_expect(&message, MICROS_RAMFS_RESULT_OK, &action)
    );
    EXPECT_TRUE(memcmp(bus.storage, "WXYZ", 4) == 0);

    reset_bus();
    memcpy(bus.storage, "fail", 4);
    bus.reject = true;
    message = transfer_request(
        MICROS_RAMFS_MESSAGE_WRITE,
        file,
        100,
        4,
        0
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_GRANT
        )
    );
    reset_bus();
    memset(bus.storage, 0x5a, sizeof(bus.storage));
    bus.reject = true;
    message = transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        file,
        0,
        3,
        0
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_GRANT
        )
    );
    EXPECT_TRUE(bus.storage[0] == 0x5a);

    reset_bus();
    message = transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        root,
        0,
        1,
        0
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_IS_DIRECTORY
        )
    );
    message.type = MICROS_RAMFS_MESSAGE_WRITE;
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_IS_DIRECTORY
        )
    );
    message = transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        file,
        0,
        0,
        0
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );
    message = transfer_request(
        MICROS_RAMFS_MESSAGE_WRITE,
        file,
        MICROS_RAMFS_FILE_SIZE_MAX,
        1,
        0
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );

    EXPECT_TRUE(initialize_mounted());
    EXPECT_TRUE(
        create_node(
            MICROS_RAMFS_MESSAGE_CREATE,
            root,
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
            "maximum",
            &action
        )
    );
    file = read_u64_le(&action.message.payload[16]);
    reset_bus();
    bus.storage[0] = 0x7f;
    message = transfer_request(
        MICROS_RAMFS_MESSAGE_WRITE,
        file,
        MICROS_RAMFS_FILE_SIZE_MAX - 1,
        1,
        0
    );
    EXPECT_TRUE(
        call_expect(&message, MICROS_RAMFS_RESULT_OK, &action)
    );
    EXPECT_TRUE(state.nodes[3].size == MICROS_RAMFS_FILE_SIZE_MAX);

    EXPECT_TRUE(initialize_mounted());
    EXPECT_TRUE(
        create_node(
            MICROS_RAMFS_MESSAGE_CREATE,
            root,
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
            "blocks",
            &action
        )
    );
    file = read_u64_le(&action.message.payload[16]);
    for (index = 0; index < 63; ++index) {
        reset_bus();
        bus.storage[0] = (uint8_t)index;
        message = transfer_request(
            MICROS_RAMFS_MESSAGE_WRITE,
            file,
            index * MICROS_RAMFS_BLOCK_SIZE,
            1,
            0
        );
        EXPECT_TRUE(
            call_expect(
                &message,
                MICROS_RAMFS_RESULT_OK,
                &action
            )
        );
    }
    EXPECT_TRUE(
        state.allocated_block_count == MICROS_RAMFS_BLOCK_CAPACITY
    );
    EXPECT_TRUE(
        create_node(
            MICROS_RAMFS_MESSAGE_CREATE,
            root,
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
            "second",
            &action
        )
    );
    second_file = read_u64_le(&action.message.payload[16]);
    reset_bus();
    bus.storage[0] = 1;
    message = transfer_request(
        MICROS_RAMFS_MESSAGE_WRITE,
        second_file,
        0,
        1,
        0
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_NO_SPACE
        )
    );
    EXPECT_TRUE(bus.calls == 0);
    return true;
}

static bool getdents_cursor_and_records_are_exact(void)
{
    struct micros_ramfs_reply_action action;
    struct micros_ipc_message message;
    uint64_t root = micros_ramfs_handle(0, 1);
    uint64_t file;

    EXPECT_TRUE(initialize_mounted());
    reset_bus();
    message = getdents_request(root, 0, 384, 0);
    EXPECT_TRUE(
        call_expect(&message, MICROS_RAMFS_RESULT_OK, &action)
    );
    EXPECT_TRUE(read_u32_le(&action.message.payload[40]) == 384);
    EXPECT_TRUE(
        read_u64_le(&action.message.payload[32])
            == MICROS_RAMFS_DIRECTORY_CURSOR_END
    );
    EXPECT_TRUE(read_u64_le(&bus.storage[0]) == root);
    EXPECT_TRUE(read_u32_le(&bus.storage[12]) == 1);
    EXPECT_TRUE(bus.storage[16] == '.');
    EXPECT_TRUE(read_u64_le(&bus.storage[128]) == root);
    EXPECT_TRUE(read_u32_le(&bus.storage[128 + 12]) == 2);
    EXPECT_TRUE(memcmp(&bus.storage[128 + 16], "..", 2) == 0);
    EXPECT_TRUE(
        read_u64_le(&bus.storage[256])
            == micros_ramfs_handle(1, 1)
    );
    EXPECT_TRUE(read_u32_le(&bus.storage[256 + 12]) == 3);
    EXPECT_TRUE(memcmp(&bus.storage[256 + 16], "etc", 3) == 0);
    EXPECT_TRUE(
        bus.storage[256 + MICROS_RAMFS_DIRECTORY_RECORD_SIZE - 1]
            == 0
    );

    reset_bus();
    message = getdents_request(root, 0, 128, 0);
    EXPECT_TRUE(
        call_expect(&message, MICROS_RAMFS_RESULT_OK, &action)
    );
    EXPECT_TRUE(read_u64_le(&action.message.payload[32]) == 1);
    reset_bus();
    message = getdents_request(root, 1, 128, 0);
    EXPECT_TRUE(
        call_expect(&message, MICROS_RAMFS_RESULT_OK, &action)
    );
    EXPECT_TRUE(read_u64_le(&action.message.payload[32]) == 3);
    reset_bus();
    message = getdents_request(root, 2, 128, 0);
    EXPECT_TRUE(
        call_expect(&message, MICROS_RAMFS_RESULT_OK, &action)
    );
    EXPECT_TRUE(
        read_u64_le(&action.message.payload[32])
            == MICROS_RAMFS_DIRECTORY_CURSOR_END
    );

    EXPECT_TRUE(
        create_node(
            MICROS_RAMFS_MESSAGE_CREATE,
            root,
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
            "file",
            &action
        )
    );
    file = read_u64_le(&action.message.payload[16]);
    EXPECT_TRUE(
        create_node(
            MICROS_RAMFS_MESSAGE_MKDIR,
            root,
            MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755),
            "dir",
            &action
        )
    );
    reset_bus();
    message = getdents_request(root, 4, 128, 0);
    EXPECT_TRUE(
        call_expect(&message, MICROS_RAMFS_RESULT_OK, &action)
    );
    EXPECT_TRUE(read_u64_le(&bus.storage[0]) == file);
    EXPECT_TRUE(read_u64_le(&action.message.payload[32]) == 6);

    reset_bus();
    memset(bus.storage, 0xa5, sizeof(bus.storage));
    message = getdents_request(
        root,
        MICROS_RAMFS_DIRECTORY_CURSOR_END,
        128,
        0
    );
    EXPECT_TRUE(
        call_expect(&message, MICROS_RAMFS_RESULT_OK, &action)
    );
    EXPECT_TRUE(read_u32_le(&action.message.payload[40]) == 0);
    EXPECT_TRUE(bus.calls == 0);
    EXPECT_TRUE(bus.storage[0] == 0xa5);

    message = getdents_request(file, 0, 128, 0);
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_NOT_DIRECTORY
        )
    );
    message = getdents_request(root, 67, 128, 0);
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );
    message = getdents_request(root, 0, 127, 0);
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );
    message = getdents_request(root, 0, 129, 0);
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );
    reset_bus();
    memset(bus.storage, 0x5a, sizeof(bus.storage));
    bus.reject = true;
    message = getdents_request(root, 0, 128, 0);
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_GRANT
        )
    );
    EXPECT_TRUE(bus.storage[0] == 0x5a);
    return true;
}

static bool putnode_and_handle_validation_are_exact(void)
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

    EXPECT_TRUE(initialize_mounted());
    for (index = 0; index < 2; ++index) {
        EXPECT_TRUE(
            lookup_path(
                root,
                root,
                motd_path,
                sizeof(motd_path),
                MICROS_RAMFS_RESULT_OK,
                &action
            )
        );
    }
    EXPECT_TRUE(state.nodes[2].reference_count == 2);
    message = putnode_request(motd, 2);
    EXPECT_TRUE(
        call_expect(&message, MICROS_RAMFS_RESULT_OK, &action)
    );
    EXPECT_TRUE(state.nodes[2].reference_count == 0);

    message = transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        motd,
        0,
        1,
        0
    );
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_REFERENCE
        )
    );
    message = putnode_request(motd, 1);
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_REFERENCE
        )
    );
    message = putnode_request(root, 1);
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_REFERENCE
        )
    );
    message = putnode_request(root, 0);
    EXPECT_TRUE(
        failure_preserves_state(
            &message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );

    for (
        index = 0;
        index < sizeof(invalid_handles) / sizeof(invalid_handles[0]);
        ++index
    ) {
        message = putnode_request(invalid_handles[index], 1);
        EXPECT_TRUE(
            failure_preserves_state(
                &message,
                MICROS_RAMFS_RESULT_NODE
            )
        );
    }
    return true;
}

int main(int argc, char **argv)
{
    if (
        argc != 2
        || !load_seed(argv[1])
        || !seed_state_is_exact_and_invariants_fail_closed()
        || !protocol_mount_and_result_layout_are_exact()
        || !lookup_semantics_and_reference_limits_are_exact()
        || !handles_create_mkdir_and_capacity_are_exact()
        || !read_write_sparse_and_block_capacity_are_exact()
        || !getdents_cursor_and_records_are_exact()
        || !putnode_and_handle_validation_are_exact()
    ) {
        return 1;
    }
    puts("RAMFS_CORE_TEST_PASS");
    return 0;
}
