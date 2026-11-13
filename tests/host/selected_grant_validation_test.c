#include "tests/host/selected_grant_validation_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(MICROS_TEST_WORK_COUNTS)
#include "tests/work/work_counts.h"
#endif
#if defined(MICROS_SELECTED_GRANT_LAUNCHER)
#include <unistd.h>
#endif

enum grant_operation {
    GRANT_CREATE,
    GRANT_INSPECT,
    GRANT_REVOKE,
    GRANT_READ,
    GRANT_WRITE,
    GRANT_DIAGNOSTIC,
    GRANT_CANCEL_PLAN,
    GRANT_CANCEL_SPARSE,
};

struct grant_case {
    const char *name;
    enum grant_operation operation;
    size_t occupied;
    size_t slot;
    enum micros_grant_error result;
};

static const struct grant_case cases[] = {
    {"create-empty", GRANT_CREATE, 0, 0, MICROS_GRANT_OK},
    {"create-last", GRANT_CREATE, 63, 63, MICROS_GRANT_OK},
    {"create-full", GRANT_CREATE, 64, 0, MICROS_GRANT_ERROR_CAPACITY},
    {"inspect-first", GRANT_INSPECT, 1, 0, MICROS_GRANT_OK},
    {"inspect-last", GRANT_INSPECT, 64, 63, MICROS_GRANT_OK},
    {"inspect-stale", GRANT_INSPECT, 0, 63, MICROS_GRANT_ERROR_STALE_GRANT},
    {"revoke-first", GRANT_REVOKE, 1, 0, MICROS_GRANT_OK},
    {"revoke-last", GRANT_REVOKE, 64, 63, MICROS_GRANT_OK},
    {"authority-first", GRANT_READ, 1, 0, MICROS_GRANT_OK},
    {"authority-last", GRANT_READ, 64, 63, MICROS_GRANT_OK},
    {"authority-write", GRANT_WRITE, 64, 31, MICROS_GRANT_OK},
    {"diagnostic-dense", GRANT_DIAGNOSTIC, 64, 0, MICROS_GRANT_OK},
    {"cancel-dense", GRANT_CANCEL_PLAN, 64, 0, MICROS_GRANT_OK},
    {"cancel-empty", GRANT_CANCEL_PLAN, 0, 0, MICROS_GRANT_OK},
    {"cancel-sparse", GRANT_CANCEL_SPARSE, 64, 0, MICROS_GRANT_OK},
};

static struct grant01_fixture fixture;

#if defined(MICROS_TEST_WORK_COUNTS)
static struct micros_test_work_snapshot snapshot;
#endif

struct grant01_fixture *grant01_fixture(void)
{
    return &fixture;
}

void grant01_snapshot(void)
{
    fixture.before_objects = fixture.objects;
    fixture.before_endpoints = fixture.endpoints;
    fixture.before_grants = fixture.grants;
}

