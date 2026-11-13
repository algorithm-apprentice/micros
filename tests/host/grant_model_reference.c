#include "grant_test_internal.h"

bool grant_test_process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

bool grant_test_records_equal(
    const struct micros_grant_record *left,
    const struct micros_grant_record *right
)
{
    return (
        left->state == right->state
        && left->generation == right->generation
        && left->free_next == right->free_next
        && left->grantor_link.previous == right->grantor_link.previous
        && left->grantor_link.next == right->grantor_link.next
        && left->grantee_link.previous == right->grantee_link.previous
        && left->grantee_link.next == right->grantee_link.next
        && grant_test_process_handles_equal(left->grantor, right->grantor)
        && left->grantor_endpoint == right->grantor_endpoint
        && left->grantee_endpoint == right->grantee_endpoint
        && left->base == right->base
        && left->length == right->length
        && left->permissions == right->permissions
    );
}

bool grant_test_registries_equal(
    const struct micros_grant_registry *left,
    const struct micros_grant_registry *right
)
{
    size_t index;

    if (
        left->initialization_magic != right->initialization_magic
        || left->active_count != right->active_count
        || left->free_head != right->free_head
    ) {
        return false;
    }
    for (index = 0; index < MICROS_GRANT_CAPACITY; ++index) {
        if (!grant_test_records_equal(
            &left->grants[index],
            &right->grants[index]
        )) {
            return false;
        }
    }
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        if (left->participants[index].endpoint != right->participants[index].endpoint
            || left->participants[index].head != right->participants[index].head) {
            return false;
        }
    }
    return true;
}

static micros_grant_t reference_token(const struct micros_grant_registry *expected, size_t slot)
{
    return (expected->grants[slot].generation << MICROS_GRANT_SLOT_BITS) | (uint32_t)slot;
}

void grant_test_model_project_indexes(
    struct micros_grant_registry *expected, const struct grant_model_index *index
)
{
    const struct micros_grant_link empty = {MICROS_GRANT_NONE, MICROS_GRANT_NONE};

    expected->free_head = index->free_count == 0 ? MICROS_GRANT_NONE
        : reference_token(expected, index->free_slots[index->free_count - 1]);
    for (size_t slot = 0; slot < MICROS_PROCESS_CAPACITY; ++slot) {
        expected->participants[slot] = (struct micros_grant_endpoint_index){MICROS_ENDPOINT_NONE, MICROS_GRANT_NONE};
    }
    for (size_t slot = 0; slot < MICROS_GRANT_CAPACITY; ++slot) {
        expected->grants[slot].free_next = MICROS_GRANT_NONE;
        expected->grants[slot].grantor_link = empty;
        expected->grants[slot].grantee_link = empty;
    }
    for (size_t position = 1; position < index->free_count; ++position) {
        expected->grants[index->free_slots[position]].free_next = reference_token(expected, index->free_slots[position - 1]);
    }
    for (size_t slot = 0; slot < MICROS_GRANT_CAPACITY; ++slot) {
        struct micros_grant_record *record = &expected->grants[slot];

        if (record->state != MICROS_GRANT_SLOT_ACTIVE) {
            continue;
        }
        for (size_t role = 0; role < 2; ++role) {
            micros_endpoint_t endpoint = role == 0 ? record->grantor_endpoint : record->grantee_endpoint;
            struct micros_grant_link *link = role == 0 ? &record->grantor_link : &record->grantee_link;
            size_t previous = MICROS_GRANT_CAPACITY, next = MICROS_GRANT_CAPACITY;

            for (size_t other = 0; other < MICROS_GRANT_CAPACITY; ++other) {
                const struct micros_grant_record *candidate = &expected->grants[other];

                if (candidate->state != MICROS_GRANT_SLOT_ACTIVE
                    || (candidate->grantor_endpoint != endpoint && candidate->grantee_endpoint != endpoint)) {
                    continue;
                }
                if (index->publication[other] > index->publication[slot]
                    && (previous == MICROS_GRANT_CAPACITY || index->publication[other] < index->publication[previous])) {
                    previous = other;
                }
                if (index->publication[other] < index->publication[slot]
                    && (next == MICROS_GRANT_CAPACITY || index->publication[other] > index->publication[next])) {
                    next = other;
                }
            }
            link->previous = previous == MICROS_GRANT_CAPACITY ? MICROS_GRANT_NONE : reference_token(expected, previous);
            link->next = next == MICROS_GRANT_CAPACITY ? MICROS_GRANT_NONE : reference_token(expected, next);
            if (previous == MICROS_GRANT_CAPACITY) {
                size_t owner = endpoint & ((UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) - 1);

                expected->participants[owner] = (struct micros_grant_endpoint_index){endpoint, reference_token(expected, slot)};
            }
        }
    }
}

