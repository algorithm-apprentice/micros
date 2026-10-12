#include "tests/host/spawn_abi_test_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/sv39.h"

static bool test_spawn_request_contract(struct spawn_abi_test_context *context)
{
    struct test_string path = spawn_abi_test_literal_string("/bin/app");
    struct test_string arguments[] = {
        spawn_abi_test_literal_string("app"),
        spawn_abi_test_literal_string("argument"),
    };
    struct test_string environment[] = {
        spawn_abi_test_literal_string("A=B"),
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
    size_t request_size = spawn_abi_test_build_spawn_request(context,
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
            context->request_bytes,
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
        && spawn_abi_test_bytes_are_zero(
            &request.arguments[request.argument_count],
            sizeof(request.arguments)
                - request.argument_count
                    * sizeof(request.arguments[0])
        )
        && spawn_abi_test_bytes_are_zero(
            &request.environment[request.environment_count],
            sizeof(request.environment)
                - request.environment_count
                    * sizeof(request.environment[0])
        )
        && spawn_abi_test_bytes_are_zero(
            &request.descriptor_actions[request.descriptor_action_count],
            sizeof(request.descriptor_actions)
                - request.descriptor_action_count
                    * sizeof(request.descriptor_actions[0])
        )
    );
    return true;
}

static bool test_spawn_request_boundaries(struct spawn_abi_test_context *context)
{
    struct test_string path;
    struct test_string arguments[MICROS_SPAWN_ARGUMENT_CAPACITY];
    struct test_string environment[MICROS_SPAWN_ENVIRONMENT_CAPACITY];
    struct micros_spawn_descriptor_action actions[
        MICROS_SPAWN_DESCRIPTOR_ACTION_CAPACITY
    ];
    struct micros_spawn_request request;
    size_t request_size;
    size_t index;

    path = spawn_abi_test_literal_string("/");
    arguments[0] = spawn_abi_test_literal_string("x");
    request_size = spawn_abi_test_build_spawn_request(context,
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
            context->request_bytes,
            request_size,
            &request
        ) == MICROS_SPAWN_REQUEST_OK
        && request.argument_count == 1
        && request.environment_count == 0
        && request.descriptor_action_count == 0
    );

    spawn_abi_test_fill_plain_string(context->boundary_path, sizeof(context->boundary_path), 'p');
    path.bytes = context->boundary_path;
    path.size = sizeof(context->boundary_path);
    for (index = 0; index < MICROS_SPAWN_ARGUMENT_CAPACITY; ++index) {
        spawn_abi_test_fill_plain_string(
            context->boundary_arguments[index],
            sizeof(context->boundary_arguments[index]),
            (uint8_t)('a' + index)
        );
        arguments[index].bytes = context->boundary_arguments[index];
        arguments[index].size = sizeof(context->boundary_arguments[index]);
    }
    for (index = 0; index < MICROS_SPAWN_ENVIRONMENT_CAPACITY; ++index) {
        uint16_t size = index < 5 ? 256 : (index == 5 ? 248 : 4);

        spawn_abi_test_fill_environment_string(context->boundary_environment[index], size);
        environment[index].bytes = context->boundary_environment[index];
        environment[index].size = size;
        actions[index].source = 0;
        actions[index].destination = (uint16_t)index;
        actions[index].operation = MICROS_SPAWN_DESCRIPTOR_DUPLICATE;
    }
    request_size = spawn_abi_test_build_spawn_request(context,
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
            context->request_bytes,
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

static bool decode_rejects_current_request(
    struct spawn_abi_test_context *context,size_t request_size)
{
    struct micros_spawn_request request;
    struct micros_spawn_request sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    request = sentinel;
    return (
        micros_spawn_request_decode(
            context->request_bytes,
            request_size,
            &request
        ) == MICROS_SPAWN_REQUEST_ERROR_MALFORMED
        && memcmp(&request, &sentinel, sizeof(request)) == 0
    );
}

static bool test_spawn_request_rejections(struct spawn_abi_test_context *context)
{
    struct test_string path = spawn_abi_test_literal_string("/x");
    struct test_string arguments[] = {spawn_abi_test_literal_string("x")};
    struct test_string environment[] = {spawn_abi_test_literal_string("A=")};
    struct micros_spawn_descriptor_action actions[2] = {
        {
            .source = 0,
            .destination = 1,
            .operation = MICROS_SPAWN_DESCRIPTOR_DUPLICATE,
        },
    };
    size_t size = spawn_abi_test_build_spawn_request(context,
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
        EXPECT_TRUE(decode_rejects_current_request(context, index));
    }
    EXPECT_TRUE(decode_rejects_current_request(context,
        MICROS_SPAWN_REQUEST_SIZE_MAX + 1
    ));
    for (index = 0; index < 32; ++index) {
        spawn_abi_test_build_spawn_request(context,
            &path,
            arguments,
            1,
            environment,
            1,
            actions,
            1
        );
        spawn_abi_test_write_u32_le(&context->request_bytes[12], UINT32_C(1) << index);
        EXPECT_TRUE(decode_rejects_current_request(context, size));
    }
    for (offset = 30; offset < 32; ++offset) {
        spawn_abi_test_build_spawn_request(context,
            &path,
            arguments,
            1,
            environment,
            1,
            actions,
            1
        );
        context->request_bytes[offset] = 1;
        EXPECT_TRUE(decode_rejects_current_request(context, size));
    }
    for (
        offset = MICROS_SPAWN_HEADER_RESERVED_OFFSET;
        offset < MICROS_SPAWN_REQUEST_HEADER_SIZE;
        ++offset
    ) {
        spawn_abi_test_build_spawn_request(context,
            &path,
            arguments,
            1,
            environment,
            1,
            actions,
            1
        );
        context->request_bytes[offset] = 1;
        EXPECT_TRUE(decode_rejects_current_request(context, size));
    }
    for (index = 0; index < 4; ++index) {
        spawn_abi_test_build_spawn_request(context,
            &path,
            arguments,
            1,
            environment,
            1,
            actions,
            1
        );
        context->request_bytes[
            MICROS_SPAWN_PATH_REFERENCE_OFFSET + 4 + index
        ] = 1;
        EXPECT_TRUE(decode_rejects_current_request(context, size));
        spawn_abi_test_build_spawn_request(context,
            &path,
            arguments,
            1,
            environment,
            1,
            actions,
            1
        );
        context->request_bytes[
            MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET + 4 + index
        ] = 1;
        EXPECT_TRUE(decode_rejects_current_request(context, size));
        spawn_abi_test_build_spawn_request(context,
            &path,
            arguments,
            1,
            environment,
            1,
            actions,
            1
        );
        context->request_bytes[
            MICROS_SPAWN_ENVIRONMENT_REFERENCES_OFFSET + 4 + index
        ] = 1;
        EXPECT_TRUE(decode_rejects_current_request(context, size));
    }
    for (
        offset = MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET
            + MICROS_SPAWN_STRING_REFERENCE_SIZE;
        offset < MICROS_SPAWN_ENVIRONMENT_REFERENCES_OFFSET;
        ++offset
    ) {
        spawn_abi_test_build_spawn_request(context,
            &path,
            arguments,
            1,
            environment,
            1,
            actions,
            1
        );
        context->request_bytes[offset] = 1;
        EXPECT_TRUE(decode_rejects_current_request(context, size));
    }
    for (
        offset = MICROS_SPAWN_ENVIRONMENT_REFERENCES_OFFSET
            + MICROS_SPAWN_STRING_REFERENCE_SIZE;
        offset < MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET;
        ++offset
    ) {
        spawn_abi_test_build_spawn_request(context,
            &path,
            arguments,
            1,
            environment,
            1,
            actions,
            1
        );
        context->request_bytes[offset] = 1;
        EXPECT_TRUE(decode_rejects_current_request(context, size));
    }
    for (
        offset = MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET
            + MICROS_SPAWN_DESCRIPTOR_ACTION_SIZE;
        offset < MICROS_SPAWN_HEADER_RESERVED_OFFSET;
        ++offset
    ) {
        spawn_abi_test_build_spawn_request(context,
            &path,
            arguments,
            1,
            environment,
            1,
            actions,
            1
        );
        context->request_bytes[offset] = 1;
        EXPECT_TRUE(decode_rejects_current_request(context, size));
    }

    spawn_abi_test_build_spawn_request(context,
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        1
    );
    spawn_abi_test_write_u32_le(&context->request_bytes[0], 2);
    EXPECT_TRUE(decode_rejects_current_request(context, size));
    spawn_abi_test_write_u32_le(&context->request_bytes[0], MICROS_SPAWN_REQUEST_VERSION);
    spawn_abi_test_write_u32_le(&context->request_bytes[4], 255);
    EXPECT_TRUE(decode_rejects_current_request(context, size));
    spawn_abi_test_write_u32_le(&context->request_bytes[4], MICROS_SPAWN_REQUEST_HEADER_SIZE);
    spawn_abi_test_write_u32_le(&context->request_bytes[8], (uint32_t)(size - 1));
    EXPECT_TRUE(decode_rejects_current_request(context, size));
    spawn_abi_test_write_u32_le(&context->request_bytes[8], (uint32_t)size);
    spawn_abi_test_write_u32_le(&context->request_bytes[12], 1);
    EXPECT_TRUE(decode_rejects_current_request(context, size));

    spawn_abi_test_build_spawn_request(context,
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        1
    );
    context->request_bytes[30] = 1;
    EXPECT_TRUE(decode_rejects_current_request(context, size));
    context->request_bytes[30] = 0;
    context->request_bytes[MICROS_SPAWN_HEADER_RESERVED_OFFSET] = 1;
    EXPECT_TRUE(decode_rejects_current_request(context, size));

    spawn_abi_test_build_spawn_request(context,
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        1
    );
    spawn_abi_test_write_u16_le(&context->request_bytes[MICROS_SPAWN_ARGUMENT_COUNT_OFFSET], 0);
    EXPECT_TRUE(decode_rejects_current_request(context, size));
    spawn_abi_test_write_u16_le(&context->request_bytes[MICROS_SPAWN_ARGUMENT_COUNT_OFFSET], 9);
    EXPECT_TRUE(decode_rejects_current_request(context, size));
    spawn_abi_test_write_u16_le(&context->request_bytes[MICROS_SPAWN_ARGUMENT_COUNT_OFFSET], 1);
    spawn_abi_test_write_u16_le(&context->request_bytes[MICROS_SPAWN_ENVIRONMENT_COUNT_OFFSET], 9);
    EXPECT_TRUE(decode_rejects_current_request(context, size));
    spawn_abi_test_write_u16_le(&context->request_bytes[MICROS_SPAWN_ENVIRONMENT_COUNT_OFFSET], 1);
    spawn_abi_test_write_u16_le(
        &context->request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTION_COUNT_OFFSET],
        9
    );
    EXPECT_TRUE(decode_rejects_current_request(context, size));

    spawn_abi_test_build_spawn_request(context,
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        1
    );
    spawn_abi_test_write_u16_le(
        &context->request_bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET],
        MICROS_SPAWN_REQUEST_HEADER_SIZE + 1
    );
    EXPECT_TRUE(decode_rejects_current_request(context, size));
    spawn_abi_test_build_spawn_request(context,
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        1
    );
    context->request_bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET + 4] = 1;
    EXPECT_TRUE(decode_rejects_current_request(context, size));
    context->request_bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET + 4] = 0;
    context->request_bytes[
        MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET
            + MICROS_SPAWN_STRING_REFERENCE_SIZE
    ] = 1;
    EXPECT_TRUE(decode_rejects_current_request(context, size));
    context->request_bytes[
        MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET
            + MICROS_SPAWN_STRING_REFERENCE_SIZE
    ] = 0;
    context->request_bytes[
        MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET
            + MICROS_SPAWN_DESCRIPTOR_ACTION_SIZE
    ] = 1;
    EXPECT_TRUE(decode_rejects_current_request(context, size));

    spawn_abi_test_build_spawn_request(context,
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        1
    );
    spawn_abi_test_write_u16_le(
        &context->request_bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET + 2],
        0
    );
    EXPECT_TRUE(decode_rejects_current_request(context, size));
    spawn_abi_test_write_u16_le(
        &context->request_bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET + 2],
        257
    );
    EXPECT_TRUE(decode_rejects_current_request(context, size));

    spawn_abi_test_build_spawn_request(context,
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        1
    );
    context->request_bytes[MICROS_SPAWN_REQUEST_HEADER_SIZE] = 0;
    EXPECT_TRUE(decode_rejects_current_request(context, size));
    spawn_abi_test_build_spawn_request(context,
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        1
    );
    context->request_bytes[
        MICROS_SPAWN_REQUEST_HEADER_SIZE + path.size - 1
    ] = 'x';
    EXPECT_TRUE(decode_rejects_current_request(context, size));
    spawn_abi_test_build_spawn_request(context,
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        1
    );
    context->request_bytes[
        MICROS_SPAWN_REQUEST_HEADER_SIZE + path.size
    ] = 0;
    EXPECT_TRUE(decode_rejects_current_request(context, size));

    spawn_abi_test_build_spawn_request(context,
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        1
    );
    context->request_bytes[
        MICROS_SPAWN_REQUEST_HEADER_SIZE
            + path.size
            + arguments[0].size
            + 1
    ] = 'B';
    EXPECT_TRUE(decode_rejects_current_request(context, size));
    spawn_abi_test_build_spawn_request(context,
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        1
    );
    context->request_bytes[
        MICROS_SPAWN_REQUEST_HEADER_SIZE
            + path.size
            + arguments[0].size
    ] = '=';
    EXPECT_TRUE(decode_rejects_current_request(context, size));

    spawn_abi_test_build_spawn_request(context,
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        1
    );
    spawn_abi_test_write_u16_le(
        &context->request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET],
        MICROS_SPAWN_DESCRIPTOR_CAPACITY
    );
    EXPECT_TRUE(decode_rejects_current_request(context, size));
    spawn_abi_test_write_u16_le(
        &context->request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET],
        0
    );
    spawn_abi_test_write_u16_le(
        &context->request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET + 2],
        MICROS_SPAWN_DESCRIPTOR_CAPACITY
    );
    EXPECT_TRUE(decode_rejects_current_request(context, size));
    spawn_abi_test_write_u16_le(
        &context->request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET + 2],
        1
    );
    spawn_abi_test_write_u32_le(
        &context->request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET + 4],
        2
    );
    EXPECT_TRUE(decode_rejects_current_request(context, size));

    actions[1] = actions[0];
    actions[1].source = 2;
    size = spawn_abi_test_build_spawn_request(context,
        &path,
        arguments,
        1,
        environment,
        1,
        actions,
        2
    );
    EXPECT_TRUE(decode_rejects_current_request(context, size));
    return true;
}

