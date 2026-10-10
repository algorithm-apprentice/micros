#include "lib/spawn/spawn_request.h"
#include "servers/vm/vm_image.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/sv39.h"

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

struct test_string {
    const uint8_t *bytes;
    uint16_t size;
};

static uint8_t request_bytes[MICROS_SPAWN_REQUEST_SIZE_MAX];
static uint8_t elf_bytes[MICROS_APPLICATION_EXECUTABLE_SIZE_MAX + 1];

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

static void write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
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

static struct test_string literal_string(const char *value)
{
    struct test_string result = {
        .bytes = (const uint8_t *)value,
        .size = (uint16_t)(strlen(value) + 1),
    };

    return result;
}

static void write_string_reference(
    uint8_t *bytes,
    size_t reference_offset,
    size_t string_offset,
    uint16_t string_size
)
{
    write_u16_le(&bytes[reference_offset], (uint16_t)string_offset);
    write_u16_le(&bytes[reference_offset + 2], string_size);
}

static size_t build_spawn_request(
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

    memset(request_bytes, 0, sizeof(request_bytes));
    write_u32_le(&request_bytes[0], MICROS_SPAWN_REQUEST_VERSION);
    write_u32_le(
        &request_bytes[4],
        MICROS_SPAWN_REQUEST_HEADER_SIZE
    );
    write_u16_le(
        &request_bytes[MICROS_SPAWN_ARGUMENT_COUNT_OFFSET],
        argument_count
    );
    write_u16_le(
        &request_bytes[MICROS_SPAWN_ENVIRONMENT_COUNT_OFFSET],
        environment_count
    );
    write_u16_le(
        &request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTION_COUNT_OFFSET],
        action_count
    );

    write_string_reference(
        request_bytes,
        MICROS_SPAWN_PATH_REFERENCE_OFFSET,
        cursor,
        path->size
    );
    memcpy(&request_bytes[cursor], path->bytes, path->size);
    cursor += path->size;

    for (index = 0; index < argument_count; ++index) {
        write_string_reference(
            request_bytes,
            MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET
                + index * MICROS_SPAWN_STRING_REFERENCE_SIZE,
            cursor,
            arguments[index].size
        );
        memcpy(
            &request_bytes[cursor],
            arguments[index].bytes,
            arguments[index].size
        );
        cursor += arguments[index].size;
    }
    for (index = 0; index < environment_count; ++index) {
        write_string_reference(
            request_bytes,
            MICROS_SPAWN_ENVIRONMENT_REFERENCES_OFFSET
                + index * MICROS_SPAWN_STRING_REFERENCE_SIZE,
            cursor,
            environment[index].size
        );
        memcpy(
            &request_bytes[cursor],
            environment[index].bytes,
            environment[index].size
        );
        cursor += environment[index].size;
    }
    for (index = 0; index < action_count; ++index) {
        size_t offset = MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET
            + index * MICROS_SPAWN_DESCRIPTOR_ACTION_SIZE;

        write_u16_le(&request_bytes[offset], actions[index].source);
        write_u16_le(
            &request_bytes[offset + 2],
            actions[index].destination
        );
        write_u32_le(&request_bytes[offset + 4], actions[index].operation);
    }
    write_u32_le(&request_bytes[8], (uint32_t)cursor);
    return cursor;
}

static void fill_plain_string(uint8_t *bytes, uint16_t size, uint8_t value)
{
    size_t index;

    for (index = 0; index + 1 < size; ++index) {
        bytes[index] = value;
    }
    bytes[size - 1] = 0;
}

static void fill_environment_string(uint8_t *bytes, uint16_t size)
{
    size_t index;

    bytes[0] = 'A';
    bytes[1] = '=';
    for (index = 2; index + 1 < size; ++index) {
        bytes[index] = 'v';
    }
    bytes[size - 1] = 0;
}