bool grant01_setup(size_t occupied, size_t actors)
{
    const struct micros_privilege_profile profiles[] = {
        {.id = 1, .name = "GRANT01", .operations = MICROS_PRIVILEGE_OPERATION_DEFINED_MASK,
         .call_targets = 2, .send_targets = 2, .notify_targets = 2},
        {.id = 2, .name = "PEER2", .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE},
        {.id = 3, .name = "PEER3", .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE},
        {.id = 4, .name = "TTY", .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
         .kernel_operations = MICROS_KERNEL_OPERATION_TTY_CONTROL},
    };

    REQUIRE(actors >= 3 && actors <= GRANT01_ACTORS);
    memset(&fixture, 0, sizeof(fixture));
    REQUIRE(micros_kernel_objects_initialize(&fixture.objects, 1, 1) == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(micros_endpoint_registry_initialize(&fixture.endpoints, profiles, 4) == MICROS_ENDPOINT_OK);
    REQUIRE(micros_grant_registry_initialize(&fixture.grants) == MICROS_GRANT_OK);
    REQUIRE(micros_hart_register(&fixture.objects, 0, &fixture.hart) == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(micros_hart_install_trap_stacks(
        &fixture.objects, fixture.hart, 0x1000000, 0x1004000, 0x2000000, 0x2001000
    ) == MICROS_KERNEL_OBJECT_OK);
    for (size_t index = 0; index < actors; ++index) {
        REQUIRE(micros_process_create(&fixture.objects, &fixture.processes[index]) == MICROS_KERNEL_OBJECT_OK);
        REQUIRE(micros_thread_create(&fixture.objects, fixture.processes[index], &fixture.threads[index])
                == MICROS_KERNEL_OBJECT_OK);
        REQUIRE(micros_endpoint_reserve(
            &fixture.endpoints, &fixture.objects, fixture.processes[index], &fixture.identities[index]
        ) == MICROS_ENDPOINT_OK);
        REQUIRE(micros_endpoint_install_profile(
            &fixture.endpoints, &fixture.objects, fixture.processes[index],
            index == MICROS_TTY_PROCESS_SLOT ? MICROS_PRIVILEGE_PROFILE_TTY : 1
        ) == MICROS_ENDPOINT_OK);
        REQUIRE(micros_endpoint_activate(&fixture.endpoints, &fixture.objects, fixture.identities[index])
                == MICROS_ENDPOINT_OK);
    }
    for (size_t index = 0; index < occupied; ++index) {
        micros_grant_t grant = MICROS_GRANT_NONE;

        REQUIRE(micros_grant_create(
            &fixture.grants, &fixture.endpoints, &fixture.objects,
            fixture.processes[0], fixture.identities[1],
            MICROS_USER_VIRTUAL_BASE + index * 64, 64,
            MICROS_GRANT_PERMISSION_READ | MICROS_GRANT_PERMISSION_WRITE, &grant
        ) == MICROS_GRANT_OK);
        REQUIRE(grant == 64 + index);
    }
    grant01_snapshot();
    return true;
}

static bool expected_active(const struct grant_case *test, size_t slot)
{
    bool active = slot < test->occupied
        || (test->operation == GRANT_CREATE && test->result == MICROS_GRANT_OK && slot == test->slot);

    return active && !(test->operation == GRANT_REVOKE && test->result == MICROS_GRANT_OK && slot == test->slot);
}

static struct micros_grant_link expected_links(const struct grant_case *test, size_t slot, size_t actor)
{
    struct micros_grant_link link = {MICROS_GRANT_NONE, MICROS_GRANT_NONE};
    size_t previous = MICROS_GRANT_CAPACITY;
    size_t next = MICROS_GRANT_CAPACITY;

    for (size_t other = 0; other < MICROS_GRANT_CAPACITY; ++other) {
        size_t grantor = test->operation == GRANT_CANCEL_SPARSE && other != 0 ? 1 : 0;

        if (!expected_active(test, other) || (actor != grantor && actor != grantor + 1)) {
            continue;
        }
        if (other > slot && other < previous) {
            previous = other;
        }
        if (other < slot && (next == MICROS_GRANT_CAPACITY || other > next)) {
            next = other;
        }
    }
    if (previous != MICROS_GRANT_CAPACITY) {
        link.previous = (uint32_t)(64 + previous);
    }
    if (next != MICROS_GRANT_CAPACITY) {
        link.next = (uint32_t)(64 + next);
    }
    return link;
}

static bool registry_matches(const struct grant_case *test)
{
    bool created = test->operation == GRANT_CREATE && test->result == MICROS_GRANT_OK;
    bool revoked = test->operation == GRANT_REVOKE && test->result == MICROS_GRANT_OK;
    size_t occupied = test->occupied + created - revoked;
    micros_grant_t heads[3] = {MICROS_GRANT_NONE, MICROS_GRANT_NONE, MICROS_GRANT_NONE};
    micros_grant_t original_free = test->occupied == 64 ? MICROS_GRANT_NONE : (uint32_t)(64 + test->occupied);

    REQUIRE(fixture.grants.active_count == occupied);
    REQUIRE(fixture.grants.initialization_magic == fixture.before_grants.initialization_magic);
    REQUIRE(fixture.grants.free_head == (revoked ? 128 + test->slot
            : occupied == 64 ? MICROS_GRANT_NONE : 64 + occupied));
    for (size_t slot = 0; slot < MICROS_GRANT_CAPACITY; ++slot) {
        const struct micros_grant_record *record = &fixture.grants.grants[slot];
        bool active = slot < test->occupied;
        size_t grantor = test->operation == GRANT_CANCEL_SPARSE && slot != 0 ? 1 : 0;
        size_t grantee = grantor + 1;
        uint32_t generation = 1;

        if (created && slot == test->slot) {
            active = true;
        }
        if (revoked && slot == test->slot) {
            active = false;
            generation = 2;
        }
        REQUIRE(record->state == (active ? MICROS_GRANT_SLOT_ACTIVE : MICROS_GRANT_SLOT_FREE));
        REQUIRE(record->generation == generation);
        REQUIRE(record->grantor.slot == (active ? grantor : 0)
                && record->grantor.generation == (active ? 1U : 0U));
        REQUIRE(record->grantor_endpoint == (active ? fixture.identities[grantor] : 0));
        REQUIRE(record->grantee_endpoint == (active ? fixture.identities[grantee] : 0));
        REQUIRE(record->base == (active ? MICROS_USER_VIRTUAL_BASE + slot * 64 : 0));
        REQUIRE(record->length == (active ? 64U : 0U));
        REQUIRE(record->permissions == (active ? MICROS_GRANT_PERMISSION_DEFINED_MASK : 0));
        if (active) {
            struct micros_grant_link grantor_link = expected_links(test, slot, grantor);
            struct micros_grant_link grantee_link = expected_links(test, slot, grantee);

            REQUIRE(record->free_next == MICROS_GRANT_NONE);
            REQUIRE(record->grantor_link.previous == grantor_link.previous && record->grantor_link.next == grantor_link.next);
            REQUIRE(record->grantee_link.previous == grantee_link.previous && record->grantee_link.next == grantee_link.next);
            heads[grantor] = heads[grantee] = (uint32_t)(64 + slot);
        } else {
            micros_grant_t next = revoked && slot == test->slot ? original_free
                : slot + 1 == MICROS_GRANT_CAPACITY ? MICROS_GRANT_NONE : (uint32_t)(65 + slot);

            REQUIRE(record->free_next == next);
            REQUIRE(record->grantor_link.previous == MICROS_GRANT_NONE && record->grantor_link.next == MICROS_GRANT_NONE
                    && record->grantee_link.previous == MICROS_GRANT_NONE && record->grantee_link.next == MICROS_GRANT_NONE);
        }
    }
    for (size_t actor = 0; actor < MICROS_PROCESS_CAPACITY; ++actor) {
        micros_grant_t head = actor < 3 ? heads[actor] : MICROS_GRANT_NONE;

        REQUIRE(fixture.grants.participants[actor].head == head);
        REQUIRE(fixture.grants.participants[actor].endpoint
                == (head == MICROS_GRANT_NONE ? MICROS_ENDPOINT_NONE : fixture.identities[actor]));
    }
    return true;
}

static uint64_t mix(uint64_t hash, const void *value, size_t length)
{
    const unsigned char *bytes = value;

    for (size_t index = 0; index < length; ++index) {
        hash = (hash ^ bytes[index]) * UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t registry_digest(const struct micros_grant_registry *registry)
{
    uint64_t hash = UINT64_C(14695981039346656037);

#define MIX(field) hash = mix(hash, &(field), sizeof(field))
    MIX(registry->initialization_magic);
    MIX(registry->active_count);
    MIX(registry->free_head);
    for (size_t slot = 0; slot < MICROS_GRANT_CAPACITY; ++slot) {
        const struct micros_grant_record *record = &registry->grants[slot];

        MIX(record->state);
        MIX(record->generation);
        MIX(record->free_next);
        MIX(record->grantor_link.previous);
        MIX(record->grantor_link.next);
        MIX(record->grantee_link.previous);
        MIX(record->grantee_link.next);
        MIX(record->grantor.slot);
        MIX(record->grantor.generation);
        MIX(record->grantor_endpoint);
        MIX(record->grantee_endpoint);
        MIX(record->base);
        MIX(record->length);
        MIX(record->permissions);
    }
    for (size_t slot = 0; slot < MICROS_PROCESS_CAPACITY; ++slot) {
        MIX(registry->participants[slot].endpoint);
        MIX(registry->participants[slot].head);
    }
#undef MIX
    return hash;
}

bool grant01_begin(size_t index)
{
#if defined(MICROS_TEST_WORK_COUNTS)
    REQUIRE(micros_test_work_begin(
        UINT64_C(0x4752414e54303101), index + 1,
        (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_OBJECTS)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_SCHEDULER)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_ENDPOINT)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_IPC)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_GRANT)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_BOOT)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_TTY)
            | (UINT32_C(1) << MICROS_WORK_OWNER_RUNTIME)
    ) == MICROS_TEST_WORK_OK);
