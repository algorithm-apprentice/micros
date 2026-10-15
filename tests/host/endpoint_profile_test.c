#include "endpoint_test_internal.h"

#include <string.h>

struct endpoint_rejected_profile_case {
    struct micros_privilege_profile profiles[2];
    struct micros_endpoint_registry before;
};

bool endpoint_test_profile_table_is_immutable_and_specific(void)
{
    struct endpoint_test_fixture *fixture = endpoint_test_context();
    struct micros_privilege_profile profiles[2];
    const struct micros_privilege_profile *resolved = NULL;

    memset(&fixture->registry, 0, sizeof(fixture->registry));
    profiles[0] = endpoint_test_profile(
        1,
        "CLIENT",
        MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL
            | MICROS_PRIVILEGE_OPERATION_NOTIFY,
        UINT32_C(1) << 2,
        0,
        UINT32_C(1) << 2,
        UINT64_C(1) << 3
    );
    profiles[1] = endpoint_test_profile(
        2,
        "SERVER",
        MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_SEND
            | MICROS_PRIVILEGE_OPERATION_REPLY
            | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
        0,
        UINT32_C(1) << 1,
        0,
        0
    );

    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_registry_initialize(
            &fixture->registry,
            profiles,
            2
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_registry_validate(&fixture->registry)
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_privilege_profile_resolve(
            &fixture->registry,
            1,
            &resolved
        )
    );
    EXPECT_TRUE(
        resolved == &fixture->registry.profiles[1]
        && strcmp(resolved->name, "CLIENT") == 0
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_privilege_profile_allows_operation(
            &fixture->registry,
            2,
            MICROS_PRIVILEGE_OPERATION_REPLY
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_privilege_profile_allows_target(
            &fixture->registry,
            1,
            MICROS_PRIVILEGE_OPERATION_CALL,
            2
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_UNAUTHORIZED,
        micros_privilege_profile_allows_target(
            &fixture->registry,
            1,
            MICROS_PRIVILEGE_OPERATION_SEND,
            2
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_privilege_profile_allows_kernel_operation(
            &fixture->registry,
            1,
            3
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_UNAUTHORIZED,
        micros_privilege_profile_allows_kernel_operation(
            &fixture->registry,
            1,
            4
        )
    );

    {
        struct micros_endpoint_registry before = fixture->registry;

        EXPECT_ERROR(
            MICROS_ENDPOINT_ERROR_ALREADY_INITIALIZED,
            micros_endpoint_registry_initialize(
                &fixture->registry,
                profiles,
                2
            )
        );
        EXPECT_TRUE(memcmp(&fixture->registry, &before, sizeof(fixture->registry)) == 0);
    }
    return true;
}

#define EXPECT_REJECTED_TABLE(expected, statement) \
    do { \
        memset(&fixture->registry, 0, sizeof(fixture->registry)); \
        work->before = fixture->registry; \
        statement; \
        EXPECT_ERROR( \
            (expected), \
            micros_endpoint_registry_initialize( \
                &fixture->registry, \
                work->profiles, \
                2 \
            ) \
        ); \
        EXPECT_TRUE(memcmp(&fixture->registry, &work->before, sizeof(fixture->registry)) == 0); \
    } while (false)

static bool endpoint_test_rejected_profiles_setup(
    struct endpoint_rejected_profile_case *work
)
{
    work->profiles[0] = endpoint_test_profile(
        1,
        "ONE",
        MICROS_PRIVILEGE_OPERATION_RECEIVE,
        0,
        0,
        0,
        0
    );
    work->profiles[1] = endpoint_test_profile(
        2,
        "TWO",
        MICROS_PRIVILEGE_OPERATION_RECEIVE,
        0,
        0,
        0,
        0
    );
    return true;
}

static bool endpoint_test_rejected_duplicate_profiles(
    struct endpoint_test_fixture *fixture,
    struct endpoint_rejected_profile_case *work
)
{
    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        work->profiles[1].id = 1
    );
    work->profiles[1] = endpoint_test_profile(
        2, "TWO", MICROS_PRIVILEGE_OPERATION_RECEIVE, 0, 0, 0, 0
    );
    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        memcpy(work->profiles[1].name, work->profiles[0].name, sizeof(work->profiles[0].name))
    );
    work->profiles[1] = endpoint_test_profile(
        2, "TWO", MICROS_PRIVILEGE_OPERATION_RECEIVE, 0, 0, 0, 0
    );
    return true;
}

static bool endpoint_test_rejected_profile_shapes(
    struct endpoint_test_fixture *fixture,
    struct endpoint_rejected_profile_case *work
)
{
    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        work->profiles[0].operations |= UINT32_C(0x80000000)
    );
    work->profiles[0] = endpoint_test_profile(
        1, "ONE", MICROS_PRIVILEGE_OPERATION_RECEIVE,
        0, 0, 0, 0
    );
    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        work->profiles[0].id = 0
    );
    work->profiles[0] = endpoint_test_profile(
        1, "ONE", MICROS_PRIVILEGE_OPERATION_RECEIVE,
        0, 0, 0, 0
    );
    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        work->profiles[0].name[0] = '\0'
    );
    work->profiles[0] = endpoint_test_profile(
        1, "ONE", MICROS_PRIVILEGE_OPERATION_RECEIVE,
        0, 0, 0, 0
    );
    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        memset(
            work->profiles[0].name,
            'A',
            sizeof(work->profiles[0].name)
        )
    );
    work->profiles[0] = endpoint_test_profile(
        1, "ONE", MICROS_PRIVILEGE_OPERATION_RECEIVE,
        0, 0, 0, 0
    );
    return true;
}

