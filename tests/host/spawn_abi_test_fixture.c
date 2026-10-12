#include "tests/host/spawn_abi_test_internal.h"

static struct spawn_abi_test_context test_context;

struct spawn_abi_test_context *spawn_abi_test_context_get(void)
{
    return &test_context;
}

void spawn_abi_test_write_u16_le(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

void spawn_abi_test_write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

void spawn_abi_test_write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

uint64_t spawn_abi_test_read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

bool spawn_abi_test_bytes_are_zero(const void *storage, size_t size)
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

struct test_string spawn_abi_test_literal_string(const char *value)
{
    struct test_string result = {
        .bytes = (const uint8_t *)value,
        .size = (uint16_t)(strlen(value) + 1),
    };

    return result;
}

void spawn_abi_test_write_string_reference(
    uint8_t *bytes,
    size_t reference_offset,
    size_t string_offset,
    uint16_t string_size
)
{
    spawn_abi_test_write_u16_le(&bytes[reference_offset], (uint16_t)string_offset);
    spawn_abi_test_write_u16_le(&bytes[reference_offset + 2], string_size);
}

size_t spawn_abi_test_build_spawn_request(
    struct spawn_abi_test_context *context,
    const struct test_string *path,
    const struct test_string *arguments,
    uint16_t argument_count,
    const struct test_string *environment,
    uint16_t environment_count,
    const struct micros_spawn_descriptor_action *actions,
    uint16_t action_count
)
{
    size_t cursor = MICROS_SPAWN_REQUEST_HEADER_SIZE;
    size_t index;

    memset(context->request_bytes, 0, sizeof(context->request_bytes));
    spawn_abi_test_write_u32_le(&context->request_bytes[0], MICROS_SPAWN_REQUEST_VERSION);
    spawn_abi_test_write_u32_le(
        &context->request_bytes[4],
        MICROS_SPAWN_REQUEST_HEADER_SIZE
    );
    spawn_abi_test_write_u16_le(
        &context->request_bytes[MICROS_SPAWN_ARGUMENT_COUNT_OFFSET],
        argument_count
    );
    spawn_abi_test_write_u16_le(
        &context->request_bytes[MICROS_SPAWN_ENVIRONMENT_COUNT_OFFSET],
        environment_count
    );
    spawn_abi_test_write_u16_le(
        &context->request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTION_COUNT_OFFSET],
        action_count
    );

    spawn_abi_test_write_string_reference(
        context->request_bytes,
        MICROS_SPAWN_PATH_REFERENCE_OFFSET,
        cursor,
        path->size
    );
    memcpy(&context->request_bytes[cursor], path->bytes, path->size);
    cursor += path->size;

    for (index = 0; index < argument_count; ++index) {
        spawn_abi_test_write_string_reference(
            context->request_bytes,
            MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET
                + index * MICROS_SPAWN_STRING_REFERENCE_SIZE,
            cursor,
            arguments[index].size
        );
        memcpy(
            &context->request_bytes[cursor],
            arguments[index].bytes,
            arguments[index].size
        );
        cursor += arguments[index].size;
    }
    for (index = 0; index < environment_count; ++index) {
        spawn_abi_test_write_string_reference(
            context->request_bytes,
            MICROS_SPAWN_ENVIRONMENT_REFERENCES_OFFSET
                + index * MICROS_SPAWN_STRING_REFERENCE_SIZE,
            cursor,
            environment[index].size
        );
        memcpy(
            &context->request_bytes[cursor],
            environment[index].bytes,
            environment[index].size
        );
        cursor += environment[index].size;
    }
    for (index = 0; index < action_count; ++index) {
        size_t offset = MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET
            + index * MICROS_SPAWN_DESCRIPTOR_ACTION_SIZE;

        spawn_abi_test_write_u16_le(&context->request_bytes[offset], actions[index].source);
        spawn_abi_test_write_u16_le(
            &context->request_bytes[offset + 2],
            actions[index].destination
        );
        spawn_abi_test_write_u32_le(&context->request_bytes[offset + 4], actions[index].operation);
    }
    spawn_abi_test_write_u32_le(&context->request_bytes[8], (uint32_t)cursor);
    return cursor;
}

void spawn_abi_test_fill_plain_string(uint8_t *bytes, uint16_t size, uint8_t value)
{
    size_t index;

    for (index = 0; index + 1 < size; ++index) {
        bytes[index] = value;
    }
    bytes[size - 1] = 0;
}