#else
    (void)index;
#endif
    return true;
}

bool grant01_finish(void)
{
#if defined(MICROS_TEST_WORK_COUNTS)
    REQUIRE(micros_test_work_finish(&snapshot) == MICROS_TEST_WORK_OK);
#endif
    return true;
}

static bool run_case(size_t index, const struct grant_case *test)
{
    struct micros_grant_record output, output_before;
    struct micros_grant_copy_authority authority;
    struct micros_grant_cancel_plan plan;
    micros_grant_t grant = MICROS_GRANT_NONE;
    uint64_t value = 0;
    enum micros_grant_error result;
    bool preserved = test->result != MICROS_GRANT_OK
        || (test->operation != GRANT_CREATE && test->operation != GRANT_REVOKE);

    REQUIRE(grant01_setup(test->operation == GRANT_CANCEL_SPARSE ? 0 : test->occupied, 3));
    if (test->operation == GRANT_CANCEL_SPARSE) {
        for (size_t slot = 0; slot < 64; ++slot) {
            size_t grantor = slot == 0 ? 0 : 1;
            micros_grant_t created = MICROS_GRANT_NONE;

            REQUIRE(micros_grant_create(
                &fixture.grants, &fixture.endpoints, &fixture.objects,
                fixture.processes[grantor], fixture.identities[grantor + 1],
                MICROS_USER_VIRTUAL_BASE + slot * 64, 64, MICROS_GRANT_PERMISSION_DEFINED_MASK, &created
            ) == MICROS_GRANT_OK);
            REQUIRE(created == 64 + slot);
        }
        grant01_snapshot();
    }
    memset(&output, 0xa5, sizeof(output));
    output_before = output;
    memset(&authority, 0xa5, sizeof(authority));
    memset(&plan, 0, sizeof(plan));
    REQUIRE(grant01_begin(index));
    switch (test->operation) {
    case GRANT_CREATE:
        result = micros_grant_create(
            &fixture.grants, &fixture.endpoints, &fixture.objects,
            fixture.processes[0], fixture.identities[1],
            MICROS_USER_VIRTUAL_BASE + test->slot * 64, 64,
            MICROS_GRANT_PERMISSION_DEFINED_MASK, &grant
        );
        value = grant;
        break;
    case GRANT_INSPECT:
        result = micros_grant_inspect(
            &fixture.grants, &fixture.endpoints, &fixture.objects,
            fixture.processes[0], (micros_grant_t)(64 + test->slot), &output
        );
        break;
    case GRANT_REVOKE:
        result = micros_grant_revoke(
            &fixture.grants, &fixture.endpoints, &fixture.objects,
            fixture.processes[0], (micros_grant_t)(64 + test->slot)
        );
        value = fixture.grants.grants[test->slot].generation;
        break;
    case GRANT_READ:
    case GRANT_WRITE:
        result = micros_grant_prepare_copy_authority(
            &fixture.grants, &fixture.endpoints, &fixture.objects, fixture.processes[1],
            fixture.identities[0], (micros_grant_t)(64 + test->slot), 8, 32,
            test->operation == GRANT_READ ? MICROS_GRANT_PERMISSION_READ : MICROS_GRANT_PERMISSION_WRITE,
            &authority
        );
        break;
    case GRANT_DIAGNOSTIC:
        result = micros_grant_registry_validate(&fixture.grants, &fixture.endpoints, &fixture.objects);
        value = fixture.grants.active_count;
        break;
    default:
        result = micros_grant_prepare_endpoint_cancel(
            &fixture.grants, &fixture.endpoints, &fixture.objects, fixture.identities[0], &plan
        );
        value = plan.selected_slots;
        break;
    }
    REQUIRE(grant01_finish());
    REQUIRE(result == test->result);
    if (test->operation == GRANT_INSPECT) {
        if (result == MICROS_GRANT_OK) {
            REQUIRE(output.state == MICROS_GRANT_SLOT_ACTIVE && output.generation == 1
                    && output.grantor.slot == 0 && output.grantor.generation == 1
                    && output.grantor_endpoint == fixture.identities[0]
                    && output.grantee_endpoint == fixture.identities[1]
                    && output.base == MICROS_USER_VIRTUAL_BASE + test->slot * 64
                    && output.length == 64 && output.permissions == MICROS_GRANT_PERMISSION_DEFINED_MASK);
            value = output.length;
        } else {
            REQUIRE(memcmp(&output, &output_before, sizeof(output)) == 0);
        }
    } else if (test->operation == GRANT_READ || test->operation == GRANT_WRITE) {
        REQUIRE(authority.grantor.slot == 0 && authority.grantor.generation == 1
                && authority.grantee.slot == 1 && authority.grantee.generation == 1
                && authority.remote_address == MICROS_USER_VIRTUAL_BASE + test->slot * 64 + 8
                && authority.length == 32
                && authority.required_permission == (test->operation == GRANT_READ
                    ? MICROS_GRANT_PERMISSION_READ : MICROS_GRANT_PERMISSION_WRITE));
        value = authority.remote_address;
    } else if (test->operation == GRANT_CANCEL_PLAN || test->operation == GRANT_CANCEL_SPARSE) {
        uint64_t selected = test->operation == GRANT_CANCEL_SPARSE ? 1
            : test->occupied == 0 ? 0 : UINT64_MAX;

        REQUIRE(plan.active && plan.registry_identity == (uintptr_t)&fixture.grants
                && plan.endpoint == fixture.identities[0] && plan.selected_slots == selected);
        for (size_t slot = 0; slot < MICROS_GRANT_CAPACITY; ++slot) {
            if ((selected & (UINT64_C(1) << slot)) != 0) {
                REQUIRE(plan.expected_generation[slot] == 1);
            }
        }
    }
    REQUIRE(registry_matches(test));
    REQUIRE(micros_grant_registry_validate(&fixture.grants, &fixture.endpoints, &fixture.objects) == MICROS_GRANT_OK);
    return grant01_report(index, test->name, result, value, preserved);
}