static bool test_spawn_request_contract(void)
{
    struct test_string path = literal_string("/bin/app");
    struct test_string arguments[] = {
        literal_string("app"),
        literal_string("argument"),
    };
    struct test_string environment[] = {
        literal_string("A=B"),
    };
    struct micros_spawn_descriptor_action actions[] = {
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
    struct micros_spawn_request request;
    size_t request_size = build_spawn_request(
        &path,
        arguments,
        2,
        environment,
        1,
        actions,
        2
    );

    EXPECT_TRUE(
        MICROS_SPAWN_REQUEST_HEADER_SIZE == 256
        && MICROS_SPAWN_REQUEST_SIZE_MIN == 256
        && MICROS_SPAWN_REQUEST_SIZE_MAX == 4096
        && MICROS_SPAWN_STRING_SIZE_MAX == 256
        && MICROS_SPAWN_PATH_SIZE_MAX == 256
        && MICROS_SPAWN_ARGUMENT_CAPACITY == 8
        && MICROS_SPAWN_ENVIRONMENT_CAPACITY == 8
        && MICROS_SPAWN_DESCRIPTOR_ACTION_CAPACITY == 8
        && MICROS_SPAWN_DESCRIPTOR_CAPACITY == 16
    );
    EXPECT_TRUE(
        micros_spawn_request_decode(
            request_bytes,
            request_size,
            &request
        ) == MICROS_SPAWN_REQUEST_OK
        && request.total_size == request_size
        && request.argument_count == 2
        && request.environment_count == 1
        && request.descriptor_action_count == 2
        && request.path.offset == MICROS_SPAWN_REQUEST_HEADER_SIZE
        && request.path.size == path.size
        && request.arguments[0].offset
            == MICROS_SPAWN_REQUEST_HEADER_SIZE + path.size
        && request.arguments[1].size == arguments[1].size
        && request.environment[0].size == environment[0].size
        && request.descriptor_actions[0].source == 1
        && request.descriptor_actions[1].destination == 2
        && bytes_are_zero(
            &request.arguments[request.argument_count],
            sizeof(request.arguments)
                - request.argument_count
                    * sizeof(request.arguments[0])
        )
        && bytes_are_zero(
            &request.environment[request.environment_count],
            sizeof(request.environment)
                - request.environment_count
                    * sizeof(request.environment[0])
        )
        && bytes_are_zero(
            &request.descriptor_actions[request.descriptor_action_count],
            sizeof(request.descriptor_actions)
                - request.descriptor_action_count
                    * sizeof(request.descriptor_actions[0])
        )
    );
    return true;
}

static bool test_spawn_request_boundaries(void)
{
    static uint8_t path_storage[MICROS_SPAWN_STRING_SIZE_MAX];
    static uint8_t argument_storage[
        MICROS_SPAWN_ARGUMENT_CAPACITY
    ][MICROS_SPAWN_STRING_SIZE_MAX];
    static uint8_t environment_storage[
        MICROS_SPAWN_ENVIRONMENT_CAPACITY
    ][MICROS_SPAWN_STRING_SIZE_MAX];
    struct test_string path;
    struct test_string arguments[MICROS_SPAWN_ARGUMENT_CAPACITY];
    struct test_string environment[MICROS_SPAWN_ENVIRONMENT_CAPACITY];
    struct micros_spawn_descriptor_action actions[
        MICROS_SPAWN_DESCRIPTOR_ACTION_CAPACITY
    ];
    struct micros_spawn_request request;
    size_t request_size;
    size_t index;

    path = literal_string("/");
    arguments[0] = literal_string("x");
    request_size = build_spawn_request(
        &path,
        arguments,
        1,
        NULL,
        0,
        NULL,
        0
    );
    EXPECT_TRUE(
        request_size == MICROS_SPAWN_REQUEST_HEADER_SIZE + 4
        && micros_spawn_request_decode(
            request_bytes,
            request_size,
            &request
        ) == MICROS_SPAWN_REQUEST_OK
        && request.argument_count == 1
        && request.environment_count == 0
        && request.descriptor_action_count == 0
    );

    fill_plain_string(path_storage, sizeof(path_storage), 'p');
    path.bytes = path_storage;
    path.size = sizeof(path_storage);
    for (index = 0; index < MICROS_SPAWN_ARGUMENT_CAPACITY; ++index) {
        fill_plain_string(
            argument_storage[index],
            sizeof(argument_storage[index]),
            (uint8_t)('a' + index)
        );
        arguments[index].bytes = argument_storage[index];
        arguments[index].size = sizeof(argument_storage[index]);
    }
    for (index = 0; index < MICROS_SPAWN_ENVIRONMENT_CAPACITY; ++index) {
        uint16_t size = index < 5 ? 256 : (index == 5 ? 248 : 4);

        fill_environment_string(environment_storage[index], size);
        environment[index].bytes = environment_storage[index];
        environment[index].size = size;
        actions[index].source = 0;
        actions[index].destination = (uint16_t)index;
        actions[index].operation = MICROS_SPAWN_DESCRIPTOR_DUPLICATE;
    }
    request_size = build_spawn_request(
        &path,
        arguments,
        MICROS_SPAWN_ARGUMENT_CAPACITY,
        environment,
        MICROS_SPAWN_ENVIRONMENT_CAPACITY,
        actions,
        MICROS_SPAWN_DESCRIPTOR_ACTION_CAPACITY
    );
    EXPECT_TRUE(
        request_size == MICROS_SPAWN_REQUEST_SIZE_MAX
        && micros_spawn_request_decode(
            request_bytes,
            request_size,
            &request
        ) == MICROS_SPAWN_REQUEST_OK
        && request.argument_count == MICROS_SPAWN_ARGUMENT_CAPACITY
        && request.environment_count
            == MICROS_SPAWN_ENVIRONMENT_CAPACITY
        && request.descriptor_action_count
            == MICROS_SPAWN_DESCRIPTOR_ACTION_CAPACITY
        && request.environment[5].size == 248
        && request.environment[7].size == 4
    );
    return true;
}

static bool decode_rejects_current_request(size_t request_size)
{
    struct micros_spawn_request request;
    struct micros_spawn_request sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    request = sentinel;
    return (
        micros_spawn_request_decode(
            request_bytes,
            request_size,
            &request
        ) == MICROS_SPAWN_REQUEST_ERROR_MALFORMED
        && memcmp(&request, &sentinel, sizeof(request)) == 0
    );
}

static bool test_spawn_request_rejections(void)
{
    struct test_string path = literal_string("/x");
    struct test_string arguments[] = {literal_string("x")};
    struct test_string environment[] = {literal_string("A=")};
    struct micros_spawn_descriptor_action actions[2] = {
        {
            .source = 0,
            .destination = 1,
            .operation = MICROS_SPAWN_DESCRIPTOR_DUPLICATE,
        },
    };
    size_t size = build_spawn_request(
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        1
    );
    size_t index;
    size_t offset;

    for (index = 0; index < MICROS_SPAWN_REQUEST_HEADER_SIZE; ++index) {
        EXPECT_TRUE(decode_rejects_current_request(index));
    }
    EXPECT_TRUE(decode_rejects_current_request(
        MICROS_SPAWN_REQUEST_SIZE_MAX + 1
    ));
    for (index = 0; index < 32; ++index) {
        build_spawn_request(
            &path,
            arguments,
            1,
            environment,
            1,
            actions,
            1
        );
        write_u32_le(&request_bytes[12], UINT32_C(1) << index);
        EXPECT_TRUE(decode_rejects_current_request(size));
    }
    for (offset = 30; offset < 32; ++offset) {
        build_spawn_request(
            &path,
            arguments,
            1,
            environment,
            1,
            actions,
            1
        );
        request_bytes[offset] = 1;
        EXPECT_TRUE(decode_rejects_current_request(size));
    }
    for (
        offset = MICROS_SPAWN_HEADER_RESERVED_OFFSET;
        offset < MICROS_SPAWN_REQUEST_HEADER_SIZE;
        ++offset
    ) {
        build_spawn_request(
            &path,
            arguments,
            1,
            environment,
            1,
            actions,
            1
        );
        request_bytes[offset] = 1;
        EXPECT_TRUE(decode_rejects_current_request(size));
    }
    for (index = 0; index < 4; ++index) {
        build_spawn_request(
            &path,
            arguments,
            1,
            environment,
            1,
            actions,
            1
        );
        request_bytes[
            MICROS_SPAWN_PATH_REFERENCE_OFFSET + 4 + index
        ] = 1;
        EXPECT_TRUE(decode_rejects_current_request(size));
        build_spawn_request(
            &path,
            arguments,
            1,
            environment,
            1,
            actions,
            1
        );
        request_bytes[
            MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET + 4 + index
        ] = 1;
        EXPECT_TRUE(decode_rejects_current_request(size));
        build_spawn_request(
            &path,
            arguments,
            1,
            environment,
            1,
            actions,
            1
        );
        request_bytes[
            MICROS_SPAWN_ENVIRONMENT_REFERENCES_OFFSET + 4 + index
        ] = 1;
        EXPECT_TRUE(decode_rejects_current_request(size));
    }
    for (
        offset = MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET
            + MICROS_SPAWN_STRING_REFERENCE_SIZE;
        offset < MICROS_SPAWN_ENVIRONMENT_REFERENCES_OFFSET;
        ++offset
    ) {
        build_spawn_request(
            &path,
            arguments,
            1,
            environment,
            1,
            actions,
            1
        );
        request_bytes[offset] = 1;
        EXPECT_TRUE(decode_rejects_current_request(size));
    }
    for (
        offset = MICROS_SPAWN_ENVIRONMENT_REFERENCES_OFFSET
            + MICROS_SPAWN_STRING_REFERENCE_SIZE;
        offset < MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET;
        ++offset
    ) {
        build_spawn_request(
            &path,
            arguments,
            1,
            environment,
            1,
            actions,
            1
        );
        request_bytes[offset] = 1;
        EXPECT_TRUE(decode_rejects_current_request(size));
    }
    for (
        offset = MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET
            + MICROS_SPAWN_DESCRIPTOR_ACTION_SIZE;
        offset < MICROS_SPAWN_HEADER_RESERVED_OFFSET;
        ++offset
    ) {
        build_spawn_request(
            &path,
            arguments,
            1,
            environment,
            1,
            actions,
            1
        );
        request_bytes[offset] = 1;
        EXPECT_TRUE(decode_rejects_current_request(size));
    }

    build_spawn_request(
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        1
    );
    write_u32_le(&request_bytes[0], 2);
    EXPECT_TRUE(decode_rejects_current_request(size));
    write_u32_le(&request_bytes[0], MICROS_SPAWN_REQUEST_VERSION);
    write_u32_le(&request_bytes[4], 255);
    EXPECT_TRUE(decode_rejects_current_request(size));
    write_u32_le(&request_bytes[4], MICROS_SPAWN_REQUEST_HEADER_SIZE);
    write_u32_le(&request_bytes[8], (uint32_t)(size - 1));
    EXPECT_TRUE(decode_rejects_current_request(size));
    write_u32_le(&request_bytes[8], (uint32_t)size);
    write_u32_le(&request_bytes[12], 1);
    EXPECT_TRUE(decode_rejects_current_request(size));

    build_spawn_request(
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        1
    );
    request_bytes[30] = 1;
    EXPECT_TRUE(decode_rejects_current_request(size));
    request_bytes[30] = 0;
    request_bytes[MICROS_SPAWN_HEADER_RESERVED_OFFSET] = 1;
    EXPECT_TRUE(decode_rejects_current_request(size));

    build_spawn_request(
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        1
    );
    write_u16_le(&request_bytes[MICROS_SPAWN_ARGUMENT_COUNT_OFFSET], 0);
    EXPECT_TRUE(decode_rejects_current_request(size));
    write_u16_le(&request_bytes[MICROS_SPAWN_ARGUMENT_COUNT_OFFSET], 9);
    EXPECT_TRUE(decode_rejects_current_request(size));
    write_u16_le(&request_bytes[MICROS_SPAWN_ARGUMENT_COUNT_OFFSET], 1);
    write_u16_le(&request_bytes[MICROS_SPAWN_ENVIRONMENT_COUNT_OFFSET], 9);
    EXPECT_TRUE(decode_rejects_current_request(size));
    write_u16_le(&request_bytes[MICROS_SPAWN_ENVIRONMENT_COUNT_OFFSET], 1);
    write_u16_le(
        &request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTION_COUNT_OFFSET],
        9
    );
    EXPECT_TRUE(decode_rejects_current_request(size));

    build_spawn_request(
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        1
    );
    write_u16_le(
        &request_bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET],
        MICROS_SPAWN_REQUEST_HEADER_SIZE + 1
    );
    EXPECT_TRUE(decode_rejects_current_request(size));
    build_spawn_request(
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        1
    );
    request_bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET + 4] = 1;
    EXPECT_TRUE(decode_rejects_current_request(size));
    request_bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET + 4] = 0;
    request_bytes[
        MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET
            + MICROS_SPAWN_STRING_REFERENCE_SIZE
    ] = 1;
    EXPECT_TRUE(decode_rejects_current_request(size));
    request_bytes[
        MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET
            + MICROS_SPAWN_STRING_REFERENCE_SIZE
    ] = 0;
    request_bytes[
        MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET
            + MICROS_SPAWN_DESCRIPTOR_ACTION_SIZE
    ] = 1;
    EXPECT_TRUE(decode_rejects_current_request(size));

    build_spawn_request(
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        1
    );
    write_u16_le(
        &request_bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET + 2],
        0
    );
    EXPECT_TRUE(decode_rejects_current_request(size));
    write_u16_le(
        &request_bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET + 2],
        257
    );
    EXPECT_TRUE(decode_rejects_current_request(size));

    build_spawn_request(
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        1
    );
    request_bytes[MICROS_SPAWN_REQUEST_HEADER_SIZE] = 0;
    EXPECT_TRUE(decode_rejects_current_request(size));
    build_spawn_request(
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        1
    );
    request_bytes[
        MICROS_SPAWN_REQUEST_HEADER_SIZE + path.size - 1
    ] = 'x';
    EXPECT_TRUE(decode_rejects_current_request(size));
    build_spawn_request(
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        1
    );
    request_bytes[
        MICROS_SPAWN_REQUEST_HEADER_SIZE + path.size
    ] = 0;
    EXPECT_TRUE(decode_rejects_current_request(size));

    build_spawn_request(
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        1
    );
    request_bytes[
        MICROS_SPAWN_REQUEST_HEADER_SIZE
            + path.size
            + arguments[0].size
            + 1
    ] = 'B';
    EXPECT_TRUE(decode_rejects_current_request(size));
    build_spawn_request(
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        1
    );
    request_bytes[
        MICROS_SPAWN_REQUEST_HEADER_SIZE
            + path.size
            + arguments[0].size
    ] = '=';
    EXPECT_TRUE(decode_rejects_current_request(size));

    build_spawn_request(
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        1
    );
    write_u16_le(
        &request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET],
        MICROS_SPAWN_DESCRIPTOR_CAPACITY
    );
    EXPECT_TRUE(decode_rejects_current_request(size));
    write_u16_le(
        &request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET],
        0
    );
    write_u16_le(
        &request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET + 2],
        MICROS_SPAWN_DESCRIPTOR_CAPACITY
    );
    EXPECT_TRUE(decode_rejects_current_request(size));
    write_u16_le(
        &request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET + 2],
        1
    );
    write_u32_le(
        &request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET + 4],
        2
    );
    EXPECT_TRUE(decode_rejects_current_request(size));

    actions[1] = actions[0];
    actions[1].source = 2;
    size = build_spawn_request(
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        2
    );
    EXPECT_TRUE(decode_rejects_current_request(size));
    return true;
}

