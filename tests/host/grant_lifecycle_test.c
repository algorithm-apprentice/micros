#include "grant_test_internal.h"

static struct micros_user_context grant_test_context_pattern(uint64_t base)
{
    struct micros_user_context context;
    uint64_t words[
        sizeof(struct micros_user_context) / sizeof(uint64_t)
    ];
    size_t index;

    for (index = 0; index < sizeof(words) / sizeof(words[0]); ++index) {
        words[index] = base + index;
    }
    memcpy(&context, words, sizeof(context));
    return context;
}

bool grant_test_token_boundaries(void)
{
    micros_grant_t grant = UINT32_C(0xdeadbeef);
    size_t slot = SIZE_MAX;
    uint32_t generation = UINT32_MAX;

    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_pack(0, 1, &grant)
    );
    EXPECT_TRUE(grant == UINT32_C(0x40));
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_unpack(grant, &slot, &generation)
    );
    EXPECT_TRUE(slot == 0 && generation == 1);

    grant = UINT32_C(0xdeadbeef);
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_ARGUMENT,
        micros_grant_pack(MICROS_GRANT_CAPACITY, 1, &grant)
    );
    EXPECT_TRUE(grant == UINT32_C(0xdeadbeef));
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_ARGUMENT,
        micros_grant_pack(0, 0, &grant)
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_ARGUMENT,
        micros_grant_pack(
            MICROS_GRANT_CAPACITY - 1,
            MICROS_GRANT_GENERATION_MAX,
            &grant
        )
    );

    slot = SIZE_MAX;
    generation = UINT32_MAX;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_ARGUMENT,
        micros_grant_unpack(
            MICROS_GRANT_NONE,
            &slot,
            &generation
        )
    );
    EXPECT_TRUE(slot == SIZE_MAX && generation == UINT32_MAX);
    return true;
}

struct grant_test_create_case {
    struct micros_grant_record observed;
    struct micros_grant_record sentinel;
    struct micros_grant_registry snapshot;
    micros_grant_t first;
    micros_grant_t second;
};

static bool grant_test_create_shape_and_setup(
    struct grant_test_fixture *fixture,
    struct grant_test_create_case *work
)
{
    EXPECT_TRUE(grant_test_setup_fixture(fixture));
    work->snapshot = fixture->grant_registry;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_ARGUMENT,
        micros_grant_create(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[0],
            fixture->endpoints[1],
            MICROS_USER_VIRTUAL_BASE + UINT64_C(0x1000),
            128,
            MICROS_GRANT_PERMISSION_READ,
            NULL
        )
    );
    EXPECT_TRUE(grant_test_registries_equal(&fixture->grant_registry, &work->snapshot));
    return true;
}

static bool grant_test_create_record(
    struct grant_test_fixture *fixture,
    struct grant_test_create_case *work
)
{
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_create(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[0],
            fixture->endpoints[1],
            MICROS_USER_VIRTUAL_BASE + UINT64_C(0x1000),
            128,
            MICROS_GRANT_PERMISSION_READ,
            &work->first
        )
    );
    EXPECT_TRUE(work->first == UINT32_C(0x40));
    memset(&work->observed, 0, sizeof(work->observed));
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_inspect(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[0],
            work->first,
            &work->observed
        )
    );
    EXPECT_TRUE(
        work->observed.state == MICROS_GRANT_SLOT_ACTIVE
        && work->observed.generation == 1
        && grant_test_process_handles_equal(work->observed.grantor, fixture->processes[0])
        && work->observed.grantor_endpoint == fixture->endpoints[0]
        && work->observed.grantee_endpoint == fixture->endpoints[1]
        && work->observed.base
            == MICROS_USER_VIRTUAL_BASE + UINT64_C(0x1000)
        && work->observed.length == 128
        && work->observed.permissions == MICROS_GRANT_PERMISSION_READ
    );
    return true;
}

