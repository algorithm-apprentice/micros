#include "grant_test_internal.h"

bool grant_test_model_create(
    struct grant_test_fixture *fixture,
    struct grant_model_run *run,
    struct grant_model_iteration *iteration,
    size_t step
)
{
    micros_grant_t produced = MICROS_GRANT_NONE;
    size_t expected_slot;

    iteration->actual = micros_grant_create(
        &fixture->grant_registry,
        &fixture->endpoint_registry,
        &fixture->objects,
        fixture->processes[iteration->grantor],
        fixture->endpoints[iteration->grantee],
        MICROS_USER_VIRTUAL_BASE
            + (iteration->random & UINT32_C(0xffff)),
        1 + (iteration->random & UINT32_C(0xff)),
        step == 0
            ? (
                MICROS_GRANT_PERMISSION_READ
                | MICROS_GRANT_PERMISSION_WRITE
            )
            : (
                (iteration->random & 1)
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
            run->expected.grants[expected_slot].state
                == MICROS_GRANT_SLOT_FREE
        ) {
            break;
        }
    }
    if (expected_slot == MICROS_GRANT_CAPACITY) {
        if (iteration->actual != MICROS_GRANT_ERROR_CAPACITY) {
            return grant_test_model_fail(fixture, step, "capacity result diverged");
        }
    } else {
        struct micros_grant_record *record =
            &run->expected.grants[expected_slot];
        micros_grant_t expected_grant = (
            record->generation << MICROS_GRANT_SLOT_BITS
        ) | (micros_grant_t)expected_slot;

        if (
            iteration->actual != MICROS_GRANT_OK
            || produced != expected_grant
        ) {
            return grant_test_model_fail(fixture, step, "create result diverged");
        }
        *record = (struct micros_grant_record){
            .state = MICROS_GRANT_SLOT_ACTIVE,
            .generation = record->generation,
            .grantor = fixture->processes[iteration->grantor],
            .grantor_endpoint = fixture->endpoints[iteration->grantor],
            .grantee_endpoint = fixture->endpoints[iteration->grantee],
            .base = MICROS_USER_VIRTUAL_BASE
                + (iteration->random & UINT32_C(0xffff)),
            .length = 1 + (iteration->random & UINT32_C(0xff)),
            .permissions = step == 0
                ? (
                    MICROS_GRANT_PERMISSION_READ
                    | MICROS_GRANT_PERMISSION_WRITE
                )
                : (
                    (iteration->random & 1)
                        ? MICROS_GRANT_PERMISSION_READ
                        : MICROS_GRANT_PERMISSION_WRITE
                ),
        };
        ++run->expected.active_count;
    }
    return true;
}

bool grant_test_model_revoke(
    struct grant_test_fixture *fixture,
    struct grant_model_run *run,
    struct grant_model_iteration *iteration,
    size_t step
)
{
    struct micros_grant_record *record =
        &run->expected.grants[iteration->slot];

    iteration->actual = micros_grant_revoke(
        &fixture->grant_registry,
        &fixture->endpoint_registry,
        &fixture->objects,
        fixture->processes[iteration->grantor],
        iteration->grant
    );
    if (
        record->state != MICROS_GRANT_SLOT_ACTIVE
        || record->generation
            != (iteration->grant >> MICROS_GRANT_SLOT_BITS)
    ) {
        if (iteration->actual != MICROS_GRANT_ERROR_STALE_GRANT) {
            return grant_test_model_fail(fixture, step, "stale revoke diverged");
        }
    } else if (!grant_test_process_handles_equal(
        record->grantor,
        fixture->processes[iteration->grantor]
    )) {
        if (iteration->actual != MICROS_GRANT_ERROR_UNAUTHORIZED) {
            return grant_test_model_fail(fixture, step, "owner revoke diverged");
        }
    } else {
        uint32_t generation = record->generation + 1;

        if (iteration->actual != MICROS_GRANT_OK) {
            return grant_test_model_fail(fixture, step, "revoke failed");
        }
        memset(record, 0, sizeof(*record));
        record->state = MICROS_GRANT_SLOT_FREE;
        record->generation = generation;
        --run->expected.active_count;
    }
    return true;
}

