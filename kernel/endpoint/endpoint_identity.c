#include "kernel/endpoint/endpoint_private.h"

#include "kernel/endpoint/endpoint_internal.h"
#include "kernel/scheduler/scheduler_core_internal.h"

_Static_assert(
    MICROS_PROCESS_CAPACITY <= (1U << MICROS_ENDPOINT_SLOT_BITS),
    "implemented process capacity must fit the endpoint ABI"
);
_Static_assert(
    MICROS_PROCESS_GENERATION_MAX == MICROS_ENDPOINT_GENERATION_MAX,
    "process and endpoint generation limits must match"
);
_Static_assert(
    MICROS_PROCESS_CAPACITY == 64,
    "pending notification bitmap requires one bit per endpoint slot"
);

bool endpoint_process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

bool endpoint_thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

bool micros_endpoint_names_slot(micros_endpoint_t endpoint, uint16_t slot)
{
    return slot < MICROS_PROCESS_CAPACITY && endpoint != 0
        && (endpoint & ((UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) - 1)) == slot;
}

enum micros_endpoint_error endpoint_require_initialized(
    const struct micros_endpoint_registry *registry
)
{
    if (registry == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    if (
        registry->initialization_magic
            != MICROS_ENDPOINT_REGISTRY_MAGIC
    ) {
        return MICROS_ENDPOINT_ERROR_NOT_INITIALIZED;
    }
    return MICROS_ENDPOINT_OK;
}

enum micros_endpoint_error endpoint_preflight_registry(
    const struct micros_endpoint_registry *registry
)
{
    enum micros_endpoint_error error = endpoint_require_initialized(registry);

    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (
        registry->profile_count == 0
        || registry->profile_count >= MICROS_PRIVILEGE_PROFILE_CAPACITY
        || !endpoint_profile_is_zero(&registry->profiles[0])
    ) {
        return MICROS_ENDPOINT_ERROR_INVARIANT;
    }
    return MICROS_ENDPOINT_OK;
}

enum micros_endpoint_error micros_endpoint_pack(
    struct micros_process_handle process,
    micros_endpoint_t *endpoint
)
{
    micros_endpoint_t candidate;

    if (endpoint == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    if (
        process.slot >= (1U << MICROS_ENDPOINT_SLOT_BITS)
        || process.generation == 0
        || process.generation > MICROS_ENDPOINT_GENERATION_MAX
    ) {
        return MICROS_ENDPOINT_ERROR_ENDPOINT;
    }
    candidate =
        (process.generation << MICROS_ENDPOINT_SLOT_BITS)
        | process.slot;
    if (
        candidate == MICROS_ENDPOINT_NONE
        || candidate == MICROS_ENDPOINT_ANY
    ) {
        return MICROS_ENDPOINT_ERROR_ENDPOINT;
    }
    *endpoint = candidate;
    return MICROS_ENDPOINT_OK;
}

enum micros_endpoint_error micros_endpoint_unpack(
    micros_endpoint_t endpoint,
    struct micros_process_handle *process
)
{
    struct micros_process_handle candidate;

    if (process == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    if (
        endpoint == MICROS_ENDPOINT_NONE
        || endpoint == MICROS_ENDPOINT_ANY
    ) {
        return MICROS_ENDPOINT_ERROR_STALE;
    }
    candidate.slot =
        (uint16_t)(endpoint & ((1U << MICROS_ENDPOINT_SLOT_BITS) - 1));
    candidate.generation =
        endpoint >> MICROS_ENDPOINT_SLOT_BITS;
    if (
        candidate.generation == 0
        || candidate.generation > MICROS_ENDPOINT_GENERATION_MAX
    ) {
        return MICROS_ENDPOINT_ERROR_STALE;
    }
    *process = candidate;
    return MICROS_ENDPOINT_OK;
}

static enum micros_endpoint_error endpoint_process_resolve_error(
    enum micros_kernel_object_error error
)
{
    if (error == MICROS_KERNEL_OBJECT_ERROR_STALE) {
        return MICROS_ENDPOINT_ERROR_STALE;
    }
    if (error == MICROS_KERNEL_OBJECT_ERROR_ARGUMENT) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    return MICROS_ENDPOINT_ERROR_INVARIANT;
}

enum micros_endpoint_error endpoint_record_resolve_validated(
    const struct micros_endpoint_registry *registry,
    micros_endpoint_t endpoint,
    const struct micros_endpoint_record **record
)
{
    struct micros_process_handle owner;
    const struct micros_endpoint_record *candidate;
    enum micros_endpoint_error error;

    error = micros_endpoint_unpack(endpoint, &owner);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (owner.slot >= MICROS_PROCESS_CAPACITY) {
        return MICROS_ENDPOINT_ERROR_STALE;
    }
    candidate = &registry->endpoints[owner.slot];
    if (
        candidate->state == MICROS_ENDPOINT_STATE_FREE
        || candidate->value != endpoint
        || !endpoint_process_handles_equal(candidate->owner, owner)
    ) {
        return MICROS_ENDPOINT_ERROR_STALE;
    }
    *record = candidate;
    return MICROS_ENDPOINT_OK;
}

static bool selected_process_metadata_is_valid(
    const struct micros_kernel_objects *objects,
    const struct micros_process *process
)
{
    return objects->live_process_count != 0
        && process->live_thread_count <= objects->max_threads_per_process
        && process->live_thread_count <= objects->live_thread_count
        && process->privilege_profile < MICROS_PRIVILEGE_PROFILE_CAPACITY;
}

enum micros_endpoint_error endpoint_preflight_process(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle handle,
    const struct micros_process **process
)
{
    const struct micros_process *candidate;
    enum micros_endpoint_error error = endpoint_preflight_registry(registry);
    enum micros_kernel_object_error object_error;

    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (scheduler_core_preflight_registry(objects) != MICROS_KERNEL_OBJECT_OK) {
        return MICROS_ENDPOINT_ERROR_INVARIANT;
    }
    object_error = micros_process_resolve(objects, handle, &candidate);
    if (object_error != MICROS_KERNEL_OBJECT_OK) {
        return endpoint_process_resolve_error(object_error);
    }
    if (!selected_process_metadata_is_valid(objects, candidate)) {
        return MICROS_ENDPOINT_ERROR_INVARIANT;
    }
    *process = candidate;
    return MICROS_ENDPOINT_OK;
}

static bool selected_process_is_valid(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_record *record,
    const struct micros_process *process
)
{
    if (
        !selected_process_metadata_is_valid(objects, process)
        || process->primary_endpoint != record->value
        || !process->endpoint_lifecycle_consumed
    ) {
        return false;
    }
    if (process->privilege_profile == 0) {
        return record->state == MICROS_ENDPOINT_STATE_RESERVED;
    }
    return registry->profiles[process->privilege_profile].id
        == process->privilege_profile;
}

enum micros_endpoint_error micros_endpoint_select(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    struct micros_endpoint_selection *selection
)
{
    struct micros_endpoint_selection candidate;
    struct micros_process_handle requested;
    micros_endpoint_t canonical;
    enum micros_endpoint_error error;

    if (registry == NULL || objects == NULL || selection == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = endpoint_preflight_registry(registry);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (scheduler_core_preflight_registry(objects) != MICROS_KERNEL_OBJECT_OK) {
        return MICROS_ENDPOINT_ERROR_INVARIANT;
    }
    error = micros_endpoint_unpack(endpoint, &requested);
    if (error != MICROS_ENDPOINT_OK || requested.slot >= MICROS_PROCESS_CAPACITY) {
        return MICROS_ENDPOINT_ERROR_STALE;
    }
    candidate.record = &registry->endpoints[requested.slot];
    if (candidate.record->state == MICROS_ENDPOINT_STATE_FREE) {
        return candidate.record->owner.slot != 0
                || candidate.record->owner.generation != 0
                || candidate.record->value != 0
            ? MICROS_ENDPOINT_ERROR_INVARIANT
            : MICROS_ENDPOINT_ERROR_STALE;
    }
    if (
        (candidate.record->state != MICROS_ENDPOINT_STATE_RESERVED
            && candidate.record->state != MICROS_ENDPOINT_STATE_ACTIVE
            && candidate.record->state != MICROS_ENDPOINT_STATE_SOURCE_ONLY)
        || candidate.record->owner.slot != requested.slot
        || micros_endpoint_pack(candidate.record->owner, &canonical) != MICROS_ENDPOINT_OK
        || candidate.record->value != canonical
    ) {
        return MICROS_ENDPOINT_ERROR_INVARIANT;
    }
    if (canonical != endpoint) {
        return MICROS_ENDPOINT_ERROR_STALE;
    }
    if (
        micros_process_resolve(objects, candidate.record->owner, &candidate.process)
            != MICROS_KERNEL_OBJECT_OK
        || !selected_process_is_valid(registry, objects, candidate.record, candidate.process)
    ) {
        return MICROS_ENDPOINT_ERROR_INVARIANT;
    }
    *selection = candidate;
    return MICROS_ENDPOINT_OK;
}

bool endpoint_process_has_current_thread(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process
)
{
    size_t index;

    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        const struct micros_hart *hart = &objects->harts[index];
        const struct micros_thread *thread;

        if (
            hart->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || hart->current_thread.generation == 0
        ) {
            continue;
        }
        thread = &objects->threads[hart->current_thread.slot];
        if (endpoint_process_handles_equal(thread->owner, process)) {
            return true;
        }
    }
    return false;
}