void grant_test_model_initialize_reference(
    struct micros_grant_registry *expected, struct grant_model_index *index, uint64_t magic
)
{
    memset(expected, 0, sizeof(*expected));
    memset(index, 0, sizeof(*index));
    expected->initialization_magic = magic;
    index->free_count = MICROS_GRANT_CAPACITY;
    for (size_t slot = 0; slot < MICROS_GRANT_CAPACITY; ++slot) {
        expected->grants[slot].generation = 1;
        index->free_slots[MICROS_GRANT_CAPACITY - 1 - slot] = (uint16_t)slot;
    }
    grant_test_model_project_indexes(expected, index);
}

void grant_test_model_retire_reference(
    struct micros_grant_registry *expected, struct grant_model_index *index, size_t slot
)
{
    struct micros_grant_record *record = &expected->grants[slot];
    uint32_t generation = record->generation;
    bool terminal = generation == MICROS_GRANT_GENERATION_MAX
        || (slot == MICROS_GRANT_CAPACITY - 1 && generation == MICROS_GRANT_GENERATION_MAX - 1);

    memset(record, 0, sizeof(*record));
    record->state = terminal ? MICROS_GRANT_SLOT_QUARANTINED : MICROS_GRANT_SLOT_FREE;
    record->generation = terminal ? generation : generation + 1;
    if (!terminal) {
        index->free_slots[index->free_count++] = (uint16_t)slot;
    }
    index->publication[slot] = 0;
    --expected->active_count;
}

uint32_t grant_test_model_next(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static const char *grant_test_model_operation_name(
    enum grant_model_operation operation
)
{
    static const char *const names[GRANT_MODEL_OPERATION_COUNT] = {
        "create",
        "authorize-from",
        "authorize-to",
        "authorize-denial",
        "authorize-stale",
        "authorize-bounds",
        "revoke",
        "inspect",
        "cancel",
        "close-reuse",
        "malformed-token",
        "plan-failure",
        "invalid-create",
    };

    return (size_t)operation < GRANT_MODEL_OPERATION_COUNT
        ? names[operation]
        : "unknown";
}

void grant_test_record_model_trace(
    struct grant_test_fixture *fixture,
    size_t step,
    enum grant_model_operation operation,
    size_t grantor,
    size_t grantee,
    micros_grant_t grant,
    size_t offset,
    size_t length,
    uint32_t permission,
    enum micros_grant_error result
)
{
    size_t index = fixture->model_trace_count % GRANT_MODEL_TRACE_COUNT;

    fixture->model_trace[index] = (struct grant_model_trace_entry){
        .step = step,
        .operation = operation,
        .grantor = grantor,
        .grantee = grantee,
        .grant = grant,
        .offset = offset,
        .length = length,
        .permission = permission,
        .result = result,
    };
    ++fixture->model_trace_count;
}

bool grant_test_model_fail(
    struct grant_test_fixture *fixture,
    size_t step,
    const char *reason
)
{
    size_t available =
        fixture->model_trace_count < GRANT_MODEL_TRACE_COUNT
            ? fixture->model_trace_count
            : GRANT_MODEL_TRACE_COUNT;
    size_t first = fixture->model_trace_count - available;
    size_t offset;

    fprintf(
        stderr,
        "grant model seed=0x38a110c5 step=%zu: %s\n",
        step,
        reason
    );
    for (offset = 0; offset < available; ++offset) {
        const struct grant_model_trace_entry *entry =
            &fixture->model_trace[
                (first + offset) % GRANT_MODEL_TRACE_COUNT
            ];

        fprintf(
            stderr,
            "  step=%zu op=%s grantor=%zu grantee=%zu "
            "grant=0x%08x offset=%zu length=%zu permission=0x%x "
            "result=%d\n",
            entry->step,
            grant_test_model_operation_name(entry->operation),
            entry->grantor,
            entry->grantee,
            entry->grant,
            entry->offset,
            entry->length,
            entry->permission,
            (int)entry->result
        );
    }
    return false;
}

bool grant_test_cancel_plan_matches_reference(
    struct grant_test_fixture *fixture,
    const struct micros_grant_cancel_plan *plan,
    const struct micros_grant_registry *model,
    micros_endpoint_t endpoint
)
{
    uint64_t selected_slots = 0;
    size_t index;

    if (
        !plan->active
        || plan->registry_identity != (uintptr_t)&fixture->grant_registry
        || plan->endpoint != endpoint
    ) {
        return false;
    }
    for (index = 0; index < MICROS_GRANT_CAPACITY; ++index) {
        const struct micros_grant_record *record =
            &model->grants[index];
        bool selected = (
            record->state == MICROS_GRANT_SLOT_ACTIVE
            && (
                record->grantor_endpoint == endpoint
                || record->grantee_endpoint == endpoint
            )
        );

        if (selected) {
            selected_slots |= UINT64_C(1) << index;
        }
        if (selected && plan->expected_generation[index] != record->generation) {
            return false;
        }
    }
    return plan->selected_slots == selected_slots;
}
