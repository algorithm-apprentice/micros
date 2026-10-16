#include "grant_test_internal.h"

bool grant_test_error_precedence_and_output_preservation(void)
{
    struct grant_test_fixture *fixture = grant_test_context();

    struct micros_grant_cancel_plan plan;
    struct micros_grant_cancel_plan plan_sentinel;
    struct micros_grant_record record;
    struct micros_grant_record record_sentinel;
    struct micros_grant_registry valid;
    struct micros_process_handle stale_process;
    micros_grant_t grant;
    micros_grant_t stale_grant;

    EXPECT_TRUE(grant_test_setup_fixture(fixture));
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_create(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[0],
            fixture->endpoints[1],
            MICROS_USER_VIRTUAL_BASE,
            64,
            MICROS_GRANT_PERMISSION_READ,
            &grant
        )
    );
    valid = fixture->grant_registry;
    ++fixture->grant_registry.active_count;
    EXPECT_TRUE(grant_test_expect_create_failure_unchanged(
        fixture,
        MICROS_GRANT_ERROR_RANGE,
        fixture->processes[0],
        fixture->endpoints[1],
        MICROS_USER_VIRTUAL_END - 1,
        2,
        MICROS_GRANT_PERMISSION_READ
    ));
    fixture->grant_registry = valid;

    memset(&record_sentinel, 0xa5, sizeof(record_sentinel));
    record = record_sentinel;
    ++fixture->grant_registry.active_count;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_INVARIANT,
        micros_grant_inspect(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[0],
            grant,
            &record
        )
    );
    EXPECT_TRUE(memcmp(&record, &record_sentinel, sizeof(record)) == 0);
    fixture->grant_registry = valid;

    stale_process = fixture->processes[0];
    ++stale_process.generation;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_pack(1, 1, &stale_grant)
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_DEAD_ENDPOINT,
        micros_grant_inspect(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            stale_process,
            stale_grant,
            &record
        )
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_ARGUMENT,
        micros_grant_inspect(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[0],
            MICROS_GRANT_NONE,
            NULL
        )
    );

    memset(&plan_sentinel, 0xa5, sizeof(plan_sentinel));
    plan = plan_sentinel;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_ARGUMENT,
        micros_grant_prepare_endpoint_cancel(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            MICROS_ENDPOINT_ANY,
            &plan
        )
    );
    EXPECT_TRUE(memcmp(&plan, &plan_sentinel, sizeof(plan)) == 0);

    memset(&plan, 0, sizeof(plan));
    plan.active = true;
    plan.registry_identity = (uintptr_t)&fixture->grant_registry + 1;
    plan.endpoint = fixture->endpoints[0];
    plan.selected_slots = UINT64_MAX;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_STATE,
        micros_grant_commit_endpoint_cancel(
            &fixture->grant_registry,
            &plan
        )
    );
    EXPECT_TRUE(grant_test_registries_equal(&fixture->grant_registry, &valid));

    EXPECT_TRUE(grant_test_setup_fixture(fixture));
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_pack(0, 1, &stale_grant)
    );
    EXPECT_TRUE(
        micros_ipc_endpoint_close(
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->endpoints[0]
        ) == MICROS_IPC_OK
    );
    record = record_sentinel;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_DEAD_ENDPOINT,
        micros_grant_inspect(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[0],
            stale_grant,
            &record
        )
    );
    EXPECT_TRUE(memcmp(&record, &record_sentinel, sizeof(record)) == 0);
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_DEAD_ENDPOINT,
        micros_grant_revoke(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[0],
            stale_grant
        )
    );
    return true;
}

bool grant_test_validator_rejects_corruption(void)
{
    struct grant_test_fixture *fixture = grant_test_context();

    struct micros_grant_registry valid;
    micros_grant_t grant;

    EXPECT_TRUE(grant_test_setup_fixture(fixture));
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_create(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[0],
            fixture->endpoints[1],
            MICROS_USER_VIRTUAL_BASE,
            64,
            MICROS_GRANT_PERMISSION_READ,
            &grant
        )
    );
    valid = fixture->grant_registry;

#define EXPECT_CORRUPTION(statement) \
    do { \
        fixture->grant_registry = valid; \
        statement; \
        EXPECT_GRANT_ERROR( \
            MICROS_GRANT_ERROR_INVARIANT, \
            micros_grant_registry_validate( \
                &fixture->grant_registry, \
                &fixture->endpoint_registry, \
                &fixture->objects \
            ) \
        ); \
    } while (false)

    EXPECT_CORRUPTION(++fixture->grant_registry.active_count);
    EXPECT_CORRUPTION(
        fixture->grant_registry.grants[0].permissions = 0
    );
    EXPECT_CORRUPTION(
        fixture->grant_registry.grants[0].grantor.generation++
    );
    EXPECT_CORRUPTION(
        fixture->grant_registry.grants[0].grantee_endpoint =
            fixture->endpoints[0]
    );
    EXPECT_CORRUPTION(
        fixture->grant_registry.grants[0].length =
            MICROS_USER_VIRTUAL_END
    );
    EXPECT_CORRUPTION(
        fixture->grant_registry.grants[1].grantor.slot = 1
    );
    EXPECT_CORRUPTION(
        fixture->grant_registry.grants[1].state =
            MICROS_GRANT_SLOT_QUARANTINED
    );

#undef EXPECT_CORRUPTION
    return true;
}
