#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "lib/runtime/application_internal.h"
#include "tests/host/spawn_abi_test_internal.h"

struct startup_fixture {
    uint8_t page[MICROS_APPLICATION_STACK_PAGE_SIZE];
    struct micros_application_stack_plan plan;
    struct micros_application_stack_values values;
};

static bool prepare_stack(
    struct startup_fixture *fixture,
    uint16_t argument_count,
    uint16_t environment_count
)
{
    static const char *const argument_text[] = {
        "probe", "child", "three", "four", "five", "six", "seven", "eight",
    };
    static const char *const environment_text[] = {
        "A=one", "B=two", "C=three", "D=four",
        "E=five", "F=six", "G=seven", "H=eight",
    };
    struct spawn_abi_test_context *context = spawn_abi_test_context_get();
    struct test_string path = spawn_abi_test_literal_string("/bin/probe");
    struct test_string arguments[MICROS_SPAWN_ARGUMENT_CAPACITY];
    struct test_string environment[MICROS_SPAWN_ENVIRONMENT_CAPACITY];
    size_t request_size;
    size_t index;

    EXPECT_TRUE(
        argument_count != 0
        && argument_count <= MICROS_SPAWN_ARGUMENT_CAPACITY
        && environment_count <= MICROS_SPAWN_ENVIRONMENT_CAPACITY
    );
    for (index = 0; index < argument_count; ++index) {
        arguments[index] = spawn_abi_test_literal_string(argument_text[index]);
    }
    for (index = 0; index < environment_count; ++index) {
        environment[index] = spawn_abi_test_literal_string(environment_text[index]);
    }
    fixture->values = (struct micros_application_stack_values){
        .pm_endpoint = UINT32_C(0x00001002),
        .vfs_endpoint = UINT32_C(0x00001003),
        .pid = UINT64_C(0x0000000100000007),
    };
    request_size = spawn_abi_test_build_spawn_request(
        context, &path, arguments, argument_count,
        environment, environment_count, NULL, 0
    );
    EXPECT_TRUE(
        micros_application_stack_build(
            context->request_bytes, request_size, &fixture->values,
            fixture->page, sizeof(fixture->page), &fixture->plan
        ) == MICROS_APPLICATION_STACK_OK
    );
    return true;
}

static bool expect_rejection(
    const uint8_t *page,
    size_t page_size,
    uint64_t stack_pointer,
    enum micros_application_startup_error expected
)
{
    struct micros_application_startup startup;
    uint8_t preserved[sizeof(startup)];

    memset(&startup, 0xa5, sizeof(startup));
    memcpy(preserved, &startup, sizeof(preserved));
    EXPECT_TRUE(
        micros_application_startup_decode(
            page, page_size, stack_pointer, &startup
        ) == expected
        && memcmp(&startup, preserved, sizeof(startup)) == 0
    );
    return true;
}

static bool decode_fixture(
    const struct startup_fixture *fixture,
    uint32_t argument_count,
    uint32_t environment_count
)
{
    struct micros_application_startup startup;
    uint8_t preserved[sizeof(fixture->page)];

    memcpy(preserved, fixture->page, sizeof(preserved));
    memset(&startup, 0xa5, sizeof(startup));
    EXPECT_TRUE(
        micros_application_startup_decode(
            fixture->page, sizeof(fixture->page),
            fixture->plan.stack_pointer, &startup
        ) == MICROS_APPLICATION_STARTUP_OK
        && startup.argument_count == argument_count
        && startup.environment_count == environment_count
        && startup.arguments_address
            == MICROS_APPLICATION_STACK_BASE + fixture->plan.arguments_offset
        && startup.environment_address
            == MICROS_APPLICATION_STACK_BASE + fixture->plan.environment_offset
        && startup.pm_endpoint == fixture->values.pm_endpoint
        && startup.vfs_endpoint == fixture->values.vfs_endpoint
        && startup.pid == fixture->values.pid
        && startup.page_size == MICROS_APPLICATION_STACK_PAGE_SIZE
        && memcmp(fixture->page, preserved, sizeof(preserved)) == 0
    );
    return true;
}

static bool test_canonical_initial_stack(void)
{
    struct startup_fixture fixture;

    EXPECT_TRUE(prepare_stack(&fixture, 2, 1));
    EXPECT_TRUE(decode_fixture(&fixture, 2, 1));
    return true;
}

