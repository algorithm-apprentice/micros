#include "grant_test_internal.h"

struct grant_test_boundary_case {
    struct micros_grant_registry expected;
    micros_grant_t terminal;
    size_t index;
};

static bool grant_test_boundary_fill_registry(
    struct grant_test_fixture *fixture,
    struct grant_test_boundary_case *work
)
{
    EXPECT_TRUE(grant_test_setup_fixture(fixture));
    work->expected = fixture->grant_registry;
    for (work->index = 0; work->index < MICROS_GRANT_CAPACITY; ++work->index) {
        struct micros_grant_record *record =
            &work->expected.grants[work->index];
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
                    + work->index * UINT64_C(0x100),
                16,
                MICROS_GRANT_PERMISSION_READ,
                &grant
            )
        );
        *record = (struct micros_grant_record){
            .state = MICROS_GRANT_SLOT_ACTIVE,
            .generation = 1,
            .grantor = fixture->processes[0],
            .grantor_endpoint = fixture->endpoints[0],
            .grantee_endpoint = fixture->endpoints[1],
            .base = MICROS_USER_VIRTUAL_BASE
                + work->index * UINT64_C(0x100),
            .length = 16,
            .permissions = MICROS_GRANT_PERMISSION_READ,
        };
        ++work->expected.active_count;
        EXPECT_TRUE(grant_test_registries_equal(&fixture->grant_registry, &work->expected));
    }
    return true;
}

static bool grant_test_boundary_capacity_output(
    struct grant_test_fixture *fixture
)
{
    {
        struct micros_grant_registry snapshot = fixture->grant_registry;
        micros_grant_t output = UINT32_C(0xdeadbeef);

        EXPECT_GRANT_ERROR(
            MICROS_GRANT_ERROR_CAPACITY,
            micros_grant_create(
                &fixture->grant_registry,
                &fixture->endpoint_registry,
                &fixture->objects,
                fixture->processes[0],
                fixture->endpoints[1],
                MICROS_USER_VIRTUAL_BASE + UINT64_C(0x10000),
                16,
                MICROS_GRANT_PERMISSION_READ,
                &output
            )
        );
        EXPECT_TRUE(
            output == UINT32_C(0xdeadbeef)
            && grant_test_registries_equal(&fixture->grant_registry, &snapshot)
        );
    }
    return true;
}

static bool grant_test_boundary_quarantine_prepare(
    struct grant_test_fixture *fixture,
    struct grant_test_boundary_case *work
)
{
    EXPECT_TRUE(grant_test_setup_fixture(fixture));
    for (work->index = 0; work->index + 1 < MICROS_GRANT_CAPACITY; ++work->index) {
        fixture->grant_registry.grants[work->index].state =
            MICROS_GRANT_SLOT_QUARANTINED;
        fixture->grant_registry.grants[work->index].generation =
            MICROS_GRANT_GENERATION_MAX;
    }
    fixture->grant_registry.grants[MICROS_GRANT_CAPACITY - 1].generation =
        MICROS_GRANT_GENERATION_MAX - 1;
    work->expected = fixture->grant_registry;
    return true;
}

static bool grant_test_boundary_terminal_revoke(
    struct grant_test_fixture *fixture,
    struct grant_test_boundary_case *work
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
            MICROS_USER_VIRTUAL_BASE,
            1,
            MICROS_GRANT_PERMISSION_WRITE,
            &work->terminal
        )
    );
    work->expected.grants[MICROS_GRANT_CAPACITY - 1] =
        (struct micros_grant_record){
            .state = MICROS_GRANT_SLOT_ACTIVE,
            .generation = MICROS_GRANT_GENERATION_MAX - 1,
            .grantor = fixture->processes[0],
            .grantor_endpoint = fixture->endpoints[0],
            .grantee_endpoint = fixture->endpoints[1],
            .base = MICROS_USER_VIRTUAL_BASE,
            .length = 1,
            .permissions = MICROS_GRANT_PERMISSION_WRITE,
        };
    work->expected.active_count = 1;
    EXPECT_TRUE(grant_test_registries_equal(&fixture->grant_registry, &work->expected));
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_revoke(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[0],
            work->terminal
        )
    );
    memset(
        &work->expected.grants[MICROS_GRANT_CAPACITY - 1],
        0,
        sizeof(work->expected.grants[MICROS_GRANT_CAPACITY - 1])
    );
    work->expected.grants[MICROS_GRANT_CAPACITY - 1].state =
        MICROS_GRANT_SLOT_QUARANTINED;
    work->expected.grants[MICROS_GRANT_CAPACITY - 1].generation =
        MICROS_GRANT_GENERATION_MAX - 1;
    work->expected.active_count = 0;
    return (
        grant_test_registries_equal(&fixture->grant_registry, &work->expected)
        && micros_grant_registry_validate(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects
        ) == MICROS_GRANT_OK
    );
}

