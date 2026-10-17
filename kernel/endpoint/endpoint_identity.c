#include "kernel/endpoint/endpoint_private.h"

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

enum micros_endpoint_error endpoint_process_resolve_error(
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

bool endpoint_process_threads_are_held(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    bool require_unassigned
)
{
    size_t index;

    if (endpoint_process_has_current_thread(objects, process)) {
        return false;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];

        if (
            thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || !endpoint_process_handles_equal(thread->owner, process)
        ) {
            continue;
        }
        if (
            thread->runtime_flags != MICROS_THREAD_RTS_INACTIVE
            || (require_unassigned && thread->scheduler_assigned)
        ) {
            return false;
        }
    }
    return true;
}

bool endpoint_process_threads_have_clear_ipc_state(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process
)
{
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];

        if (
            thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || !endpoint_process_handles_equal(thread->owner, process)
        ) {
            continue;
        }
        if (!micros_thread_ipc_state_is_clear(thread)) {
            return false;
        }
    }
    return true;
}