static bool test_all_vector_counts(void)
{
    struct startup_fixture fixture;
    uint16_t arguments;
    uint16_t environment;

    for (arguments = 1; arguments <= MICROS_SPAWN_ARGUMENT_CAPACITY; ++arguments) {
        for (environment = 0; environment <= MICROS_SPAWN_ENVIRONMENT_CAPACITY;
             ++environment) {
            EXPECT_TRUE(prepare_stack(&fixture, arguments, environment));
            EXPECT_TRUE(decode_fixture(&fixture, arguments, environment));
        }
    }
    return true;
}

static bool test_argument_rejections_preserve_output(void)
{
    struct startup_fixture fixture;
    uint64_t pointers[] = {
        0, MICROS_APPLICATION_STACK_BASE,
        MICROS_APPLICATION_STACK_TOP - MICROS_APPLICATION_STACK_OCCUPIED_MAX - 16,
        MICROS_APPLICATION_STACK_TOP, UINT64_MAX,
    };
    size_t sizes[] = {0, MICROS_APPLICATION_STACK_PAGE_SIZE - 1,
                      MICROS_APPLICATION_STACK_PAGE_SIZE + 1};
    size_t index;

    EXPECT_TRUE(prepare_stack(&fixture, 2, 1));
    EXPECT_TRUE(expect_rejection(
        NULL, sizeof(fixture.page), fixture.plan.stack_pointer,
        MICROS_APPLICATION_STARTUP_ERROR_ARGUMENT
    ));
    for (index = 0; index < sizeof(pointers) / sizeof(pointers[0]); ++index) {
        EXPECT_TRUE(expect_rejection(
            fixture.page, sizeof(fixture.page), pointers[index],
            MICROS_APPLICATION_STARTUP_ERROR_ARGUMENT
        ));
    }
    for (index = 0; index < sizeof(sizes) / sizeof(sizes[0]); ++index) {
        EXPECT_TRUE(expect_rejection(
            fixture.page, sizes[index], fixture.plan.stack_pointer,
            MICROS_APPLICATION_STARTUP_ERROR_ARGUMENT
        ));
    }
    EXPECT_TRUE(expect_rejection(
        fixture.page, sizeof(fixture.page), fixture.plan.stack_pointer + 8,
        MICROS_APPLICATION_STARTUP_ERROR_ARGUMENT
    ));
    EXPECT_TRUE(
        micros_application_startup_decode(
            fixture.page, sizeof(fixture.page), fixture.plan.stack_pointer, NULL
        ) == MICROS_APPLICATION_STARTUP_ERROR_ARGUMENT
    );
    return true;
}

static bool test_vector_shapes(void)
{
    struct startup_fixture fixture;
    uint8_t preserved[MICROS_APPLICATION_STACK_PAGE_SIZE];
    uint64_t invalid_counts[] = {0, MICROS_SPAWN_ARGUMENT_CAPACITY + 1, UINT64_MAX};
    size_t index;

    EXPECT_TRUE(prepare_stack(&fixture, 2, 1));
    memcpy(preserved, fixture.page, sizeof(preserved));
    for (index = 0; index < sizeof(invalid_counts) / sizeof(invalid_counts[0]); ++index) {
        memcpy(fixture.page, preserved, sizeof(preserved));
        spawn_abi_test_write_u64_le(
            &fixture.page[fixture.plan.argc_offset], invalid_counts[index]
        );
        EXPECT_TRUE(expect_rejection(
            fixture.page, sizeof(fixture.page), fixture.plan.stack_pointer,
            MICROS_APPLICATION_STARTUP_ERROR_MALFORMED
        ));
    }
    memcpy(fixture.page, preserved, sizeof(preserved));
    spawn_abi_test_write_u64_le(
        &fixture.page[fixture.plan.arguments_offset + 2 * sizeof(uint64_t)], 1
    );
    EXPECT_TRUE(expect_rejection(
        fixture.page, sizeof(fixture.page), fixture.plan.stack_pointer,
        MICROS_APPLICATION_STARTUP_ERROR_MALFORMED
    ));
    EXPECT_TRUE(prepare_stack(&fixture, 1, MICROS_SPAWN_ENVIRONMENT_CAPACITY));
    spawn_abi_test_write_u64_le(
        &fixture.page[fixture.plan.environment_offset
            + MICROS_SPAWN_ENVIRONMENT_CAPACITY * sizeof(uint64_t)],
        MICROS_APPLICATION_STACK_BASE + fixture.plan.strings_offset
    );
    EXPECT_TRUE(expect_rejection(
        fixture.page, sizeof(fixture.page), fixture.plan.stack_pointer,
        MICROS_APPLICATION_STARTUP_ERROR_MALFORMED
    ));
    return true;
}

