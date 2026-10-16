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

static bool grant_test_grant_model(void)
{
    struct grant_test_fixture *fixture = grant_test_context();

    const uint32_t seed = UINT32_C(0x38a110c5);
    struct micros_grant_registry expected;
    uint16_t expected_process_slot[GRANT_PROCESS_COUNT];
    uint32_t expected_process_generation[GRANT_PROCESS_COUNT];
    micros_endpoint_t expected_endpoint[GRANT_PROCESS_COUNT];
    unsigned char grantor_bytes[64];
    unsigned char grantee_bytes[64];
    unsigned char expected_grantor_bytes[64];
    unsigned char expected_grantee_bytes[64];
    uint32_t random_state = seed;
    size_t coverage[GRANT_MODEL_OPERATION_COUNT] = {0};
    size_t step;

    EXPECT_TRUE(grant_test_setup_fixture(fixture));
    expected = fixture->grant_registry;
    for (step = 0; step < GRANT_PROCESS_COUNT; ++step) {
        expected_process_slot[step] = fixture->processes[step].slot;
        expected_process_generation[step] =
            fixture->processes[step].generation;
        expected_endpoint[step] = fixture->endpoints[step];
    }
    memset(fixture->model_trace, 0, sizeof(fixture->model_trace));
    fixture->model_trace_count = 0;
    for (step = 0; step < GRANT_MODEL_STEPS; ++step) {
        uint32_t random = grant_test_model_next(&random_state);
        enum grant_model_operation operation =
            (enum grant_model_operation)(
                (random >> 24) % GRANT_MODEL_OPERATION_COUNT
            );
        size_t grantor = (random >> 8) % GRANT_PROCESS_COUNT;
        size_t grantee = (random >> 16) % GRANT_PROCESS_COUNT;
        size_t slot = random % MICROS_GRANT_CAPACITY;
        micros_grant_t grant = (
            expected.grants[slot].generation
                << MICROS_GRANT_SLOT_BITS
        ) | (micros_grant_t)slot;
        size_t trace_offset = 0;
        size_t trace_length = 0;
        uint32_t trace_permission = 0;
        enum micros_grant_error actual;

        if (grantee == grantor) {
            grantee = (grantee + 1) % GRANT_PROCESS_COUNT;
        }
        if (step < GRANT_MODEL_OPERATION_COUNT) {
            operation = (enum grant_model_operation)step;
        }
        switch (operation) {
        case GRANT_MODEL_CREATE: {
            micros_grant_t produced = MICROS_GRANT_NONE;
            size_t expected_slot;

            actual = micros_grant_create(
                &fixture->grant_registry,
                &fixture->endpoint_registry,
                &fixture->objects,
                fixture->processes[grantor],
                fixture->endpoints[grantee],
                MICROS_USER_VIRTUAL_BASE
                    + (random & UINT32_C(0xffff)),
                1 + (random & UINT32_C(0xff)),
                step == 0
                    ? (
                        MICROS_GRANT_PERMISSION_READ
                        | MICROS_GRANT_PERMISSION_WRITE
                    )
                    : (
                        (random & 1)
                            ? MICROS_GRANT_PERMISSION_READ
                            : MICROS_GRANT_PERMISSION_WRITE
                    ),
                &produced
            );
            for (
                expected_slot = 0;
                expected_slot < MICROS_GRANT_CAPACITY;
                ++expected_slot
            ) {
                if (
                    expected.grants[expected_slot].state
                        == MICROS_GRANT_SLOT_FREE
                ) {
                    break;
                }
            }
            if (expected_slot == MICROS_GRANT_CAPACITY) {
                if (actual != MICROS_GRANT_ERROR_CAPACITY) {
                    return grant_test_model_fail(fixture, step, "capacity result diverged");
                }
            } else {
                struct micros_grant_record *record =
                    &expected.grants[expected_slot];
                micros_grant_t expected_grant = (
                    record->generation << MICROS_GRANT_SLOT_BITS
                ) | (micros_grant_t)expected_slot;

                if (
                    actual != MICROS_GRANT_OK
                    || produced != expected_grant
                ) {
                    return grant_test_model_fail(fixture, step, "create result diverged");
                }
                *record = (struct micros_grant_record){
                    .state = MICROS_GRANT_SLOT_ACTIVE,
                    .generation = record->generation,
                    .grantor = fixture->processes[grantor],
                    .grantor_endpoint = fixture->endpoints[grantor],
                    .grantee_endpoint = fixture->endpoints[grantee],
                    .base = MICROS_USER_VIRTUAL_BASE
                        + (random & UINT32_C(0xffff)),
                    .length = 1 + (random & UINT32_C(0xff)),
                    .permissions = step == 0
                        ? (
                            MICROS_GRANT_PERMISSION_READ
                            | MICROS_GRANT_PERMISSION_WRITE
                        )
                        : (
                            (random & 1)
                                ? MICROS_GRANT_PERMISSION_READ
                                : MICROS_GRANT_PERMISSION_WRITE
                        ),
                };
                ++expected.active_count;
            }
            break;
        }
        case GRANT_MODEL_AUTHORIZE_FROM:
        case GRANT_MODEL_AUTHORIZE_TO:
        case GRANT_MODEL_AUTHORIZE_DENIAL:
        case GRANT_MODEL_AUTHORIZE_STALE:
        case GRANT_MODEL_AUTHORIZE_BOUNDS: {
            struct micros_grant_registry snapshot = fixture->grant_registry;
            struct micros_grant_copy_authority authority;
            struct micros_grant_copy_authority sentinel;
            struct micros_grant_copy_range_plan source_plan = {0};
            struct micros_grant_copy_range_plan destination_plan = {0};
            const struct micros_grant_record *record;
            struct micros_process_handle candidate_grantee = fixture->processes[0];
            micros_endpoint_t candidate_grantor = fixture->endpoints[0];
            enum micros_grant_error expected_result;
            size_t selected_slot = MICROS_GRANT_CAPACITY;
            size_t record_grantor = GRANT_PROCESS_COUNT;
            size_t record_grantee = GRANT_PROCESS_COUNT;
            size_t index;

            if (operation == GRANT_MODEL_AUTHORIZE_STALE) {
                selected_slot = random % MICROS_GRANT_CAPACITY;
            } else {
                for (
                    index = 0;
                    index < MICROS_GRANT_CAPACITY;
                    ++index
                ) {
                    if (
                        expected.grants[index].state
                            == MICROS_GRANT_SLOT_ACTIVE
                    ) {
                        selected_slot = index;
                        break;
                    }
                }
                if (selected_slot == MICROS_GRANT_CAPACITY) {
                    selected_slot = random % MICROS_GRANT_CAPACITY;
                }
            }
            slot = selected_slot;
            record = &expected.grants[slot];
            grant = (
                record->generation << MICROS_GRANT_SLOT_BITS
            ) | (micros_grant_t)slot;
            if (
                operation == GRANT_MODEL_AUTHORIZE_STALE
                && record->state == MICROS_GRANT_SLOT_ACTIVE
            ) {
                uint32_t stale_generation =
                    record->generation == 1
                        ? 2
                        : record->generation - 1;

                grant = (
                    stale_generation << MICROS_GRANT_SLOT_BITS
                ) | (micros_grant_t)slot;
            }
            for (index = 0; index < sizeof(grantor_bytes); ++index) {
                grantor_bytes[index] =
                    (unsigned char)(random + index);
                grantee_bytes[index] =
                    (unsigned char)(UINT8_C(0xc0) - index);
                expected_grantor_bytes[index] =
                    grantor_bytes[index];
                expected_grantee_bytes[index] =
                    grantee_bytes[index];
            }
            if (
                record->state != MICROS_GRANT_SLOT_ACTIVE
                || operation == GRANT_MODEL_AUTHORIZE_STALE
            ) {
                expected_result = MICROS_GRANT_ERROR_STALE_GRANT;
                trace_permission = MICROS_GRANT_PERMISSION_READ;
            } else {
                size_t limit =
                    record->length < sizeof(grantor_bytes)
                        ? record->length
                        : sizeof(grantor_bytes);

                for (
                    index = 0;
                    index < GRANT_PROCESS_COUNT;
                    ++index
                ) {
                    if (grant_test_process_handles_equal(
                        record->grantor,
                        fixture->processes[index]
                    )) {
                        record_grantor = index;
                    }
                    if (
                        record->grantee_endpoint
                            == fixture->endpoints[index]
                    ) {
                        record_grantee = index;
                    }
                }
                if (
                    record_grantor == GRANT_PROCESS_COUNT
                    || record_grantee == GRANT_PROCESS_COUNT
                    || record->grantor_endpoint
                        != fixture->endpoints[record_grantor]
                ) {
                    return grant_test_model_fail(
                        fixture,
                        step,
                        "active participant reference diverged"
                    );
                }
                grantor = record_grantor;
                grantee = record_grantee;
                candidate_grantee = fixture->processes[record_grantee];
                candidate_grantor = record->grantor_endpoint;
                trace_length = limit == 0
                    ? 0
                    : (random >> 16) % (limit + 1);
                trace_offset =
                    (random >> 8)
                    % (record->length - trace_length + 1);
                switch (operation) {
                case GRANT_MODEL_AUTHORIZE_FROM:
                    trace_permission = MICROS_GRANT_PERMISSION_READ;
                    break;
                case GRANT_MODEL_AUTHORIZE_TO:
                    trace_permission = MICROS_GRANT_PERMISSION_WRITE;
                    break;
                case GRANT_MODEL_AUTHORIZE_DENIAL:
                    trace_permission = (
                        record->permissions
                            & MICROS_GRANT_PERMISSION_READ
                    )
                        ? MICROS_GRANT_PERMISSION_READ
                        : MICROS_GRANT_PERMISSION_WRITE;
                    grantee =
                        (record_grantee + 1) % GRANT_PROCESS_COUNT;
                    candidate_grantee = fixture->processes[grantee];
                    break;
                case GRANT_MODEL_AUTHORIZE_BOUNDS:
                    trace_permission = (
                        record->permissions
                            & MICROS_GRANT_PERMISSION_READ
                    )
                        ? MICROS_GRANT_PERMISSION_READ
                        : MICROS_GRANT_PERMISSION_WRITE;
                    trace_offset = record->length;
                    trace_length = 1;
                    break;
                default:
                    return grant_test_model_fail(
                        fixture,
                        step,
                        "unexpected authorize operation"
                    );
                }
                if (operation == GRANT_MODEL_AUTHORIZE_DENIAL) {
                    expected_result = MICROS_GRANT_ERROR_UNAUTHORIZED;
                } else if (
                    (record->permissions & trace_permission) == 0
                ) {
                    expected_result = MICROS_GRANT_ERROR_UNAUTHORIZED;
                } else if (
                    operation == GRANT_MODEL_AUTHORIZE_BOUNDS
                ) {
                    expected_result = MICROS_GRANT_ERROR_RANGE;
                } else {
                    expected_result = MICROS_GRANT_OK;
                }
            }
            memset(&sentinel, 0xa5, sizeof(sentinel));
            authority = sentinel;
            actual = micros_grant_prepare_copy_authority(
                &fixture->grant_registry,
                &fixture->endpoint_registry,
                &fixture->objects,
                candidate_grantee,
                candidate_grantor,
                grant,
                trace_offset,
                trace_length,
                trace_permission,
                &authority
            );
            if (
                actual != expected_result
                || !grant_test_registries_equal(&fixture->grant_registry, &snapshot)
            ) {
                return grant_test_model_fail(
                    fixture,
                    step,
                    "copy authorization result diverged"
                );
            }
            if (actual != MICROS_GRANT_OK) {
                if (
                    memcmp(&authority, &sentinel, sizeof(authority)) != 0
                    || memcmp(
                        grantor_bytes,
                        expected_grantor_bytes,
                        sizeof(grantor_bytes)
                    ) != 0
                    || memcmp(
                        grantee_bytes,
                        expected_grantee_bytes,
                        sizeof(grantee_bytes)
                    ) != 0
                ) {
                    return grant_test_model_fail(
                        fixture,
                        step,
                        "failed authorization changed output or bytes"
                    );
                }
                break;
            }
            if (
                !grant_test_process_handles_equal(
                    authority.grantor,
                    record->grantor
                )
                || !grant_test_process_handles_equal(
                    authority.grantee,
                    fixture->processes[record_grantee]
                )
                || authority.remote_address
                    != record->base + trace_offset
                || authority.length != trace_length
                || authority.required_permission != trace_permission
            ) {
                return grant_test_model_fail(
                    fixture,
                    step,
                    "copy authority output diverged"
                );
            }
            if (trace_length != 0) {
                source_plan.chunk_count = 1;
                destination_plan.chunk_count = 1;
                source_plan.chunks[0].length = trace_length;
                destination_plan.chunks[0].length = trace_length;
                if (
                    trace_permission == MICROS_GRANT_PERMISSION_READ
                ) {
                    source_plan.chunks[0].physical_address =
                        (uintptr_t)&grantor_bytes[0];
                    destination_plan.chunks[0].physical_address =
                        (uintptr_t)&grantee_bytes[0];
                    memcpy(
                        expected_grantee_bytes,
                        expected_grantor_bytes,
                        trace_length
                    );
                } else {
                    source_plan.chunks[0].physical_address =
                        (uintptr_t)&grantee_bytes[0];
                    destination_plan.chunks[0].physical_address =
                        (uintptr_t)&grantor_bytes[0];
                    memcpy(
                        expected_grantor_bytes,
                        expected_grantee_bytes,
                        trace_length
                    );
                }
            }
            if (
                micros_grant_copy_plan_validate(
                    &source_plan,
                    &destination_plan,
                    trace_length
                ) != MICROS_GRANT_OK
            ) {
                return grant_test_model_fail(fixture, step, "copy plan rejected");
            }
            micros_grant_copy_commit(
                &source_plan,
                &destination_plan,
                trace_length
            );
            if (
                memcmp(
                    grantor_bytes,
                    expected_grantor_bytes,
                    sizeof(grantor_bytes)
                ) != 0
                || memcmp(
                    grantee_bytes,
                    expected_grantee_bytes,
                    sizeof(grantee_bytes)
                ) != 0
            ) {
                return grant_test_model_fail(fixture, step, "copy bytes diverged");
            }
            break;
        }
        case GRANT_MODEL_REVOKE: {
            struct micros_grant_record *record =
                &expected.grants[slot];

            actual = micros_grant_revoke(
                &fixture->grant_registry,
                &fixture->endpoint_registry,
                &fixture->objects,
                fixture->processes[grantor],
                grant
            );
            if (
                record->state != MICROS_GRANT_SLOT_ACTIVE
                || record->generation
                    != (grant >> MICROS_GRANT_SLOT_BITS)
            ) {
                if (actual != MICROS_GRANT_ERROR_STALE_GRANT) {
                    return grant_test_model_fail(fixture, step, "stale revoke diverged");
                }
            } else if (!grant_test_process_handles_equal(
                record->grantor,
                fixture->processes[grantor]
            )) {
                if (actual != MICROS_GRANT_ERROR_UNAUTHORIZED) {
                    return grant_test_model_fail(fixture, step, "owner revoke diverged");
                }
            } else {
                uint32_t generation = record->generation + 1;

                if (actual != MICROS_GRANT_OK) {
                    return grant_test_model_fail(fixture, step, "revoke failed");
                }
                memset(record, 0, sizeof(*record));
                record->state = MICROS_GRANT_SLOT_FREE;
                record->generation = generation;
                --expected.active_count;
            }
            break;
        }
        case GRANT_MODEL_INSPECT: {
            struct micros_grant_record observed;
            const struct micros_grant_record *record =
                &expected.grants[slot];

            memset(&observed, 0, sizeof(observed));
            actual = micros_grant_inspect(
                &fixture->grant_registry,
                &fixture->endpoint_registry,
                &fixture->objects,
                fixture->processes[grantor],
                grant,
                &observed
            );
            if (record->state != MICROS_GRANT_SLOT_ACTIVE) {
                if (actual != MICROS_GRANT_ERROR_STALE_GRANT) {
                    return grant_test_model_fail(fixture, step, "inspect stale diverged");
                }
            } else if (!grant_test_process_handles_equal(
                record->grantor,
                fixture->processes[grantor]
            )) {
                if (actual != MICROS_GRANT_ERROR_UNAUTHORIZED) {
                    return grant_test_model_fail(fixture, step, "inspect owner diverged");
                }
            } else if (
                actual != MICROS_GRANT_OK
                || !grant_test_records_equal(record, &observed)
            ) {
                return grant_test_model_fail(fixture, step, "inspect result diverged");
            }
            break;
        }
        case GRANT_MODEL_CANCEL: {
            struct micros_grant_cancel_plan plan;
            size_t index;

            memset(&plan, 0, sizeof(plan));
            actual = micros_grant_prepare_endpoint_cancel(
                &fixture->grant_registry,
                &fixture->endpoint_registry,
                &fixture->objects,
                fixture->endpoints[grantor],
                &plan
            );
            if (
                actual != MICROS_GRANT_OK
                || !grant_test_cancel_plan_matches_reference(
                    fixture,
                    &plan,
                    &expected,
                    fixture->endpoints[grantor]
                )
                || micros_grant_commit_endpoint_cancel(
                    &fixture->grant_registry,
                    &plan
                ) != MICROS_GRANT_OK
            ) {
                return grant_test_model_fail(fixture, step, "cancel failed");
            }
            for (index = 0; index < MICROS_GRANT_CAPACITY; ++index) {
                struct micros_grant_record *record =
                    &expected.grants[index];

                if (
                    record->state == MICROS_GRANT_SLOT_ACTIVE
                    && (
                        record->grantor_endpoint
                            == fixture->endpoints[grantor]
                        || record->grantee_endpoint
                            == fixture->endpoints[grantor]
                    )
                ) {
                    uint32_t generation = record->generation + 1;

                    memset(record, 0, sizeof(*record));
                    record->state = MICROS_GRANT_SLOT_FREE;
                    record->generation = generation;
                    --expected.active_count;
                }
            }
            break;
        }
        case GRANT_MODEL_CLOSE_REUSE: {
            struct micros_grant_cancel_plan plan;
            micros_endpoint_t old_endpoint = fixture->endpoints[grantor];
            uint32_t old_generation =
                fixture->processes[grantor].generation;
            size_t index;

            memset(&plan, 0, sizeof(plan));
            actual = micros_grant_prepare_endpoint_cancel(
                &fixture->grant_registry,
                &fixture->endpoint_registry,
                &fixture->objects,
                old_endpoint,
                &plan
            );
            if (
                actual != MICROS_GRANT_OK
                || !grant_test_cancel_plan_matches_reference(
                    fixture,
                    &plan,
                    &expected,
                    old_endpoint
                )
                || micros_ipc_endpoint_close(
                    &fixture->endpoint_registry,
                    &fixture->objects,
                    old_endpoint
                ) != MICROS_IPC_OK
                || micros_grant_commit_endpoint_cancel(
                    &fixture->grant_registry,
                    &plan
                ) != MICROS_GRANT_OK
                || !grant_test_recreate_process(fixture, grantor)
                || fixture->processes[grantor].generation
                    != old_generation + 1
                || fixture->endpoints[grantor] == old_endpoint
            ) {
                return grant_test_model_fail(fixture, step, "close reuse failed");
            }
            for (index = 0; index < MICROS_GRANT_CAPACITY; ++index) {
                struct micros_grant_record *record =
                    &expected.grants[index];

                if (
                    record->state == MICROS_GRANT_SLOT_ACTIVE
                    && (
                        record->grantor_endpoint == old_endpoint
                        || record->grantee_endpoint == old_endpoint
                    )
                ) {
                    uint32_t generation = record->generation + 1;

                    memset(record, 0, sizeof(*record));
                    record->state = MICROS_GRANT_SLOT_FREE;
                    record->generation = generation;
                    --expected.active_count;
                }
            }
            expected_process_generation[grantor] =
                old_generation + 1;
            expected_endpoint[grantor] = fixture->endpoints[grantor];
            break;
        }
        case GRANT_MODEL_MALFORMED_TOKEN: {
            struct micros_grant_registry snapshot = fixture->grant_registry;
            struct micros_grant_record output;
            struct micros_grant_record sentinel;

            memset(&sentinel, 0xa5, sizeof(sentinel));
            output = sentinel;
            actual = micros_grant_inspect(
                &fixture->grant_registry,
                &fixture->endpoint_registry,
                &fixture->objects,
                fixture->processes[grantor],
                (random & 1) ? MICROS_GRANT_NONE : 0,
                &output
            );
            if (
                actual != MICROS_GRANT_ERROR_ARGUMENT
                || memcmp(&output, &sentinel, sizeof(output)) != 0
                || !grant_test_registries_equal(&fixture->grant_registry, &snapshot)
            ) {
                return grant_test_model_fail(
                    fixture,
                    step,
                    "malformed token changed state or output"
                );
            }
            break;
        }
        case GRANT_MODEL_PLAN_FAILURE: {
            struct micros_grant_cancel_plan plan;
            struct micros_grant_cancel_plan plan_snapshot;
            struct micros_grant_registry snapshot = fixture->grant_registry;

            memset(&plan, 0, sizeof(plan));
            actual = micros_grant_prepare_endpoint_cancel(
                &fixture->grant_registry,
                &fixture->endpoint_registry,
                &fixture->objects,
                fixture->endpoints[grantor],
                &plan
            );
            if (
                actual != MICROS_GRANT_OK
                || !grant_test_cancel_plan_matches_reference(
                    fixture,
                    &plan,
                    &expected,
                    fixture->endpoints[grantor]
                )
            ) {
                return grant_test_model_fail(fixture, step, "plan preparation diverged");
            }
            plan.registry_identity++;
            plan_snapshot = plan;
            actual = micros_grant_commit_endpoint_cancel(
                &fixture->grant_registry,
                &plan
            );
            if (
                actual != MICROS_GRANT_ERROR_STATE
                || memcmp(&plan, &plan_snapshot, sizeof(plan)) != 0
                || !grant_test_registries_equal(&fixture->grant_registry, &snapshot)
            ) {
                return grant_test_model_fail(
                    fixture,
                    step,
                    "foreign plan changed state"
                );
            }
            break;
        }
        case GRANT_MODEL_INVALID_CREATE: {
            struct micros_grant_registry snapshot = fixture->grant_registry;
            micros_grant_t output = UINT32_C(0xdeadbeef);

            actual = micros_grant_create(
                &fixture->grant_registry,
                &fixture->endpoint_registry,
                &fixture->objects,
                fixture->processes[grantor],
                fixture->endpoints[grantee],
                MICROS_USER_VIRTUAL_BASE,
                1,
                0,
                &output
            );
            if (
                actual != MICROS_GRANT_ERROR_ARGUMENT
                || output != UINT32_C(0xdeadbeef)
                || !grant_test_registries_equal(&fixture->grant_registry, &snapshot)
            ) {
                return grant_test_model_fail(fixture, step, "invalid create changed state");
            }
            break;
        }
        default:
            return grant_test_model_fail(fixture, step, "unknown operation");
        }
        ++coverage[operation];
        grant_test_record_model_trace(
            fixture,
            step,
            operation,
            grantor,
            grantee,
            grant,
            trace_offset,
            trace_length,
            trace_permission,
            actual
        );
        if (
            !grant_test_registries_equal(&fixture->grant_registry, &expected)
            || micros_grant_registry_validate(
                &fixture->grant_registry,
                &fixture->endpoint_registry,
                &fixture->objects
            ) != MICROS_GRANT_OK
        ) {
            return grant_test_model_fail(fixture, step, "registry state diverged");
        }
        for (slot = 0; slot < GRANT_PROCESS_COUNT; ++slot) {
            struct micros_process_handle expected_owner = {
                .slot = expected_process_slot[slot],
                .generation = expected_process_generation[slot],
            };
            struct micros_process_handle unpacked_owner;
            const struct micros_process *process =
                &fixture->objects.processes[expected_process_slot[slot]];
            const struct micros_endpoint_record *endpoint =
                &fixture->endpoint_registry.endpoints[
                    expected_process_slot[slot]
                ];

            if (
                !grant_test_process_handles_equal(
                    fixture->processes[slot],
                    expected_owner
                )
                || fixture->endpoints[slot] != expected_endpoint[slot]
                || process->slot_state
                    != MICROS_KERNEL_OBJECT_SLOT_LIVE
                || process->generation
                    != expected_process_generation[slot]
                || process->primary_endpoint
                    != expected_endpoint[slot]
                || micros_endpoint_unpack(
                    expected_endpoint[slot],
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
                || endpoint->value != expected_endpoint[slot]
            ) {
                return grant_test_model_fail(
                    fixture,
                    step,
                    "authoritative participant generation diverged"
                );
            }
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