static bool grant_test_create_foreign_denials(
    struct grant_test_fixture *fixture,
    struct grant_test_create_case *work
)
{
    memset(&work->sentinel, 0xa5, sizeof(work->sentinel));
    work->observed = work->sentinel;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_UNAUTHORIZED,
        micros_grant_inspect(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[2],
            work->first,
            &work->observed
        )
    );
    EXPECT_TRUE(memcmp(&work->observed, &work->sentinel, sizeof(work->observed)) == 0);

    work->snapshot = fixture->grant_registry;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_UNAUTHORIZED,
        micros_grant_revoke(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[2],
            work->first
        )
    );
    EXPECT_TRUE(grant_test_registries_equal(&fixture->grant_registry, &work->snapshot));
    return true;
}

static bool grant_test_create_revoke_and_reuse(
    struct grant_test_fixture *fixture,
    struct grant_test_create_case *work
)
{
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_revoke(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[0],
            work->first
        )
    );
    EXPECT_TRUE(
        fixture->grant_registry.active_count == 0
        && fixture->grant_registry.grants[0].state
            == MICROS_GRANT_SLOT_FREE
        && fixture->grant_registry.grants[0].generation == 2
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_STALE_GRANT,
        micros_grant_inspect(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[0],
            work->first,
            &work->observed
        )
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_create(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[0],
            fixture->endpoints[1],
            MICROS_USER_VIRTUAL_BASE + UINT64_C(0x2000),
            64,
            MICROS_GRANT_PERMISSION_WRITE,
            &work->second
        )
    );
    EXPECT_TRUE(work->second != work->first && work->second == UINT32_C(0x80));
    return true;
}

bool grant_test_create_inspect_revoke_and_reuse(void)
{
    struct grant_test_fixture *fixture = grant_test_context();
    struct grant_test_create_case work;
    work.first = MICROS_GRANT_NONE;
    work.second = MICROS_GRANT_NONE;

    if (!grant_test_create_shape_and_setup(fixture, &work)) {
        return false;
    }
    if (!grant_test_create_record(fixture, &work)) {
        return false;
    }
    if (!grant_test_create_foreign_denials(fixture, &work)) {
        return false;
    }
    return grant_test_create_revoke_and_reuse(fixture, &work);
}

bool grant_test_create_failures_and_capacity(void)
{
    struct grant_test_fixture *fixture = grant_test_context();

    micros_endpoint_t stale;
    size_t index;

    EXPECT_TRUE(grant_test_setup_fixture(fixture));
    stale =
        fixture->endpoints[1]
        + (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS);
    EXPECT_TRUE(grant_test_expect_create_failure_unchanged(
        fixture,
        MICROS_GRANT_ERROR_ARGUMENT,
        fixture->processes[0],
        MICROS_ENDPOINT_ANY,
        MICROS_USER_VIRTUAL_BASE,
        1,
        MICROS_GRANT_PERMISSION_READ
    ));
    EXPECT_TRUE(grant_test_expect_create_failure_unchanged(
        fixture,
        MICROS_GRANT_ERROR_UNAUTHORIZED,
        fixture->processes[0],
        fixture->endpoints[0],
        MICROS_USER_VIRTUAL_BASE,
        1,
        MICROS_GRANT_PERMISSION_READ
    ));
    EXPECT_TRUE(grant_test_expect_create_failure_unchanged(
        fixture,
        MICROS_GRANT_ERROR_ARGUMENT,
        fixture->processes[0],
        fixture->endpoints[1],
        MICROS_USER_VIRTUAL_BASE,
        1,
        0
    ));
    EXPECT_TRUE(grant_test_expect_create_failure_unchanged(
        fixture,
        MICROS_GRANT_ERROR_RANGE,
        fixture->processes[0],
        fixture->endpoints[1],
        MICROS_USER_VIRTUAL_END - 1,
        2,
        MICROS_GRANT_PERMISSION_READ
    ));
    EXPECT_TRUE(grant_test_expect_create_failure_unchanged(
        fixture,
        MICROS_GRANT_ERROR_DEAD_ENDPOINT,
        fixture->processes[0],
        stale,
        MICROS_USER_VIRTUAL_BASE,
        1,
        MICROS_GRANT_PERMISSION_READ
    ));

    for (index = 0; index < MICROS_GRANT_CAPACITY; ++index) {
        micros_grant_t grant;

        EXPECT_GRANT_ERROR(
            MICROS_GRANT_OK,
            micros_grant_create(
                &fixture->grant_registry,
                &fixture->endpoint_registry,
                &fixture->objects,
                fixture->processes[0],
                fixture->endpoints[1],
                MICROS_USER_VIRTUAL_BASE
                    + index * UINT64_C(0x100),
                16,
                MICROS_GRANT_PERMISSION_READ,
                &grant
            )
        );
    }
    EXPECT_TRUE(grant_test_expect_create_failure_unchanged(
        fixture,
        MICROS_GRANT_ERROR_CAPACITY,
        fixture->processes[0],
        fixture->endpoints[1],
        MICROS_USER_VIRTUAL_BASE + UINT64_C(0x10000),
        16,
        MICROS_GRANT_PERMISSION_READ
    ));
    return true;
}