bool grant_test_model_inspect(
    struct grant_test_fixture *fixture,
    struct grant_model_run *run,
    struct grant_model_iteration *iteration,
    size_t step
)
{
    struct micros_grant_record observed;
    const struct micros_grant_record *record =
        &run->expected.grants[iteration->slot];

    memset(&observed, 0, sizeof(observed));
    iteration->actual = micros_grant_inspect(
        &fixture->grant_registry,
        &fixture->endpoint_registry,
        &fixture->objects,
        fixture->processes[iteration->grantor],
        iteration->grant,
        &observed
    );
    if (record->state != MICROS_GRANT_SLOT_ACTIVE) {
        if (iteration->actual != MICROS_GRANT_ERROR_STALE_GRANT) {
            return grant_test_model_fail(fixture, step, "inspect stale diverged");
        }
    } else if (!grant_test_process_handles_equal(
        record->grantor,
        fixture->processes[iteration->grantor]
    )) {
        if (iteration->actual != MICROS_GRANT_ERROR_UNAUTHORIZED) {
            return grant_test_model_fail(fixture, step, "inspect owner diverged");
        }
    } else if (
        iteration->actual != MICROS_GRANT_OK
        || !grant_test_records_equal(record, &observed)
    ) {
        return grant_test_model_fail(fixture, step, "inspect result diverged");
    }
    return true;
}

