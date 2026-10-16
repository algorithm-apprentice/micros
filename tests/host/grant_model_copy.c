#include "grant_test_internal.h"

struct grant_model_authorization {
    struct micros_grant_registry snapshot;
    struct micros_grant_copy_authority authority;
    struct micros_grant_copy_authority sentinel;
    struct micros_grant_copy_range_plan source_plan;
    struct micros_grant_copy_range_plan destination_plan;
    const struct micros_grant_record *record;
    struct micros_process_handle candidate_grantee;
    micros_endpoint_t candidate_grantor;
    enum micros_grant_error expected_result;
    size_t selected_slot;
    size_t record_grantor;
    size_t record_grantee;
    size_t index;
};

static void grant_test_model_select_reference(
    struct grant_model_run *run,
    struct grant_model_iteration *iteration,
    struct grant_model_authorization *work
)
{
    if (iteration->operation == GRANT_MODEL_AUTHORIZE_STALE) {
        work->selected_slot = iteration->random % MICROS_GRANT_CAPACITY;
    } else {
        for (
            work->index = 0;
            work->index < MICROS_GRANT_CAPACITY;
            ++work->index
        ) {
            if (
                run->expected.grants[work->index].state
                    == MICROS_GRANT_SLOT_ACTIVE
            ) {
                work->selected_slot = work->index;
                break;
            }
        }
        if (work->selected_slot == MICROS_GRANT_CAPACITY) {
            work->selected_slot = iteration->random % MICROS_GRANT_CAPACITY;
        }
    }
    iteration->slot = work->selected_slot;
    work->record = &run->expected.grants[iteration->slot];
    iteration->grant = (
        work->record->generation << MICROS_GRANT_SLOT_BITS
    ) | (micros_grant_t)iteration->slot;
    if (
        iteration->operation == GRANT_MODEL_AUTHORIZE_STALE
        && work->record->state == MICROS_GRANT_SLOT_ACTIVE
    ) {
        uint32_t stale_generation =
            work->record->generation == 1
                ? 2
                : work->record->generation - 1;

        iteration->grant = (
            stale_generation << MICROS_GRANT_SLOT_BITS
        ) | (micros_grant_t)iteration->slot;
    }
    for (work->index = 0; work->index < sizeof(run->grantor_bytes); ++work->index) {
        run->grantor_bytes[work->index] =
            (unsigned char)(iteration->random + work->index);
        run->grantee_bytes[work->index] =
            (unsigned char)(UINT8_C(0xc0) - work->index);
        run->expected_grantor_bytes[work->index] =
            run->grantor_bytes[work->index];
        run->expected_grantee_bytes[work->index] =
            run->grantee_bytes[work->index];
    }
}

static bool grant_test_model_select_participants(
    struct grant_test_fixture *fixture,
    struct grant_model_iteration *iteration,
    struct grant_model_authorization *work,
    size_t step
)
{
    for (
        work->index = 0;
        work->index < GRANT_PROCESS_COUNT;
        ++work->index
    ) {
        if (grant_test_process_handles_equal(
            work->record->grantor,
            fixture->processes[work->index]
        )) {
            work->record_grantor = work->index;
        }
        if (
            work->record->grantee_endpoint
                == fixture->endpoints[work->index]
        ) {
            work->record_grantee = work->index;
        }
    }
    if (
        work->record_grantor == GRANT_PROCESS_COUNT
        || work->record_grantee == GRANT_PROCESS_COUNT
        || work->record->grantor_endpoint
            != fixture->endpoints[work->record_grantor]
    ) {
        return grant_test_model_fail(
            fixture,
            step,
            "active participant reference diverged"
        );
    }
    iteration->grantor = work->record_grantor;
    iteration->grantee = work->record_grantee;
    work->candidate_grantee = fixture->processes[work->record_grantee];
    work->candidate_grantor = work->record->grantor_endpoint;
    return true;
}

