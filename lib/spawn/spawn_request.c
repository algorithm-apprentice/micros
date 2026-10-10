#include "lib/spawn/spawn_request.h"

#include <stdbool.h>

static uint16_t spawn_request_read_u16_le(const uint8_t *bytes)
{
    return (uint16_t)(
        (uint16_t)bytes[0]
        | (uint16_t)((uint16_t)bytes[1] << 8)
    );
}

static uint32_t spawn_request_read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

static bool spawn_request_bytes_are_zero(const uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool spawn_request_string_bytes_are_canonical(
    const uint8_t *bytes,
    uint16_t size,
    bool require_nonempty,
    bool require_environment_separator
)
{
    bool found_separator = false;
    size_t index;

    if (
        size == 0
        || size > MICROS_SPAWN_STRING_SIZE_MAX
        || bytes[size - 1] != 0
        || (require_nonempty && size == 1)
    ) {
        return false;
    }
    for (index = 0; index + 1 < size; ++index) {
        if (bytes[index] == 0) {
            return false;
        }
        if (
            require_environment_separator
            && !found_separator
            && bytes[index] == '='
        ) {
            if (index == 0) {
                return false;
            }
            found_separator = true;
        }
    }
    return !require_environment_separator || found_separator;
}

static bool spawn_request_decode_string_reference(
    const uint8_t *bytes,
    size_t total_size,
    size_t reference_offset,
    size_t expected_offset,
    bool require_nonempty,
    bool environment,
    struct micros_spawn_string_reference *reference,
    size_t *next_offset
)
{
    uint16_t offset = spawn_request_read_u16_le(&bytes[reference_offset]);
    uint16_t size = spawn_request_read_u16_le(&bytes[reference_offset + 2]);

    if (
        !spawn_request_bytes_are_zero(&bytes[reference_offset + 4], 4)
        || offset != expected_offset
        || size == 0
        || expected_offset > total_size
        || size > total_size - expected_offset
        || !spawn_request_string_bytes_are_canonical(
            &bytes[expected_offset],
            size,
            require_nonempty,
            environment
        )
    ) {
        return false;
    }
    reference->offset = offset;
    reference->size = size;
    *next_offset = expected_offset + size;
    return true;
}

enum micros_spawn_request_error micros_spawn_request_decode(
    const uint8_t *bytes,
    size_t byte_count,
    struct micros_spawn_request *request
)
{
    struct micros_spawn_request decoded = {0};
    bool destinations[MICROS_SPAWN_DESCRIPTOR_CAPACITY] = {false};
    size_t cursor = MICROS_SPAWN_REQUEST_HEADER_SIZE;
    size_t index;

    if (bytes == NULL || request == NULL) {
        return MICROS_SPAWN_REQUEST_ERROR_ARGUMENT;
    }
    if (
        byte_count < MICROS_SPAWN_REQUEST_SIZE_MIN
        || byte_count > MICROS_SPAWN_REQUEST_SIZE_MAX
        || spawn_request_read_u32_le(&bytes[0]) != MICROS_SPAWN_REQUEST_VERSION
        || spawn_request_read_u32_le(&bytes[4])
            != MICROS_SPAWN_REQUEST_HEADER_SIZE
        || spawn_request_read_u32_le(&bytes[8]) != byte_count
        || spawn_request_read_u32_le(&bytes[12]) != 0
        || !spawn_request_bytes_are_zero(&bytes[30], 2)
        || !spawn_request_bytes_are_zero(
            &bytes[MICROS_SPAWN_HEADER_RESERVED_OFFSET],
            MICROS_SPAWN_REQUEST_HEADER_SIZE
                - MICROS_SPAWN_HEADER_RESERVED_OFFSET
        )
    ) {
        return MICROS_SPAWN_REQUEST_ERROR_MALFORMED;
    }

    decoded.total_size = (uint32_t)byte_count;
    decoded.argument_count = spawn_request_read_u16_le(
        &bytes[MICROS_SPAWN_ARGUMENT_COUNT_OFFSET]
    );
    decoded.environment_count = spawn_request_read_u16_le(
        &bytes[MICROS_SPAWN_ENVIRONMENT_COUNT_OFFSET]
    );
    decoded.descriptor_action_count = spawn_request_read_u16_le(
        &bytes[MICROS_SPAWN_DESCRIPTOR_ACTION_COUNT_OFFSET]
    );
    if (
        decoded.argument_count == 0
        || decoded.argument_count > MICROS_SPAWN_ARGUMENT_CAPACITY
        || decoded.environment_count
            > MICROS_SPAWN_ENVIRONMENT_CAPACITY
        || decoded.descriptor_action_count
            > MICROS_SPAWN_DESCRIPTOR_ACTION_CAPACITY
        || !spawn_request_decode_string_reference(
            bytes,
            byte_count,
            MICROS_SPAWN_PATH_REFERENCE_OFFSET,
            cursor,
            true,
            false,
            &decoded.path,
            &cursor
        )
    ) {
        return MICROS_SPAWN_REQUEST_ERROR_MALFORMED;
    }

    for (index = 0; index < decoded.argument_count; ++index) {
        if (!spawn_request_decode_string_reference(
            bytes,
            byte_count,
            MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET
                + index * MICROS_SPAWN_STRING_REFERENCE_SIZE,
            cursor,
            index == 0,
            false,
            &decoded.arguments[index],
            &cursor
        )) {
            return MICROS_SPAWN_REQUEST_ERROR_MALFORMED;
        }
    }
    for (
        index = decoded.argument_count;
        index < MICROS_SPAWN_ARGUMENT_CAPACITY;
        ++index
    ) {
        if (!spawn_request_bytes_are_zero(
            &bytes[MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET
                + index * MICROS_SPAWN_STRING_REFERENCE_SIZE],
            MICROS_SPAWN_STRING_REFERENCE_SIZE
        )) {
            return MICROS_SPAWN_REQUEST_ERROR_MALFORMED;
        }
    }

    for (index = 0; index < decoded.environment_count; ++index) {
        if (!spawn_request_decode_string_reference(
            bytes,
            byte_count,
            MICROS_SPAWN_ENVIRONMENT_REFERENCES_OFFSET
                + index * MICROS_SPAWN_STRING_REFERENCE_SIZE,
            cursor,
            false,
            true,
            &decoded.environment[index],
            &cursor
        )) {
            return MICROS_SPAWN_REQUEST_ERROR_MALFORMED;
        }
    }
    for (
        index = decoded.environment_count;
        index < MICROS_SPAWN_ENVIRONMENT_CAPACITY;
        ++index
    ) {
        if (!spawn_request_bytes_are_zero(
            &bytes[MICROS_SPAWN_ENVIRONMENT_REFERENCES_OFFSET
                + index * MICROS_SPAWN_STRING_REFERENCE_SIZE],
            MICROS_SPAWN_STRING_REFERENCE_SIZE
        )) {
            return MICROS_SPAWN_REQUEST_ERROR_MALFORMED;
        }
    }
    if (cursor != byte_count) {
        return MICROS_SPAWN_REQUEST_ERROR_MALFORMED;
    }

    for (index = 0; index < decoded.descriptor_action_count; ++index) {
        struct micros_spawn_descriptor_action *action =
            &decoded.descriptor_actions[index];
        size_t offset = MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET
            + index * MICROS_SPAWN_DESCRIPTOR_ACTION_SIZE;

        action->source = spawn_request_read_u16_le(&bytes[offset]);
        action->destination = spawn_request_read_u16_le(&bytes[offset + 2]);
        action->operation = spawn_request_read_u32_le(&bytes[offset + 4]);
        if (
            action->source >= MICROS_SPAWN_DESCRIPTOR_CAPACITY
            || action->destination >= MICROS_SPAWN_DESCRIPTOR_CAPACITY
            || action->operation != MICROS_SPAWN_DESCRIPTOR_DUPLICATE
            || destinations[action->destination]
        ) {
            return MICROS_SPAWN_REQUEST_ERROR_MALFORMED;
        }
        destinations[action->destination] = true;
    }
    for (
        index = decoded.descriptor_action_count;
        index < MICROS_SPAWN_DESCRIPTOR_ACTION_CAPACITY;
        ++index
    ) {
        if (!spawn_request_bytes_are_zero(
            &bytes[MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET
                + index * MICROS_SPAWN_DESCRIPTOR_ACTION_SIZE],
            MICROS_SPAWN_DESCRIPTOR_ACTION_SIZE
        )) {
            return MICROS_SPAWN_REQUEST_ERROR_MALFORMED;
        }
    }

    *request = decoded;
    return MICROS_SPAWN_REQUEST_OK;
}
