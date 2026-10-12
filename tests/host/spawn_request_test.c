#include "tests/host/spawn_abi_test_internal.h"

bool test_spawn_request_contract(struct spawn_abi_test_context *context)
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

bool test_spawn_request_boundaries(struct spawn_abi_test_context *context)
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
    struct spawn_abi_test_context *context, size_t request_size)
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

struct request_rejection_case {
    struct test_string path;
    struct test_string arguments[1];
    struct test_string environment[1];
    struct micros_spawn_descriptor_action actions[2];
    size_t size;
};


static bool reject_request_size_and_flags(struct spawn_abi_test_context *context, struct request_rejection_case *scenario)
{
    size_t index;

    for (index = 0; index < MICROS_SPAWN_REQUEST_HEADER_SIZE; ++index) {
        EXPECT_TRUE(decode_rejects_current_request(context, index));
    }
    EXPECT_TRUE(decode_rejects_current_request(context,
        MICROS_SPAWN_REQUEST_SIZE_MAX + 1
    ));
    for (index = 0; index < 32; ++index) {
        spawn_abi_test_build_spawn_request(context,
            &scenario->path,
            scenario->arguments,
            1,
            scenario->environment,
            1,
            scenario->actions,
            1
        );
        spawn_abi_test_write_u32_le(&context->request_bytes[12], UINT32_C(1) << index);
        EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    }
    return true;
}

static bool reject_request_reserved_header(struct spawn_abi_test_context *context, struct request_rejection_case *scenario)
{
    size_t offset;

    for (offset = 30; offset < 32; ++offset) {
        spawn_abi_test_build_spawn_request(context,
            &scenario->path,
            scenario->arguments,
            1,
            scenario->environment,
            1,
            scenario->actions,
            1
        );
        context->request_bytes[offset] = 1;
        EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    }
    for (
        offset = MICROS_SPAWN_HEADER_RESERVED_OFFSET;
        offset < MICROS_SPAWN_REQUEST_HEADER_SIZE;
        ++offset
    ) {
        spawn_abi_test_build_spawn_request(context,
            &scenario->path,
            scenario->arguments,
            1,
            scenario->environment,
            1,
            scenario->actions,
            1
        );
        context->request_bytes[offset] = 1;
        EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    }
    return true;
}

static bool reject_reference_reserved_and_unused_tails(struct spawn_abi_test_context *context, struct request_rejection_case *scenario)
{
    size_t index;
    size_t offset;

    for (index = 0; index < 4; ++index) {
        spawn_abi_test_build_spawn_request(context,
            &scenario->path,
            scenario->arguments,
            1,
            scenario->environment,
            1,
            scenario->actions,
            1
        );
        context->request_bytes[
            MICROS_SPAWN_PATH_REFERENCE_OFFSET + 4 + index
        ] = 1;
        EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
        spawn_abi_test_build_spawn_request(context,
            &scenario->path,
            scenario->arguments,
            1,
            scenario->environment,
            1,
            scenario->actions,
            1
        );
        context->request_bytes[
            MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET + 4 + index
        ] = 1;
        EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
        spawn_abi_test_build_spawn_request(context,
            &scenario->path,
            scenario->arguments,
            1,
            scenario->environment,
            1,
            scenario->actions,
            1
        );
        context->request_bytes[
            MICROS_SPAWN_ENVIRONMENT_REFERENCES_OFFSET + 4 + index
        ] = 1;
        EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    }
    for (
        offset = MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET
            + MICROS_SPAWN_STRING_REFERENCE_SIZE;
        offset < MICROS_SPAWN_ENVIRONMENT_REFERENCES_OFFSET;
        ++offset
    ) {
        spawn_abi_test_build_spawn_request(context,
            &scenario->path,
            scenario->arguments,
            1,
            scenario->environment,
            1,
            scenario->actions,
            1
        );
        context->request_bytes[offset] = 1;
        EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    }
    for (
        offset = MICROS_SPAWN_ENVIRONMENT_REFERENCES_OFFSET
            + MICROS_SPAWN_STRING_REFERENCE_SIZE;
        offset < MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET;
        ++offset
    ) {
        spawn_abi_test_build_spawn_request(context,
            &scenario->path,
            scenario->arguments,
            1,
            scenario->environment,
            1,
            scenario->actions,
            1
        );
        context->request_bytes[offset] = 1;
        EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    }
    for (
        offset = MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET
            + MICROS_SPAWN_DESCRIPTOR_ACTION_SIZE;
        offset < MICROS_SPAWN_HEADER_RESERVED_OFFSET;
        ++offset
    ) {
        spawn_abi_test_build_spawn_request(context,
            &scenario->path,
            scenario->arguments,
            1,
            scenario->environment,
            1,
            scenario->actions,
            1
        );
        context->request_bytes[offset] = 1;
        EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    }
    return true;
}

