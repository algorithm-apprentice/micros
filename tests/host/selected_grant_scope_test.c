#include "tests/host/selected_grant_validation_internal.h"

enum scope_operation { SCOPE_INSPECT, SCOPE_CREATE, SCOPE_REVOKE, SCOPE_AUTHORITY };
enum scope_condition {
    SPARSE, UNRELATED_GRANT, UNRELATED_ENDPOINT, COUNT_MISMATCH, ZERO_COUNT,
    STALE_BAD_STORAGE, BAD_STORED_PARTICIPANT, CALLER_PROFILE, CALLER_PRIMARY,
    CALLER_UNBOUND_PROFILE, PREFIX_STORAGE, PREFIX_COUNT, FREE_COUNT,
    QUARANTINE_COUNT, ALL_QUARANTINED, SKIPPED_RELATION, PARTICIPANT_FIRST, SOURCE_ONLY,
};

struct scope_case {
    const char *name;
    enum scope_operation operation;
    enum scope_condition condition;
    size_t occupied;
    size_t slot;
    enum micros_grant_error result;
    enum micros_grant_error diagnostic;
};

static const struct scope_case cases[] = {
    {"inspect-sparse-high", SCOPE_INSPECT, SPARSE, 64, 63, MICROS_GRANT_OK, MICROS_GRANT_OK},
    {"inspect-unrelated-grant", SCOPE_INSPECT, UNRELATED_GRANT, 1, 0, MICROS_GRANT_OK, MICROS_GRANT_ERROR_INVARIANT},
    {"inspect-unrelated-endpoint", SCOPE_INSPECT, UNRELATED_ENDPOINT, 1, 0, MICROS_GRANT_OK, MICROS_GRANT_ERROR_INVARIANT},
    {"inspect-count-mismatch", SCOPE_INSPECT, COUNT_MISMATCH, 1, 0, MICROS_GRANT_OK, MICROS_GRANT_ERROR_INVARIANT},
    {"revoke-zero-count", SCOPE_REVOKE, ZERO_COUNT, 1, 0, MICROS_GRANT_ERROR_INVARIANT, MICROS_GRANT_ERROR_INVARIANT},
    {"inspect-stale-corrupt", SCOPE_INSPECT, STALE_BAD_STORAGE, 1, 0, MICROS_GRANT_ERROR_INVARIANT, MICROS_GRANT_ERROR_INVARIANT},
    {"authority-stored-participant", SCOPE_AUTHORITY, BAD_STORED_PARTICIPANT, 1, 0, MICROS_GRANT_ERROR_INVARIANT, MICROS_GRANT_ERROR_INVARIANT},
    {"inspect-caller-profile", SCOPE_INSPECT, CALLER_PROFILE, 1, 0, MICROS_GRANT_ERROR_INVARIANT, MICROS_GRANT_ERROR_INVARIANT},
    {"inspect-caller-primary", SCOPE_INSPECT, CALLER_PRIMARY, 1, 0, MICROS_GRANT_ERROR_INVARIANT, MICROS_GRANT_ERROR_INVARIANT},
    {"inspect-unbound-profile", SCOPE_INSPECT, CALLER_UNBOUND_PROFILE, 1, 0, MICROS_GRANT_ERROR_INVARIANT, MICROS_GRANT_ERROR_INVARIANT},
    {"create-unvisited-suffix", SCOPE_CREATE, UNRELATED_GRANT, 0, 0, MICROS_GRANT_OK, MICROS_GRANT_ERROR_INVARIANT},
    {"create-prefix-storage", SCOPE_CREATE, PREFIX_STORAGE, 1, 1, MICROS_GRANT_ERROR_INVARIANT, MICROS_GRANT_ERROR_INVARIANT},
    {"create-prefix-count", SCOPE_CREATE, PREFIX_COUNT, 1, 1, MICROS_GRANT_ERROR_INVARIANT, MICROS_GRANT_ERROR_INVARIANT},
    {"create-full-count", SCOPE_CREATE, FREE_COUNT, 0, 0, MICROS_GRANT_ERROR_INVARIANT, MICROS_GRANT_ERROR_INVARIANT},
    {"create-quarantine-count", SCOPE_CREATE, QUARANTINE_COUNT, 64, 0, MICROS_GRANT_ERROR_INVARIANT, MICROS_GRANT_ERROR_INVARIANT},
    {"create-all-quarantined", SCOPE_CREATE, ALL_QUARANTINED, 0, 0, MICROS_GRANT_ERROR_CAPACITY, MICROS_GRANT_OK},
    {"create-skipped-relation", SCOPE_CREATE, SKIPPED_RELATION, 1, 1, MICROS_GRANT_OK, MICROS_GRANT_ERROR_INVARIANT},
    {"create-participant-before-prefix", SCOPE_CREATE, PARTICIPANT_FIRST, 1, 1, MICROS_GRANT_ERROR_DEAD_ENDPOINT, MICROS_GRANT_ERROR_INVARIANT},
    {"inspect-source-only", SCOPE_INSPECT, SOURCE_ONLY, 1, 0, MICROS_GRANT_ERROR_STATE, MICROS_GRANT_OK},
};

