#include "tests/host/spawn_abi_test_internal.h"

bool test_application_stack_boundaries(struct spawn_abi_test_context *context)
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

struct application_stack_case {
    struct test_string path;
    struct test_string arguments[2];
    struct test_string environment[1];
    struct micros_application_stack_values values;
    struct micros_application_stack_plan plan;
    uint8_t stack_page[MICROS_APPLICATION_STACK_PAGE_SIZE];
    size_t request_size;
    uint64_t argv0;
    uint64_t argv1;
    uint64_t env0;
};

static bool check_stack_plan_pointers_and_strings(struct spawn_abi_test_context *context, struct application_stack_case *scenario)
{
    memset(scenario->stack_page, 0xa5, sizeof(scenario->stack_page));
    EXPECT_TRUE(
        micros_application_stack_build(
            context->request_bytes,
            scenario->request_size,
            &scenario->values,
            scenario->stack_page,
            sizeof(scenario->stack_page),
            &scenario->plan
        ) == MICROS_APPLICATION_STACK_OK
        && scenario->plan.stack_pointer % 16 == 0
        && scenario->plan.stack_pointer >= MICROS_APPLICATION_STACK_BASE
        && scenario->plan.occupied_size
            == MICROS_APPLICATION_STACK_TOP - scenario->plan.stack_pointer
        && scenario->plan.occupied_size <= MICROS_APPLICATION_STACK_OCCUPIED_MAX
        && spawn_abi_test_read_u64_le(&scenario->stack_page[scenario->plan.argc_offset]) == 2
    );

    scenario->argv0 = spawn_abi_test_read_u64_le(&scenario->stack_page[scenario->plan.arguments_offset]);
    scenario->argv1 = spawn_abi_test_read_u64_le(&scenario->stack_page[scenario->plan.arguments_offset + 8]);
    scenario->env0 = spawn_abi_test_read_u64_le(&scenario->stack_page[scenario->plan.environment_offset]);
    EXPECT_TRUE(
        scenario->argv0 >= MICROS_APPLICATION_STACK_BASE
        && scenario->argv1 > scenario->argv0
        && scenario->env0 > scenario->argv1
        && spawn_abi_test_read_u64_le(&scenario->stack_page[scenario->plan.arguments_offset + 16]) == 0
        && spawn_abi_test_read_u64_le(&scenario->stack_page[scenario->plan.environment_offset + 8]) == 0
        && strcmp(
            (const char *)&scenario->stack_page[
                scenario->argv0 - MICROS_APPLICATION_STACK_BASE
            ],
            "app"
        ) == 0
        && strcmp(
            (const char *)&scenario->stack_page[
                scenario->argv1 - MICROS_APPLICATION_STACK_BASE
            ],
            "one"
        ) == 0
        && strcmp(
            (const char *)&scenario->stack_page[
                scenario->env0 - MICROS_APPLICATION_STACK_BASE
            ],
            "A=B"
        ) == 0
    );
    return true;
}

static bool check_stack_auxiliary_values(struct application_stack_case *scenario)
{
    EXPECT_TRUE(
        spawn_abi_test_read_u64_le(&scenario->stack_page[scenario->plan.auxiliary_offset])
            == MICROS_AT_PM_ENDPOINT
        && spawn_abi_test_read_u64_le(&scenario->stack_page[scenario->plan.auxiliary_offset + 8])
            == scenario->values.pm_endpoint
        && spawn_abi_test_read_u64_le(&scenario->stack_page[scenario->plan.auxiliary_offset + 16])
            == MICROS_AT_VFS_ENDPOINT
        && spawn_abi_test_read_u64_le(&scenario->stack_page[scenario->plan.auxiliary_offset + 24])
            == scenario->values.vfs_endpoint
        && spawn_abi_test_read_u64_le(&scenario->stack_page[scenario->plan.auxiliary_offset + 32])
            == MICROS_AT_PID
        && spawn_abi_test_read_u64_le(&scenario->stack_page[scenario->plan.auxiliary_offset + 40])
            == scenario->values.pid
        && spawn_abi_test_read_u64_le(&scenario->stack_page[scenario->plan.auxiliary_offset + 48])
            == MICROS_AT_PAGE_SIZE
        && spawn_abi_test_read_u64_le(&scenario->stack_page[scenario->plan.auxiliary_offset + 56])
            == MICROS_APPLICATION_STACK_PAGE_SIZE
        && spawn_abi_test_read_u64_le(&scenario->stack_page[scenario->plan.auxiliary_offset + 64])
            == MICROS_AT_NULL
        && spawn_abi_test_read_u64_le(&scenario->stack_page[scenario->plan.auxiliary_offset + 72]) == 0
    );
    return true;
}

