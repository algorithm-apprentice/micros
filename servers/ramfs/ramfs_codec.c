#include "servers/ramfs/ramfs_internal.h"

void ramfs_zero_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

bool ramfs_bytes_are_zero(const uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

uint16_t ramfs_read_u16_le(const uint8_t *bytes)
{
    return (uint16_t)(
        (uint16_t)bytes[0]
        | (uint16_t)bytes[1] << 8
    );
}

uint32_t ramfs_read_u32_le(const uint8_t *bytes)
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

void ramfs_write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

void ramfs_write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

static bool request_type_is_known(uint32_t type)
{
    return (
        type >= MICROS_RAMFS_MESSAGE_MOUNT
        && type <= MICROS_RAMFS_MESSAGE_PUTNODE
    );
}

static bool result_is_defined(enum micros_ramfs_result result)
{
    return (
        result >= MICROS_RAMFS_RESULT_REFERENCE
        && result <= MICROS_RAMFS_RESULT_OK
    );
}

enum micros_ramfs_protocol_status micros_ramfs_decode_request(
    const struct micros_ipc_message *message,
    struct micros_ramfs_request *request
)
{
    struct micros_ramfs_request decoded;
    uint32_t version;
    uint32_t flags;

    if (message == NULL || request == NULL) {
        return MICROS_RAMFS_PROTOCOL_INVARIANT;
    }
    if (!request_type_is_known(message->type)) {
        return MICROS_RAMFS_PROTOCOL_BAD_TYPE;
    }
    version = ramfs_read_u32_le(&message->payload[0]);
    if (version != MICROS_RAMFS_PROTOCOL_VERSION) {
        return MICROS_RAMFS_PROTOCOL_BAD_VERSION;
    }
    flags = ramfs_read_u32_le(&message->payload[4]);
    if (flags != 0 || message->reply_token == 0) {
        return (
            message->reply_token == 0
                ? MICROS_RAMFS_PROTOCOL_INVARIANT
                : MICROS_RAMFS_PROTOCOL_MALFORMED
        );
    }

    ramfs_zero_bytes(&decoded, sizeof(decoded));
    decoded.type = message->type;
    decoded.source = message->source;
    decoded.reply_token = message->reply_token;

    switch (message->type) {
    case MICROS_RAMFS_MESSAGE_MOUNT:
        if (!ramfs_bytes_are_zero(&message->payload[8], 40)) {
            return MICROS_RAMFS_PROTOCOL_MALFORMED;
        }
        break;
    case MICROS_RAMFS_MESSAGE_LOOKUP:
        if (!ramfs_bytes_are_zero(&message->payload[40], 8)) {
            return MICROS_RAMFS_PROTOCOL_MALFORMED;
        }
        decoded.start = read_u64_le(&message->payload[8]);
        decoded.root = read_u64_le(&message->payload[16]);
        decoded.grant = ramfs_read_u32_le(&message->payload[24]);
        decoded.length = ramfs_read_u32_le(&message->payload[28]);
        decoded.grant_offset = read_u64_le(&message->payload[32]);
        break;
    case MICROS_RAMFS_MESSAGE_CREATE:
    case MICROS_RAMFS_MESSAGE_MKDIR:
        if (!ramfs_bytes_are_zero(&message->payload[36], 12)) {
            return MICROS_RAMFS_PROTOCOL_MALFORMED;
        }
        decoded.node = read_u64_le(&message->payload[8]);
        decoded.mode = ramfs_read_u32_le(&message->payload[16]);
        decoded.grant = ramfs_read_u32_le(&message->payload[20]);
        decoded.grant_offset = read_u64_le(&message->payload[24]);
        decoded.length = ramfs_read_u32_le(&message->payload[32]);
        break;
    case MICROS_RAMFS_MESSAGE_READ:
    case MICROS_RAMFS_MESSAGE_WRITE:
        if (!ramfs_bytes_are_zero(&message->payload[40], 8)) {
            return MICROS_RAMFS_PROTOCOL_MALFORMED;
        }
        decoded.node = read_u64_le(&message->payload[8]);
        decoded.file_offset = read_u64_le(&message->payload[16]);
        decoded.grant = ramfs_read_u32_le(&message->payload[24]);
        decoded.count = ramfs_read_u32_le(&message->payload[28]);
        decoded.grant_offset = read_u64_le(&message->payload[32]);
        break;
    case MICROS_RAMFS_MESSAGE_GETDENTS:
        if (!ramfs_bytes_are_zero(&message->payload[40], 8)) {
            return MICROS_RAMFS_PROTOCOL_MALFORMED;
        }
        decoded.node = read_u64_le(&message->payload[8]);
        decoded.cursor = read_u64_le(&message->payload[16]);
        decoded.grant = ramfs_read_u32_le(&message->payload[24]);
        decoded.count = ramfs_read_u32_le(&message->payload[28]);
        decoded.grant_offset = read_u64_le(&message->payload[32]);
        break;
    case MICROS_RAMFS_MESSAGE_PUTNODE:
        if (!ramfs_bytes_are_zero(&message->payload[20], 28)) {
            return MICROS_RAMFS_PROTOCOL_MALFORMED;
        }
        decoded.node = read_u64_le(&message->payload[8]);
        decoded.count = ramfs_read_u32_le(&message->payload[16]);
        break;
    default:
        return MICROS_RAMFS_PROTOCOL_INVARIANT;
    }
    *request = decoded;
    return MICROS_RAMFS_PROTOCOL_OK;
}

enum micros_ramfs_core_error micros_ramfs_build_result(
    uint32_t request_type,
    enum micros_ramfs_result result,
    uint64_t node,
    uint64_t file_size,
    uint64_t position,
    uint32_t count,
    uint32_t mode,
    struct micros_ipc_message *message
)
{
    struct micros_ipc_message built;

    if (
        message == NULL
        || !result_is_defined(result)
        || (
            result != MICROS_RAMFS_RESULT_OK
            && (
                node != 0
                || file_size != 0
                || position != 0
                || count != 0
                || mode != 0
            )
        )
    ) {
        return MICROS_RAMFS_CORE_ERROR_ARGUMENT;
    }
    ramfs_zero_bytes(&built, sizeof(built));
    built.type = MICROS_RAMFS_MESSAGE_RESULT;
    ramfs_write_u32_le(
        &built.payload[0],
        MICROS_RAMFS_PROTOCOL_VERSION
    );
    ramfs_write_u32_le(&built.payload[4], request_type);
    ramfs_write_u32_le(&built.payload[8], (uint32_t)(int32_t)result);
    ramfs_write_u64_le(&built.payload[16], node);
    ramfs_write_u64_le(&built.payload[24], file_size);
    ramfs_write_u64_le(&built.payload[32], position);
    ramfs_write_u32_le(&built.payload[40], count);
    ramfs_write_u32_le(&built.payload[44], mode);
    *message = built;
    return MICROS_RAMFS_CORE_OK;
}

enum micros_ramfs_core_error ramfs_prepare_reply(
    const struct micros_ipc_message *request,
    enum micros_ramfs_result result,
    const struct ramfs_result_fields *fields,
    struct micros_ramfs_reply_action *action
)
{
    struct micros_ramfs_reply_action candidate;
    struct ramfs_result_fields empty = {0};

    if (result != MICROS_RAMFS_RESULT_OK) {
        fields = &empty;
    }
    ramfs_zero_bytes(&candidate, sizeof(candidate));
    if (
        request->reply_token == 0
        || micros_ramfs_build_result(
            request->type,
            result,
            fields->node,
            fields->file_size,
            fields->position,
            fields->count,
            fields->mode,
            &candidate.message
        ) != MICROS_RAMFS_CORE_OK
    ) {
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }
    candidate.active = true;
    candidate.reply_token = request->reply_token;
    *action = candidate;
    return MICROS_RAMFS_CORE_OK;
}
