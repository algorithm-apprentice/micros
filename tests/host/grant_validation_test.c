#include "grant_test_internal.h"

struct grant_test_precedence_case {
    struct micros_grant_cancel_plan plan;
    struct micros_grant_cancel_plan plan_sentinel;
    struct micros_grant_record record;
    struct micros_grant_record record_sentinel;
    struct micros_grant_registry valid;
    struct micros_process_handle stale_process;
    micros_grant_t grant;
    micros_grant_t stale_grant;
};

static bool grant_test_precedence_range_before_invariant(
    struct grant_test_fixture *fixture,
    struct grant_test_precedence_case *work
)
{
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
            &work->grant
        )
    );
    work->valid = fixture->grant_registry;
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
    fixture->grant_registry = work->valid;
    return true;
}

static bool grant_test_precedence_record_output(
    struct grant_test_fixture *fixture,
    struct grant_test_precedence_case *work
)
{
    memset(&work->record_sentinel, 0xa5, sizeof(work->record_sentinel));
    work->record = work->record_sentinel;
    ++fixture->grant_registry.active_count;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_INVARIANT,
        micros_grant_inspect(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[0],
            work->grant,
            &work->record
        )
    );
    EXPECT_TRUE(memcmp(&work->record, &work->record_sentinel, sizeof(work->record)) == 0);
    fixture->grant_registry = work->valid;
    return true;
}

static bool grant_test_precedence_stale_shape(
    struct grant_test_fixture *fixture,
    struct grant_test_precedence_case *work
)
{
    work->stale_process = fixture->processes[0];
    ++work->stale_process.generation;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_pack(1, 1, &work->stale_grant)
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_DEAD_ENDPOINT,
        micros_grant_inspect(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            work->stale_process,
            work->stale_grant,
            &work->record
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
    return true;
}

static bool grant_test_precedence_cancel_shape(
    struct grant_test_fixture *fixture,
    struct grant_test_precedence_case *work
)
{
    memset(&work->plan_sentinel, 0xa5, sizeof(work->plan_sentinel));
    work->plan = work->plan_sentinel;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_ARGUMENT,
        micros_grant_prepare_endpoint_cancel(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            MICROS_ENDPOINT_ANY,
            &work->plan
        )
    );
    EXPECT_TRUE(memcmp(&work->plan, &work->plan_sentinel, sizeof(work->plan)) == 0);

    memset(&work->plan, 0, sizeof(work->plan));
    work->plan.active = true;
    work->plan.registry_identity = (uintptr_t)&fixture->grant_registry + 1;
    work->plan.endpoint = fixture->endpoints[0];
    work->plan.selected_slots = UINT64_MAX;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_STATE,
        micros_grant_commit_endpoint_cancel(
            &fixture->grant_registry,
            &work->plan
        )
    );
    EXPECT_TRUE(grant_test_registries_equal(&fixture->grant_registry, &work->valid));
    return true;
}

static bool grant_test_precedence_closed_endpoint(
    struct grant_test_fixture *fixture,
    struct grant_test_precedence_case *work
)
{
    EXPECT_TRUE(grant_test_setup_fixture(fixture));
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_pack(0, 1, &work->stale_grant)
    );
    EXPECT_TRUE(
        micros_ipc_endpoint_close(
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->endpoints[0]
        ) == MICROS_IPC_OK
    );
    work->record = work->record_sentinel;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_DEAD_ENDPOINT,
        micros_grant_inspect(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[0],
            work->stale_grant,
            &work->record
        )
    );
    EXPECT_TRUE(memcmp(&work->record, &work->record_sentinel, sizeof(work->record)) == 0);
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_DEAD_ENDPOINT,
        micros_grant_revoke(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[0],
            work->stale_grant
        )
    );
    return true;
}

bool grant_test_error_precedence_and_output_preservation(void)
{
    struct grant_test_fixture *fixture = grant_test_context();
    struct grant_test_precedence_case work;

    if (!grant_test_precedence_range_before_invariant(fixture, &work)) {
        return false;
    }
    if (!grant_test_precedence_record_output(fixture, &work)) {
        return false;
    }
    if (!grant_test_precedence_stale_shape(fixture, &work)) {
        return false;
    }
    if (!grant_test_precedence_cancel_shape(fixture, &work)) {
        return false;
    }
    return grant_test_precedence_closed_endpoint(fixture, &work);
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