static void quarantine(struct micros_grant_record *record, size_t slot)
{
    memset(record, 0, sizeof(*record));
    record->state = MICROS_GRANT_SLOT_QUARANTINED;
    record->generation = MICROS_GRANT_GENERATION_MAX - (slot == 63);
}

static bool scope_setup(const struct scope_case *test)
{
    struct grant01_fixture *fixture = grant01_fixture();

    REQUIRE(grant01_setup(test->occupied, 3));
    switch (test->condition) {
    case SPARSE:
        for (size_t slot = 0; slot < 63; ++slot) {
            REQUIRE(micros_grant_revoke(
                &fixture->grants, &fixture->endpoints, &fixture->objects, fixture->processes[0],
                (micros_grant_t)(64 + slot)
            ) == MICROS_GRANT_OK);
        }
        break;
    case UNRELATED_GRANT: fixture->grants.grants[63].grantor.slot = 1; break;
    case UNRELATED_ENDPOINT: fixture->endpoints.endpoints[2].owner.generation = 0; break;
    case COUNT_MISMATCH: fixture->grants.active_count = 2; break;
    case ZERO_COUNT:
    case PREFIX_COUNT: fixture->grants.active_count = 0; break;
    case STALE_BAD_STORAGE:
    case PREFIX_STORAGE:
    case PARTICIPANT_FIRST: fixture->grants.grants[0].permissions = 0; break;
    case BAD_STORED_PARTICIPANT:
        fixture->grants.grants[0].grantee_endpoint += UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS;
        break;
    case CALLER_PROFILE: fixture->objects.processes[2].privilege_profile = MICROS_PRIVILEGE_PROFILE_CAPACITY; break;
    case CALLER_PRIMARY: fixture->objects.processes[2].primary_endpoint = fixture->identities[0]; break;
    case CALLER_UNBOUND_PROFILE: fixture->objects.processes[2].primary_endpoint = MICROS_PROCESS_ENDPOINT_NONE; break;
    case FREE_COUNT: fixture->grants.active_count = MICROS_GRANT_CAPACITY; break;
    case QUARANTINE_COUNT: quarantine(&fixture->grants.grants[63], 63); break;
    case ALL_QUARANTINED:
        for (size_t slot = 0; slot < MICROS_GRANT_CAPACITY; ++slot) {
            quarantine(&fixture->grants.grants[slot], slot);
        }
        break;
    case SKIPPED_RELATION: ++fixture->grants.grants[0].grantor.generation; break;
    case SOURCE_ONLY: fixture->endpoints.endpoints[0].state = MICROS_ENDPOINT_STATE_SOURCE_ONLY; break;
    }
    grant01_snapshot();
    return true;
}