static bool check_stack_zero_gaps_and_tails(struct application_stack_case *scenario)
{
    EXPECT_TRUE(
        spawn_abi_test_bytes_are_zero(scenario->stack_page, scenario->plan.argc_offset)
        && spawn_abi_test_bytes_are_zero(
            &scenario->stack_page[
                scenario->plan.environment_offset
                    + (1 + 1) * sizeof(uint64_t)
            ],
            scenario->plan.auxiliary_offset
                - (
                    scenario->plan.environment_offset
                    + (1 + 1) * sizeof(uint64_t)
                )
        )
        && spawn_abi_test_bytes_are_zero(
            &scenario->stack_page[scenario->plan.strings_offset
                + scenario->arguments[0].size
                + scenario->arguments[1].size
                + scenario->environment[0].size],
            MICROS_APPLICATION_STACK_PAGE_SIZE
                - (
                    scenario->plan.strings_offset
                    + scenario->arguments[0].size
                    + scenario->arguments[1].size
                    + scenario->environment[0].size
                )
        )
    );
    return true;
}

static bool check_stack_path_is_excluded(struct spawn_abi_test_context *context, struct application_stack_case *scenario)
{
    scenario->path = spawn_abi_test_literal_string("/not-copied");
    scenario->arguments[0] = spawn_abi_test_literal_string("x");
    scenario->request_size = spawn_abi_test_build_spawn_request(context,
        &scenario->path,
        scenario->arguments,
        1,
        NULL,
        0,
        NULL,
        0
    );
    memset(scenario->stack_page, 0xa5, sizeof(scenario->stack_page));
    EXPECT_TRUE(
        micros_application_stack_build(
            context->request_bytes,
            scenario->request_size,
            &scenario->values,
            scenario->stack_page,
            sizeof(scenario->stack_page),
            &scenario->plan
        ) == MICROS_APPLICATION_STACK_OK
        && spawn_abi_test_read_u64_le(&scenario->stack_page[scenario->plan.argc_offset]) == 1
        && spawn_abi_test_read_u64_le(&scenario->stack_page[scenario->plan.environment_offset]) == 0
        && strcmp(
            (const char *)&scenario->stack_page[scenario->plan.strings_offset],
            "x"
        ) == 0
        && scenario->plan.frame_size
            == (
                scenario->plan.strings_offset - scenario->plan.argc_offset
                + scenario->arguments[0].size
            )
    );
    return true;
}

bool test_application_stack(struct spawn_abi_test_context *context)
{
    struct application_stack_case scenario = {
        .path = spawn_abi_test_literal_string("/bin/app"),
        .arguments = {spawn_abi_test_literal_string("app"), spawn_abi_test_literal_string("one")},
        .environment = {spawn_abi_test_literal_string("A=B")},
        .values = {
            .pm_endpoint = UINT32_C(0x00001004),
            .vfs_endpoint = UINT32_C(0x00001006),
            .pid = UINT64_C(0x0102030405060708),
        },
    };

    scenario.request_size = spawn_abi_test_build_spawn_request(
        context, &scenario.path, scenario.arguments, 2,
        scenario.environment, 1, NULL, 0
    );
    return (
        check_stack_plan_pointers_and_strings(context, &scenario)
        && check_stack_auxiliary_values(&scenario)
        && check_stack_zero_gaps_and_tails(&scenario)
        && check_stack_path_is_excluded(context, &scenario)
    );
}
