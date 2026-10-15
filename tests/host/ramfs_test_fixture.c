#include "tests/host/ramfs_test_internal.h"

#include <string.h>

static struct ramfs_test_fixture test_fixture;

const micros_endpoint_t ramfs_test_vfs_endpoint = UINT32_C(0x00001005);

void ramfs_test_write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

void ramfs_test_write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

uint32_t ramfs_test_read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

uint64_t ramfs_test_read_u64_le(const uint8_t *bytes)
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
    if (selected->invariant || endpoint != ramfs_test_vfs_endpoint) {
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

bool ramfs_test_load_seed(
    struct ramfs_test_fixture *fixture,
    const char *path
)
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
        || fread(fixture->seed_image, 1, (size_t)length, stream) != (size_t)length
        || fclose(stream) != 0
    ) {
        return false;
    }
    fixture->seed_image_size = (size_t)length;
    return true;
}

void ramfs_test_reset_bus(
    struct ramfs_test_fixture *fixture
)
{
    memset(&fixture->bus, 0, sizeof(fixture->bus));
}

struct micros_ipc_message ramfs_test_request(uint32_t type)
{
    struct micros_ipc_message message = {0};

    message.type = type;
    message.source = ramfs_test_vfs_endpoint;
    message.reply_token = 1;
    ramfs_test_write_u32_le(
        &message.payload[0],
        MICROS_RAMFS_PROTOCOL_VERSION
    );
    return message;
}

struct micros_ipc_message ramfs_test_lookup_request(
    uint64_t start,
    uint64_t root,
    uint32_t length,
    uint64_t grant_offset
)
{
    struct micros_ipc_message message =
        ramfs_test_request(MICROS_RAMFS_MESSAGE_LOOKUP);

    ramfs_test_write_u64_le(&message.payload[8], start);
    ramfs_test_write_u64_le(&message.payload[16], root);
    ramfs_test_write_u32_le(&message.payload[24], TEST_GRANT);
    ramfs_test_write_u32_le(&message.payload[28], length);
    ramfs_test_write_u64_le(&message.payload[32], grant_offset);
    return message;
}

struct micros_ipc_message ramfs_test_create_request(
    uint32_t type,
    uint64_t parent,
    uint32_t mode,
    uint32_t length,
    uint64_t grant_offset
)
{
    struct micros_ipc_message message = ramfs_test_request(type);

    ramfs_test_write_u64_le(&message.payload[8], parent);
    ramfs_test_write_u32_le(&message.payload[16], mode);
    ramfs_test_write_u32_le(&message.payload[20], TEST_GRANT);
    ramfs_test_write_u64_le(&message.payload[24], grant_offset);
    ramfs_test_write_u32_le(&message.payload[32], length);
    return message;
}

struct micros_ipc_message ramfs_test_transfer_request(
    uint32_t type,
    uint64_t node,
    uint64_t file_offset,
    uint32_t count,
    uint64_t grant_offset
)
{
    struct micros_ipc_message message = ramfs_test_request(type);

    ramfs_test_write_u64_le(&message.payload[8], node);
    ramfs_test_write_u64_le(&message.payload[16], file_offset);
    ramfs_test_write_u32_le(&message.payload[24], TEST_GRANT);
    ramfs_test_write_u32_le(&message.payload[28], count);
    ramfs_test_write_u64_le(&message.payload[32], grant_offset);
    return message;
}

struct micros_ipc_message ramfs_test_getdents_request(
    uint64_t node,
    uint64_t cursor,
    uint32_t count,
    uint64_t grant_offset
)
{
    struct micros_ipc_message message =
        ramfs_test_request(MICROS_RAMFS_MESSAGE_GETDENTS);

    ramfs_test_write_u64_le(&message.payload[8], node);
    ramfs_test_write_u64_le(&message.payload[16], cursor);
    ramfs_test_write_u32_le(&message.payload[24], TEST_GRANT);
    ramfs_test_write_u32_le(&message.payload[28], count);
    ramfs_test_write_u64_le(&message.payload[32], grant_offset);
    return message;
}

struct micros_ipc_message ramfs_test_putnode_request(
    uint64_t node,
    uint32_t count
)
{
    struct micros_ipc_message message =
        ramfs_test_request(MICROS_RAMFS_MESSAGE_PUTNODE);

    ramfs_test_write_u64_le(&message.payload[8], node);
    ramfs_test_write_u32_le(&message.payload[16], count);
    return message;
}

