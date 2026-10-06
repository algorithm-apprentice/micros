#include "micros/endpoint.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

#define EXPECT_ERROR(expected, expression) \
    do { \
        enum micros_endpoint_error actual = (expression); \
        if (actual != (expected)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %d got %d\n", \
                __FILE__, \
                __LINE__, \
                (int)(expected), \
                (int)actual \
            ); \
            return false; \
        } \
    } while (false)

static struct micros_endpoint_registry registry;

static struct micros_privilege_profile profile(
    uint8_t id,
    const char *name,
    uint32_t operations,
    uint32_t call_targets,
    uint32_t send_targets,
    uint32_t notify_targets,
    uint64_t kernel_operations
)
{
    struct micros_privilege_profile result;
    size_t index;

    memset(&result, 0, sizeof(result));
    result.id = id;
    for (
        index = 0;
        index + 1 < sizeof(result.name) && name[index] != '\0';
        ++index
    ) {
        result.name[index] = name[index];
    }
    result.operations = operations;
    result.call_targets = call_targets;
    result.send_targets = send_targets;
    result.notify_targets = notify_targets;
    result.kernel_operations = kernel_operations;
    return result;
}

static bool test_endpoint_encoding_boundaries(void)
{
    struct micros_process_handle process = {0, 1};
    struct micros_process_handle decoded = {UINT16_MAX, UINT32_MAX};
    micros_endpoint_t endpoint = UINT32_C(0xdeadbeef);

    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_pack(process, &endpoint)
    );
    EXPECT_TRUE(endpoint == UINT32_C(0x00001000));
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_unpack(endpoint, &decoded)
    );
    EXPECT_TRUE(decoded.slot == 0 && decoded.generation == 1);

    process.slot = UINT16_C(0x0ffd);
    process.generation = MICROS_ENDPOINT_GENERATION_MAX;
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_pack(process, &endpoint)
    );
    EXPECT_TRUE(endpoint == UINT32_C(0xfffffffd));

    process.slot = UINT16_C(0x0ffe);
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_ENDPOINT,
        micros_endpoint_pack(process, &endpoint)
    );
    process.slot = UINT16_C(0x0fff);
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_ENDPOINT,
        micros_endpoint_pack(process, &endpoint)
    );
    process.slot = UINT16_C(0x1000);
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_ENDPOINT,
        micros_endpoint_pack(process, &endpoint)
    );
    process.slot = 0;
    process.generation = 0;
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_ENDPOINT,
        micros_endpoint_pack(process, &endpoint)
    );
    process.generation = MICROS_ENDPOINT_GENERATION_MAX + 1;
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_ENDPOINT,
        micros_endpoint_pack(process, &endpoint)
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_STALE,
        micros_endpoint_unpack(MICROS_ENDPOINT_NONE, &decoded)
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_STALE,
        micros_endpoint_unpack(MICROS_ENDPOINT_ANY, &decoded)
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_STALE,
        micros_endpoint_unpack(0, &decoded)
    );
    return true;
}

