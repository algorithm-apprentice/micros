#include "tests/host/ramfs_model_internal.h"

#include <stdio.h>
#include <string.h>

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

static struct micros_ipc_message request(uint32_t type)
{
    struct micros_ipc_message message = {0};

    message.type = type;
    message.source = UINT32_C(0x00001005);
    message.reply_token = 1;
    ramfs_model_write_u32_le(
        &message.payload[0],
        MICROS_RAMFS_PROTOCOL_VERSION
    );
    return message;
}

static struct micros_ipc_message lookup_request(
    uint64_t start,
    uint64_t root,
    uint32_t length
)
{
    struct micros_ipc_message message =
        request(MICROS_RAMFS_MESSAGE_LOOKUP);

    ramfs_model_write_u64_le(&message.payload[8], start);
    ramfs_model_write_u64_le(&message.payload[16], root);
    ramfs_model_write_u32_le(&message.payload[24], MODEL_GRANT);
    ramfs_model_write_u32_le(&message.payload[28], length);
    return message;
}

static struct micros_ipc_message create_request(
    uint32_t type,
    uint64_t parent,
    uint32_t mode,
    uint32_t length
)
{
    struct micros_ipc_message message = request(type);

    ramfs_model_write_u64_le(&message.payload[8], parent);
    ramfs_model_write_u32_le(&message.payload[16], mode);
    ramfs_model_write_u32_le(&message.payload[20], MODEL_GRANT);
    ramfs_model_write_u32_le(&message.payload[32], length);
    return message;
}

static struct micros_ipc_message transfer_request(
    uint32_t type,
    uint64_t node,
    uint64_t file_offset,
    uint32_t count
)
{
    struct micros_ipc_message message = request(type);

    ramfs_model_write_u64_le(&message.payload[8], node);
    ramfs_model_write_u64_le(&message.payload[16], file_offset);
    ramfs_model_write_u32_le(&message.payload[24], MODEL_GRANT);
    ramfs_model_write_u32_le(&message.payload[28], count);
    return message;
}

static struct micros_ipc_message getdents_request(
    uint64_t node,
    uint64_t cursor,
    uint32_t count
)
{
    struct micros_ipc_message message =
        request(MICROS_RAMFS_MESSAGE_GETDENTS);

    ramfs_model_write_u64_le(&message.payload[8], node);
    ramfs_model_write_u64_le(&message.payload[16], cursor);
    ramfs_model_write_u32_le(&message.payload[24], MODEL_GRANT);
    ramfs_model_write_u32_le(&message.payload[28], count);
    return message;
}

static struct micros_ipc_message putnode_request(
    uint64_t node,
    uint32_t count
)
{
    struct micros_ipc_message message =
        request(MICROS_RAMFS_MESSAGE_PUTNODE);

    ramfs_model_write_u64_le(&message.payload[8], node);
    ramfs_model_write_u32_le(&message.payload[16], count);
    return message;
}

static bool execute_call(
    struct ramfs_model *selected,
    enum model_operation operation,
    uint32_t detail,
    const struct micros_ipc_message *message,
    enum micros_ramfs_result expected_result,
    const struct model_fields *expected_fields
)
{
    struct micros_ramfs_reply_action action;
    enum micros_ramfs_result actual_result;

    ramfs_model_record_operation(selected, operation, detail);
    MODEL_EXPECT(
        selected,
        micros_ramfs_handle_call(
            &selected->production,
            message,
            &ramfs_model_io,
            &action
        ) == MICROS_RAMFS_CORE_OK
    );
    actual_result = (enum micros_ramfs_result)(int32_t)read_u32_le(
        &action.message.payload[8]
    );
    MODEL_EXPECT(selected, action.active);
    MODEL_EXPECT(selected, action.reply_token == message->reply_token);
    MODEL_EXPECT(
        selected,
        action.message.type == MICROS_RAMFS_MESSAGE_RESULT
    );
    MODEL_EXPECT(
        selected,
        read_u32_le(&action.message.payload[4]) == message->type
    );
    MODEL_EXPECT(selected, actual_result == expected_result);
    if (expected_result == MICROS_RAMFS_RESULT_OK) {
        MODEL_EXPECT(
            selected,
            read_u64_le(&action.message.payload[16])
                == expected_fields->node
        );
        MODEL_EXPECT(
            selected,
            read_u64_le(&action.message.payload[24])
                == expected_fields->file_size
        );
        MODEL_EXPECT(
            selected,
            read_u64_le(&action.message.payload[32])
                == expected_fields->position
        );
        MODEL_EXPECT(
            selected,
            read_u32_le(&action.message.payload[40])
                == expected_fields->count
        );
        MODEL_EXPECT(
            selected,
            read_u32_le(&action.message.payload[44])
                == expected_fields->mode
        );
    } else {
        MODEL_EXPECT(
            selected,
            memcmp(
                &action.message.payload[12],
                (uint8_t[36]){0},
                36
            ) == 0
        );
    }
    MODEL_EXPECT(
        selected,
        memcmp(
            selected->bus.storage,
            selected->bus.expected,
            MODEL_GRANT_CAPACITY
        ) == 0
    );
    MODEL_EXPECT(selected, ramfs_model_compare_state(selected));
    if (expected_result == MICROS_RAMFS_RESULT_NO_SPACE) {
        ++selected->coverage.no_space;
    }
    return true;
}