bool grant01_report(size_t index, const char *name, enum micros_grant_error result,
                    uint64_t value, bool preserved)
{
    REQUIRE(memcmp(&fixture.objects, &fixture.before_objects, sizeof(fixture.objects)) == 0);
    REQUIRE(memcmp(&fixture.endpoints, &fixture.before_endpoints, sizeof(fixture.endpoints)) == 0);
    REQUIRE((memcmp(&fixture.grants, &fixture.before_grants, sizeof(fixture.grants)) == 0) == preserved);
    printf("MICROS_GRANT01_CASE {\"version\":1,\"case\":%zu,\"name\":\"%s\",\"result\":%u,"
           "\"value\":%" PRIu64 ",\"preserved\":%u,\"before_state\":%" PRIu64 ",\"after_state\":%" PRIu64 "}\n",
           index + 1, name, (unsigned)result, value, (unsigned)preserved,
           registry_digest(&fixture.before_grants), registry_digest(&fixture.grants));
#if defined(MICROS_TEST_WORK_COUNTS)
    printf("MICROS_GRANT01_WORK {\"version\":%u,\"reserved0\":%u,\"producer\":%" PRIu64
           ",\"case\":%" PRIu64 ",\"owner_mask\":%u,\"reserved1\":%u,\"counts\":[",
           snapshot.version, snapshot.reserved0, snapshot.producer_label, snapshot.capture_label,
           snapshot.owner_mask, snapshot.reserved1);
    for (size_t owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        printf("%s[", owner == 0 ? "" : ",");
        for (size_t kind = 0; kind < MICROS_WORK_KIND_COUNT; ++kind) {
            printf("%s%" PRIu64, kind == 0 ? "" : ",", snapshot.counts[owner][kind]);
        }
        printf("]");
    }
    printf("]}\n");
#endif
    return true;
}