static bool test_application_stack(struct spawn_abi_test_context *context)
{
    struct test_string path = spawn_abi_test_literal_string("/bin/app");
    struct test_string arguments[] = {
        spawn_abi_test_literal_string("app"),
        spawn_abi_test_literal_string("one"),
    };
    struct test_string environment[] = {
        spawn_abi_test_literal_string("A=B"),
    };
    struct micros_application_stack_values values = {
        .pm_endpoint = UINT32_C(0x00001004),
        .vfs_endpoint = UINT32_C(0x00001006),
        .pid = UINT64_C(0x0102030405060708),
    };
    struct micros_application_stack_plan plan;
    uint8_t stack_page[MICROS_APPLICATION_STACK_PAGE_SIZE];
    size_t request_size = spawn_abi_test_build_spawn_request(context,
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
            context->request_bytes,
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
        && spawn_abi_test_read_u64_le(&stack_page[plan.argc_offset]) == 2
    );

    argv0 = spawn_abi_test_read_u64_le(&stack_page[plan.arguments_offset]);
    argv1 = spawn_abi_test_read_u64_le(&stack_page[plan.arguments_offset + 8]);
    env0 = spawn_abi_test_read_u64_le(&stack_page[plan.environment_offset]);
    EXPECT_TRUE(
        argv0 >= MICROS_APPLICATION_STACK_BASE
        && argv1 > argv0
        && env0 > argv1
        && spawn_abi_test_read_u64_le(&stack_page[plan.arguments_offset + 16]) == 0
        && spawn_abi_test_read_u64_le(&stack_page[plan.environment_offset + 8]) == 0
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
        spawn_abi_test_read_u64_le(&stack_page[plan.auxiliary_offset])
            == MICROS_AT_PM_ENDPOINT
        && spawn_abi_test_read_u64_le(&stack_page[plan.auxiliary_offset + 8])
            == values.pm_endpoint
        && spawn_abi_test_read_u64_le(&stack_page[plan.auxiliary_offset + 16])
            == MICROS_AT_VFS_ENDPOINT
        && spawn_abi_test_read_u64_le(&stack_page[plan.auxiliary_offset + 24])
            == values.vfs_endpoint
        && spawn_abi_test_read_u64_le(&stack_page[plan.auxiliary_offset + 32])
            == MICROS_AT_PID
        && spawn_abi_test_read_u64_le(&stack_page[plan.auxiliary_offset + 40])
            == values.pid
        && spawn_abi_test_read_u64_le(&stack_page[plan.auxiliary_offset + 48])
            == MICROS_AT_PAGE_SIZE
        && spawn_abi_test_read_u64_le(&stack_page[plan.auxiliary_offset + 56])
            == MICROS_APPLICATION_STACK_PAGE_SIZE
        && spawn_abi_test_read_u64_le(&stack_page[plan.auxiliary_offset + 64])
            == MICROS_AT_NULL
        && spawn_abi_test_read_u64_le(&stack_page[plan.auxiliary_offset + 72]) == 0
    );
    EXPECT_TRUE(
        spawn_abi_test_bytes_are_zero(stack_page, plan.argc_offset)
        && spawn_abi_test_bytes_are_zero(
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
        && spawn_abi_test_bytes_are_zero(
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

    path = spawn_abi_test_literal_string("/not-copied");
    arguments[0] = spawn_abi_test_literal_string("x");
    request_size = spawn_abi_test_build_spawn_request(context,
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
            context->request_bytes,
            request_size,
            &values,
            stack_page,
            sizeof(stack_page),
            &plan
        ) == MICROS_APPLICATION_STACK_OK
        && spawn_abi_test_read_u64_le(&stack_page[plan.argc_offset]) == 1
        && spawn_abi_test_read_u64_le(&stack_page[plan.environment_offset]) == 0
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

static bool test_application_stack_boundaries(struct spawn_abi_test_context *context)
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

    EXPECT_TRUE(spawn_abi_test_build_occupancy_request(context, false, &request_size));
    EXPECT_TRUE(
        micros_application_stack_plan(
            context->request_bytes,
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
            context->request_bytes,
            request_size,
            &values,
            stack_page,
            sizeof(stack_page),
            &plan
        ) == MICROS_APPLICATION_STACK_OK
        && spawn_abi_test_bytes_are_zero(
            stack_page,
            MICROS_APPLICATION_STACK_RESERVE_MIN
        )
    );

    EXPECT_TRUE(spawn_abi_test_build_occupancy_request(context, true, &request_size));
    memset(&sentinel, 0xa5, sizeof(sentinel));
    plan = sentinel;
    EXPECT_TRUE(
        micros_application_stack_plan(
            context->request_bytes,
            request_size,
            &plan
        ) == MICROS_APPLICATION_STACK_ERROR_TOO_BIG
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );
    memset(stack_page, 0xa5, sizeof(stack_page));
    memcpy(page_sentinel, stack_page, sizeof(stack_page));
    EXPECT_TRUE(
        micros_application_stack_build(
            context->request_bytes,
            request_size,
            &values,
            stack_page,
            sizeof(stack_page),
            &plan
        ) == MICROS_APPLICATION_STACK_ERROR_TOO_BIG
        && memcmp(stack_page, page_sentinel, sizeof(stack_page)) == 0
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );

    EXPECT_TRUE(spawn_abi_test_build_occupancy_request(context, false, &request_size));
    memset(stack_page, 0xa5, sizeof(stack_page));
    memcpy(page_sentinel, stack_page, sizeof(stack_page));
    values.pid = 0;
    EXPECT_TRUE(
        micros_application_stack_build(
            context->request_bytes,
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

static bool elf_plan_is_unchanged_on_error(
    struct spawn_abi_test_context *context,
    size_t file_size,
    enum micros_vm_elf_error expected
)
{
    struct micros_vm_elf_plan plan;
    struct micros_vm_elf_plan sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    plan = sentinel;
    return (
        micros_vm_elf_plan(context->elf_bytes, file_size, &plan) == expected
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );
}

static bool test_elf_plan(struct spawn_abi_test_context *context)
{
    struct micros_vm_elf_plan plan;
    size_t file_size = spawn_abi_test_build_valid_elf(context, 1, 1, 1);

    EXPECT_TRUE(
        micros_vm_elf_plan(context->elf_bytes, file_size, &plan)
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
        && spawn_abi_test_bytes_are_zero(
            &plan.pages[plan.page_count],
            sizeof(plan.pages)
                - plan.page_count * sizeof(plan.pages[0])
        )
    );

    file_size = spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u32_le(&context->elf_bytes[48], 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[64 + 112 + 32], 0);
    EXPECT_TRUE(
        micros_vm_elf_plan(context->elf_bytes, file_size, &plan)
            == MICROS_VM_ELF_OK
        && plan.pages[2].file_offset == 0
        && plan.pages[2].file_size == 0
    );

    file_size = spawn_abi_test_build_valid_elf(context, 5, 5, 5);
    EXPECT_TRUE(
        micros_vm_elf_plan(context->elf_bytes, file_size, &plan)
            == MICROS_VM_ELF_OK
        && plan.page_count == MICROS_APPLICATION_IMAGE_PAGE_MAX
        && plan.pages[14].virtual_address
            == MICROS_APPLICATION_ENTRY + 14 * UINT64_C(4096)
        && plan.pages[14].file_size == 0
    );
    file_size = spawn_abi_test_build_valid_elf(context, 5, 5, 6);
    EXPECT_TRUE(
        elf_plan_is_unchanged_on_error(context,
            file_size,
            MICROS_VM_ELF_ERROR_TOO_BIG
        )
    );

    file_size = spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    EXPECT_TRUE(
        micros_vm_elf_plan(
            context->elf_bytes,
            MICROS_APPLICATION_EXECUTABLE_SIZE_MAX,
            &plan
        ) == MICROS_VM_ELF_OK
    );
    EXPECT_TRUE(
        elf_plan_is_unchanged_on_error(context,
            MICROS_APPLICATION_EXECUTABLE_SIZE_MAX + 1,
            MICROS_VM_ELF_ERROR_TOO_BIG
        )
    );
    return true;
}

static bool test_elf_header_rejections(struct spawn_abi_test_context *context)
{
    size_t file_size = spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    size_t index;

    for (index = 0; index < 232; ++index) {
        EXPECT_TRUE(
            elf_plan_is_unchanged_on_error(context,
                index,
                MICROS_VM_ELF_ERROR_EXECUTABLE
            )
        );
    }

    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    context->elf_bytes[0] = 0;
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    context->elf_bytes[4] = 1;
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    context->elf_bytes[5] = 2;
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    context->elf_bytes[6] = 0;
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    context->elf_bytes[7] = 1;
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    context->elf_bytes[8] = 1;
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    for (index = 9; index < 16; ++index) {
        spawn_abi_test_build_valid_elf(context, 1, 1, 1);
        context->elf_bytes[index] = 1;
        EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
            file_size,
            MICROS_VM_ELF_ERROR_EXECUTABLE
        ));
    }

    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u16_le(&context->elf_bytes[16], 3);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u16_le(&context->elf_bytes[18], 62);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u32_le(&context->elf_bytes[20], 0);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[24], MICROS_APPLICATION_ENTRY + 4);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[32], 128);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u32_le(&context->elf_bytes[48], 2);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u32_le(&context->elf_bytes[48], 1);
    {
        struct micros_vm_elf_plan plan;

        EXPECT_TRUE(
            micros_vm_elf_plan(context->elf_bytes, file_size, &plan)
                == MICROS_VM_ELF_OK
        );
    }
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u16_le(&context->elf_bytes[52], 63);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u16_le(&context->elf_bytes[54], 55);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u16_le(&context->elf_bytes[56], 4);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));

    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[40], UINT64_MAX);
    spawn_abi_test_write_u16_le(&context->elf_bytes[58], UINT16_MAX);
    spawn_abi_test_write_u16_le(&context->elf_bytes[60], UINT16_MAX);
    spawn_abi_test_write_u16_le(&context->elf_bytes[62], UINT16_MAX);
    {
        struct micros_vm_elf_plan plan;

        EXPECT_TRUE(
            micros_vm_elf_plan(context->elf_bytes, file_size, &plan)
                == MICROS_VM_ELF_OK
        );
    }
    return true;
}