bool grant_test_terminal_generation_quarantines(void)
{
    struct grant_test_fixture *fixture = grant_test_context();

    micros_grant_t grant;
    size_t index;

    EXPECT_TRUE(grant_test_setup_fixture(fixture));
    for (index = 0; index + 1 < MICROS_GRANT_CAPACITY; ++index) {
        fixture->grant_registry.grants[index].state =
            MICROS_GRANT_SLOT_QUARANTINED;
        fixture->grant_registry.grants[index].generation =
            MICROS_GRANT_GENERATION_MAX;
    }
    fixture->grant_registry.grants[MICROS_GRANT_CAPACITY - 1].generation =
        MICROS_GRANT_GENERATION_MAX - 1;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_registry_validate(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects
        )
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_create(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[0],
            fixture->endpoints[1],
            MICROS_USER_VIRTUAL_BASE,
            1,
            MICROS_GRANT_PERMISSION_READ,
            &grant
        )
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_revoke(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[0],
            grant
        )
    );
    EXPECT_TRUE(
        fixture->grant_registry.grants[MICROS_GRANT_CAPACITY - 1].state
            == MICROS_GRANT_SLOT_QUARANTINED
        && fixture->grant_registry.grants[
            MICROS_GRANT_CAPACITY - 1
        ].generation == MICROS_GRANT_GENERATION_MAX - 1
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_STALE_GRANT,
        micros_grant_revoke(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[0],
            grant
        )
    );
    return true;
}

struct grant_test_cancel_case {
    struct micros_grant_cancel_plan plan;
    struct micros_grant_cancel_plan corrupted;
    struct micros_grant_registry snapshot;
    struct micros_grant_registry foreign_registry;
    struct micros_grant_record observed;
    struct micros_process_handle old_process;
    micros_grant_t from_closing;
    micros_grant_t to_closing;
    micros_grant_t unrelated;
};

static bool grant_test_cancel_create_participants(
    struct grant_test_fixture *fixture,
    struct grant_test_cancel_case *work
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
            &work->from_closing
        )
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_create(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[1],
            fixture->endpoints[0],
            MICROS_USER_VIRTUAL_BASE + UINT64_C(0x1000),
            64,
            MICROS_GRANT_PERMISSION_WRITE,
            &work->to_closing
        )
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_create(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[1],
            fixture->endpoints[2],
            MICROS_USER_VIRTUAL_BASE + UINT64_C(0x2000),
            64,
            MICROS_GRANT_PERMISSION_READ,
            &work->unrelated
        )
    );
    return true;
}

static bool grant_test_cancel_prepare_source(
    struct grant_test_fixture *fixture,
    struct grant_test_cancel_case *work
)
{
    EXPECT_TRUE(
        micros_endpoint_seal_source_only(
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->endpoints[0]
        ) == MICROS_ENDPOINT_OK
    );
    memset(&work->plan, 0xa5, sizeof(work->plan));
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_prepare_endpoint_cancel(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->endpoints[0],
            &work->plan
        )
    );
    return true;
}

static bool grant_test_cancel_reject_foreign_plans(
    struct grant_test_fixture *fixture,
    struct grant_test_cancel_case *work
)
{
    work->snapshot = fixture->grant_registry;
    work->corrupted = work->plan;
    work->corrupted.expected_generation[
        work->from_closing
            & ((UINT32_C(1) << MICROS_GRANT_SLOT_BITS) - 1)
    ]++;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_INVARIANT,
        micros_grant_commit_endpoint_cancel(
            &fixture->grant_registry,
            &work->corrupted
        )
    );
    EXPECT_TRUE(grant_test_registries_equal(&fixture->grant_registry, &work->snapshot));

    work->foreign_registry = fixture->grant_registry;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_STATE,
        micros_grant_commit_endpoint_cancel(
            &work->foreign_registry,
            &work->plan
        )
    );
    EXPECT_TRUE(grant_test_registries_equal(&fixture->grant_registry, &work->snapshot));
    return true;
}