enum copy_span_operation {
    COPY_SPAN_CORE, COPY_SPAN_FROM, COPY_SPAN_TO, COPY_SPAN_VALIDATE,
    COPY_SPAN_LATE_FAULT, COPY_SPAN_LATE_INVARIANT, COPY_SPAN_OVERLAP,
};

struct copy_span_case {
    const char *name;
    size_t length;
    size_t source_offset;
    size_t destination_offset;
    size_t source_first;
    size_t destination_first;
    enum copy_span_operation operation;
    bool reverse;
    enum micros_grant_error result;
};

static const struct copy_span_case copy_span_cases[] = {
    {"span-zero", 0, 0, 0, 0, 0, COPY_SPAN_CORE, false, MICROS_GRANT_OK},
    {"span-one", 1, 0, 0, 0, 0, COPY_SPAN_CORE, false, MICROS_GRANT_OK},
    {"span-seven", 7, 0, 0, 0, 0, COPY_SPAN_CORE, false, MICROS_GRANT_OK},
    {"span-eight", 8, 0, 0, 0, 0, COPY_SPAN_CORE, false, MICROS_GRANT_OK},
    {"span-nine", 9, 0, 0, 0, 0, COPY_SPAN_CORE, false, MICROS_GRANT_OK},
    {"span-block", 64, 0, 0, 0, 0, COPY_SPAN_CORE, false, MICROS_GRANT_OK},
    {"span-page", 4096, 0, 0, 0, 0, COPY_SPAN_CORE, false, MICROS_GRANT_OK},
    {"span-prefix-tail", 64, 1, 1, 0, 0, COPY_SPAN_CORE, false, MICROS_GRANT_OK},
    {"span-different-residues", 64, 0, 1, 0, 0, COPY_SPAN_CORE, false, MICROS_GRANT_OK},
    {"span-source-cross", 4096, 4088, 0, 8, 0, COPY_SPAN_CORE, false, MICROS_GRANT_OK},
    {"span-destination-cross", 4096, 0, 4080, 0, 16, COPY_SPAN_CORE, false, MICROS_GRANT_OK},
    {"span-coincident-cross", 4096, 4080, 4080, 16, 16, COPY_SPAN_CORE, false, MICROS_GRANT_OK},
    {"span-unequal-cross", 4096, 4088, 4080, 8, 16, COPY_SPAN_CORE, false, MICROS_GRANT_OK},
    {"span-unaligned-cross", 4096, 4083, 4075, 13, 21, COPY_SPAN_CORE, false, MICROS_GRANT_OK},
    {"span-reversed", 64, 0, 0, 0, 0, COPY_SPAN_CORE, true, MICROS_GRANT_OK},
    {"span-from-page", 4096, 4088, 4080, 8, 16, COPY_SPAN_FROM, false, MICROS_GRANT_OK},
    {"span-to-page", 4096, 4083, 4075, 13, 21, COPY_SPAN_TO, false, MICROS_GRANT_OK},
    {"span-validate-page", 4096, 4088, 4080, 8, 16, COPY_SPAN_VALIDATE, false, MICROS_GRANT_OK},
    {"span-late-fault", 4096, 4088, 4080, 8, 16, COPY_SPAN_LATE_FAULT, false, MICROS_GRANT_ERROR_FAULT},
    {"span-late-invariant", 4096, 4088, 4080, 8, 16, COPY_SPAN_LATE_INVARIANT, false, MICROS_GRANT_ERROR_INVARIANT},
    {"span-physical-overlap", 64, 0, 0, 0, 0, COPY_SPAN_OVERLAP, false, MICROS_GRANT_ERROR_INVARIANT},
};