static bool test_elf_segment_rejections(struct spawn_abi_test_context *context)
{
    size_t file_size = spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    size_t ph0 = 64;
    size_t ph1 = 64 + 56;
    size_t ph2 = 64 + 112;

    spawn_abi_test_write_u32_le(&context->elf_bytes[ph0], 0);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u32_le(&context->elf_bytes[ph0 + 4], 7);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph0 + 48], 2048);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph1 + 8], 4097);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(
        &context->elf_bytes[ph1 + 16],
        MICROS_APPLICATION_ENTRY + 4097
    );
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(
        &context->elf_bytes[ph1 + 24],
        MICROS_APPLICATION_ENTRY + 4097
    );
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(
        &context->elf_bytes[ph1 + 24],
        MICROS_APPLICATION_ENTRY + 8192
    );
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph1 + 40], 0);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph1 + 40], 4097);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph1 + 32], 8192);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph2 + 32], 4096);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph2 + 8], UINT64_MAX - 4095);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph2 + 32], 8192);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph2 + 40], 8192);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph0 + 16], UINT64_MAX - 4095);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph0 + 24], UINT64_MAX - 4095);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph0 + 40], 8192);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(
        &context->elf_bytes[ph0 + 16],
        MICROS_APPLICATION_ENTRY + 4096
    );
    spawn_abi_test_write_u64_le(
        &context->elf_bytes[ph0 + 24],
        MICROS_APPLICATION_ENTRY + 4096
    );
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(
        &context->elf_bytes[ph1 + 16],
        MICROS_APPLICATION_ENTRY + 8192
    );
    spawn_abi_test_write_u64_le(
        &context->elf_bytes[ph1 + 24],
        MICROS_APPLICATION_ENTRY + 8192
    );
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(
        &context->elf_bytes[ph1 + 16],
        MICROS_APPLICATION_ENTRY
    );
    spawn_abi_test_write_u64_le(
        &context->elf_bytes[ph1 + 24],
        MICROS_APPLICATION_ENTRY
    );
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph1 + 8], 0);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));

    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(
        &context->elf_bytes[ph0 + 40],
        MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
            - MICROS_APPLICATION_ENTRY
    );
    spawn_abi_test_write_u64_le(
        &context->elf_bytes[ph1 + 16],
        MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
    );
    spawn_abi_test_write_u64_le(
        &context->elf_bytes[ph1 + 24],
        MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
    );
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    return true;
}

int main(void)
{
    struct spawn_abi_test_context *context = spawn_abi_test_context_get();
    if (
        !test_spawn_request_contract(context)
        || !test_spawn_request_boundaries(context)
        || !test_spawn_request_rejections(context)
        || !test_application_stack(context)
        || !test_application_stack_boundaries(context)
        || !test_elf_plan(context)
        || !test_elf_header_rejections(context)
        || !test_elf_segment_rejections(context)
    ) {
        return 1;
    }
    printf("spawn ABI tests passed\n");
    return 0;
}