static bool grant_test_grant_model_boundaries(void)
{
    struct grant_test_fixture *fixture = grant_test_context();
    struct grant_test_boundary_case work;

    if (!grant_test_boundary_fill_registry(fixture, &work)) {
        return false;
    }
    if (!grant_test_boundary_capacity_output(fixture)) {
        return false;
    }
    if (!grant_test_boundary_quarantine_prepare(fixture, &work)) {
        return false;
    }
    return grant_test_boundary_terminal_revoke(fixture, &work);
}

static void grant_test_model_select_operation(
    struct grant_model_run *run,
    struct grant_model_iteration *iteration,
    size_t step
)
{
    iteration->random = grant_test_model_next(&run->random_state);
    iteration->operation = (enum grant_model_operation)(
                (iteration->random >> 24) % GRANT_MODEL_OPERATION_COUNT
            );
    iteration->grantor = (iteration->random >> 8) % GRANT_PROCESS_COUNT;
    iteration->grantee = (iteration->random >> 16) % GRANT_PROCESS_COUNT;
    iteration->slot = iteration->random % MICROS_GRANT_CAPACITY;
    iteration->grant = (
            run->expected.grants[iteration->slot].generation
                << MICROS_GRANT_SLOT_BITS
        ) | (micros_grant_t)iteration->slot;
    iteration->trace_offset = 0;
    iteration->trace_length = 0;
    iteration->trace_permission = 0;

    if (iteration->grantee == iteration->grantor) {
        iteration->grantee = (iteration->grantee + 1) % GRANT_PROCESS_COUNT;
    }
    if (step < GRANT_MODEL_OPERATION_COUNT) {
        iteration->operation = (enum grant_model_operation)step;
    }
}

static bool grant_test_model_apply_operation(
    struct grant_test_fixture *fixture,
    struct grant_model_run *run,
    struct grant_model_iteration *iteration,
    size_t step
)
{
    switch (iteration->operation) {
    case GRANT_MODEL_CREATE:
        return grant_test_model_create(fixture, run, iteration, step);
    case GRANT_MODEL_AUTHORIZE_FROM:
    case GRANT_MODEL_AUTHORIZE_TO:
    case GRANT_MODEL_AUTHORIZE_DENIAL:
    case GRANT_MODEL_AUTHORIZE_STALE:
    case GRANT_MODEL_AUTHORIZE_BOUNDS:
        return grant_test_model_authorize(fixture, run, iteration, step);
    case GRANT_MODEL_REVOKE:
        return grant_test_model_revoke(fixture, run, iteration, step);
    case GRANT_MODEL_INSPECT:
        return grant_test_model_inspect(fixture, run, iteration, step);
    case GRANT_MODEL_CANCEL:
        return grant_test_model_cancel(fixture, run, iteration, step);
    case GRANT_MODEL_CLOSE_REUSE:
        return grant_test_model_close_reuse(fixture, run, iteration, step);
    case GRANT_MODEL_MALFORMED_TOKEN:
        return grant_test_model_malformed_token(fixture, iteration, step);
    case GRANT_MODEL_PLAN_FAILURE:
        return grant_test_model_plan_failure(fixture, run, iteration, step);
    case GRANT_MODEL_INVALID_CREATE:
        return grant_test_model_invalid_create(fixture, iteration, step);
    default:
        return grant_test_model_fail(fixture, step, "unknown operation");
    }
}

