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

static bool selected_query_rejection_preserves_state(
    struct endpoint_test_fixture *fixture,
    micros_endpoint_t endpoint
)
{
    struct micros_endpoint_registry registry = fixture->registry;
    struct micros_kernel_objects objects = fixture->objects;
    const struct micros_endpoint_record *record = NULL;

    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_INVARIANT,
        micros_endpoint_resolve_active(
            &fixture->registry, &fixture->objects, endpoint, &record
        )
    );
    EXPECT_TRUE(record == NULL);
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_INVARIANT,
        micros_endpoint_authorize_operation(
            &fixture->registry, &fixture->objects, endpoint,
            MICROS_PRIVILEGE_OPERATION_RECEIVE
        )
    );
    EXPECT_TRUE(memcmp(&registry, &fixture->registry, sizeof(registry)) == 0);
    EXPECT_TRUE(memcmp(&objects, &fixture->objects, sizeof(objects)) == 0);
    return true;
}

bool endpoint_test_selected_queries_retain_local_authority(void)
{
    struct endpoint_test_fixture *fixture = endpoint_test_context();
    const struct micros_privilege_profile *profile = NULL;
    const struct micros_endpoint_record *record = NULL;
    struct micros_process *process;
    struct micros_endpoint_record *selected;
    struct micros_endpoint_registry registry;
    micros_endpoint_t endpoint;

    EXPECT_TRUE(endpoint_test_setup_lifecycle_fixture(fixture));
    EXPECT_ERROR(MICROS_ENDPOINT_OK, micros_endpoint_reserve(
        &fixture->registry, &fixture->objects, fixture->processes[0], &endpoint
    ));
    EXPECT_ERROR(MICROS_ENDPOINT_OK, micros_endpoint_install_profile(
        &fixture->registry, &fixture->objects, fixture->processes[0], 1
    ));
    EXPECT_ERROR(MICROS_ENDPOINT_OK, micros_endpoint_activate(
        &fixture->registry, &fixture->objects, endpoint
    ));
    process = &fixture->objects.processes[fixture->processes[0].slot];
    selected = &fixture->registry.endpoints[fixture->processes[0].slot];

    fixture->registry.endpoints[MICROS_PROCESS_CAPACITY - 1].value = 1;
    registry = fixture->registry;
    EXPECT_ERROR(MICROS_ENDPOINT_OK, micros_privilege_profile_resolve(
        &fixture->registry, 1, &profile
    ));
    EXPECT_TRUE(profile == &fixture->registry.profiles[1]);
    EXPECT_ERROR(MICROS_ENDPOINT_OK, micros_endpoint_resolve_active(
        &fixture->registry, &fixture->objects, endpoint, &record
    ));
    EXPECT_TRUE(record == selected);
    EXPECT_ERROR(MICROS_ENDPOINT_OK, micros_endpoint_authorize_operation(
        &fixture->registry, &fixture->objects, endpoint,
        MICROS_PRIVILEGE_OPERATION_RECEIVE
    ));
    EXPECT_ERROR(MICROS_ENDPOINT_ERROR_INVARIANT, micros_endpoint_registry_validate_objects(
        &fixture->registry, &fixture->objects
    ));
    EXPECT_TRUE(memcmp(&registry, &fixture->registry, sizeof(registry)) == 0);
    fixture->registry.endpoints[MICROS_PROCESS_CAPACITY - 1].value = 0;

    process->privilege_profile = UINT32_C(257);
    EXPECT_TRUE(selected_query_rejection_preserves_state(fixture, endpoint));
    process->privilege_profile = 1;
    selected->owner.slot = MICROS_PROCESS_CAPACITY;
    EXPECT_TRUE(selected_query_rejection_preserves_state(fixture, endpoint));
    selected->owner = fixture->processes[0];
    selected->value = MICROS_ENDPOINT_NONE;
    EXPECT_TRUE(selected_query_rejection_preserves_state(fixture, endpoint));
    selected->value = endpoint;
    fixture->registry.profiles[1].id = 2;
    EXPECT_TRUE(selected_query_rejection_preserves_state(fixture, endpoint));
    profile = NULL;
    EXPECT_ERROR(MICROS_ENDPOINT_ERROR_INVARIANT, micros_privilege_profile_resolve(
        &fixture->registry, 1, &profile
    ));
    EXPECT_TRUE(profile == NULL);
    fixture->registry.profiles[1].id = 1;
    fixture->registry.profiles[1].call_targets |= UINT32_C(1);
    profile = NULL;
    EXPECT_ERROR(MICROS_ENDPOINT_ERROR_INVARIANT, micros_privilege_profile_resolve(
        &fixture->registry, 1, &profile
    ));
    EXPECT_TRUE(profile == NULL);
    fixture->registry.profiles[1].call_targets &= ~UINT32_C(1);
    EXPECT_ERROR(MICROS_ENDPOINT_OK, micros_endpoint_registry_validate_objects(
        &fixture->registry, &fixture->objects
    ));
    return true;
}