bool grant_test_model_cancel(
    struct grant_test_fixture *fixture,
    struct grant_model_run *run,
    struct grant_model_iteration *iteration,
    size_t step
)
{
    struct micros_grant_cancel_plan plan;
    size_t index;

    memset(&plan, 0, sizeof(plan));
    iteration->actual = micros_grant_prepare_endpoint_cancel(
        &fixture->grant_registry,
        &fixture->endpoint_registry,
        &fixture->objects,
        fixture->endpoints[iteration->grantor],
        &plan
    );
    if (
        iteration->actual != MICROS_GRANT_OK
        || !grant_test_cancel_plan_matches_reference(
            fixture,
            &plan,
            &run->expected,
            fixture->endpoints[iteration->grantor]
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
            &run->expected.grants[index];

        if (
            record->state == MICROS_GRANT_SLOT_ACTIVE
            && (
                record->grantor_endpoint
                    == fixture->endpoints[iteration->grantor]
                || record->grantee_endpoint
                    == fixture->endpoints[iteration->grantor]
            )
        ) {
            uint32_t generation = record->generation + 1;

            memset(record, 0, sizeof(*record));
            record->state = MICROS_GRANT_SLOT_FREE;
            record->generation = generation;
            --run->expected.active_count;
        }
    }
    return true;
}

bool grant_test_model_close_reuse(
    struct grant_test_fixture *fixture,
    struct grant_model_run *run,
    struct grant_model_iteration *iteration,
    size_t step
)
{
    struct micros_grant_cancel_plan plan;
    micros_endpoint_t old_endpoint = fixture->endpoints[iteration->grantor];
    uint32_t old_generation =
        fixture->processes[iteration->grantor].generation;
    size_t index;

    memset(&plan, 0, sizeof(plan));
    iteration->actual = micros_grant_prepare_endpoint_cancel(
        &fixture->grant_registry,
        &fixture->endpoint_registry,
        &fixture->objects,
        old_endpoint,
        &plan
    );
    if (
        iteration->actual != MICROS_GRANT_OK
        || !grant_test_cancel_plan_matches_reference(
            fixture,
            &plan,
            &run->expected,
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
        || !grant_test_recreate_process(fixture, iteration->grantor)
        || fixture->processes[iteration->grantor].generation
            != old_generation + 1
        || fixture->endpoints[iteration->grantor] == old_endpoint
    ) {
        return grant_test_model_fail(fixture, step, "close reuse failed");
    }
    for (index = 0; index < MICROS_GRANT_CAPACITY; ++index) {
        struct micros_grant_record *record =
            &run->expected.grants[index];

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
            --run->expected.active_count;
        }
    }
    run->expected_process_generation[iteration->grantor] =
        old_generation + 1;
    run->expected_endpoint[iteration->grantor] = fixture->endpoints[iteration->grantor];
    return true;
}

bool grant_test_model_malformed_token(
    struct grant_test_fixture *fixture,
    struct grant_model_iteration *iteration,
    size_t step
)
{
    struct micros_grant_registry snapshot = fixture->grant_registry;
    struct micros_grant_record output;
    struct micros_grant_record sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    output = sentinel;
    iteration->actual = micros_grant_inspect(
        &fixture->grant_registry,
        &fixture->endpoint_registry,
        &fixture->objects,
        fixture->processes[iteration->grantor],
        (iteration->random & 1) ? MICROS_GRANT_NONE : 0,
        &output
    );
    if (
        iteration->actual != MICROS_GRANT_ERROR_ARGUMENT
        || memcmp(&output, &sentinel, sizeof(output)) != 0
        || !grant_test_registries_equal(&fixture->grant_registry, &snapshot)
    ) {
        return grant_test_model_fail(
            fixture,
            step,
            "malformed token changed state or output"
        );
    }
    return true;
}

bool grant_test_model_plan_failure(
    struct grant_test_fixture *fixture,
    struct grant_model_run *run,
    struct grant_model_iteration *iteration,
    size_t step
)
{
    struct micros_grant_cancel_plan plan;
    struct micros_grant_cancel_plan plan_snapshot;
    struct micros_grant_registry snapshot = fixture->grant_registry;

    memset(&plan, 0, sizeof(plan));
    iteration->actual = micros_grant_prepare_endpoint_cancel(
        &fixture->grant_registry,
        &fixture->endpoint_registry,
        &fixture->objects,
        fixture->endpoints[iteration->grantor],
        &plan
    );
    if (
        iteration->actual != MICROS_GRANT_OK
        || !grant_test_cancel_plan_matches_reference(
            fixture,
            &plan,
            &run->expected,
            fixture->endpoints[iteration->grantor]
        )
    ) {
        return grant_test_model_fail(fixture, step, "plan preparation diverged");
    }
    plan.registry_identity++;
    plan_snapshot = plan;
    iteration->actual = micros_grant_commit_endpoint_cancel(
        &fixture->grant_registry,
        &plan
    );
    if (
        iteration->actual != MICROS_GRANT_ERROR_STATE
        || memcmp(&plan, &plan_snapshot, sizeof(plan)) != 0
        || !grant_test_registries_equal(&fixture->grant_registry, &snapshot)
    ) {
        return grant_test_model_fail(
            fixture,
            step,
            "foreign plan changed state"
        );
    }
    return true;
}

bool grant_test_model_invalid_create(
    struct grant_test_fixture *fixture,
    struct grant_model_iteration *iteration,
    size_t step
)
{
    struct micros_grant_registry snapshot = fixture->grant_registry;
    micros_grant_t output = UINT32_C(0xdeadbeef);

    iteration->actual = micros_grant_create(
        &fixture->grant_registry,
        &fixture->endpoint_registry,
        &fixture->objects,
        fixture->processes[iteration->grantor],
        fixture->endpoints[iteration->grantee],
        MICROS_USER_VIRTUAL_BASE,
        1,
        0,
        &output
    );
    if (
        iteration->actual != MICROS_GRANT_ERROR_ARGUMENT
        || output != UINT32_C(0xdeadbeef)
        || !grant_test_registries_equal(&fixture->grant_registry, &snapshot)
    ) {
        return grant_test_model_fail(fixture, step, "invalid create changed state");
    }
    return true;
}