static unsigned char *copy_span_byte(
    const struct copy_span_case *test, unsigned char memory[][GRANT01_MEMORY_SIZE],
    bool source, size_t logical
)
{
    size_t actor = source ? 0 : 1;
    size_t offset = source ? test->source_offset : test->destination_offset;
    size_t first = source ? test->source_first : test->destination_first;

    if (test->reverse || test->operation == COPY_SPAN_TO) {
        actor ^= 1;
    }
    if (test->operation == COPY_SPAN_CORE && first != 0 && logical >= first) {
        actor += 2;
        offset = 0;
        logical -= first;
    }
    return &memory[actor][offset + logical];
}

static struct micros_grant_copy_range_plan copy_span_plan(
    const struct copy_span_case *test, bool source
)
{
    struct micros_grant_copy_range_plan plan = {0};
    size_t first = source ? test->source_first : test->destination_first;

    if (test->length != 0) {
        plan.chunk_count = first == 0 ? 1 : 2;
        plan.chunks[0] = (struct micros_grant_copy_chunk){
            .physical_address = (uintptr_t)copy_span_byte(test, fixture.memory, source, 0),
            .length = first == 0 ? test->length : first,
        };
        if (first != 0) {
            plan.chunks[1] = (struct micros_grant_copy_chunk){
                .physical_address = (uintptr_t)copy_span_byte(test, fixture.memory, source, first),
                .length = test->length - first,
            };
        }
    }
    return plan;
}

