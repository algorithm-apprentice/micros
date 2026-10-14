#include "kernel/bootstrap_manifest_internal.h"

static struct micros_bootstrap_runtime_entry *runtime_entry(
    struct micros_bootstrap_runtime *runtime,
    uint32_t service_id
)
{
    size_t index;

    for (index = 0; index < runtime->entry_count; ++index) {
        if (runtime->entries[index].service_id == service_id) {
            return &runtime->entries[index];
        }
    }
    return NULL;
}

static const struct micros_bootstrap_runtime_entry *runtime_entry_const(
    const struct micros_bootstrap_runtime *runtime,
    uint32_t service_id
)
{
    size_t index;

    for (index = 0; index < runtime->entry_count; ++index) {
        if (runtime->entries[index].service_id == service_id) {
            return &runtime->entries[index];
        }
    }
    return NULL;
}

enum micros_bootstrap_error micros_bootstrap_runtime_initialize(
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_bootstrap_manifest_plan *plan,
    struct micros_bootstrap_runtime *runtime
)
{
    struct micros_bootstrap_runtime candidate;
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
    bootstrap_manifest_copy_bytes(runtime, &candidate, sizeof(*runtime));
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error runtime_release(
    struct micros_bootstrap_runtime *runtime,
    uint32_t service_id,
    uint64_t now,
    uint64_t retained_deadline,
    bool retain_deadline
)
{
    struct micros_bootstrap_runtime candidate;
    struct micros_bootstrap_runtime_entry *entry;
    uint64_t ready_ids = 0;
    size_t index;

    if (runtime == NULL || service_id == 0) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (runtime->phase != MICROS_BOOTSTRAP_PHASE_RUNNING) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    if (runtime_entry(runtime, service_id) == NULL) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (
        runtime->starting_service_id != 0
        || runtime->next_order_index >= runtime->entry_count
        || runtime->ordered_service_ids[runtime->next_order_index]
            != service_id
    ) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    bootstrap_manifest_copy_bytes(&candidate, runtime, sizeof(candidate));
    entry = runtime_entry(&candidate, service_id);
    if (
        entry == NULL
        || entry->state != MICROS_BOOTSTRAP_SERVICE_PREPARED
        || entry->endpoint_state
            != MICROS_BOOTSTRAP_ENDPOINT_RESERVED
        || entry->profile_installed
        || entry->scheduler_assigned
    ) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    for (index = 0; index < candidate.entry_count; ++index) {
        if (
            candidate.entries[index].state
                == MICROS_BOOTSTRAP_SERVICE_READY
        ) {
            ready_ids |= UINT64_C(1)
                << (candidate.entries[index].service_id - 1);
        }
    }
    if ((entry->prerequisites & ~ready_ids) != 0) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
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
    entry->profile_installed = true;
    entry->endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_ACTIVE;
    entry->scheduler_assigned = true;
    entry->state = MICROS_BOOTSTRAP_SERVICE_STARTING;
    entry->ready_deadline = retain_deadline
        ? retained_deadline
        : now + entry->ready_timeout_counter_ticks;
    candidate.starting_service_id = service_id;
    bootstrap_manifest_copy_bytes(runtime, &candidate, sizeof(*runtime));
    return MICROS_BOOTSTRAP_OK;
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

enum micros_bootstrap_error micros_bootstrap_runtime_accept_ready(
    struct micros_bootstrap_runtime *runtime,
    uint32_t service_id,
    uint64_t now
)
{
    struct micros_bootstrap_runtime candidate;
    struct micros_bootstrap_runtime_entry *entry;

    if (runtime == NULL || service_id == 0) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (runtime->phase != MICROS_BOOTSTRAP_PHASE_RUNNING) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    if (runtime_entry(runtime, service_id) == NULL) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (runtime->starting_service_id != service_id) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    bootstrap_manifest_copy_bytes(&candidate, runtime, sizeof(candidate));
    entry = runtime_entry(&candidate, service_id);
    if (
        entry == NULL
        || entry->state != MICROS_BOOTSTRAP_SERVICE_STARTING
        || entry->ready_deadline == 0
        || now >= entry->ready_deadline
    ) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    entry->state = MICROS_BOOTSTRAP_SERVICE_READY;
    entry->ready_deadline = 0;
    candidate.starting_service_id = 0;
    ++candidate.next_order_index;
    bootstrap_manifest_copy_bytes(runtime, &candidate, sizeof(*runtime));
    return MICROS_BOOTSTRAP_OK;
}

enum micros_bootstrap_error micros_bootstrap_runtime_complete(
    struct micros_bootstrap_runtime *runtime
)
{
    struct micros_bootstrap_runtime candidate;
    struct micros_bootstrap_runtime_entry *controller;
    size_t index;

    if (runtime == NULL) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (
        runtime->phase != MICROS_BOOTSTRAP_PHASE_RUNNING
        || runtime->starting_service_id != 0
        || runtime->next_order_index != runtime->entry_count
    ) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
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
    bootstrap_manifest_copy_bytes(&candidate, runtime, sizeof(candidate));
    controller = runtime_entry(
        &candidate,
        candidate.controller_service_id
    );
    if (
        controller == NULL
        || controller->endpoint_state
            != MICROS_BOOTSTRAP_ENDPOINT_ACTIVE
        || !controller->profile_installed
        || !controller->scheduler_assigned
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    controller->endpoint_state =
        MICROS_BOOTSTRAP_ENDPOINT_SOURCE_ONLY;
    controller->scheduler_assigned = false;
    candidate.phase = MICROS_BOOTSTRAP_PHASE_SEALED;
    candidate.controller_service_id = 0;
    bootstrap_manifest_copy_bytes(runtime, &candidate, sizeof(*runtime));
    return MICROS_BOOTSTRAP_OK;
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
    entry = runtime_entry_const(runtime, source_service_id);
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