static bool test_auxiliary_shape_and_values(void)
{
    struct startup_fixture fixture;
    uint8_t preserved[MICROS_APPLICATION_STACK_PAGE_SIZE];
    uint64_t bad_endpoints[] = {
        0, 1, MICROS_ENDPOINT_NONE, MICROS_ENDPOINT_ANY,
        UINT64_C(0x0000000100001002),
        (UINT64_C(1) << MICROS_ENDPOINT_SLOT_BITS) | MICROS_PROCESS_CAPACITY,
    };
    size_t index;
    size_t role;

    EXPECT_TRUE(prepare_stack(&fixture, 2, 1));
    memcpy(preserved, fixture.page, sizeof(preserved));
    for (index = 0; index < MICROS_APPLICATION_AUXV_COUNT; ++index) {
        memcpy(fixture.page, preserved, sizeof(preserved));
        spawn_abi_test_write_u64_le(
            &fixture.page[fixture.plan.auxiliary_offset + index * 16], 99
        );
        EXPECT_TRUE(expect_rejection(
            fixture.page, sizeof(fixture.page), fixture.plan.stack_pointer,
            MICROS_APPLICATION_STARTUP_ERROR_MALFORMED
        ));
    }
    for (role = 0; role < 2; ++role) {
        for (index = 0; index < sizeof(bad_endpoints) / sizeof(bad_endpoints[0]); ++index) {
            memcpy(fixture.page, preserved, sizeof(preserved));
            spawn_abi_test_write_u64_le(
                &fixture.page[fixture.plan.auxiliary_offset + role * 16 + 8],
                bad_endpoints[index]
            );
            EXPECT_TRUE(expect_rejection(
                fixture.page, sizeof(fixture.page), fixture.plan.stack_pointer,
                MICROS_APPLICATION_STARTUP_ERROR_MALFORMED
            ));
        }
    }
    for (role = 2; role < MICROS_APPLICATION_AUXV_COUNT; ++role) {
        memcpy(fixture.page, preserved, sizeof(preserved));
        spawn_abi_test_write_u64_le(
            &fixture.page[fixture.plan.auxiliary_offset + role * 16 + 8],
            role == 4 ? 1 : 0
        );
        EXPECT_TRUE(expect_rejection(
            fixture.page, sizeof(fixture.page), fixture.plan.stack_pointer,
            MICROS_APPLICATION_STARTUP_ERROR_MALFORMED
        ));
    }
    return true;
}

static bool test_string_pointer_authority_and_density(void)
{
    struct startup_fixture fixture;
    uint8_t preserved[MICROS_APPLICATION_STACK_PAGE_SIZE];
    uint64_t pointers[] = {
        0, MICROS_APPLICATION_STACK_BASE - 1, MICROS_APPLICATION_STACK_TOP,
        UINT64_MAX, MICROS_APPLICATION_STACK_BASE,
    };
    size_t index;

    EXPECT_TRUE(prepare_stack(&fixture, 2, 1));
    memcpy(preserved, fixture.page, sizeof(preserved));
    for (index = 0; index < sizeof(pointers) / sizeof(pointers[0]); ++index) {
        memcpy(fixture.page, preserved, sizeof(preserved));
        spawn_abi_test_write_u64_le(
            &fixture.page[fixture.plan.arguments_offset], pointers[index]
        );
        EXPECT_TRUE(expect_rejection(
            fixture.page, sizeof(fixture.page), fixture.plan.stack_pointer,
            MICROS_APPLICATION_STARTUP_ERROR_MALFORMED
        ));
    }
    for (index = 0; index < 3; ++index) {
        size_t offset = index == 2
            ? fixture.plan.environment_offset
            : fixture.plan.arguments_offset + index * sizeof(uint64_t);
        uint64_t value = MICROS_APPLICATION_STACK_BASE + fixture.plan.strings_offset;

        memcpy(fixture.page, preserved, sizeof(preserved));
        spawn_abi_test_write_u64_le(
            &fixture.page[offset], index == 0 ? value + 1 : value
        );
        EXPECT_TRUE(expect_rejection(
            fixture.page, sizeof(fixture.page), fixture.plan.stack_pointer,
            MICROS_APPLICATION_STARTUP_ERROR_MALFORMED
        ));
    }
    return true;
}

