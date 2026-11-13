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
    {"create-quarantine-count", SCOPE_CREATE, QUARANTINE_COUNT, 64, 0, MICROS_GRANT_ERROR_CAPACITY, MICROS_GRANT_ERROR_INVARIANT},
    {"create-all-quarantined", SCOPE_CREATE, ALL_QUARANTINED, 0, 0, MICROS_GRANT_ERROR_CAPACITY, MICROS_GRANT_OK},
    {"create-skipped-relation", SCOPE_CREATE, SKIPPED_RELATION, 3, 3, MICROS_GRANT_OK, MICROS_GRANT_ERROR_INVARIANT},
    {"create-participant-before-prefix", SCOPE_CREATE, PARTICIPANT_FIRST, 1, 1, MICROS_GRANT_ERROR_DEAD_ENDPOINT, MICROS_GRANT_ERROR_INVARIANT},
    {"inspect-source-only", SCOPE_INSPECT, SOURCE_ONLY, 1, 0, MICROS_GRANT_ERROR_STATE, MICROS_GRANT_OK},
};

static void quarantine(struct micros_grant_record *record, size_t slot)
{
    memset(record, 0, sizeof(*record));
    record->state = MICROS_GRANT_SLOT_QUARANTINED;
    record->generation = MICROS_GRANT_GENERATION_MAX - (slot == 63);
    record->free_next = MICROS_GRANT_NONE;
    record->grantor_link = (struct micros_grant_link){MICROS_GRANT_NONE, MICROS_GRANT_NONE};
    record->grantee_link = record->grantor_link;
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
    case QUARANTINE_COUNT:
        REQUIRE(micros_grant_revoke(
            &fixture->grants, &fixture->endpoints, &fixture->objects, fixture->processes[0], 127
        ) == MICROS_GRANT_OK);
        quarantine(&fixture->grants.grants[63], 63);
        fixture->grants.free_head = MICROS_GRANT_NONE;
        fixture->grants.active_count = 64;
        break;
    case ALL_QUARANTINED:
        for (size_t slot = 0; slot < MICROS_GRANT_CAPACITY; ++slot) {
            quarantine(&fixture->grants.grants[slot], slot);
        }
        fixture->grants.free_head = MICROS_GRANT_NONE;
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
            struct micros_grant_record expected = fixture->before_grants.grants[slot];

            if (test->occupied != 0 && slot == test->occupied - 1) {
                expected.grantor_link.previous = token;
                expected.grantee_link.previous = token;
            }
            REQUIRE(memcmp(&fixture->grants.grants[slot], &expected, sizeof(*record)) == 0);
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

enum index_case_kind {
    INDEX_FREE_SHAPE, INDEX_FREE_STALE, INDEX_FREE_ACTIVE, INDEX_FREE_CYCLE, INDEX_ROOT_TAG,
    INDEX_REVOKE_STALE, INDEX_REVOKE_RECIPROCAL, INDEX_INSPECT_UNCONSUMED,
    INDEX_PREPARE_CYCLE, INDEX_PREPARE_ROLE, INDEX_PREPARE_POISON,
    INDEX_COMMIT_EXTRA, INDEX_COMMIT_MISSING, INDEX_COMMIT_GENERATION,
    INDEX_COMMIT_EMPTY, INDEX_COMMIT_SPARSE, INDEX_COMMIT_DENSE, INDEX_COMMIT_MIXED, INDEX_COMMIT_CLOSED,
    INDEX_COMMIT_QUARANTINE,
};

struct index_case {
    const char *name;
    enum index_case_kind kind;
    enum micros_grant_error result;
};

static const struct index_case index_cases[] = {
    {"index-free-shape", INDEX_FREE_SHAPE, MICROS_GRANT_ERROR_INVARIANT},
    {"index-free-stale", INDEX_FREE_STALE, MICROS_GRANT_ERROR_INVARIANT},
    {"index-free-active", INDEX_FREE_ACTIVE, MICROS_GRANT_ERROR_INVARIANT},
    {"index-free-cycle", INDEX_FREE_CYCLE, MICROS_GRANT_ERROR_INVARIANT},
    {"index-root-tag", INDEX_ROOT_TAG, MICROS_GRANT_ERROR_INVARIANT},
    {"index-revoke-stale", INDEX_REVOKE_STALE, MICROS_GRANT_ERROR_INVARIANT},
    {"index-revoke-reciprocal", INDEX_REVOKE_RECIPROCAL, MICROS_GRANT_ERROR_INVARIANT},
    {"index-inspect-unconsumed", INDEX_INSPECT_UNCONSUMED, MICROS_GRANT_OK},
    {"index-prepare-cycle", INDEX_PREPARE_CYCLE, MICROS_GRANT_ERROR_INVARIANT},
    {"index-prepare-role", INDEX_PREPARE_ROLE, MICROS_GRANT_ERROR_INVARIANT},
    {"cancel-prepare-poison", INDEX_PREPARE_POISON, MICROS_GRANT_OK},
    {"cancel-commit-extra", INDEX_COMMIT_EXTRA, MICROS_GRANT_ERROR_INVARIANT},
    {"cancel-commit-missing", INDEX_COMMIT_MISSING, MICROS_GRANT_ERROR_INVARIANT},
    {"cancel-commit-generation", INDEX_COMMIT_GENERATION, MICROS_GRANT_ERROR_INVARIANT},
    {"cancel-commit-empty", INDEX_COMMIT_EMPTY, MICROS_GRANT_OK},
    {"cancel-commit-sparse", INDEX_COMMIT_SPARSE, MICROS_GRANT_OK},
    {"cancel-commit-dense", INDEX_COMMIT_DENSE, MICROS_GRANT_OK},
    {"cancel-commit-mixed", INDEX_COMMIT_MIXED, MICROS_GRANT_OK},
    {"cancel-commit-closed", INDEX_COMMIT_CLOSED, MICROS_GRANT_OK},
    {"cancel-commit-quarantine", INDEX_COMMIT_QUARANTINE, MICROS_GRANT_OK},
};

static bool prepare_index_case(const struct index_case *test, uint64_t *selected)
{
    struct grant01_fixture *fixture = grant01_fixture();
    const unsigned char mixed_grantor[] = {0, 1, 1, 0, 2, 1};
    const unsigned char mixed_grantee[] = {1, 2, 0, 2, 1, 0};
    size_t count = test->kind < INDEX_PREPARE_POISON ? 3
        : test->kind >= INDEX_COMMIT_MIXED ? 6 : 64;

    REQUIRE(grant01_setup(0, 3));
    *selected = 0;
    for (size_t slot = 0; slot < count; ++slot) {
        size_t grantor = 0, grantee = 1;
        micros_grant_t grant;

        if (test->kind >= INDEX_COMMIT_MIXED) {
            grantor = mixed_grantor[slot];
            grantee = mixed_grantee[slot];
        } else if (test->kind >= INDEX_PREPARE_POISON && test->kind != INDEX_COMMIT_DENSE
                   && (slot != 0 || test->kind == INDEX_COMMIT_EMPTY)) {
            grantor = 1;
            grantee = 2;
        }
        REQUIRE(micros_grant_create(
            &fixture->grants, &fixture->endpoints, &fixture->objects, fixture->processes[grantor],
            fixture->identities[grantee], MICROS_USER_VIRTUAL_BASE + slot * 64, 64,
            MICROS_GRANT_PERMISSION_DEFINED_MASK, &grant
        ) == MICROS_GRANT_OK && grant == 64 + slot);
        if (grantor == 0 || grantee == 0) {
            *selected |= UINT64_C(1) << slot;
        }
    }
    if (test->kind == INDEX_COMMIT_QUARANTINE) {
        micros_grant_t terminal = (MICROS_GRANT_GENERATION_MAX << MICROS_GRANT_SLOT_BITS) | 5;

        fixture->grants.grants[5].generation = MICROS_GRANT_GENERATION_MAX;
        fixture->grants.participants[0].head = terminal;
        fixture->grants.participants[1].head = terminal;
        fixture->grants.grants[3].grantor_link.previous = terminal;
        fixture->grants.grants[4].grantee_link.previous = terminal;
    }
    REQUIRE(micros_grant_registry_validate(&fixture->grants, &fixture->endpoints, &fixture->objects)
            == MICROS_GRANT_OK);
    return true;
}

static void damage_index_case(const struct index_case *test, struct micros_grant_cancel_plan *plan)
{
    struct grant01_fixture *fixture = grant01_fixture();

    switch (test->kind) {
    case INDEX_FREE_SHAPE: fixture->grants.free_head = 0; break;
    case INDEX_FREE_STALE: fixture->grants.free_head = 131; break;
    case INDEX_FREE_ACTIVE: fixture->grants.free_head = 64; break;
    case INDEX_FREE_CYCLE: fixture->grants.grants[4].free_next = 67; break;
    case INDEX_ROOT_TAG: fixture->grants.participants[0].endpoint += UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS; break;
    case INDEX_REVOKE_STALE: fixture->grants.grants[1].grantor_link.previous = 130; break;
    case INDEX_REVOKE_RECIPROCAL: fixture->grants.grants[2].grantor_link.next = 64; break;
    case INDEX_INSPECT_UNCONSUMED: fixture->grants.grants[0].grantor_link.next = 128; break;
    case INDEX_PREPARE_CYCLE: fixture->grants.grants[0].grantor_link.next = 66; break;
    case INDEX_PREPARE_ROLE:
        fixture->grants.participants[2] = (struct micros_grant_endpoint_index){fixture->identities[2], 64};
        break;
    case INDEX_COMMIT_EXTRA: plan->selected_slots |= UINT64_C(2); break;
    case INDEX_COMMIT_MISSING: plan->selected_slots = 0; break;
    case INDEX_COMMIT_GENERATION: ++plan->expected_generation[0]; break;
    default: break;
    }
}

static bool check_sparse_plan(
    const struct index_case *test, const struct micros_grant_cancel_plan *plan,
    const struct micros_grant_cancel_plan *before, uint64_t selected
)
{
    const struct grant01_fixture *fixture = grant01_fixture();
    struct micros_grant_cancel_plan expected = *before;

    expected.active = true;
    expected.registry_identity = (uintptr_t)&fixture->grants;
    expected.endpoint = fixture->identities[0];
    expected.selected_slots = selected;
    for (size_t slot = 0; slot < MICROS_GRANT_CAPACITY; ++slot) {
        if ((selected & (UINT64_C(1) << slot)) != 0) {
            expected.expected_generation[slot] = test->kind == INDEX_COMMIT_QUARANTINE && slot == 5
                ? MICROS_GRANT_GENERATION_MAX : 1;
        }
    }
    REQUIRE(memcmp(plan, &expected, sizeof(expected)) == 0);
    return true;
}

static bool check_cancellation_graph(uint64_t selected)
{
    const struct grant01_fixture *fixture = grant01_fixture();
    struct micros_grant_registry expected = fixture->before_grants;
    const struct micros_grant_link empty = {MICROS_GRANT_NONE, MICROS_GRANT_NONE};

    for (size_t slot = 0; slot < MICROS_GRANT_CAPACITY; ++slot) {
        struct micros_grant_record *record = &expected.grants[slot];

        if ((selected & (UINT64_C(1) << slot)) != 0) {
            uint32_t generation = record->generation;
            bool terminal = generation == MICROS_GRANT_GENERATION_MAX;

            memset(record, 0, sizeof(*record));
            record->state = terminal ? MICROS_GRANT_SLOT_QUARANTINED : MICROS_GRANT_SLOT_FREE;
            record->generation = terminal ? generation : generation + 1;
            record->free_next = terminal ? MICROS_GRANT_NONE : expected.free_head;
            if (!terminal) {
                expected.free_head = (record->generation << MICROS_GRANT_SLOT_BITS) | (uint32_t)slot;
            }
            --expected.active_count;
        }
        record->grantor_link = record->grantee_link = empty;
    }
    for (size_t actor = 0; actor < MICROS_PROCESS_CAPACITY; ++actor) {
        expected.participants[actor] = (struct micros_grant_endpoint_index){MICROS_ENDPOINT_NONE, MICROS_GRANT_NONE};
    }
    for (size_t slot = 0; slot < MICROS_GRANT_CAPACITY; ++slot) {
        struct micros_grant_record *record = &expected.grants[slot];

        if (record->state != MICROS_GRANT_SLOT_ACTIVE) {
            continue;
        }
        for (size_t role = 0; role < 2; ++role) {
            micros_endpoint_t endpoint = role == 0 ? record->grantor_endpoint : record->grantee_endpoint;
            struct micros_grant_link *link = role == 0 ? &record->grantor_link : &record->grantee_link;
            size_t previous = MICROS_GRANT_CAPACITY, next = MICROS_GRANT_CAPACITY;

            for (size_t other = 0; other < MICROS_GRANT_CAPACITY; ++other) {
                const struct micros_grant_record *peer = &expected.grants[other];

                if (peer->state != MICROS_GRANT_SLOT_ACTIVE
                    || (peer->grantor_endpoint != endpoint && peer->grantee_endpoint != endpoint)) {
                    continue;
                }
                if (other > slot && other < previous) {
                    previous = other;
                }
                if (other < slot && (next == MICROS_GRANT_CAPACITY || other > next)) {
                    next = other;
                }
            }
            link->previous = previous == MICROS_GRANT_CAPACITY ? MICROS_GRANT_NONE : (uint32_t)(64 + previous);
            link->next = next == MICROS_GRANT_CAPACITY ? MICROS_GRANT_NONE : (uint32_t)(64 + next);
            if (previous == MICROS_GRANT_CAPACITY) {
                size_t actor = endpoint & ((UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) - 1);
                expected.participants[actor] = (struct micros_grant_endpoint_index){endpoint, (uint32_t)(64 + slot)};
            }
        }
    }
    REQUIRE(memcmp(&expected, &fixture->grants, sizeof(expected)) == 0);
    return true;
}

static bool run_index_case(size_t index, const struct index_case *test)
{
    struct grant01_fixture *fixture = grant01_fixture();
    struct micros_grant_cancel_plan plan, before_plan;
    struct micros_grant_record output, before_output;
    micros_grant_t created = MICROS_GRANT_NONE;
    enum micros_grant_error result;
    uint64_t selected, value = 0;
    bool committing = test->kind >= INDEX_COMMIT_EXTRA;
    bool preserved;

    REQUIRE(prepare_index_case(test, &selected));
    memset(&plan, 0xa5, sizeof(plan));
    plan.active = false;
    before_plan = plan;
    memset(&output, 0xa5, sizeof(output));
    before_output = output;
    if (committing) {
        REQUIRE(micros_grant_prepare_endpoint_cancel(
            &fixture->grants, &fixture->endpoints, &fixture->objects, fixture->identities[0], &plan
        ) == MICROS_GRANT_OK);
        REQUIRE(check_sparse_plan(test, &plan, &before_plan, selected));
    }
    if (test->kind == INDEX_COMMIT_CLOSED) {
        REQUIRE(micros_endpoint_close(&fixture->endpoints, &fixture->objects, fixture->identities[0])
                == MICROS_ENDPOINT_OK);
        REQUIRE(micros_grant_registry_validate(&fixture->grants, &fixture->endpoints, &fixture->objects)
                == MICROS_GRANT_ERROR_INVARIANT);
    }
    damage_index_case(test, &plan);
    before_plan = plan;
    grant01_snapshot();
    REQUIRE(grant01_begin(index));
    if (test->kind <= INDEX_ROOT_TAG) {
        result = micros_grant_create(
            &fixture->grants, &fixture->endpoints, &fixture->objects, fixture->processes[0],
            fixture->identities[1], MICROS_USER_VIRTUAL_BASE, 64, MICROS_GRANT_PERMISSION_READ, &created
        );
    } else if (test->kind <= INDEX_REVOKE_RECIPROCAL) {
        result = micros_grant_revoke(&fixture->grants, &fixture->endpoints, &fixture->objects, fixture->processes[0], 65);
    } else if (test->kind == INDEX_INSPECT_UNCONSUMED) {
        result = micros_grant_inspect(&fixture->grants, &fixture->endpoints, &fixture->objects, fixture->processes[0], 64, &output);
        value = result == MICROS_GRANT_OK ? output.length : 0;
    } else if (!committing) {
        result = micros_grant_prepare_endpoint_cancel(
            &fixture->grants, &fixture->endpoints, &fixture->objects,
            fixture->identities[test->kind == INDEX_PREPARE_ROLE ? 2 : 0], &plan
        );
        value = result == MICROS_GRANT_OK ? plan.selected_slots : 0;
    } else {
        result = micros_grant_commit_endpoint_cancel(&fixture->grants, &plan);
        value = result == MICROS_GRANT_OK ? fixture->grants.active_count : 0;
    }
    REQUIRE(grant01_finish());
    REQUIRE(result == test->result && created == MICROS_GRANT_NONE);
    if (result != MICROS_GRANT_OK) {
        REQUIRE(memcmp(&plan, &before_plan, sizeof(plan)) == 0);
        REQUIRE(memcmp(&output, &before_output, sizeof(output)) == 0);
    } else if (committing) {
        before_plan.active = false;
        REQUIRE(memcmp(&plan, &before_plan, sizeof(plan)) == 0);
        REQUIRE(check_cancellation_graph(selected));
        REQUIRE(micros_grant_registry_validate(&fixture->grants, &fixture->endpoints, &fixture->objects) == MICROS_GRANT_OK);
    } else if (test->kind == INDEX_PREPARE_POISON) {
        REQUIRE(check_sparse_plan(test, &plan, &before_plan, selected));
    } else {
        REQUIRE(memcmp(&output, &fixture->before_grants.grants[0], sizeof(output)) == 0);
        REQUIRE(output.generation == 1 && output.grantor.slot == 0 && output.grantor.generation == 1
                && output.grantor_endpoint == fixture->identities[0] && output.grantee_endpoint == fixture->identities[1]
                && output.base == MICROS_USER_VIRTUAL_BASE && output.length == 64
                && output.permissions == MICROS_GRANT_PERMISSION_DEFINED_MASK);
        REQUIRE(micros_grant_registry_validate(&fixture->grants, &fixture->endpoints, &fixture->objects) == MICROS_GRANT_ERROR_INVARIANT);
    }
    preserved = !committing || result != MICROS_GRANT_OK || selected == 0;
    return grant01_report(index, test->name, result, value, preserved);
}

bool grant01_scope_cases(size_t *count)
{
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        REQUIRE(run_scope(*count, &cases[index]));
        ++*count;
    }
    for (size_t index = 0; index < sizeof(index_cases) / sizeof(index_cases[0]); ++index) {
        REQUIRE(run_index_case(*count, &index_cases[index]));
        ++*count;
    }
    return true;
}