static bool reject_request_header_values_and_counts(struct spawn_abi_test_context *context, struct request_rejection_case *scenario)
{
    spawn_abi_test_build_spawn_request(context,
        &scenario->path,
        scenario->arguments,
        1,
        scenario->environment,
        1,
        scenario->actions,
        1
    );
    spawn_abi_test_write_u32_le(&context->request_bytes[0], 2);
    EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    spawn_abi_test_write_u32_le(&context->request_bytes[0], MICROS_SPAWN_REQUEST_VERSION);
    spawn_abi_test_write_u32_le(&context->request_bytes[4], 255);
    EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    spawn_abi_test_write_u32_le(&context->request_bytes[4], MICROS_SPAWN_REQUEST_HEADER_SIZE);
    spawn_abi_test_write_u32_le(&context->request_bytes[8], (uint32_t)(scenario->size - 1));
    EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    spawn_abi_test_write_u32_le(&context->request_bytes[8], (uint32_t)scenario->size);
    spawn_abi_test_write_u32_le(&context->request_bytes[12], 1);
    EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));

    spawn_abi_test_build_spawn_request(context,
        &scenario->path,
        scenario->arguments,
        1,
        scenario->environment,
        1,
        scenario->actions,
        1
    );
    context->request_bytes[30] = 1;
    EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    context->request_bytes[30] = 0;
    context->request_bytes[MICROS_SPAWN_HEADER_RESERVED_OFFSET] = 1;
    EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));

    spawn_abi_test_build_spawn_request(context,
        &scenario->path,
        scenario->arguments,
        1,
        scenario->environment,
        1,
        scenario->actions,
        1
    );
    spawn_abi_test_write_u16_le(&context->request_bytes[MICROS_SPAWN_ARGUMENT_COUNT_OFFSET], 0);
    EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    spawn_abi_test_write_u16_le(&context->request_bytes[MICROS_SPAWN_ARGUMENT_COUNT_OFFSET], 9);
    EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    spawn_abi_test_write_u16_le(&context->request_bytes[MICROS_SPAWN_ARGUMENT_COUNT_OFFSET], 1);
    spawn_abi_test_write_u16_le(&context->request_bytes[MICROS_SPAWN_ENVIRONMENT_COUNT_OFFSET], 9);
    EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    spawn_abi_test_write_u16_le(&context->request_bytes[MICROS_SPAWN_ENVIRONMENT_COUNT_OFFSET], 1);
    spawn_abi_test_write_u16_le(
        &context->request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTION_COUNT_OFFSET],
        9
    );
    EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    return true;
}

static bool reject_reference_offsets_and_tails(struct spawn_abi_test_context *context, struct request_rejection_case *scenario)
{
    spawn_abi_test_build_spawn_request(context,
        &scenario->path,
        scenario->arguments,
        1,
        scenario->environment,
        1,
        scenario->actions,
        1
    );
    spawn_abi_test_write_u16_le(
        &context->request_bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET],
        MICROS_SPAWN_REQUEST_HEADER_SIZE + 1
    );
    EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    spawn_abi_test_build_spawn_request(context,
        &scenario->path,
        scenario->arguments,
        1,
        scenario->environment,
        1,
        scenario->actions,
        1
    );
    context->request_bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET + 4] = 1;
    EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    context->request_bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET + 4] = 0;
    context->request_bytes[
        MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET
            + MICROS_SPAWN_STRING_REFERENCE_SIZE
    ] = 1;
    EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    context->request_bytes[
        MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET
            + MICROS_SPAWN_STRING_REFERENCE_SIZE
    ] = 0;
    context->request_bytes[
        MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET
            + MICROS_SPAWN_DESCRIPTOR_ACTION_SIZE
    ] = 1;
    EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    return true;
}

