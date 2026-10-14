#include "tests/host/application_runtime_test_internal.h"

bool application_test_prepare_stack(
    struct application_runtime_fixture *fixture,
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

bool application_test_expect_rejection(
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

bool application_test_decode_fixture(
    const struct application_runtime_fixture *fixture,
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