static bool build_occupancy_request(bool overflow, size_t *request_size)
{
    static uint8_t argument_storage[8][MICROS_SPAWN_STRING_SIZE_MAX];
    static uint8_t environment_storage[8][MICROS_SPAWN_STRING_SIZE_MAX];
    static const uint16_t argument_sizes[8] = {
        256, 256, 256, 256, 256, 256, 1, 1,
    };
    static const uint16_t environment_sizes[8] = {
        249, 3, 3, 3, 3, 3, 3, 3,
    };
    struct test_string path = literal_string("/x");
    struct test_string arguments[8];
    struct test_string environment[8];
    size_t index;

    for (index = 0; index < 8; ++index) {
        uint16_t size = argument_sizes[index];

        fill_plain_string(argument_storage[index], size, 'a');
        arguments[index].bytes = argument_storage[index];
        arguments[index].size = size;
    }
    for (index = 0; index < 8; ++index) {
        uint16_t size = environment_sizes[index];

        if (index == 0 && overflow) {
            ++size;
        }
        fill_environment_string(environment_storage[index], size);
        environment[index].bytes = environment_storage[index];
        environment[index].size = size;
    }
    *request_size = build_spawn_request(
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

static bool test_application_stack(void)
{
    struct test_string path = literal_string("/bin/app");
    struct test_string arguments[] = {
        literal_string("app"),
        literal_string("one"),
    };
    struct test_string environment[] = {
        literal_string("A=B"),
    };
    struct micros_application_stack_values values = {
        .pm_endpoint = UINT32_C(0x00001004),
        .vfs_endpoint = UINT32_C(0x00001006),
        .pid = UINT64_C(0x0102030405060708),
    };
    struct micros_application_stack_plan plan;
    uint8_t stack_page[MICROS_APPLICATION_STACK_PAGE_SIZE];
    size_t request_size = build_spawn_request(
        &path,
        arguments,
        2,
        environment,
        1,
        NULL,
        0
    );
    uint64_t argv0;
    uint64_t argv1;
    uint64_t env0;

    memset(stack_page, 0xa5, sizeof(stack_page));
    EXPECT_TRUE(
        micros_application_stack_build(
            request_bytes,
            request_size,
            &values,
            stack_page,
            sizeof(stack_page),
            &plan
        ) == MICROS_APPLICATION_STACK_OK
        && plan.stack_pointer % 16 == 0
        && plan.stack_pointer >= MICROS_APPLICATION_STACK_BASE
        && plan.occupied_size
            == MICROS_APPLICATION_STACK_TOP - plan.stack_pointer
        && plan.occupied_size <= MICROS_APPLICATION_STACK_OCCUPIED_MAX
        && read_u64_le(&stack_page[plan.argc_offset]) == 2
    );

    argv0 = read_u64_le(&stack_page[plan.arguments_offset]);
    argv1 = read_u64_le(&stack_page[plan.arguments_offset + 8]);
    env0 = read_u64_le(&stack_page[plan.environment_offset]);
    EXPECT_TRUE(
        argv0 >= MICROS_APPLICATION_STACK_BASE
        && argv1 > argv0
        && env0 > argv1
        && read_u64_le(&stack_page[plan.arguments_offset + 16]) == 0
        && read_u64_le(&stack_page[plan.environment_offset + 8]) == 0
        && strcmp(
            (const char *)&stack_page[
                argv0 - MICROS_APPLICATION_STACK_BASE
            ],
            "app"
        ) == 0
        && strcmp(
            (const char *)&stack_page[
                argv1 - MICROS_APPLICATION_STACK_BASE
            ],
            "one"
        ) == 0
        && strcmp(
            (const char *)&stack_page[
                env0 - MICROS_APPLICATION_STACK_BASE
            ],
            "A=B"
        ) == 0
    );
    EXPECT_TRUE(
        read_u64_le(&stack_page[plan.auxiliary_offset])
            == MICROS_AT_PM_ENDPOINT
        && read_u64_le(&stack_page[plan.auxiliary_offset + 8])
            == values.pm_endpoint
        && read_u64_le(&stack_page[plan.auxiliary_offset + 16])
            == MICROS_AT_VFS_ENDPOINT
        && read_u64_le(&stack_page[plan.auxiliary_offset + 24])
            == values.vfs_endpoint
        && read_u64_le(&stack_page[plan.auxiliary_offset + 32])
            == MICROS_AT_PID
        && read_u64_le(&stack_page[plan.auxiliary_offset + 40])
            == values.pid
        && read_u64_le(&stack_page[plan.auxiliary_offset + 48])
            == MICROS_AT_PAGE_SIZE
        && read_u64_le(&stack_page[plan.auxiliary_offset + 56])
            == MICROS_APPLICATION_STACK_PAGE_SIZE
        && read_u64_le(&stack_page[plan.auxiliary_offset + 64])
            == MICROS_AT_NULL
        && read_u64_le(&stack_page[plan.auxiliary_offset + 72]) == 0
    );
    EXPECT_TRUE(
        bytes_are_zero(stack_page, plan.argc_offset)
        && bytes_are_zero(
            &stack_page[
                plan.environment_offset
                    + (1 + 1) * sizeof(uint64_t)
            ],
            plan.auxiliary_offset
                - (
                    plan.environment_offset
                    + (1 + 1) * sizeof(uint64_t)
                )
        )
        && bytes_are_zero(
            &stack_page[plan.strings_offset
                + arguments[0].size
                + arguments[1].size
                + environment[0].size],
            MICROS_APPLICATION_STACK_PAGE_SIZE
                - (
                    plan.strings_offset
                    + arguments[0].size
                    + arguments[1].size
                    + environment[0].size
                )
        )
    );

    path = literal_string("/not-copied");
    arguments[0] = literal_string("x");
    request_size = build_spawn_request(
        &path,
        arguments,
        1,
        NULL,
        0,
        NULL,
        0
    );
    memset(stack_page, 0xa5, sizeof(stack_page));
    EXPECT_TRUE(
        micros_application_stack_build(
            request_bytes,
            request_size,
            &values,
            stack_page,
            sizeof(stack_page),
            &plan
        ) == MICROS_APPLICATION_STACK_OK
        && read_u64_le(&stack_page[plan.argc_offset]) == 1
        && read_u64_le(&stack_page[plan.environment_offset]) == 0
        && strcmp(
            (const char *)&stack_page[plan.strings_offset],
            "x"
        ) == 0
        && plan.frame_size
            == (
                plan.strings_offset - plan.argc_offset
                + arguments[0].size
            )
    );
    return true;
}

static bool test_application_stack_boundaries(void)
{
    struct micros_application_stack_plan plan;
    struct micros_application_stack_plan sentinel;
    struct micros_application_stack_values values = {
        .pm_endpoint = UINT32_C(0x00001004),
        .vfs_endpoint = UINT32_C(0x00001006),
        .pid = 7,
    };
    uint8_t stack_page[MICROS_APPLICATION_STACK_PAGE_SIZE];
    uint8_t page_sentinel[MICROS_APPLICATION_STACK_PAGE_SIZE];
    size_t request_size;

    EXPECT_TRUE(build_occupancy_request(false, &request_size));
    EXPECT_TRUE(
        micros_application_stack_plan(
            request_bytes,
            request_size,
            &plan
        ) == MICROS_APPLICATION_STACK_OK
        && plan.frame_size == MICROS_APPLICATION_STACK_OCCUPIED_MAX
        && plan.occupied_size == MICROS_APPLICATION_STACK_OCCUPIED_MAX
        && plan.stack_pointer
            == MICROS_APPLICATION_STACK_TOP
                - MICROS_APPLICATION_STACK_OCCUPIED_MAX
        && plan.argc_offset == MICROS_APPLICATION_STACK_RESERVE_MIN
    );
    memset(stack_page, 0xa5, sizeof(stack_page));
    EXPECT_TRUE(
        micros_application_stack_build(
            request_bytes,
            request_size,
            &values,
            stack_page,
            sizeof(stack_page),
            &plan
        ) == MICROS_APPLICATION_STACK_OK
        && bytes_are_zero(
            stack_page,
            MICROS_APPLICATION_STACK_RESERVE_MIN
        )
    );

    EXPECT_TRUE(build_occupancy_request(true, &request_size));
    memset(&sentinel, 0xa5, sizeof(sentinel));
    plan = sentinel;
    EXPECT_TRUE(
        micros_application_stack_plan(
            request_bytes,
            request_size,
            &plan
        ) == MICROS_APPLICATION_STACK_ERROR_TOO_BIG
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );
    memset(stack_page, 0xa5, sizeof(stack_page));
    memcpy(page_sentinel, stack_page, sizeof(stack_page));
    EXPECT_TRUE(
        micros_application_stack_build(
            request_bytes,
            request_size,
            &values,
            stack_page,
            sizeof(stack_page),
            &plan
        ) == MICROS_APPLICATION_STACK_ERROR_TOO_BIG
        && memcmp(stack_page, page_sentinel, sizeof(stack_page)) == 0
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );

    EXPECT_TRUE(build_occupancy_request(false, &request_size));
    memset(stack_page, 0xa5, sizeof(stack_page));
    memcpy(page_sentinel, stack_page, sizeof(stack_page));
    values.pid = 0;
    EXPECT_TRUE(
        micros_application_stack_build(
            request_bytes,
            request_size,
            &values,
            stack_page,
            sizeof(stack_page),
            &plan
        ) == MICROS_APPLICATION_STACK_ERROR_ARGUMENT
        && memcmp(stack_page, page_sentinel, sizeof(stack_page)) == 0
    );
    return true;
}

static size_t build_valid_elf(
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

    memset(elf_bytes, 0, sizeof(elf_bytes));
    elf_bytes[0] = 0x7f;
    elf_bytes[1] = 'E';
    elf_bytes[2] = 'L';
    elf_bytes[3] = 'F';
    elf_bytes[4] = 2;
    elf_bytes[5] = 1;
    elf_bytes[6] = 1;
    write_u16_le(&elf_bytes[16], 2);
    write_u16_le(&elf_bytes[18], 243);
    write_u32_le(&elf_bytes[20], 1);
    write_u64_le(&elf_bytes[24], MICROS_APPLICATION_ENTRY);
    write_u64_le(&elf_bytes[32], 64);
    write_u16_le(&elf_bytes[52], 64);
    write_u16_le(&elf_bytes[54], 56);
    write_u16_le(&elf_bytes[56], 3);

    for (index = 0; index < 3; ++index) {
        size_t offset = 64 + index * 56;
        uint64_t file_offset = index * UINT64_C(4096);
        uint64_t file_size = index == 2 ? UINT64_C(1024) : UINT64_C(4096);
        uint64_t memory_size = page_counts[index] * UINT64_C(4096);

        write_u32_le(&elf_bytes[offset], 1);
        write_u32_le(&elf_bytes[offset + 4], flags[index]);
        write_u64_le(&elf_bytes[offset + 8], file_offset);
        write_u64_le(&elf_bytes[offset + 16], virtual_address);
        write_u64_le(&elf_bytes[offset + 24], virtual_address);
        write_u64_le(&elf_bytes[offset + 32], file_size);
        write_u64_le(&elf_bytes[offset + 40], memory_size);
        write_u64_le(&elf_bytes[offset + 48], 4096);
        virtual_address += memory_size;
    }
    return 8192 + 1024;
}

static bool elf_plan_is_unchanged_on_error(
    size_t file_size,
    enum micros_vm_elf_error expected
)
{
    struct micros_vm_elf_plan plan;
    struct micros_vm_elf_plan sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    plan = sentinel;
    return (
        micros_vm_elf_plan(elf_bytes, file_size, &plan) == expected
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );
}

static bool test_elf_plan(void)
{
    struct micros_vm_elf_plan plan;
    size_t file_size = build_valid_elf(1, 1, 1);

    EXPECT_TRUE(
        micros_vm_elf_plan(elf_bytes, file_size, &plan)
            == MICROS_VM_ELF_OK
        && plan.entry == MICROS_APPLICATION_ENTRY
        && plan.segment_count == MICROS_APPLICATION_ELF_SEGMENT_COUNT
        && plan.page_count == MICROS_APPLICATION_IMAGE_PAGE_MIN
        && plan.segments[0].permissions
            == (
                MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_EXECUTE
            )
        && plan.segments[1].permissions
            == (
                MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
            )
        && plan.segments[2].permissions
            == (
                MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
            )
        && plan.pages[0].virtual_address == MICROS_APPLICATION_ENTRY
        && plan.pages[0].file_offset == 0
        && plan.pages[0].file_size == 4096
        && plan.pages[2].file_offset == 8192
        && plan.pages[2].file_size == 1024
        && bytes_are_zero(
            &plan.pages[plan.page_count],
            sizeof(plan.pages)
                - plan.page_count * sizeof(plan.pages[0])
        )
    );

    file_size = build_valid_elf(1, 1, 1);
    write_u32_le(&elf_bytes[48], 1);
    write_u64_le(&elf_bytes[64 + 112 + 32], 0);
    EXPECT_TRUE(
        micros_vm_elf_plan(elf_bytes, file_size, &plan)
            == MICROS_VM_ELF_OK
        && plan.pages[2].file_offset == 0
        && plan.pages[2].file_size == 0
    );

    file_size = build_valid_elf(5, 5, 5);
    EXPECT_TRUE(
        micros_vm_elf_plan(elf_bytes, file_size, &plan)
            == MICROS_VM_ELF_OK
        && plan.page_count == MICROS_APPLICATION_IMAGE_PAGE_MAX
        && plan.pages[14].virtual_address
            == MICROS_APPLICATION_ENTRY + 14 * UINT64_C(4096)
        && plan.pages[14].file_size == 0
    );
    file_size = build_valid_elf(5, 5, 6);
    EXPECT_TRUE(
        elf_plan_is_unchanged_on_error(
            file_size,
            MICROS_VM_ELF_ERROR_TOO_BIG
        )
    );

    file_size = build_valid_elf(1, 1, 1);
    EXPECT_TRUE(
        micros_vm_elf_plan(
            elf_bytes,
            MICROS_APPLICATION_EXECUTABLE_SIZE_MAX,
            &plan
        ) == MICROS_VM_ELF_OK
    );
    EXPECT_TRUE(
        elf_plan_is_unchanged_on_error(
            MICROS_APPLICATION_EXECUTABLE_SIZE_MAX + 1,
            MICROS_VM_ELF_ERROR_TOO_BIG
        )
    );
    return true;
}

static bool test_elf_header_rejections(void)
{
    size_t file_size = build_valid_elf(1, 1, 1);
    size_t index;

    for (index = 0; index < 232; ++index) {
        EXPECT_TRUE(
            elf_plan_is_unchanged_on_error(
                index,
                MICROS_VM_ELF_ERROR_EXECUTABLE
            )
        );
    }

    build_valid_elf(1, 1, 1);
    elf_bytes[0] = 0;
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    elf_bytes[4] = 1;
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    elf_bytes[5] = 2;
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    elf_bytes[6] = 0;
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    elf_bytes[7] = 1;
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    elf_bytes[8] = 1;
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    for (index = 9; index < 16; ++index) {
        build_valid_elf(1, 1, 1);
        elf_bytes[index] = 1;
        EXPECT_TRUE(elf_plan_is_unchanged_on_error(
            file_size,
            MICROS_VM_ELF_ERROR_EXECUTABLE
        ));
    }

    build_valid_elf(1, 1, 1);
    write_u16_le(&elf_bytes[16], 3);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    write_u16_le(&elf_bytes[18], 62);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    write_u32_le(&elf_bytes[20], 0);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    write_u64_le(&elf_bytes[24], MICROS_APPLICATION_ENTRY + 4);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    write_u64_le(&elf_bytes[32], 128);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    write_u32_le(&elf_bytes[48], 2);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    write_u32_le(&elf_bytes[48], 1);
    {
        struct micros_vm_elf_plan plan;

        EXPECT_TRUE(
            micros_vm_elf_plan(elf_bytes, file_size, &plan)
                == MICROS_VM_ELF_OK
        );
    }
    build_valid_elf(1, 1, 1);
    write_u16_le(&elf_bytes[52], 63);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    write_u16_le(&elf_bytes[54], 55);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    write_u16_le(&elf_bytes[56], 4);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));

    build_valid_elf(1, 1, 1);
    write_u64_le(&elf_bytes[40], UINT64_MAX);
    write_u16_le(&elf_bytes[58], UINT16_MAX);
    write_u16_le(&elf_bytes[60], UINT16_MAX);
    write_u16_le(&elf_bytes[62], UINT16_MAX);
    {
        struct micros_vm_elf_plan plan;

        EXPECT_TRUE(
            micros_vm_elf_plan(elf_bytes, file_size, &plan)
                == MICROS_VM_ELF_OK
        );
    }
    return true;
}