static bool test_profile_table_is_immutable_and_specific(void)
{
    struct micros_privilege_profile profiles[2];
    const struct micros_privilege_profile *resolved = NULL;

    memset(&registry, 0, sizeof(registry));
    profiles[0] = profile(
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
    profiles[1] = profile(
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
            &registry,
            profiles,
            2
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_registry_validate(&registry)
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_privilege_profile_resolve(
            &registry,
            1,
            &resolved
        )
    );
    EXPECT_TRUE(
        resolved == &registry.profiles[1]
        && strcmp(resolved->name, "CLIENT") == 0
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_privilege_profile_allows_operation(
            &registry,
            2,
            MICROS_PRIVILEGE_OPERATION_REPLY
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_privilege_profile_allows_target(
            &registry,
            1,
            MICROS_PRIVILEGE_OPERATION_CALL,
            2
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_UNAUTHORIZED,
        micros_privilege_profile_allows_target(
            &registry,
            1,
            MICROS_PRIVILEGE_OPERATION_SEND,
            2
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_privilege_profile_allows_kernel_operation(
            &registry,
            1,
            3
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_UNAUTHORIZED,
        micros_privilege_profile_allows_kernel_operation(
            &registry,
            1,
            4
        )
    );

    {
        struct micros_endpoint_registry before = registry;

        EXPECT_ERROR(
            MICROS_ENDPOINT_ERROR_ALREADY_INITIALIZED,
            micros_endpoint_registry_initialize(
                &registry,
                profiles,
                2
            )
        );
        EXPECT_TRUE(memcmp(&registry, &before, sizeof(registry)) == 0);
    }
    return true;
}

static bool test_invalid_profile_tables_preserve_storage(void)
{
    struct micros_privilege_profile profiles[2];
    struct micros_endpoint_registry before;

    profiles[0] = profile(
        1,
        "ONE",
        MICROS_PRIVILEGE_OPERATION_RECEIVE,
        0,
        0,
        0,
        0
    );
    profiles[1] = profile(
        2,
        "TWO",
        MICROS_PRIVILEGE_OPERATION_RECEIVE,
        0,
        0,
        0,
        0
    );

#define EXPECT_REJECTED_TABLE(expected, statement) \
    do { \
        memset(&registry, 0, sizeof(registry)); \
        before = registry; \
        statement; \
        EXPECT_ERROR( \
            (expected), \
            micros_endpoint_registry_initialize( \
                &registry, \
                profiles, \
                2 \
            ) \
        ); \
        EXPECT_TRUE(memcmp(&registry, &before, sizeof(registry)) == 0); \
    } while (false)

    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        profiles[1].id = 1
    );
    profiles[1] = profile(
        2, "TWO", MICROS_PRIVILEGE_OPERATION_RECEIVE, 0, 0, 0, 0
    );
    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        memcpy(profiles[1].name, profiles[0].name, sizeof(profiles[0].name))
    );
    profiles[1] = profile(
        2, "TWO", MICROS_PRIVILEGE_OPERATION_RECEIVE, 0, 0, 0, 0
    );
    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        profiles[0].operations |= UINT32_C(0x80000000)
    );
    profiles[0] = profile(
        1, "ONE", MICROS_PRIVILEGE_OPERATION_RECEIVE,
        0, 0, 0, 0
    );
    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        profiles[0].id = 0
    );
    profiles[0] = profile(
        1, "ONE", MICROS_PRIVILEGE_OPERATION_RECEIVE,
        0, 0, 0, 0
    );
    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        profiles[0].name[0] = '\0'
    );
    profiles[0] = profile(
        1, "ONE", MICROS_PRIVILEGE_OPERATION_RECEIVE,
        0, 0, 0, 0
    );
    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        memset(
            profiles[0].name,
            'A',
            sizeof(profiles[0].name)
        )
    );
    profiles[0] = profile(
        1, "ONE", MICROS_PRIVILEGE_OPERATION_RECEIVE,
        0, 0, 0, 0
    );
    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        profiles[0].call_targets = UINT32_C(1) << 2
    );
    profiles[0] = profile(
        1, "ONE", MICROS_PRIVILEGE_OPERATION_CALL,
        UINT32_C(1) << 3, 0, 0, 0
    );
    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        (void)0
    );
    profiles[0] = profile(
        1, "ONE", MICROS_PRIVILEGE_OPERATION_CALL,
        UINT32_C(1), 0, 0, 0
    );
    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        (void)0
    );

#undef EXPECT_REJECTED_TABLE

    memset(&registry, 0, sizeof(registry));
    before = registry;
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_CAPACITY,
        micros_endpoint_registry_initialize(
            &registry,
            profiles,
            0
        )
    );
    EXPECT_TRUE(memcmp(&registry, &before, sizeof(registry)) == 0);
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_CAPACITY,
        micros_endpoint_registry_initialize(
            &registry,
            profiles,
            MICROS_PRIVILEGE_PROFILE_CAPACITY
        )
    );
    EXPECT_TRUE(memcmp(&registry, &before, sizeof(registry)) == 0);
    return true;
}

static bool test_validator_rejects_corruption(void)
{
    struct micros_privilege_profile profiles[2];
    struct micros_endpoint_registry snapshot;

    memset(&registry, 0, sizeof(registry));
    profiles[0] = profile(
        1, "CLIENT", MICROS_PRIVILEGE_OPERATION_CALL,
        UINT32_C(1) << 2, 0, 0, 0
    );
    profiles[1] = profile(
        2, "SERVER", MICROS_PRIVILEGE_OPERATION_RECEIVE,
        0, 0, 0, 0
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_registry_initialize(&registry, profiles, 2)
    );
    snapshot = registry;

#define EXPECT_CORRUPTION(statement) \
    do { \
        registry = snapshot; \
        statement; \
        EXPECT_ERROR( \
            MICROS_ENDPOINT_ERROR_INVARIANT, \
            micros_endpoint_registry_validate(&registry) \
        ); \
    } while (false)

    EXPECT_CORRUPTION(registry.profile_count = 1);
    EXPECT_CORRUPTION(registry.profiles[1].id = 2);
    EXPECT_CORRUPTION(registry.profiles[1].name[0] = '\0');
    EXPECT_CORRUPTION(registry.profiles[1].name[7] = 'X');
    EXPECT_CORRUPTION(
        registry.profiles[1].call_targets =
            UINT32_C(1) << 3
    );
    EXPECT_CORRUPTION(
        registry.endpoints[0].state = MICROS_ENDPOINT_STATE_RESERVED
    );

#undef EXPECT_CORRUPTION
    return true;
}

int main(void)
{
    static const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {
            "endpoint encoding boundaries",
            test_endpoint_encoding_boundaries,
        },
        {
            "profile table is immutable and specific",
            test_profile_table_is_immutable_and_specific,
        },
        {
            "invalid profile tables preserve storage",
            test_invalid_profile_tables_preserve_storage,
        },
        {
            "validator rejects corruption",
            test_validator_rejects_corruption,
        },
    };
    size_t index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run()) {
            fprintf(
                stderr,
                "not ok %zu - %s\n",
                index + 1,
                tests[index].name
            );
            return 1;
        }
        printf("ok %zu - %s\n", index + 1, tests[index].name);
    }
    return 0;
}