static bool grant_test_model_compare_state(
    struct grant_test_fixture *fixture,
    struct grant_model_run *run,
    struct grant_model_iteration *iteration,
    size_t step
)
{
    if (
        !grant_test_registries_equal(&fixture->grant_registry, &run->expected)
        || micros_grant_registry_validate(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects
        ) != MICROS_GRANT_OK
    ) {
        return grant_test_model_fail(fixture, step, "registry state diverged");
    }
    for (iteration->slot = 0; iteration->slot < GRANT_PROCESS_COUNT; ++iteration->slot) {
        struct micros_process_handle expected_owner = {
            .slot = run->expected_process_slot[iteration->slot],
            .generation = run->expected_process_generation[iteration->slot],
        };
        struct micros_process_handle unpacked_owner;
        const struct micros_process *process =
            &fixture->objects.processes[run->expected_process_slot[iteration->slot]];
        const struct micros_endpoint_record *endpoint =
            &fixture->endpoint_registry.endpoints[
                run->expected_process_slot[iteration->slot]
            ];

        if (
            !grant_test_process_handles_equal(
                fixture->processes[iteration->slot],
                expected_owner
            )
            || fixture->endpoints[iteration->slot] != run->expected_endpoint[iteration->slot]
            || process->slot_state
                != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || process->generation
                != run->expected_process_generation[iteration->slot]
            || process->primary_endpoint
                != run->expected_endpoint[iteration->slot]
            || micros_endpoint_unpack(
                run->expected_endpoint[iteration->slot],
                &unpacked_owner
            ) != MICROS_ENDPOINT_OK
            || !grant_test_process_handles_equal(
                unpacked_owner,
                expected_owner
            )
            || endpoint->state != MICROS_ENDPOINT_STATE_ACTIVE
            || !grant_test_process_handles_equal(
                endpoint->owner,
                expected_owner
            )
            || endpoint->value != run->expected_endpoint[iteration->slot]
        ) {
            return grant_test_model_fail(
                fixture,
                step,
                "authoritative participant generation diverged"
            );
        }
    }
    return true;
}

static bool grant_test_grant_model(void)
{
    struct grant_test_fixture *fixture = grant_test_context();

    const uint32_t seed = UINT32_C(0x38a110c5);
    struct grant_model_run run;
    run.random_state = seed;
    size_t coverage[GRANT_MODEL_OPERATION_COUNT] = {0};
    size_t step;

    EXPECT_TRUE(grant_test_setup_fixture(fixture));
    run.expected = fixture->grant_registry;
    for (step = 0; step < GRANT_PROCESS_COUNT; ++step) {
        run.expected_process_slot[step] = fixture->processes[step].slot;
        run.expected_process_generation[step] =
            fixture->processes[step].generation;
        run.expected_endpoint[step] = fixture->endpoints[step];
    }
    memset(fixture->model_trace, 0, sizeof(fixture->model_trace));
    fixture->model_trace_count = 0;
    for (step = 0; step < GRANT_MODEL_STEPS; ++step) {
        struct grant_model_iteration iteration;

        grant_test_model_select_operation(&run, &iteration, step);
        if (!grant_test_model_apply_operation(fixture, &run, &iteration, step)) {
            return false;
        }
        ++coverage[iteration.operation];
        grant_test_record_model_trace(
            fixture,
            step,
            iteration.operation,
            iteration.grantor,
            iteration.grantee,
            iteration.grant,
            iteration.trace_offset,
            iteration.trace_length,
            iteration.trace_permission,
            iteration.actual
        );
        if (!grant_test_model_compare_state(fixture, &run, &iteration, step)) {
            return false;
        }
    }
    for (step = 0; step < GRANT_MODEL_OPERATION_COUNT; ++step) {
        if (coverage[step] == 0) {
            return grant_test_model_fail(
                fixture,
                GRANT_MODEL_STEPS,
                "operation coverage incomplete"
            );
        }
    }
    printf(
        "# grant and checked copy model seed=0x%08x transitions=%u\n",
        seed,
        GRANT_MODEL_STEPS
    );
    return true;
}

bool micros_grant_model_test_run(void)
{
    return grant_test_grant_model_boundaries() && grant_test_grant_model();
}