void spawn_abi_test_fill_environment_string(uint8_t *bytes, uint16_t size)
{
    size_t index;

    bytes[0] = 'A';
    bytes[1] = '=';
    for (index = 2; index + 1 < size; ++index) {
        bytes[index] = 'v';
    }
    bytes[size - 1] = 0;
}

bool spawn_abi_test_build_occupancy_request(
    struct spawn_abi_test_context *context,bool overflow, size_t *request_size)
{
    static const uint16_t argument_sizes[8] = {
        256, 256, 256, 256, 256, 256, 1, 1,
    };
    static const uint16_t environment_sizes[8] = {
        249, 3, 3, 3, 3, 3, 3, 3,
    };
    struct test_string path = spawn_abi_test_literal_string("/x");
    struct test_string arguments[8];
    struct test_string environment[8];
    size_t index;

    for (index = 0; index < 8; ++index) {
        uint16_t size = argument_sizes[index];

        spawn_abi_test_fill_plain_string(context->occupancy_arguments[index], size, 'a');
        arguments[index].bytes = context->occupancy_arguments[index];
        arguments[index].size = size;
    }
    for (index = 0; index < 8; ++index) {
        uint16_t size = environment_sizes[index];

        if (index == 0 && overflow) {
            ++size;
        }
        spawn_abi_test_fill_environment_string(context->occupancy_environment[index], size);
        environment[index].bytes = context->occupancy_environment[index];
        environment[index].size = size;
    }
    *request_size = spawn_abi_test_build_spawn_request(context,
        &path,
        arguments,
        8,
        environment,
        8,
        NULL,
        0
    );
    return true;
}

size_t spawn_abi_test_build_valid_elf(
    struct spawn_abi_test_context *context,
    uint64_t text_pages,
    uint64_t rodata_pages,
    uint64_t data_pages
)
{
    static const uint32_t flags[3] = {
        UINT32_C(0x5),
        UINT32_C(0x4),
        UINT32_C(0x6),
    };
    uint64_t page_counts[3] = {
        text_pages,
        rodata_pages,
        data_pages,
    };
    uint64_t virtual_address = MICROS_APPLICATION_ENTRY;
    size_t index;

    memset(context->elf_bytes, 0, sizeof(context->elf_bytes));
    context->elf_bytes[0] = 0x7f;
    context->elf_bytes[1] = 'E';
    context->elf_bytes[2] = 'L';
    context->elf_bytes[3] = 'F';
    context->elf_bytes[4] = 2;
    context->elf_bytes[5] = 1;
    context->elf_bytes[6] = 1;
    spawn_abi_test_write_u16_le(&context->elf_bytes[16], 2);
    spawn_abi_test_write_u16_le(&context->elf_bytes[18], 243);
    spawn_abi_test_write_u32_le(&context->elf_bytes[20], 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[24], MICROS_APPLICATION_ENTRY);
    spawn_abi_test_write_u64_le(&context->elf_bytes[32], 64);
    spawn_abi_test_write_u16_le(&context->elf_bytes[52], 64);
    spawn_abi_test_write_u16_le(&context->elf_bytes[54], 56);
    spawn_abi_test_write_u16_le(&context->elf_bytes[56], 3);

    for (index = 0; index < 3; ++index) {
        size_t offset = 64 + index * 56;
        uint64_t file_offset = index * UINT64_C(4096);
        uint64_t file_size = index == 2 ? UINT64_C(1024) : UINT64_C(4096);
        uint64_t memory_size = page_counts[index] * UINT64_C(4096);

        spawn_abi_test_write_u32_le(&context->elf_bytes[offset], 1);
        spawn_abi_test_write_u32_le(&context->elf_bytes[offset + 4], flags[index]);
        spawn_abi_test_write_u64_le(&context->elf_bytes[offset + 8], file_offset);
        spawn_abi_test_write_u64_le(&context->elf_bytes[offset + 16], virtual_address);
        spawn_abi_test_write_u64_le(&context->elf_bytes[offset + 24], virtual_address);
        spawn_abi_test_write_u64_le(&context->elf_bytes[offset + 32], file_size);
        spawn_abi_test_write_u64_le(&context->elf_bytes[offset + 40], memory_size);
        spawn_abi_test_write_u64_le(&context->elf_bytes[offset + 48], 4096);
        virtual_address += memory_size;
    }
    return 8192 + 1024;
}