static void prepare_bus(struct ramfs_model *selected, uint8_t fill)
{
    memset(selected->bus.storage, fill, MODEL_GRANT_CAPACITY);
    memset(selected->bus.expected, fill, MODEL_GRANT_CAPACITY);
    selected->bus.reject = false;
}

bool ramfs_model_execute_mount(struct ramfs_model *selected)
{
    struct micros_ipc_message message =
        request(MICROS_RAMFS_MESSAGE_MOUNT);
    struct model_fields fields = {0};
    enum micros_ramfs_result result;

    prepare_bus(selected, 0xa5);
    result = ramfs_model_reference_mount(&selected->reference, &fields);
    ++selected->coverage.mount;
    return execute_call(
        selected,
        MODEL_OPERATION_MOUNT,
        0,
        &message,
        result,
        &fields
    );
}

bool ramfs_model_execute_lookup(
    struct ramfs_model *selected,
    uint64_t start,
    uint64_t root,
    const uint8_t *path,
    size_t length,
    bool relative,
    bool reject
)
{
    struct micros_ipc_message message;
    struct model_fields fields = {0};
    enum micros_ramfs_result result;

    prepare_bus(selected, 0);
    memcpy(selected->bus.storage, path, length);
    memcpy(selected->bus.expected, path, length);
    selected->bus.reject = reject;
    message = lookup_request(start, root, (uint32_t)length);
    if (reject) {
        result = MICROS_RAMFS_RESULT_GRANT;
        ++selected->coverage.grant_failure;
    } else {
        result = ramfs_model_reference_lookup(
            &selected->reference,
            start,
            root,
            path,
            length,
            &fields
        );
    }
    if (relative) {
        ++selected->coverage.lookup_relative;
    } else {
        ++selected->coverage.lookup_absolute;
    }
    return execute_call(
        selected,
        reject
            ? MODEL_OPERATION_GRANT_FAILURE
            : (
                relative
                    ? MODEL_OPERATION_LOOKUP_RELATIVE
                    : MODEL_OPERATION_LOOKUP_ABSOLUTE
            ),
        (uint32_t)length,
        &message,
        result,
        &fields
    );
}

bool ramfs_model_execute_create(
    struct ramfs_model *selected,
    uint32_t type,
    uint64_t parent,
    const char *name
)
{
    struct micros_ipc_message message;
    struct model_fields fields = {0};
    enum micros_ramfs_result result;
    uint32_t mode = (
        type == MICROS_RAMFS_MESSAGE_MKDIR
            ? MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755)
            : MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644)
    );
    size_t length = strlen(name) + 1;

    prepare_bus(selected, 0);
    memcpy(selected->bus.storage, name, length);
    memcpy(selected->bus.expected, name, length);
    message = create_request(
        type,
        parent,
        mode,
        (uint32_t)length
    );
    result = ramfs_model_reference_create(
        &selected->reference,
        type,
        parent,
        mode,
        (const uint8_t *)name,
        length,
        &fields
    );
    if (type == MICROS_RAMFS_MESSAGE_MKDIR) {
        ++selected->coverage.mkdir;
    } else {
        ++selected->coverage.create;
    }
    return execute_call(
        selected,
        type == MICROS_RAMFS_MESSAGE_MKDIR
            ? MODEL_OPERATION_MKDIR
            : MODEL_OPERATION_CREATE,
        (uint32_t)selected->reference.node_count,
        &message,
        result,
        &fields
    );
}

