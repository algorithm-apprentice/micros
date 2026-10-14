#include "bootstrap_runtime_model_internal.h"

#include <string.h>

static bool reference_is_zero(const struct reference_runtime *runtime)
{
    const struct reference_runtime zero = {0};

    return memcmp(runtime, &zero, sizeof(*runtime)) == 0;
}

static struct reference_runtime_entry *reference_entry(
    struct reference_runtime *runtime,
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

static const struct reference_runtime_entry *reference_entry_const(
    const struct reference_runtime *runtime,
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

enum micros_bootstrap_error bootstrap_runtime_reference_initialize(
    struct reference_runtime *runtime
)
{
    if (!reference_is_zero(runtime)) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    runtime->phase = MICROS_BOOTSTRAP_PHASE_RUNNING;
    runtime->entry_count = 2;
    runtime->next_order_index = 1;
    runtime->controller_service_id = 1;
    runtime->entries[0] = (struct reference_runtime_entry){
        .service_id = 2,
        .prerequisites = UINT64_C(1),
        .ready_timeout_counter_ticks = 100,
        .state = MICROS_BOOTSTRAP_SERVICE_PREPARED,
        .endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_RESERVED,
    };
    runtime->entries[1] = (struct reference_runtime_entry){
        .service_id = 1,
        .state = MICROS_BOOTSTRAP_SERVICE_READY,
        .endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_ACTIVE,
        .profile_installed = true,
        .scheduler_assigned = true,
    };
    runtime->ordered_service_ids[0] = 1;
    runtime->ordered_service_ids[1] = 2;
    return MICROS_BOOTSTRAP_OK;
}

enum micros_bootstrap_error bootstrap_runtime_reference_release(
    struct reference_runtime *runtime,
    uint32_t service_id,
    uint64_t now
)
{
    struct reference_runtime_entry *entry;

    if (service_id == 0) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (runtime->phase != MICROS_BOOTSTRAP_PHASE_RUNNING) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    entry = reference_entry(runtime, service_id);
    if (entry == NULL) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (
        runtime->starting_service_id != 0
        || runtime->next_order_index >= runtime->entry_count
        || runtime->ordered_service_ids[runtime->next_order_index]
            != service_id
        || entry->state != MICROS_BOOTSTRAP_SERVICE_PREPARED
        || entry->endpoint_state
            != MICROS_BOOTSTRAP_ENDPOINT_RESERVED
        || entry->profile_installed
        || entry->scheduler_assigned
    ) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    if (
        entry->ready_timeout_counter_ticks == 0
        || UINT64_MAX - now < entry->ready_timeout_counter_ticks
    ) {
        return MICROS_BOOTSTRAP_ERROR_RANGE;
    }
    entry->profile_installed = true;
    entry->endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_ACTIVE;
    entry->scheduler_assigned = true;
    entry->state = MICROS_BOOTSTRAP_SERVICE_STARTING;
    entry->ready_deadline =
        now + entry->ready_timeout_counter_ticks;
    runtime->starting_service_id = service_id;
    return MICROS_BOOTSTRAP_OK;
}

enum micros_bootstrap_error bootstrap_runtime_reference_accept_ready(
    struct reference_runtime *runtime,
    uint32_t service_id,
    uint64_t now
)
{
    struct reference_runtime_entry *entry;

    if (service_id == 0) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (runtime->phase != MICROS_BOOTSTRAP_PHASE_RUNNING) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    entry = reference_entry(runtime, service_id);
    if (entry == NULL) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (
        runtime->starting_service_id != service_id
        || entry->state != MICROS_BOOTSTRAP_SERVICE_STARTING
        || entry->ready_deadline == 0
        || now >= entry->ready_deadline
    ) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    entry->state = MICROS_BOOTSTRAP_SERVICE_READY;
    entry->ready_deadline = 0;
    runtime->starting_service_id = 0;
    ++runtime->next_order_index;
    return MICROS_BOOTSTRAP_OK;
}

enum micros_bootstrap_error bootstrap_runtime_reference_complete(
    struct reference_runtime *runtime
)
{
    struct reference_runtime_entry *controller;
    size_t index;

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
    controller = reference_entry(runtime, 1);
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
    runtime->phase = MICROS_BOOTSTRAP_PHASE_SEALED;
    runtime->controller_service_id = 0;
    return MICROS_BOOTSTRAP_OK;
}

enum micros_bootstrap_ready_class bootstrap_runtime_reference_classify_ready(
    const struct reference_runtime *runtime,
    uint32_t source_service_id,
    uint32_t payload_service_id,
    bool message_shape_valid
)
{
    const struct reference_runtime_entry *entry;

    if (!message_shape_valid) {
        return MICROS_BOOTSTRAP_READY_MALFORMED;
    }
    if (source_service_id == 0) {
        return MICROS_BOOTSTRAP_READY_FOREIGN;
    }
    entry = reference_entry_const(runtime, source_service_id);
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

void bootstrap_runtime_project_reference(
    const struct reference_runtime *reference,
    struct micros_bootstrap_runtime *runtime
)
{
    size_t index;

    memset(runtime, 0, sizeof(*runtime));
    runtime->phase = reference->phase;
    runtime->entry_count = reference->entry_count;
    runtime->next_order_index = reference->next_order_index;
    runtime->controller_service_id =
        reference->controller_service_id;
    runtime->starting_service_id = reference->starting_service_id;
    for (index = 0; index < reference->entry_count; ++index) {
        runtime->entries[index].service_id =
            reference->entries[index].service_id;
        runtime->entries[index].prerequisites =
            reference->entries[index].prerequisites;
        runtime->entries[index].ready_timeout_counter_ticks =
            reference->entries[index].ready_timeout_counter_ticks;
        runtime->entries[index].ready_deadline =
            reference->entries[index].ready_deadline;
        runtime->entries[index].state =
            reference->entries[index].state;
        runtime->entries[index].endpoint_state =
            reference->entries[index].endpoint_state;
        runtime->entries[index].profile_installed =
            reference->entries[index].profile_installed;
        runtime->entries[index].scheduler_assigned =
            reference->entries[index].scheduler_assigned;
        runtime->ordered_service_ids[index] =
            reference->ordered_service_ids[index];
    }
}
