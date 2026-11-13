#include "kernel/boot/bootstrap_manifest_internal.h"
#include "kernel/boot/bootstrap_transitions_internal.h"
#include "micros/work_observation.h"

enum micros_bootstrap_error bootstrap_runtime_select_slot(
    const struct micros_bootstrap_runtime *runtime,
    uint32_t service_id,
    uint16_t *index
)
{
    uint8_t slot;

    if (
        runtime == NULL
        || index == NULL
        || service_id == 0
        || service_id > MICROS_BOOTSTRAP_SERVICE_ID_LIMIT
    ) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (
        runtime->entry_count == 0
        || runtime->entry_count > MICROS_BOOTSTRAP_SERVICE_CAPACITY
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    slot = runtime->service_slots[service_id];
    if (slot == 0) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (
        slot > runtime->entry_count
        || runtime->entries[slot - 1].service_id != service_id
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    *index = (uint16_t)(slot - 1);
    return MICROS_BOOTSTRAP_OK;
}

const struct micros_bootstrap_runtime_entry *bootstrap_runtime_find_entry(
    const struct micros_bootstrap_runtime *runtime,
    uint32_t service_id
)
{
    uint16_t index;

    return bootstrap_runtime_select_slot(runtime, service_id, &index) == MICROS_BOOTSTRAP_OK
        ? &runtime->entries[index] : NULL;
}

bool bootstrap_runtime_validate_index(
    const struct micros_bootstrap_runtime *runtime
)
{
#ifdef MICROS_TEST_BOOTSTRAP_COMMAND_COUNTS
    MICROS_TEST_WORK_HIT("boot02-runtime-index", MICROS_WORK_OWNER_KERNEL_BOOT,
                         MICROS_WORK_KIND_COMPLETE_VALIDATOR_CALL, 1);
#endif
    size_t index;

    if (
        runtime == NULL
        || runtime->entry_count == 0
        || runtime->entry_count > MICROS_BOOTSTRAP_SERVICE_CAPACITY
        || runtime->service_slots[0] != 0
    ) {
        return false;
    }
    for (index = 0; index < runtime->entry_count; ++index) {
        uint32_t service_id = runtime->entries[index].service_id;

        if (
            service_id == 0
            || service_id > MICROS_BOOTSTRAP_SERVICE_ID_LIMIT
            || runtime->service_slots[service_id] != index + 1
        ) {
            return false;
        }
    }
    for (index = 1; index <= MICROS_BOOTSTRAP_SERVICE_ID_LIMIT; ++index) {
        uint8_t slot = runtime->service_slots[index];

        if (
            slot != 0
            && (
                slot > runtime->entry_count
                || runtime->entries[slot - 1].service_id != index
            )
        ) {
            return false;
        }
    }
    return true;
}

enum micros_bootstrap_error micros_bootstrap_runtime_initialize(
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_bootstrap_manifest_plan *plan,
    struct micros_bootstrap_runtime *runtime
)
{
    struct micros_bootstrap_runtime candidate;
    uint64_t ordered_ids = 0;
    size_t index;

    if (manifest == NULL || plan == NULL || runtime == NULL) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    bootstrap_manifest_clear_bytes(&candidate, sizeof(candidate));
    if (
        !bootstrap_manifest_pointer_is_aligned(
            manifest,
            _Alignof(struct micros_bootstrap_manifest)
        )
        || !bootstrap_manifest_pointer_is_aligned(
            plan,
            _Alignof(struct micros_bootstrap_manifest_plan)
        )
        || !bootstrap_manifest_pointer_is_aligned(
            runtime,
            _Alignof(struct micros_bootstrap_runtime)
        )
    ) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (!bootstrap_manifest_bytes_are_zero(runtime, sizeof(*runtime))) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    if (
        plan->entry_count == 0
        || plan->entry_count > MICROS_BOOTSTRAP_SERVICE_CAPACITY
        || plan->entry_count != manifest->header.entry_count
        || plan->controller_service_id == 0
        || plan->ordered_service_ids[0]
            != plan->controller_service_id
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    candidate.phase = MICROS_BOOTSTRAP_PHASE_RUNNING;
    candidate.entry_count = plan->entry_count;
    candidate.next_order_index = 1;
    candidate.controller_service_id = plan->controller_service_id;
    for (index = 0; index < plan->entry_count; ++index) {
        const struct micros_bootstrap_manifest_entry *manifest_entry =
            &manifest->entries[index];
        struct micros_bootstrap_runtime_entry *entry =
            &candidate.entries[index];

        if (
            manifest_entry->service_id == 0
            || manifest_entry->service_id > MICROS_BOOTSTRAP_SERVICE_ID_LIMIT
            || candidate.service_slots[manifest_entry->service_id] != 0
        ) {
            return MICROS_BOOTSTRAP_ERROR_INVARIANT;
        }
        candidate.service_slots[manifest_entry->service_id] =
            (uint8_t)(index + 1);
        entry->service_id = manifest_entry->service_id;
        entry->prerequisites = manifest_entry->prerequisites;
        entry->ready_timeout_counter_ticks =
            manifest_entry->ready_timeout_counter_ticks;
        if (
            entry->service_id == plan->controller_service_id
        ) {
            entry->state = MICROS_BOOTSTRAP_SERVICE_READY;
            entry->endpoint_state =
                MICROS_BOOTSTRAP_ENDPOINT_ACTIVE;
            entry->profile_installed = true;
            entry->scheduler_assigned = true;
        } else {
            entry->state = MICROS_BOOTSTRAP_SERVICE_PREPARED;
            entry->endpoint_state =
                MICROS_BOOTSTRAP_ENDPOINT_RESERVED;
        }
        candidate.ordered_service_ids[index] =
            plan->ordered_service_ids[index];
    }
    for (index = 0; index < plan->entry_count; ++index) {
        uint32_t service_id = plan->ordered_service_ids[index];
        uint64_t bit;

        if (
            service_id == 0
            || service_id > MICROS_BOOTSTRAP_SERVICE_ID_LIMIT
            || candidate.service_slots[service_id] == 0
        ) {
            return MICROS_BOOTSTRAP_ERROR_INVARIANT;
        }
        bit = UINT64_C(1) << (service_id - 1);
        if ((ordered_ids & bit) != 0) {
            return MICROS_BOOTSTRAP_ERROR_INVARIANT;
        }
        ordered_ids |= bit;
    }
    bootstrap_manifest_copy_bytes(runtime, &candidate, sizeof(*runtime));
    return MICROS_BOOTSTRAP_OK;
}

static void initialize_transition_plan(
    const struct micros_bootstrap_runtime *runtime,
    uint16_t index,
    struct bootstrap_transition_plan *plan
)
{
    bootstrap_manifest_clear_bytes(plan, sizeof(*plan));
    plan->entry_index = index;
    plan->next_order_index = runtime->next_order_index;
    plan->phase = runtime->phase;
    plan->controller_service_id = runtime->controller_service_id;
    plan->starting_service_id = runtime->starting_service_id;
    bootstrap_manifest_copy_bytes(
        &plan->entry, &runtime->entries[index], sizeof(plan->entry)
    );
}

static uint32_t lowest_prerequisite_id(uint64_t bits)
{
    uint32_t service_id = 1;

    if ((bits & UINT64_C(0xffffffff)) == 0) {
        bits >>= 32;
        service_id += 32;
    }
    if ((bits & UINT64_C(0xffff)) == 0) {
        bits >>= 16;
        service_id += 16;
    }
    if ((bits & UINT64_C(0xff)) == 0) {
        bits >>= 8;
        service_id += 8;
    }
    if ((bits & UINT64_C(0xf)) == 0) {
        bits >>= 4;
        service_id += 4;
    }
    if ((bits & UINT64_C(3)) == 0) {
        bits >>= 2;
        service_id += 2;
    }
    if ((bits & UINT64_C(1)) == 0) {
        ++service_id;
    }
    return service_id;
}

enum micros_bootstrap_error bootstrap_runtime_prepare_release(
    const struct micros_bootstrap_runtime *runtime,
    uint32_t service_id,
    uint64_t now,
    uint64_t retained_deadline,
    bool retain_deadline,
    struct bootstrap_transition_plan *plan
)
{
    struct bootstrap_transition_plan candidate;
    const struct micros_bootstrap_runtime_entry *entry;
    enum micros_bootstrap_error error;
    uint64_t prerequisites;
    uint16_t index;

    if (runtime == NULL || service_id == 0 || plan == NULL) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (runtime->phase != MICROS_BOOTSTRAP_PHASE_RUNNING) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    error = bootstrap_runtime_select_slot(runtime, service_id, &index);
    if (error != MICROS_BOOTSTRAP_OK) {
        return error;
    }
    if (
        runtime->starting_service_id != 0
        || runtime->next_order_index >= runtime->entry_count
        || runtime->ordered_service_ids[runtime->next_order_index]
            != service_id
    ) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    entry = &runtime->entries[index];
    if (
        entry->state != MICROS_BOOTSTRAP_SERVICE_PREPARED
        || entry->endpoint_state
            != MICROS_BOOTSTRAP_ENDPOINT_RESERVED
        || entry->profile_installed
        || entry->scheduler_assigned
    ) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    prerequisites = entry->prerequisites;
    if ((prerequisites & (UINT64_C(1) << 63)) != 0) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    while (prerequisites != 0) {
        uint16_t dependency_index;

        error = bootstrap_runtime_select_slot(
            runtime, lowest_prerequisite_id(prerequisites), &dependency_index
        );
        if (error == MICROS_BOOTSTRAP_ERROR_INVARIANT) {
            return error;
        }
        if (
            error != MICROS_BOOTSTRAP_OK
            || runtime->entries[dependency_index].state
                != MICROS_BOOTSTRAP_SERVICE_READY
        ) {
            return MICROS_BOOTSTRAP_ERROR_STATE;
        }
        prerequisites &= prerequisites - 1;
    }
    if (entry->ready_timeout_counter_ticks == 0) {
        return MICROS_BOOTSTRAP_ERROR_RANGE;
    }
    if (retain_deadline) {
        if (retained_deadline == 0 || now >= retained_deadline) {
            return MICROS_BOOTSTRAP_ERROR_STATE;
        }
    } else if (
        UINT64_MAX - now < entry->ready_timeout_counter_ticks
    ) {
        return MICROS_BOOTSTRAP_ERROR_RANGE;
    }
    initialize_transition_plan(runtime, index, &candidate);
    candidate.entry.profile_installed = true;
    candidate.entry.endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_ACTIVE;
    candidate.entry.scheduler_assigned = true;
    candidate.entry.state = MICROS_BOOTSTRAP_SERVICE_STARTING;
    candidate.entry.ready_deadline = retain_deadline
        ? retained_deadline
        : now + entry->ready_timeout_counter_ticks;
    candidate.starting_service_id = service_id;
    bootstrap_manifest_copy_bytes(plan, &candidate, sizeof(*plan));
    return MICROS_BOOTSTRAP_OK;
}

void bootstrap_runtime_commit_prevalidated(
    struct micros_bootstrap_runtime *runtime,
    const struct bootstrap_transition_plan *plan
)
{
    bootstrap_manifest_copy_bytes(
        &runtime->entries[plan->entry_index], &plan->entry, sizeof(plan->entry)
    );
    runtime->next_order_index = plan->next_order_index;
    runtime->phase = plan->phase;
    runtime->controller_service_id = plan->controller_service_id;
    runtime->starting_service_id = plan->starting_service_id;
}

static enum micros_bootstrap_error runtime_release(
    struct micros_bootstrap_runtime *runtime,
    uint32_t service_id,
    uint64_t now,
    uint64_t retained_deadline,
    bool retain_deadline
)
{
    struct bootstrap_transition_plan plan;
    enum micros_bootstrap_error error = bootstrap_runtime_prepare_release(
        runtime, service_id, now, retained_deadline, retain_deadline, &plan
    );

    if (error == MICROS_BOOTSTRAP_OK) {
        bootstrap_runtime_commit_prevalidated(runtime, &plan);
    }
    return error;
}

enum micros_bootstrap_error micros_bootstrap_runtime_release(
    struct micros_bootstrap_runtime *runtime,
    uint32_t service_id,
    uint64_t now
)
{
    return runtime_release(runtime, service_id, now, 0, false);
}

enum micros_bootstrap_error
micros_bootstrap_runtime_release_retaining_deadline(
    struct micros_bootstrap_runtime *runtime,
    uint32_t service_id,
    uint64_t now,
    uint64_t deadline
)
{
    return runtime_release(
        runtime,
        service_id,
        now,
        deadline,
        true
    );
}

enum micros_bootstrap_error bootstrap_runtime_prepare_ready(
    const struct micros_bootstrap_runtime *runtime,
    uint32_t service_id,
    uint64_t now,
    struct bootstrap_transition_plan *plan
)
{
    struct bootstrap_transition_plan candidate;
    const struct micros_bootstrap_runtime_entry *entry;
    enum micros_bootstrap_error error;
    uint16_t index;

    if (runtime == NULL || service_id == 0 || plan == NULL) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (runtime->phase != MICROS_BOOTSTRAP_PHASE_RUNNING) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    error = bootstrap_runtime_select_slot(runtime, service_id, &index);
    if (error != MICROS_BOOTSTRAP_OK) {
        return error;
    }
    if (runtime->starting_service_id != service_id) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    entry = &runtime->entries[index];
    if (
        entry->state != MICROS_BOOTSTRAP_SERVICE_STARTING
        || entry->ready_deadline == 0
        || now >= entry->ready_deadline
    ) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    if (
        runtime->next_order_index == 0
        || runtime->next_order_index >= runtime->entry_count
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    initialize_transition_plan(runtime, index, &candidate);
    candidate.entry.state = MICROS_BOOTSTRAP_SERVICE_READY;
    candidate.entry.ready_deadline = 0;
    candidate.starting_service_id = 0;
    ++candidate.next_order_index;
    bootstrap_manifest_copy_bytes(plan, &candidate, sizeof(*plan));
    return MICROS_BOOTSTRAP_OK;
}

enum micros_bootstrap_error bootstrap_runtime_prepare_complete(
    const struct micros_bootstrap_runtime *runtime,
    struct bootstrap_transition_plan *plan
)
{
    struct bootstrap_transition_plan candidate;
    const struct micros_bootstrap_runtime_entry *controller;
    uint16_t controller_index;
    size_t index;

    if (runtime == NULL || plan == NULL) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (
        runtime->phase != MICROS_BOOTSTRAP_PHASE_RUNNING
        || runtime->starting_service_id != 0
        || runtime->next_order_index != runtime->entry_count
    ) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    if (
        runtime->entry_count == 0
        || runtime->entry_count > MICROS_BOOTSTRAP_SERVICE_CAPACITY
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    for (index = 0; index < runtime->entry_count; ++index) {
        if (
            runtime->entries[index].state
                != MICROS_BOOTSTRAP_SERVICE_READY
            || runtime->entries[index].ready_deadline != 0
        ) {
            return MICROS_BOOTSTRAP_ERROR_STATE;
        }
    }
    if (
        bootstrap_runtime_select_slot(runtime, runtime->controller_service_id, &controller_index)
            != MICROS_BOOTSTRAP_OK
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    controller = &runtime->entries[controller_index];
    if (
        controller->endpoint_state
            != MICROS_BOOTSTRAP_ENDPOINT_ACTIVE
        || !controller->profile_installed
        || !controller->scheduler_assigned
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    initialize_transition_plan(runtime, controller_index, &candidate);
    candidate.entry.endpoint_state =
        MICROS_BOOTSTRAP_ENDPOINT_SOURCE_ONLY;
    candidate.entry.scheduler_assigned = false;
    candidate.phase = MICROS_BOOTSTRAP_PHASE_SEALED;
    candidate.controller_service_id = 0;
    bootstrap_manifest_copy_bytes(plan, &candidate, sizeof(*plan));
    return MICROS_BOOTSTRAP_OK;
}

enum micros_bootstrap_error micros_bootstrap_runtime_accept_ready(
    struct micros_bootstrap_runtime *runtime,
    uint32_t service_id,
    uint64_t now
)
{
    struct bootstrap_transition_plan plan;
    enum micros_bootstrap_error error = bootstrap_runtime_prepare_ready(
        runtime, service_id, now, &plan
    );

    if (error == MICROS_BOOTSTRAP_OK) {
        bootstrap_runtime_commit_prevalidated(runtime, &plan);
    }
    return error;
}

enum micros_bootstrap_error micros_bootstrap_runtime_complete(
    struct micros_bootstrap_runtime *runtime
)
{
    struct bootstrap_transition_plan plan;
    enum micros_bootstrap_error error = bootstrap_runtime_prepare_complete(
        runtime, &plan
    );

    if (error == MICROS_BOOTSTRAP_OK) {
        bootstrap_runtime_commit_prevalidated(runtime, &plan);
    }
    return error;
}

enum micros_bootstrap_ready_class micros_bootstrap_classify_ready(
    const struct micros_bootstrap_runtime *runtime,
    uint32_t source_service_id,
    uint32_t payload_service_id,
    bool message_shape_valid
)
{
    const struct micros_bootstrap_runtime_entry *entry;

    if (!message_shape_valid) {
        return MICROS_BOOTSTRAP_READY_MALFORMED;
    }
    if (runtime == NULL || source_service_id == 0) {
        return MICROS_BOOTSTRAP_READY_FOREIGN;
    }
    entry = bootstrap_runtime_find_entry(runtime, source_service_id);
    if (entry == NULL) {
        return MICROS_BOOTSTRAP_READY_FOREIGN;
    }
    if (source_service_id == runtime->starting_service_id) {
        return payload_service_id == source_service_id
            ? MICROS_BOOTSTRAP_READY_ACCEPT
            : MICROS_BOOTSTRAP_READY_MALFORMED;
    }
    if (entry->state == MICROS_BOOTSTRAP_SERVICE_READY) {
        return MICROS_BOOTSTRAP_READY_DUPLICATE;
    }
    if (entry->state == MICROS_BOOTSTRAP_SERVICE_PREPARED) {
        return MICROS_BOOTSTRAP_READY_EARLY;
    }
    return MICROS_BOOTSTRAP_READY_FOREIGN;
}