static bool test_elf_segment_rejections(void)
{
    size_t file_size = build_valid_elf(1, 1, 1);
    size_t ph0 = 64;
    size_t ph1 = 64 + 56;
    size_t ph2 = 64 + 112;

    write_u32_le(&elf_bytes[ph0], 0);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    write_u32_le(&elf_bytes[ph0 + 4], 7);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    write_u64_le(&elf_bytes[ph0 + 48], 2048);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    write_u64_le(&elf_bytes[ph1 + 8], 4097);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    write_u64_le(
        &elf_bytes[ph1 + 16],
        MICROS_APPLICATION_ENTRY + 4097
    );
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    write_u64_le(
        &elf_bytes[ph1 + 24],
        MICROS_APPLICATION_ENTRY + 4097
    );
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    write_u64_le(
        &elf_bytes[ph1 + 24],
        MICROS_APPLICATION_ENTRY + 8192
    );
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    write_u64_le(&elf_bytes[ph1 + 40], 0);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    write_u64_le(&elf_bytes[ph1 + 40], 4097);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    write_u64_le(&elf_bytes[ph1 + 32], 8192);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    write_u64_le(&elf_bytes[ph2 + 32], 4096);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    write_u64_le(&elf_bytes[ph2 + 8], UINT64_MAX - 4095);
    write_u64_le(&elf_bytes[ph2 + 32], 8192);
    write_u64_le(&elf_bytes[ph2 + 40], 8192);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    write_u64_le(&elf_bytes[ph0 + 16], UINT64_MAX - 4095);
    write_u64_le(&elf_bytes[ph0 + 24], UINT64_MAX - 4095);
    write_u64_le(&elf_bytes[ph0 + 40], 8192);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    write_u64_le(
        &elf_bytes[ph0 + 16],
        MICROS_APPLICATION_ENTRY + 4096
    );
    write_u64_le(
        &elf_bytes[ph0 + 24],
        MICROS_APPLICATION_ENTRY + 4096
    );
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    write_u64_le(
        &elf_bytes[ph1 + 16],
        MICROS_APPLICATION_ENTRY + 8192
    );
    write_u64_le(
        &elf_bytes[ph1 + 24],
        MICROS_APPLICATION_ENTRY + 8192
    );
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    write_u64_le(
        &elf_bytes[ph1 + 16],
        MICROS_APPLICATION_ENTRY
    );
    write_u64_le(
        &elf_bytes[ph1 + 24],
        MICROS_APPLICATION_ENTRY
    );
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    build_valid_elf(1, 1, 1);
    write_u64_le(&elf_bytes[ph1 + 8], 0);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));

    build_valid_elf(1, 1, 1);
    write_u64_le(
        &elf_bytes[ph0 + 40],
        MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
            - MICROS_APPLICATION_ENTRY
    );
    write_u64_le(
        &elf_bytes[ph1 + 16],
        MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
    );
    write_u64_le(
        &elf_bytes[ph1 + 24],
        MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
    );
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    return true;
}

int main(void)
{
    if (
        !test_spawn_request_contract()
        || !test_spawn_request_boundaries()
        || !test_spawn_request_rejections()
        || !test_application_stack()
        || !test_application_stack_boundaries()
        || !test_elf_plan()
        || !test_elf_header_rejections()
        || !test_elf_segment_rejections()
    ) {
        return 1;
    }
    printf("spawn ABI tests passed\n");
    return 0;
}