static bool run_copy_span(size_t index, const struct copy_span_case *test)
{
    struct micros_grant_copy_range_plan source, destination;
    micros_grant_t grant = MICROS_GRANT_NONE;
    enum micros_grant_error result = MICROS_GRANT_OK;
    bool core = test->operation == COPY_SPAN_CORE;
    bool to = test->operation == COPY_SPAN_TO;
    bool writes = test->result == MICROS_GRANT_OK && test->operation != COPY_SPAN_VALIDATE;
    size_t remote_offset = to ? test->destination_offset : test->source_offset;
    size_t local_offset = to ? test->source_offset : test->destination_offset;
    size_t remote_visits = core ? 0 : 1 + (remote_offset + test->length - 1) / 4096;
    size_t local_visits = core || test->operation == COPY_SPAN_VALIDATE
        ? 0 : 1 + (local_offset + test->length - 1) / 4096;

    REQUIRE(grant01_setup(0, 4));
    memset(tty_handoff_context(), 0, sizeof(*tty_handoff_context()));
    fixture.ledger_available = true;
    fixture.ledger.phase = to ? MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF : MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP;
    for (size_t actor = 0; actor < GRANT01_ACTORS; ++actor) {
        fixture.memory_base[actor] = MICROS_USER_VIRTUAL_BASE;
        fixture.permissions[actor][0] = fixture.permissions[actor][1] =
            MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE;
        for (size_t byte = 0; byte < GRANT01_MEMORY_SIZE; ++byte) {
            fixture.memory[actor][byte] = (unsigned char)(13 * actor + 17 * byte + 7);
        }
    }
    if (!core) {
        REQUIRE(micros_grant_create(
            &fixture.grants, &fixture.endpoints, &fixture.objects, fixture.processes[0],
            fixture.identities[1], MICROS_USER_VIRTUAL_BASE, GRANT01_MEMORY_SIZE,
            MICROS_GRANT_PERMISSION_DEFINED_MASK, &grant
        ) == MICROS_GRANT_OK);
    }
    if (test->operation == COPY_SPAN_LATE_FAULT) {
        fixture.permissions[1][1] = MICROS_SV39_PERMISSION_READ;
    } else if (test->operation == COPY_SPAN_LATE_INVARIANT) {
        fixture.translation_error[0][1] = MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
        fixture.permissions[1][0] = MICROS_SV39_PERMISSION_READ;
        local_visits = 1;
    }
    fixture.overlap_memory = test->operation == COPY_SPAN_OVERLAP;
    source = copy_span_plan(test, true);
    destination = copy_span_plan(test, false);
    REQUIRE(micros_grant_copy_plan_validate(&source, &destination, test->length) == MICROS_GRANT_OK);
    memcpy(fixture.before_memory, fixture.memory, sizeof(fixture.memory));
    if (writes) {
        for (size_t byte = 0; byte < test->length; ++byte) {
            *copy_span_byte(test, fixture.before_memory, false, byte) =
                *copy_span_byte(test, fixture.before_memory, true, byte);
        }
    }
    grant01_snapshot();
    REQUIRE(grant01_begin(index));
    if (core) {
        micros_grant_copy_commit(&source, &destination, test->length);
    } else if (test->operation == COPY_SPAN_VALIDATE) {
        result = micros_grant_validate_range(
            &fixture.grants, &fixture.endpoints, &fixture.objects, fixture.processes[1],
            fixture.identities[0], grant, remote_offset, test->length, MICROS_GRANT_PERMISSION_READ
        );
    } else if (to) {
        result = micros_grant_copy_to(
            &fixture.grants, &fixture.endpoints, &fixture.objects, fixture.processes[1],
            fixture.identities[0], grant, remote_offset, MICROS_USER_VIRTUAL_BASE + local_offset, test->length
        );
    } else {
        result = micros_grant_copy_from(
            &fixture.grants, &fixture.endpoints, &fixture.objects, fixture.processes[1],
            fixture.identities[0], grant, remote_offset, MICROS_USER_VIRTUAL_BASE + local_offset, test->length
        );
    }
    REQUIRE(grant01_finish());
    REQUIRE(result == test->result);
    REQUIRE(fixture.translations[0] == remote_visits && fixture.translations[1] == local_visits);
    REQUIRE(fixture.translations[2] == 0 && fixture.translations[3] == 0 && fixture.irq_status == 0);
    REQUIRE(memcmp(fixture.memory, fixture.before_memory, sizeof(fixture.memory)) == 0);
    return grant01_report(index, test->name, result, writes ? test->length : 0, true);
}

