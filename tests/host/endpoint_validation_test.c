#include "endpoint_test_internal.h"

#include <string.h>

bool endpoint_test_validator_rejects_corruption(void)
{
    struct endpoint_test_fixture *fixture = endpoint_test_context();
    struct micros_privilege_profile profiles[2];
    struct micros_endpoint_registry snapshot;

    memset(&fixture->registry, 0, sizeof(fixture->registry));
    profiles[0] = endpoint_test_profile(
        1, "CLIENT", MICROS_PRIVILEGE_OPERATION_CALL,
        UINT32_C(1) << 2, 0, 0, 0
    );
    profiles[1] = endpoint_test_profile(
        2, "SERVER", MICROS_PRIVILEGE_OPERATION_RECEIVE,
        0, 0, 0, 0
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_registry_initialize(&fixture->registry, profiles, 2)
    );
    snapshot = fixture->registry;

#define EXPECT_CORRUPTION(statement) \
    do { \
        fixture->registry = snapshot; \
        statement; \
        EXPECT_ERROR( \
            MICROS_ENDPOINT_ERROR_INVARIANT, \
            micros_endpoint_registry_validate(&fixture->registry) \
        ); \
    } while (false)

    EXPECT_CORRUPTION(fixture->registry.profile_count = 1);
    EXPECT_CORRUPTION(fixture->registry.profiles[1].id = 2);
    EXPECT_CORRUPTION(fixture->registry.profiles[1].name[0] = '\0');
    EXPECT_CORRUPTION(fixture->registry.profiles[1].name[7] = 'X');
    EXPECT_CORRUPTION(
        fixture->registry.profiles[1].call_targets =
            UINT32_C(1) << 3
    );
    EXPECT_CORRUPTION(
        fixture->registry.endpoints[0].state = MICROS_ENDPOINT_STATE_RESERVED
    );

#undef EXPECT_CORRUPTION
    return true;
}

bool endpoint_test_relationship_validator_rejects_corruption(void)
{
    struct endpoint_test_fixture *fixture = endpoint_test_context();
    micros_endpoint_t endpoint;
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;

    EXPECT_TRUE(endpoint_test_setup_lifecycle_fixture(fixture));
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_reserve(
            &fixture->registry,
            &fixture->objects,
            fixture->processes[0],
            &endpoint
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_install_profile(
            &fixture->registry,
            &fixture->objects,
            fixture->processes[0],
            1
        )
    );
    registry_snapshot = fixture->registry;
    objects_snapshot = fixture->objects;

#define EXPECT_RELATION_CORRUPTION(statement) \
    do { \
        fixture->registry = registry_snapshot; \
        fixture->objects = objects_snapshot; \
        statement; \
        EXPECT_ERROR( \
            MICROS_ENDPOINT_ERROR_INVARIANT, \
            micros_endpoint_registry_validate_objects( \
                &fixture->registry, \
                &fixture->objects \
            ) \
        ); \
    } while (false)

    EXPECT_RELATION_CORRUPTION(
        ++fixture->registry.endpoints[fixture->processes[0].slot].owner.generation
    );
    EXPECT_RELATION_CORRUPTION(
        fixture->registry.endpoints[fixture->processes[0].slot].value = MICROS_ENDPOINT_NONE
    );
    EXPECT_RELATION_CORRUPTION(
        fixture->objects.processes[fixture->processes[0].slot].primary_endpoint =
            MICROS_ENDPOINT_NONE
    );
    EXPECT_RELATION_CORRUPTION(
        fixture->objects.processes[fixture->processes[0].slot]
            .endpoint_lifecycle_consumed = false
    );
    EXPECT_RELATION_CORRUPTION(
        fixture->objects.processes[fixture->processes[0].slot].privilege_profile =
            UINT32_C(257)
    );
    EXPECT_RELATION_CORRUPTION(
        fixture->registry.endpoints[fixture->processes[0].slot].state =
            MICROS_ENDPOINT_STATE_ACTIVE;
        fixture->objects.processes[fixture->processes[0].slot].privilege_profile = 0
    );

#undef EXPECT_RELATION_CORRUPTION
    return true;
}