static bool grant_test_model_expect_authority(
    struct grant_test_fixture *fixture,
    struct grant_model_run *run,
    struct grant_model_iteration *iteration,
    struct grant_model_authorization *work,
    size_t step
)
{
    if (
        work->record->state != MICROS_GRANT_SLOT_ACTIVE
        || iteration->operation == GRANT_MODEL_AUTHORIZE_STALE
    ) {
        work->expected_result = MICROS_GRANT_ERROR_STALE_GRANT;
        iteration->trace_permission = MICROS_GRANT_PERMISSION_READ;
    } else {
        size_t limit =
            work->record->length < sizeof(run->grantor_bytes)
                ? work->record->length
                : sizeof(run->grantor_bytes);

        if (!grant_test_model_select_participants(fixture, iteration, work, step)) {
            return false;
        }
        iteration->trace_length = limit == 0
            ? 0
            : (iteration->random >> 16) % (limit + 1);
        iteration->trace_offset =
            (iteration->random >> 8)
            % (work->record->length - iteration->trace_length + 1);
        switch (iteration->operation) {
        case GRANT_MODEL_AUTHORIZE_FROM:
            iteration->trace_permission = MICROS_GRANT_PERMISSION_READ;
            break;
        case GRANT_MODEL_AUTHORIZE_TO:
            iteration->trace_permission = MICROS_GRANT_PERMISSION_WRITE;
            break;
        case GRANT_MODEL_AUTHORIZE_DENIAL:
            iteration->trace_permission = (
                work->record->permissions
                    & MICROS_GRANT_PERMISSION_READ
            )
                ? MICROS_GRANT_PERMISSION_READ
                : MICROS_GRANT_PERMISSION_WRITE;
            iteration->grantee =
                (work->record_grantee + 1) % GRANT_PROCESS_COUNT;
            work->candidate_grantee = fixture->processes[iteration->grantee];
            break;
        case GRANT_MODEL_AUTHORIZE_BOUNDS:
            iteration->trace_permission = (
                work->record->permissions
                    & MICROS_GRANT_PERMISSION_READ
            )
                ? MICROS_GRANT_PERMISSION_READ
                : MICROS_GRANT_PERMISSION_WRITE;
            iteration->trace_offset = work->record->length;
            iteration->trace_length = 1;
            break;
        default:
            return grant_test_model_fail(
                fixture,
                step,
                "unexpected authorize operation"
            );
        }
        if (iteration->operation == GRANT_MODEL_AUTHORIZE_DENIAL) {
            work->expected_result = MICROS_GRANT_ERROR_UNAUTHORIZED;
        } else if (
            (work->record->permissions & iteration->trace_permission) == 0
        ) {
            work->expected_result = MICROS_GRANT_ERROR_UNAUTHORIZED;
        } else if (
            iteration->operation == GRANT_MODEL_AUTHORIZE_BOUNDS
        ) {
            work->expected_result = MICROS_GRANT_ERROR_RANGE;
        } else {
            work->expected_result = MICROS_GRANT_OK;
        }
    }
    return true;
}

static bool grant_test_model_observe_authority(
    struct grant_test_fixture *fixture,
    struct grant_model_iteration *iteration,
    struct grant_model_authorization *work,
    size_t step
)
{
    memset(&work->sentinel, 0xa5, sizeof(work->sentinel));
    work->authority = work->sentinel;
    iteration->actual = micros_grant_prepare_copy_authority(
        &fixture->grant_registry,
        &fixture->endpoint_registry,
        &fixture->objects,
        work->candidate_grantee,
        work->candidate_grantor,
        iteration->grant,
        iteration->trace_offset,
        iteration->trace_length,
        iteration->trace_permission,
        &work->authority
    );
    if (
        iteration->actual != work->expected_result
        || !grant_test_registries_equal(&fixture->grant_registry, &work->snapshot)
    ) {
        return grant_test_model_fail(
            fixture,
            step,
            "copy authorization result diverged"
        );
    }
    return true;
}

static bool grant_test_model_denial_outputs(
    struct grant_test_fixture *fixture,
    struct grant_model_run *run,
    struct grant_model_authorization *work,
    size_t step
)
{
    if (
        memcmp(&work->authority, &work->sentinel, sizeof(work->authority)) != 0
        || memcmp(
            run->grantor_bytes,
            run->expected_grantor_bytes,
            sizeof(run->grantor_bytes)
        ) != 0
        || memcmp(
            run->grantee_bytes,
            run->expected_grantee_bytes,
            sizeof(run->grantee_bytes)
        ) != 0
    ) {
        return grant_test_model_fail(
            fixture,
            step,
            "failed authorization changed output or bytes"
        );
    }
    return true;
}