static bool test_padding_and_runtime_reserve(void)
{
    struct startup_fixture fixture;
    size_t padding;

    EXPECT_TRUE(prepare_stack(&fixture, 2, 0));
    padding = fixture.plan.environment_offset + sizeof(uint64_t);
    EXPECT_TRUE(padding < fixture.plan.auxiliary_offset);
    fixture.page[padding] = 1;
    EXPECT_TRUE(expect_rejection(
        fixture.page, sizeof(fixture.page), fixture.plan.stack_pointer,
        MICROS_APPLICATION_STARTUP_ERROR_MALFORMED
    ));
    EXPECT_TRUE(prepare_stack(&fixture, 2, 1));
    EXPECT_TRUE(fixture.plan.frame_size < fixture.plan.occupied_size);
    fixture.page[MICROS_APPLICATION_STACK_PAGE_SIZE - 1] = 1;
    EXPECT_TRUE(expect_rejection(
        fixture.page, sizeof(fixture.page), fixture.plan.stack_pointer,
        MICROS_APPLICATION_STARTUP_ERROR_MALFORMED
    ));
    EXPECT_TRUE(prepare_stack(&fixture, 2, 1));
    fixture.page[0] = 0xa5;
    fixture.page[fixture.plan.argc_offset - 1] = 0x5a;
    EXPECT_TRUE(decode_fixture(&fixture, 2, 1));
    return true;
}

static bool test_maximum_string_and_occupancy(void)
{
    struct spawn_abi_test_context *context = spawn_abi_test_context_get();
    struct test_string path = spawn_abi_test_literal_string("/bin/probe");
    struct test_string argument;
    struct startup_fixture fixture;
    struct micros_application_startup startup;
    size_t request_size;

    fixture.values = (struct micros_application_stack_values){
        .pm_endpoint = UINT32_C(0x00001002),
        .vfs_endpoint = UINT32_C(0x00001003),
        .pid = UINT64_MAX,
    };
    spawn_abi_test_fill_plain_string(
        context->boundary_arguments[0], MICROS_SPAWN_STRING_SIZE_MAX, 'x'
    );
    argument.bytes = context->boundary_arguments[0];
    argument.size = MICROS_SPAWN_STRING_SIZE_MAX;
    request_size = spawn_abi_test_build_spawn_request(
        context, &path, &argument, 1, NULL, 0, NULL, 0
    );
    EXPECT_TRUE(
        micros_application_stack_build(
            context->request_bytes, request_size, &fixture.values,
            fixture.page, sizeof(fixture.page), &fixture.plan
        ) == MICROS_APPLICATION_STACK_OK
    );
    EXPECT_TRUE(decode_fixture(&fixture, 1, 0));
    fixture.page[MICROS_APPLICATION_STACK_PAGE_SIZE - 1] = 'x';
    EXPECT_TRUE(expect_rejection(
        fixture.page, sizeof(fixture.page), fixture.plan.stack_pointer,
        MICROS_APPLICATION_STARTUP_ERROR_MALFORMED
    ));
    EXPECT_TRUE(spawn_abi_test_build_occupancy_request(context, false, &request_size));
    EXPECT_TRUE(
        micros_application_stack_build(
            context->request_bytes, request_size, &fixture.values,
            fixture.page, sizeof(fixture.page), &fixture.plan
        ) == MICROS_APPLICATION_STACK_OK
        && fixture.plan.occupied_size == MICROS_APPLICATION_STACK_OCCUPIED_MAX
        && micros_application_startup_decode(
            fixture.page, sizeof(fixture.page), fixture.plan.stack_pointer, &startup
        ) == MICROS_APPLICATION_STARTUP_OK
        && startup.pid == UINT64_MAX
    );
    return true;
}

int main(void)
{
    static const struct {
        const char *name;
        bool (*run)(void);
    } cases[] = {
        {"canonical application startup", test_canonical_initial_stack},
        {"all argument and environment counts", test_all_vector_counts},
        {"argument rejection preserves output", test_argument_rejections_preserve_output},
        {"vector shape and termination", test_vector_shapes},
        {"exact auxiliary shape and values", test_auxiliary_shape_and_values},
        {"local string authority and density", test_string_pointer_authority_and_density},
        {"padding and live runtime reserve", test_padding_and_runtime_reserve},
        {"maximum strings and initial occupancy", test_maximum_string_and_occupancy},
    };
    size_t index;

    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        if (!cases[index].run()) {
            fprintf(stderr, "not ok %zu - %s\n", index + 1, cases[index].name);
            return 1;
        }
        printf("ok %zu - %s\n", index + 1, cases[index].name);
    }
    return 0;
}