static bool reject_request_string_shapes(struct spawn_abi_test_context *context, struct request_rejection_case *scenario)
{
    spawn_abi_test_build_spawn_request(context,
        &scenario->path,
        scenario->arguments,
        1,
        scenario->environment,
        1,
        scenario->actions,
        1
    );
    spawn_abi_test_write_u16_le(
        &context->request_bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET + 2],
        0
    );
    EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    spawn_abi_test_write_u16_le(
        &context->request_bytes[MICROS_SPAWN_PATH_REFERENCE_OFFSET + 2],
        257
    );
    EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));

    spawn_abi_test_build_spawn_request(context,
        &scenario->path,
        scenario->arguments,
        1,
        scenario->environment,
        1,
        scenario->actions,
        1
    );
    context->request_bytes[MICROS_SPAWN_REQUEST_HEADER_SIZE] = 0;
    EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    spawn_abi_test_build_spawn_request(context,
        &scenario->path,
        scenario->arguments,
        1,
        scenario->environment,
        1,
        scenario->actions,
        1
    );
    context->request_bytes[
        MICROS_SPAWN_REQUEST_HEADER_SIZE + scenario->path.size - 1
    ] = 'x';
    EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    spawn_abi_test_build_spawn_request(context,
        &scenario->path,
        scenario->arguments,
        1,
        scenario->environment,
        1,
        scenario->actions,
        1
    );
    context->request_bytes[
        MICROS_SPAWN_REQUEST_HEADER_SIZE + scenario->path.size
    ] = 0;
    EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));

    spawn_abi_test_build_spawn_request(context,
        &scenario->path,
        scenario->arguments,
        1,
        scenario->environment,
        1,
        scenario->actions,
        1
    );
    context->request_bytes[
        MICROS_SPAWN_REQUEST_HEADER_SIZE
            + scenario->path.size
            + scenario->arguments[0].size
            + 1
    ] = 'B';
    EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    spawn_abi_test_build_spawn_request(context,
        &scenario->path,
        scenario->arguments,
        1,
        scenario->environment,
        1,
        scenario->actions,
        1
    );
    context->request_bytes[
        MICROS_SPAWN_REQUEST_HEADER_SIZE
            + scenario->path.size
            + scenario->arguments[0].size
    ] = '=';
    EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    return true;
}

static bool reject_descriptor_shapes_and_duplicates(struct spawn_abi_test_context *context, struct request_rejection_case *scenario)
{
    spawn_abi_test_build_spawn_request(context,
        &scenario->path,
        scenario->arguments,
        1,
        scenario->environment,
        1,
        scenario->actions,
        1
    );
    spawn_abi_test_write_u16_le(
        &context->request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET],
        MICROS_SPAWN_DESCRIPTOR_CAPACITY
    );
    EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    spawn_abi_test_write_u16_le(
        &context->request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET],
        0
    );
    spawn_abi_test_write_u16_le(
        &context->request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET + 2],
        MICROS_SPAWN_DESCRIPTOR_CAPACITY
    );
    EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    spawn_abi_test_write_u16_le(
        &context->request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET + 2],
        1
    );
    spawn_abi_test_write_u32_le(
        &context->request_bytes[MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET + 4],
        2
    );
    EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));

    scenario->actions[1] = scenario->actions[0];
    scenario->actions[1].source = 2;
    scenario->size = spawn_abi_test_build_spawn_request(context,
        &scenario->path,
        scenario->arguments,
        1,
        scenario->environment,
        1,
        scenario->actions,
        2
    );
    EXPECT_TRUE(decode_rejects_current_request(context, scenario->size));
    return true;
}

bool test_spawn_request_rejections(struct spawn_abi_test_context *context)
{
    struct request_rejection_case scenario = {
        .path = spawn_abi_test_literal_string("/x"),
        .arguments = {spawn_abi_test_literal_string("x")},
        .environment = {spawn_abi_test_literal_string("A=")},
        .actions = {{
            .source = 0, .destination = 1,
            .operation = MICROS_SPAWN_DESCRIPTOR_DUPLICATE,
        }},
    };

    scenario.size = spawn_abi_test_build_spawn_request(
        context, &scenario.path, scenario.arguments, 1,
        scenario.environment, 1, scenario.actions, 1
    );
    return (
        reject_request_size_and_flags(context, &scenario)
        && reject_request_reserved_header(context, &scenario)
        && reject_reference_reserved_and_unused_tails(context, &scenario)
        && reject_request_header_values_and_counts(context, &scenario)
        && reject_reference_offsets_and_tails(context, &scenario)
        && reject_request_string_shapes(context, &scenario)
        && reject_descriptor_shapes_and_duplicates(context, &scenario)
    );
}