bool ramfs_model_execute_write(
    struct ramfs_model *selected,
    uint64_t handle,
    uint64_t offset,
    const uint8_t *bytes,
    uint32_t count
)
{
    struct micros_ipc_message message;
    struct model_fields fields = {0};
    enum micros_ramfs_result result;

    prepare_bus(selected, 0);
    memcpy(selected->bus.storage, bytes, count);
    memcpy(selected->bus.expected, bytes, count);
    message = transfer_request(
        MICROS_RAMFS_MESSAGE_WRITE,
        handle,
        offset,
        count
    );
    result = ramfs_model_reference_write(
        &selected->reference,
        handle,
        offset,
        bytes,
        count,
        &fields
    );
    ++selected->coverage.write;
    return execute_call(
        selected,
        MODEL_OPERATION_WRITE,
        (uint32_t)(offset / MICROS_RAMFS_BLOCK_SIZE),
        &message,
        result,
        &fields
    );
}

bool ramfs_model_execute_read(
    struct ramfs_model *selected,
    uint64_t handle,
    uint64_t offset,
    uint32_t count
)
{
    struct micros_ipc_message message;
    struct model_fields fields = {0};
    enum micros_ramfs_result result;

    prepare_bus(selected, 0xa5);
    message = transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        handle,
        offset,
        count
    );
    result = ramfs_model_reference_read(
        &selected->reference,
        handle,
        offset,
        count,
        selected->bus.expected,
        &fields
    );
    ++selected->coverage.read;
    return execute_call(
        selected,
        MODEL_OPERATION_READ,
        count,
        &message,
        result,
        &fields
    );
}

bool ramfs_model_execute_getdents(
    struct ramfs_model *selected,
    uint64_t handle,
    uint32_t capacity
)
{
    struct micros_ipc_message message;
    struct model_fields fields = {0};
    enum micros_ramfs_result result;

    if (
        selected->directory_cursor
            == MICROS_RAMFS_DIRECTORY_CURSOR_END
    ) {
        selected->directory_cursor = 0;
    }
    prepare_bus(selected, 0xa5);
    message = getdents_request(
        handle,
        selected->directory_cursor,
        capacity
    );
    result = ramfs_model_reference_getdents(
        &selected->reference,
        handle,
        selected->directory_cursor,
        capacity,
        selected->bus.expected,
        &fields
    );
    if (result == MICROS_RAMFS_RESULT_OK) {
        selected->directory_cursor = fields.position;
    }
    ++selected->coverage.getdents;
    return execute_call(
        selected,
        MODEL_OPERATION_GETDENTS,
        capacity,
        &message,
        result,
        &fields
    );
}

bool ramfs_model_execute_putnode(
    struct ramfs_model *selected,
    uint64_t handle,
    uint32_t count
)
{
    struct micros_ipc_message message =
        putnode_request(handle, count);
    struct model_fields fields = {0};
    enum micros_ramfs_result result;

    prepare_bus(selected, 0xa5);
    result = ramfs_model_reference_putnode(
        &selected->reference,
        handle,
        count
    );
    ++selected->coverage.putnode;
    return execute_call(
        selected,
        MODEL_OPERATION_PUTNODE,
        count,
        &message,
        result,
        &fields
    );
}

bool ramfs_model_execute_stale(struct ramfs_model *selected)
{
    struct micros_ipc_message message =
        putnode_request(micros_ramfs_handle(2, 2), 1);
    struct model_fields fields = {0};

    prepare_bus(selected, 0xa5);
    ++selected->coverage.stale;
    return execute_call(
        selected,
        MODEL_OPERATION_STALE,
        2,
        &message,
        MICROS_RAMFS_RESULT_NODE,
        &fields
    );
}

bool ramfs_model_execute_malformed(struct ramfs_model *selected)
{
    struct micros_ipc_message message =
        request(MICROS_RAMFS_MESSAGE_READ);
    struct model_fields fields = {0};

    ramfs_model_write_u32_le(&message.payload[4], 1);
    prepare_bus(selected, 0xa5);
    ++selected->coverage.malformed;
    return execute_call(
        selected,
        MODEL_OPERATION_MALFORMED,
        1,
        &message,
        MICROS_RAMFS_RESULT_MALFORMED,
        &fields
    );
}
