#include "kernel/endpoint/endpoint_private.h"
#include "kernel/endpoint/endpoint_internal.h"

static bool endpoint_is_pending_notification_source(
    const struct micros_endpoint_registry *registry,
    const struct micros_endpoint_record *source
);

enum micros_endpoint_error
micros_endpoint_preflight_source_only_for_teardown(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint
)
{
    const struct micros_endpoint_record *record;
    enum micros_endpoint_error error;
    if (registry == NULL || objects == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_endpoint_registry_validate_objects(registry, objects);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    error = endpoint_record_resolve_validated(
        registry,
        endpoint,
        &record
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (
        record->state != MICROS_ENDPOINT_STATE_ACTIVE
    ) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    return MICROS_ENDPOINT_OK;
}

enum micros_endpoint_error
micros_endpoint_preflight_source_only(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint
)
{
    const struct micros_endpoint_record *record;
    enum micros_endpoint_error error;
    size_t index;

    error = micros_endpoint_preflight_source_only_for_teardown(
        registry,
        objects,
        endpoint
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    error = endpoint_record_resolve_validated(
        registry,
        endpoint,
        &record
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (
        !endpoint_ipc_state_is_zero(record)
        || endpoint_is_pending_notification_source(registry, record)
    ) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];

        if (
            thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && (
                (
                    thread->ipc_queue_kind
                        == MICROS_IPC_QUEUE_RECEIVER
                    && thread->ipc_receive_source == endpoint
                )
                || (
                    thread->ipc_reply_token != 0
                    && thread->ipc_reply_callee == endpoint
                )
            )
        ) {
            return MICROS_ENDPOINT_ERROR_STATE;
        }
    }
    return MICROS_ENDPOINT_OK;
}

void micros_endpoint_commit_source_only_prevalidated(
    struct micros_endpoint_registry *registry,
    micros_endpoint_t endpoint
)
{
    struct micros_process_handle owner;

    (void)micros_endpoint_unpack(endpoint, &owner);
    registry->endpoints[owner.slot].state =
        MICROS_ENDPOINT_STATE_SOURCE_ONLY;
}

enum micros_endpoint_error micros_endpoint_seal_source_only(
    struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint
)
{
    const struct micros_endpoint_record *record;
    enum micros_endpoint_error error;

    if (registry == NULL || objects == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_endpoint_preflight_source_only(
        registry,
        objects,
        endpoint
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    error = endpoint_record_resolve_validated(
        registry,
        endpoint,
        &record
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (
        !endpoint_process_threads_are_held(objects, record->owner, false)
        || !endpoint_process_threads_have_clear_ipc_state(objects, record->owner)
    ) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    micros_endpoint_commit_source_only_prevalidated(
        registry,
        endpoint
    );
    return MICROS_ENDPOINT_OK;
}

static bool endpoint_has_foreign_waiters(
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint
)
{
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];

        if (thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE) {
            continue;
        }
        if (
            (
                thread->ipc_queue_kind
                    == MICROS_IPC_QUEUE_RECEIVER
                && thread->ipc_receive_source == endpoint
            )
            || (
                thread->ipc_reply_token != 0
                && thread->ipc_reply_callee == endpoint
            )
            || (
                thread->ipc_delivery_pending
                && thread->ipc_staged_result == MICROS_IPC_OK
                && thread->ipc_inbound_message.source == endpoint
            )
        ) {
            return true;
        }
    }
    return false;
}

static bool endpoint_is_pending_notification_source(
    const struct micros_endpoint_registry *registry,
    const struct micros_endpoint_record *source
)
{
    uint64_t source_bit = UINT64_C(1) << source->owner.slot;
    size_t index;

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct micros_endpoint_record *destination =
            &registry->endpoints[index];

        if (
            (
                destination->state
                    == MICROS_ENDPOINT_STATE_ACTIVE
                || destination->state
                    == MICROS_ENDPOINT_STATE_SOURCE_ONLY
            )
            && (
                destination->pending_notification_sources
                & source_bit
            ) != 0
        ) {
            return true;
        }
    }
    return false;
}

void micros_endpoint_close_commit_prevalidated(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_process_handle owner
)
{
    struct micros_endpoint_record *record =
        &registry->endpoints[owner.slot];
    struct micros_process *process =
        &objects->processes[owner.slot];

    endpoint_clear_endpoint_record(record);
    process->primary_endpoint = MICROS_PROCESS_ENDPOINT_NONE;
    process->privilege_profile = 0;
}

enum micros_endpoint_error micros_endpoint_close(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint
)
{
    const struct micros_endpoint_record *resolved_record;
    struct micros_process_handle owner;
    enum micros_endpoint_error error;

    if (registry == NULL || objects == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_endpoint_registry_validate_objects(registry, objects);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    error = endpoint_record_resolve_validated(
        registry,
        endpoint,
        &resolved_record
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (!endpoint_ipc_state_is_zero(resolved_record)) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    if (
        endpoint_is_pending_notification_source(
            registry,
            resolved_record
        )
    ) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    if (endpoint_has_foreign_waiters(objects, endpoint)) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    owner = resolved_record->owner;
    if (
        !endpoint_process_threads_are_held(objects, owner, false)
        || !endpoint_process_threads_have_clear_ipc_state(objects, owner)
    ) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }

    micros_endpoint_close_commit_prevalidated(
        registry,
        objects,
        owner
    );
    return MICROS_ENDPOINT_OK;
}