static bool create_effects(const struct scope_case *test, micros_grant_t token)
{
    const struct grant01_fixture *fixture = grant01_fixture();
    const struct micros_grant_record *record = &fixture->grants.grants[test->slot];

    REQUIRE(token == 64 + test->slot);
    REQUIRE(fixture->grants.active_count == fixture->before_grants.active_count + 1);
    REQUIRE(record->state == MICROS_GRANT_SLOT_ACTIVE && record->generation == 1
            && record->grantor.slot == 0 && record->grantor.generation == 1
            && record->grantor_endpoint == fixture->identities[0]
            && record->grantee_endpoint == fixture->identities[1]
            && record->base == MICROS_USER_VIRTUAL_BASE && record->length == 64
            && record->permissions == MICROS_GRANT_PERMISSION_READ);
    for (size_t slot = 0; slot < MICROS_GRANT_CAPACITY; ++slot) {
        if (slot != test->slot) {
            REQUIRE(memcmp(&fixture->grants.grants[slot], &fixture->before_grants.grants[slot],
                           sizeof(*record)) == 0);
        }
    }
    return true;
}

static bool run_scope(size_t index, const struct scope_case *test)
{
    struct grant01_fixture *fixture = grant01_fixture();
    struct micros_grant_record output, before_output;
    struct micros_grant_copy_authority authority, before_authority;
    struct micros_process_handle caller;
    enum micros_grant_error result;
    micros_grant_t token = MICROS_GRANT_NONE;
    uint64_t value = 0;
    bool preserved = test->operation != SCOPE_CREATE || test->result != MICROS_GRANT_OK;

    REQUIRE(scope_setup(test));
    caller = fixture->processes[test->condition == CALLER_PROFILE || test->condition == CALLER_PRIMARY
                               || test->condition == CALLER_UNBOUND_PROFILE ? 2 : 0];
    if (test->condition == PARTICIPANT_FIRST) {
        ++caller.generation;
    }
    memset(&output, 0xa5, sizeof(output));
    before_output = output;
    memset(&authority, 0xa5, sizeof(authority));
    before_authority = authority;
    REQUIRE(grant01_begin(index));
    switch (test->operation) {
    case SCOPE_CREATE:
        result = micros_grant_create(
            &fixture->grants, &fixture->endpoints, &fixture->objects, caller, fixture->identities[1],
            MICROS_USER_VIRTUAL_BASE, 64, MICROS_GRANT_PERMISSION_READ, &token
        );
        value = token;
        break;
    case SCOPE_INSPECT:
        result = micros_grant_inspect(
            &fixture->grants, &fixture->endpoints, &fixture->objects, caller,
            test->condition == STALE_BAD_STORAGE ? 128 : (micros_grant_t)(64 + test->slot), &output
        );
        if (result == MICROS_GRANT_OK) {
            value = output.length;
        }
        break;
    case SCOPE_REVOKE:
        result = micros_grant_revoke(
            &fixture->grants, &fixture->endpoints, &fixture->objects, caller, 64
        );
        break;
    default:
        result = micros_grant_prepare_copy_authority(
            &fixture->grants, &fixture->endpoints, &fixture->objects, fixture->processes[1],
            fixture->identities[0], 64, 0, 32, MICROS_GRANT_PERMISSION_READ, &authority
        );
        break;
    }
    REQUIRE(grant01_finish());
    REQUIRE(result == test->result);
    if (test->operation == SCOPE_CREATE && result == MICROS_GRANT_OK) {
        REQUIRE(create_effects(test, token));
    } else if (test->operation == SCOPE_INSPECT && result == MICROS_GRANT_OK) {
        REQUIRE(output.state == MICROS_GRANT_SLOT_ACTIVE && output.generation == 1
                && output.grantor.slot == 0 && output.grantor.generation == 1
                && output.grantor_endpoint == fixture->identities[0]
                && output.grantee_endpoint == fixture->identities[1]
                && output.base == MICROS_USER_VIRTUAL_BASE + test->slot * 64
                && output.length == 64 && output.permissions == MICROS_GRANT_PERMISSION_DEFINED_MASK);
    } else {
        REQUIRE(memcmp(&output, &before_output, sizeof(output)) == 0);
        REQUIRE(memcmp(&authority, &before_authority, sizeof(authority)) == 0);
    }
    REQUIRE(micros_grant_registry_validate(&fixture->grants, &fixture->endpoints, &fixture->objects)
            == test->diagnostic);
    return grant01_report(index, test->name, result, value, preserved);
}

bool grant01_scope_cases(size_t *count)
{
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        REQUIRE(run_scope(*count, &cases[index]));
        ++*count;
    }
    return true;
}
