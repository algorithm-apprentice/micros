#include "lib/runtime/application_internal.h"

#include <stdbool.h>

struct application_stack_layout {
    size_t arguments_offset;
    size_t environment_offset;
    size_t auxiliary_offset;
    size_t strings_offset;
};

static bool read_word(const uint8_t *page, size_t offset, uint64_t *value)
{
    uint64_t decoded = 0;
    size_t index;

    if (offset > MICROS_APPLICATION_STACK_PAGE_SIZE - sizeof(uint64_t)) {
        return false;
    }
    for (index = 0; index < sizeof(uint64_t); ++index) {
        decoded |= (uint64_t)page[offset + index] << (index * 8);
    }
    *value = decoded;
    return true;
}

static bool zero_range(const uint8_t *page, size_t begin, size_t end)
{
    size_t index;

    for (index = begin; index < end; ++index) {
        if (page[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool decode_vectors(
    const uint8_t *page,
    size_t stack_offset,
    struct application_stack_layout *layout,
    struct micros_application_startup *startup
)
{
    uint64_t value;
    size_t offset;
    uint32_t count;

    if (
        !read_word(page, stack_offset, &value)
        || value == 0
        || value > MICROS_SPAWN_ARGUMENT_CAPACITY
    ) {
        return false;
    }
    startup->argument_count = (uint32_t)value;
    layout->arguments_offset = stack_offset + sizeof(uint64_t);
    offset = layout->arguments_offset
        + startup->argument_count * sizeof(uint64_t);
    if (!read_word(page, offset, &value) || value != 0) {
        return false;
    }
    layout->environment_offset = offset + sizeof(uint64_t);
    for (count = 0; count <= MICROS_SPAWN_ENVIRONMENT_CAPACITY; ++count) {
        offset = layout->environment_offset + count * sizeof(uint64_t);
        if (!read_word(page, offset, &value)) {
            return false;
        }
        if (value == 0) {
            break;
        }
    }
    if (count > MICROS_SPAWN_ENVIRONMENT_CAPACITY) {
        return false;
    }
    startup->environment_count = count;
    offset += sizeof(uint64_t);
    layout->auxiliary_offset = (offset + 15) & ~(size_t)15;
    if (
        layout->auxiliary_offset
            > MICROS_APPLICATION_STACK_PAGE_SIZE
                - MICROS_APPLICATION_AUXV_COUNT * 2 * sizeof(uint64_t)
        || !zero_range(page, offset, layout->auxiliary_offset)
    ) {
        return false;
    }
    layout->strings_offset = layout->auxiliary_offset
        + MICROS_APPLICATION_AUXV_COUNT * 2 * sizeof(uint64_t);
    return true;
}

static bool endpoint_value_is_valid(uint64_t value)
{
    const uint64_t slot_mask =
        (UINT64_C(1) << MICROS_ENDPOINT_SLOT_BITS) - 1;

    return (
        value <= UINT32_MAX
        && value != MICROS_ENDPOINT_NONE
        && value != MICROS_ENDPOINT_ANY
        && (value >> MICROS_ENDPOINT_SLOT_BITS) != 0
        && (value & slot_mask) < MICROS_PROCESS_CAPACITY
    );
}

static bool decode_auxiliary(
    const uint8_t *page,
    size_t offset,
    struct micros_application_startup *startup
)
{
    static const uint64_t types[MICROS_APPLICATION_AUXV_COUNT] = {
        MICROS_AT_PM_ENDPOINT,
        MICROS_AT_VFS_ENDPOINT,
        MICROS_AT_PID,
        MICROS_AT_PAGE_SIZE,
        MICROS_AT_NULL,
    };
    uint64_t values[MICROS_APPLICATION_AUXV_COUNT];
    size_t index;

    for (index = 0; index < MICROS_APPLICATION_AUXV_COUNT; ++index) {
        uint64_t type;
        size_t entry = offset + index * 2 * sizeof(uint64_t);

        if (
            !read_word(page, entry, &type)
            || type != types[index]
            || !read_word(page, entry + sizeof(uint64_t), &values[index])
        ) {
            return false;
        }
    }
    if (
        !endpoint_value_is_valid(values[0])
        || !endpoint_value_is_valid(values[1])
        || values[2] == 0
        || values[3] != MICROS_APPLICATION_STACK_PAGE_SIZE
        || values[4] != 0
    ) {
        return false;
    }
    startup->pm_endpoint = (micros_endpoint_t)values[0];
    startup->vfs_endpoint = (micros_endpoint_t)values[1];
    startup->pid = values[2];
    startup->page_size = values[3];
    return true;
}

static bool consume_string(const uint8_t *page, size_t *cursor)
{
    size_t remaining = MICROS_APPLICATION_STACK_PAGE_SIZE - *cursor;
    size_t index;

    if (remaining > MICROS_SPAWN_STRING_SIZE_MAX) {
        remaining = MICROS_SPAWN_STRING_SIZE_MAX;
    }
    for (index = 0; index < remaining; ++index) {
        if (page[*cursor + index] == 0) {
            *cursor += index + 1;
            return true;
        }
    }
    return false;
}

static bool consume_vector_strings(
    const uint8_t *page,
    size_t vector_offset,
    uint32_t count,
    size_t *cursor
)
{
    uint32_t index;

    for (index = 0; index < count; ++index) {
        uint64_t pointer;

        if (
            !read_word(page, vector_offset + index * sizeof(uint64_t), &pointer)
            || pointer != MICROS_APPLICATION_STACK_BASE + *cursor
            || !consume_string(page, cursor)
        ) {
            return false;
        }
    }
    return true;
}

enum micros_application_startup_error micros_application_startup_decode(
    const uint8_t *page,
    size_t page_size,
    uint64_t stack_pointer,
    struct micros_application_startup *startup
)
{
    struct micros_application_startup candidate = {0};
    struct application_stack_layout layout;
    size_t cursor;

    if (
        page == NULL
        || startup == NULL
        || page_size != MICROS_APPLICATION_STACK_PAGE_SIZE
        || stack_pointer % 16 != 0
        || stack_pointer < MICROS_APPLICATION_STACK_TOP
            - MICROS_APPLICATION_STACK_OCCUPIED_MAX
        || stack_pointer >= MICROS_APPLICATION_STACK_TOP
    ) {
        return MICROS_APPLICATION_STARTUP_ERROR_ARGUMENT;
    }
    if (
        !decode_vectors(
            page, (size_t)(stack_pointer - MICROS_APPLICATION_STACK_BASE),
            &layout, &candidate
        )
        || !decode_auxiliary(page, layout.auxiliary_offset, &candidate)
    ) {
        return MICROS_APPLICATION_STARTUP_ERROR_MALFORMED;
    }
    cursor = layout.strings_offset;
    if (
        !consume_vector_strings(
            page, layout.arguments_offset, candidate.argument_count, &cursor
        )
        || !consume_vector_strings(
            page, layout.environment_offset, candidate.environment_count, &cursor
        )
        || MICROS_APPLICATION_STACK_PAGE_SIZE - cursor >= 16
        || !zero_range(page, cursor, MICROS_APPLICATION_STACK_PAGE_SIZE)
    ) {
        return MICROS_APPLICATION_STARTUP_ERROR_MALFORMED;
    }
    candidate.arguments_address =
        MICROS_APPLICATION_STACK_BASE + layout.arguments_offset;
    candidate.environment_address =
        MICROS_APPLICATION_STACK_BASE + layout.environment_offset;
    *startup = candidate;
    return MICROS_APPLICATION_STARTUP_OK;
}