static bool endpoint_test_rejected_profile_targets(
    struct endpoint_test_fixture *fixture,
    struct endpoint_rejected_profile_case *work
)
{
    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        work->profiles[0].call_targets = UINT32_C(1) << 2
    );
    work->profiles[0] = endpoint_test_profile(
        1, "ONE", MICROS_PRIVILEGE_OPERATION_CALL,
        UINT32_C(1) << 3, 0, 0, 0
    );
    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        (void)0
    );
    work->profiles[0] = endpoint_test_profile(
        1, "ONE", MICROS_PRIVILEGE_OPERATION_CALL,
        UINT32_C(1), 0, 0, 0
    );
    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        (void)0
    );
    return true;
}

static bool endpoint_test_rejected_profile_capacities(
    struct endpoint_test_fixture *fixture,
    struct endpoint_rejected_profile_case *work
)
{
    memset(&fixture->registry, 0, sizeof(fixture->registry));
    work->before = fixture->registry;
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_CAPACITY,
        micros_endpoint_registry_initialize(
            &fixture->registry,
            work->profiles,
            0
        )
    );
    EXPECT_TRUE(memcmp(&fixture->registry, &work->before, sizeof(fixture->registry)) == 0);
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_CAPACITY,
        micros_endpoint_registry_initialize(
            &fixture->registry,
            work->profiles,
            MICROS_PRIVILEGE_PROFILE_CAPACITY
        )
    );
    EXPECT_TRUE(memcmp(&fixture->registry, &work->before, sizeof(fixture->registry)) == 0);
    return true;
}

#undef EXPECT_REJECTED_TABLE

bool endpoint_test_invalid_profile_tables_preserve_storage(void)
{
    struct endpoint_test_fixture *fixture = endpoint_test_context();
    struct endpoint_rejected_profile_case work;

    return endpoint_test_rejected_profiles_setup(&work)
        && endpoint_test_rejected_duplicate_profiles(fixture, &work)
        && endpoint_test_rejected_profile_shapes(fixture, &work)
        && endpoint_test_rejected_profile_targets(fixture, &work)
        && endpoint_test_rejected_profile_capacities(fixture, &work);
}