static bool grant_test_cancel_commit_and_survivor(
    struct grant_test_fixture *fixture,
    struct grant_test_cancel_case *work
)
{
    work->old_process = fixture->processes[0];
    EXPECT_TRUE(
        micros_ipc_endpoint_close(
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->endpoints[0]
        ) == MICROS_IPC_OK
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_commit_endpoint_cancel(
            &fixture->grant_registry,
            &work->plan
        )
    );
    EXPECT_TRUE(fixture->grant_registry.active_count == 1);
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_STALE_GRANT,
        micros_grant_inspect(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[1],
            work->to_closing,
            &work->observed
        )
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_inspect(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[1],
            work->unrelated,
            &work->observed
        )
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_STATE,
        micros_grant_commit_endpoint_cancel(
            &fixture->grant_registry,
            &work->plan
        )
    );
    return true;
}

static bool grant_test_cancel_generation_reuse(
    struct grant_test_fixture *fixture,
    struct grant_test_cancel_case *work
)
{
    EXPECT_TRUE(grant_test_recreate_process(fixture, 0));
    EXPECT_TRUE(
        fixture->processes[0].slot == work->old_process.slot
        && fixture->processes[0].generation == work->old_process.generation + 1
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_STALE_GRANT,
        micros_grant_inspect(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[0],
            work->from_closing,
            &work->observed
        )
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_registry_validate(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects
        )
    );
    return true;
}

bool grant_test_endpoint_cancellation_and_reuse(void)
{
    struct grant_test_fixture *fixture = grant_test_context();
    struct grant_test_cancel_case work;

    if (!grant_test_cancel_create_participants(fixture, &work)) {
        return false;
    }
    if (!grant_test_cancel_prepare_source(fixture, &work)) {
        return false;
    }
    if (!grant_test_cancel_reject_foreign_plans(fixture, &work)) {
        return false;
    }
    if (!grant_test_cancel_commit_and_survivor(fixture, &work)) {
        return false;
    }
    return grant_test_cancel_generation_reuse(fixture, &work);
}

bool grant_test_failed_ipc_close_preserves_cancel_plan(void)
{
    struct grant_test_fixture *fixture = grant_test_context();

    struct micros_grant_cancel_plan plan;
    struct micros_grant_cancel_plan plan_snapshot;
    struct micros_grant_registry registry_snapshot;
    struct micros_user_context context = grant_test_context_pattern(UINT64_C(0x9000));
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
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_prepare_endpoint_cancel(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->endpoints[0],
            &plan
        )
    );
    EXPECT_TRUE(
        micros_thread_attach_execution_context(
            &fixture->objects,
            fixture->threads[0],
            UINT64_C(0x30000000),
            UINT64_C(0x30004000),
            &context
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_thread_scheduler_admit(
            &fixture->objects,
            fixture->hart,
            fixture->threads[0],
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            100,
            true
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_thread_scheduler_hold(&fixture->objects, fixture->threads[0])
            == MICROS_KERNEL_OBJECT_OK
        && micros_ipc_stage_no_message_completion(
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->threads[0],
            MICROS_IPC_OK
        ) == MICROS_IPC_OK
    );
    registry_snapshot = fixture->grant_registry;
    plan_snapshot = plan;
    EXPECT_TRUE(
        micros_ipc_endpoint_close(
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->endpoints[0]
        ) == MICROS_IPC_ERROR_STATE
        && grant_test_registries_equal(&fixture->grant_registry, &registry_snapshot)
        && memcmp(&plan, &plan_snapshot, sizeof(plan)) == 0
        && fixture->grant_registry.grants[
            grant & ((UINT32_C(1) << MICROS_GRANT_SLOT_BITS) - 1)
        ].state == MICROS_GRANT_SLOT_ACTIVE
    );
    return true;
}
