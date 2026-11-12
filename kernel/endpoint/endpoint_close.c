#include "kernel/endpoint/endpoint_private.h"

static enum micros_endpoint_error preflight_pending_source_references(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_record *source
)
{
    uint64_t bit = UINT64_C(1) << source->owner.slot;

    for (size_t index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct micros_endpoint_record *destination = &registry->endpoints[index];
        struct micros_endpoint_selection selected;
        bool pending = (destination->pending_notification_sources & bit) != 0;

        if (pending != (destination->pending_events[source->owner.slot] != 0)) {
            return MICROS_ENDPOINT_ERROR_INVARIANT;
        }
        if (!pending) {
            continue;
        }
        if (
            micros_endpoint_select(registry, objects, destination->value, &selected)
                != MICROS_ENDPOINT_OK
            || selected.record != destination
            || (destination->state != MICROS_ENDPOINT_STATE_ACTIVE
                && destination->state != MICROS_ENDPOINT_STATE_SOURCE_ONLY)
            || (source->state != MICROS_ENDPOINT_STATE_ACTIVE
                && source->state != MICROS_ENDPOINT_STATE_SOURCE_ONLY)
        ) {
            return MICROS_ENDPOINT_ERROR_INVARIANT;
        }
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    return MICROS_ENDPOINT_OK;
}

static enum micros_endpoint_error preflight_foreign_references(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_record *endpoint,
    bool include_staged
)
{
    for (size_t index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];
        struct micros_thread_handle handle;
        bool sender;

        if (thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE) {
            continue;
        }
        sender = micros_endpoint_names_slot(thread->ipc_send_destination, endpoint->owner.slot);
        if (
            !sender
            && !micros_endpoint_names_slot(thread->ipc_receive_source, endpoint->owner.slot)
            && !micros_endpoint_names_slot(thread->ipc_reply_callee, endpoint->owner.slot)
            && !(include_staged
                && micros_endpoint_names_slot(thread->ipc_inbound_message.source, endpoint->owner.slot))
        ) {
            continue;
        }
        handle.slot = (uint16_t)index;
        handle.generation = thread->generation;
        if (sender || !micros_endpoint_thread_ipc_state_is_valid(registry, objects, handle)) {
            return MICROS_ENDPOINT_ERROR_INVARIANT;
        }
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    return MICROS_ENDPOINT_OK;
}

static enum micros_endpoint_error resolve_teardown_endpoint(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    struct micros_endpoint_selection *selection
)
{
    enum micros_endpoint_error error = micros_endpoint_select(registry, objects, endpoint, selection);

    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    error = endpoint_preflight_record(registry, objects, selection);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    return selection->record->state == MICROS_ENDPOINT_STATE_ACTIVE
        ? MICROS_ENDPOINT_OK : MICROS_ENDPOINT_ERROR_STATE;
}

enum micros_endpoint_error micros_endpoint_preflight_source_only_for_teardown(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint
)
{
    struct micros_endpoint_selection selection;

    return resolve_teardown_endpoint(registry, objects, endpoint, &selection);
}

static enum micros_endpoint_error preflight_plain_source_only(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    struct micros_endpoint_selection *selection
)
{
    enum micros_endpoint_error error = resolve_teardown_endpoint(registry, objects, endpoint, selection);

    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (!endpoint_ipc_state_is_zero(selection->record)) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    error = preflight_pending_source_references(registry, objects, selection->record);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    return preflight_foreign_references(registry, objects, selection->record, false);
}

enum micros_endpoint_error micros_endpoint_preflight_source_only(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint
)
{
    struct micros_endpoint_selection selection;

    return preflight_plain_source_only(registry, objects, endpoint, &selection);
}

void micros_endpoint_commit_source_only_prevalidated(
    struct micros_endpoint_registry *registry,
    micros_endpoint_t endpoint
)
{
    struct micros_process_handle owner;

    (void)micros_endpoint_unpack(endpoint, &owner);
    registry->endpoints[owner.slot].state = MICROS_ENDPOINT_STATE_SOURCE_ONLY;
}

enum micros_endpoint_error micros_endpoint_seal_source_only(
    struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint
)
{
    struct micros_endpoint_selection selection;
    enum micros_endpoint_error error = preflight_plain_source_only(registry, objects, endpoint, &selection);

    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    error = endpoint_preflight_owned_threads(registry, objects, selection.record->owner, false);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    micros_endpoint_commit_source_only_prevalidated(registry, endpoint);
    return MICROS_ENDPOINT_OK;
}

void micros_endpoint_close_commit_prevalidated(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_process_handle owner
)
{
    endpoint_clear_endpoint_record(&registry->endpoints[owner.slot]);
    objects->processes[owner.slot].primary_endpoint = MICROS_PROCESS_ENDPOINT_NONE;
    objects->processes[owner.slot].privilege_profile = 0;
}

enum micros_endpoint_error micros_endpoint_close(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint
)
{
    struct micros_endpoint_selection selection;
    enum micros_endpoint_error error = micros_endpoint_select(registry, objects, endpoint, &selection);

    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    error = endpoint_preflight_record(registry, objects, &selection);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (!endpoint_ipc_state_is_zero(selection.record)) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    error = preflight_pending_source_references(registry, objects, selection.record);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    error = preflight_foreign_references(registry, objects, selection.record, true);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    error = endpoint_preflight_owned_threads(registry, objects, selection.record->owner, false);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    micros_endpoint_close_commit_prevalidated(registry, objects, selection.record->owner);
    return MICROS_ENDPOINT_OK;
}
