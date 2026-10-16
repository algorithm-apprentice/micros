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
    return true;
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
        if (
            plan->expected_generation[index]
                != (selected ? record->generation : 0)
        ) {
            return false;
        }
    }
    return plan->selected_slots == selected_slots;
}