static bool grant_test_model_authority_output(
    struct grant_test_fixture *fixture,
    struct grant_model_iteration *iteration,
    struct grant_model_authorization *work,
    size_t step
)
{
    if (
        !grant_test_process_handles_equal(
            work->authority.grantor,
            work->record->grantor
        )
        || !grant_test_process_handles_equal(
            work->authority.grantee,
            fixture->processes[work->record_grantee]
        )
        || work->authority.remote_address
            != work->record->base + iteration->trace_offset
        || work->authority.length != iteration->trace_length
        || work->authority.required_permission != iteration->trace_permission
    ) {
        return grant_test_model_fail(
            fixture,
            step,
            "copy authority output diverged"
        );
    }
    return true;
}

static bool grant_test_model_copy_bytes(
    struct grant_test_fixture *fixture,
    struct grant_model_run *run,
    struct grant_model_iteration *iteration,
    struct grant_model_authorization *work,
    size_t step
)
{
    if (iteration->trace_length != 0) {
        work->source_plan.chunk_count = 1;
        work->destination_plan.chunk_count = 1;
        work->source_plan.chunks[0].length = iteration->trace_length;
        work->destination_plan.chunks[0].length = iteration->trace_length;
        if (
            iteration->trace_permission == MICROS_GRANT_PERMISSION_READ
        ) {
            work->source_plan.chunks[0].physical_address =
                (uintptr_t)&run->grantor_bytes[0];
            work->destination_plan.chunks[0].physical_address =
                (uintptr_t)&run->grantee_bytes[0];
            memcpy(
                run->expected_grantee_bytes,
                run->expected_grantor_bytes,
                iteration->trace_length
            );
        } else {
            work->source_plan.chunks[0].physical_address =
                (uintptr_t)&run->grantee_bytes[0];
            work->destination_plan.chunks[0].physical_address =
                (uintptr_t)&run->grantor_bytes[0];
            memcpy(
                run->expected_grantor_bytes,
                run->expected_grantee_bytes,
                iteration->trace_length
            );
        }
    }
    if (
        micros_grant_copy_plan_validate(
            &work->source_plan,
            &work->destination_plan,
            iteration->trace_length
        ) != MICROS_GRANT_OK
    ) {
        return grant_test_model_fail(fixture, step, "copy plan rejected");
    }
    micros_grant_copy_commit(
        &work->source_plan,
        &work->destination_plan,
        iteration->trace_length
    );
    if (
        memcmp(
            run->grantor_bytes,
            run->expected_grantor_bytes,
            sizeof(run->grantor_bytes)
        ) != 0
        || memcmp(
            run->grantee_bytes,
            run->expected_grantee_bytes,
            sizeof(run->grantee_bytes)
        ) != 0
    ) {
        return grant_test_model_fail(fixture, step, "copy bytes diverged");
    }
    return true;
}

bool grant_test_model_authorize(
    struct grant_test_fixture *fixture,
    struct grant_model_run *run,
    struct grant_model_iteration *iteration,
    size_t step
)
{
    struct grant_model_authorization work;
    work.snapshot = fixture->grant_registry;
    work.source_plan = (struct micros_grant_copy_range_plan){0};
    work.destination_plan = (struct micros_grant_copy_range_plan){0};
    work.candidate_grantee = fixture->processes[0];
    work.candidate_grantor = fixture->endpoints[0];
    work.selected_slot = MICROS_GRANT_CAPACITY;
    work.record_grantor = GRANT_PROCESS_COUNT;
    work.record_grantee = GRANT_PROCESS_COUNT;

    grant_test_model_select_reference(run, iteration, &work);
    if (!grant_test_model_expect_authority(fixture, run, iteration, &work, step)) {
        return false;
    }
    if (!grant_test_model_observe_authority(fixture, iteration, &work, step)) {
        return false;
    }
    if (iteration->actual != MICROS_GRANT_OK) {
        if (!grant_test_model_denial_outputs(fixture, run, &work, step)) {
            return false;
        }
        return true;
    }
    if (!grant_test_model_authority_output(fixture, iteration, &work, step)) {
        return false;
    }
    return grant_test_model_copy_bytes(fixture, run, iteration, &work, step);
}