static enum micros_ramfs_result reply_result(
    const struct micros_ramfs_reply_action *action
)
{
    return (enum micros_ramfs_result)(int32_t)ramfs_test_read_u32_le(
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

bool ramfs_test_call_expect(
    struct ramfs_test_fixture *fixture,
    const struct micros_ipc_message *message,
    enum micros_ramfs_result expected,
    struct micros_ramfs_reply_action *action
)
{
    if (
        micros_ramfs_handle_call(
            &fixture->state,
            message,
            &ramfs_test_io,
            action
        ) != MICROS_RAMFS_CORE_OK
        || !action->active
        || action->reply_token != message->reply_token
        || action->message.type != MICROS_RAMFS_MESSAGE_RESULT
        || ramfs_test_read_u32_le(&action->message.payload[0])
            != MICROS_RAMFS_PROTOCOL_VERSION
        || ramfs_test_read_u32_le(&action->message.payload[4])
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

bool ramfs_test_failure_preserves_state(
    struct ramfs_test_fixture *fixture,
    const struct micros_ipc_message *message,
    enum micros_ramfs_result expected
)
{
    struct micros_ramfs_reply_action action;

    fixture->state_snapshot = fixture->state;
    memcpy(fixture->storage_snapshot, fixture->bus.storage, sizeof(fixture->storage_snapshot));
    if (!ramfs_test_call_expect(fixture, message, expected, &action)) {
        return false;
    }
    return (
        memcmp(&fixture->state, &fixture->state_snapshot, sizeof(fixture->state)) == 0
        && memcmp(
            fixture->bus.storage,
            fixture->storage_snapshot,
            sizeof(fixture->storage_snapshot)
        ) == 0
    );
}

bool ramfs_test_initialize_unmounted(
    struct ramfs_test_fixture *fixture
)
{
    ramfs_test_reset_bus(fixture);
    return micros_ramfs_state_initialize(
        &fixture->state,
        ramfs_test_vfs_endpoint,
        fixture->seed_image,
        fixture->seed_image_size
    ) == MICROS_RAMFS_CORE_OK;
}

bool ramfs_test_initialize_mounted(
    struct ramfs_test_fixture *fixture
)
{
    struct micros_ramfs_reply_action action;
    struct micros_ipc_message message =
        ramfs_test_request(MICROS_RAMFS_MESSAGE_MOUNT);

    return (
        ramfs_test_initialize_unmounted(fixture)
        && ramfs_test_call_expect(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_OK,
            &action
        )
    );
}

static bool write_grant_bytes(
    struct ramfs_test_fixture *fixture,
    const uint8_t *bytes,
    size_t length,
    size_t offset
)
{
    if (offset > sizeof(fixture->bus.storage) || length > sizeof(fixture->bus.storage) - offset) {
        return false;
    }
    memcpy(&fixture->bus.storage[offset], bytes, length);
    return true;
}

bool ramfs_test_lookup_path(
    struct ramfs_test_fixture *fixture,
    uint64_t start,
    uint64_t root,
    const uint8_t *path,
    uint32_t length,
    enum micros_ramfs_result expected,
    struct micros_ramfs_reply_action *action
)
{
    struct micros_ipc_message message;

    ramfs_test_reset_bus(fixture);
    if (!write_grant_bytes(fixture, path, length, 0)) {
        return false;
    }
    message = ramfs_test_lookup_request(start, root, length, 0);
    return ramfs_test_call_expect(fixture, &message, expected, action);
}

bool ramfs_test_create_node(
    struct ramfs_test_fixture *fixture,
    uint32_t type,
    uint64_t parent,
    uint32_t mode,
    const char *name,
    struct micros_ramfs_reply_action *action
)
{
    struct micros_ipc_message message;
    size_t length = strlen(name) + 1;

    ramfs_test_reset_bus(fixture);
    if (!write_grant_bytes(fixture, (const uint8_t *)name, length, 0)) {
        return false;
    }
    message = ramfs_test_create_request(
        type,
        parent,
        mode,
        (uint32_t)length,
        0
    );
    return ramfs_test_call_expect(fixture, &message, MICROS_RAMFS_RESULT_OK, action);
}

const struct micros_ramfs_io ramfs_test_io = {
    .copy = test_copy,
    .context = &test_fixture.bus,
};

struct ramfs_test_fixture *ramfs_test_context(void)
{
    return &test_fixture;
}