int main(int argc, char **argv)
{
    size_t count = sizeof(cases) / sizeof(cases[0]);
#if defined(MICROS_SELECTED_GRANT_LAUNCHER)
    if (argc == 2) {
        const char *launcher = getenv("__PYVENV_LAUNCHER__");
        char *const arguments[] = {
            MICROS_SELECTED_GRANT_PYTHON, "-E", "-S", "-B", MICROS_SELECTED_GRANT_DRIVER,
            "--executable", argv[1], "--executable", argv[0], NULL,
        };

        if (launcher != NULL && launcher[0] != '\0') {
            fputs("__PYVENV_LAUNCHER__ is unsupported\n", stderr);
            return 2;
        }
        if (chdir(MICROS_SELECTED_GRANT_ROOT) != 0) {
            return 2;
        }
        execv(MICROS_SELECTED_GRANT_PYTHON, arguments);
        perror("GRANT01 launcher");
        return 2;
    }
#endif
    if (argc != 3 || strcmp(argv[1], "--run") != 0
#if defined(MICROS_TEST_WORK_COUNTS)
        || strcmp(argv[2], "work") != 0
#else
        || strcmp(argv[2], "ordinary") != 0
#endif
    ) {
        return 2;
    }
    for (size_t index = 0; index < count; ++index) {
        if (!run_case(index, &cases[index])) {
            return 1;
        }
    }
    if (!grant01_runtime_cases(&count) || !grant01_scope_cases(&count)) {
        return 1;
    }
    for (size_t index = 0; index < sizeof(copy_span_cases) / sizeof(copy_span_cases[0]); ++index) {
        if (!run_copy_span(count, &copy_span_cases[index])) {
            return 1;
        }
        ++count;
    }
    printf("MICROS_GRANT01_NATIVE_PASS cases=%zu\n", count);
    return 0;
}
