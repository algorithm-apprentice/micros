#include "ipc_internal.h"

#include "scheduler_core_internal.h"

static void canonicalize_notification(
    struct micros_ipc_message *message,
    micros_endpoint_t source,
    uint64_t event_mask
)
{
    size_t index;

    ipc_clear_message(message);
    message->source = source;
    message->type = MICROS_IPC_TYPE_KERNEL_NOTIFICATION;
    for (index = 0; index < sizeof(event_mask); ++index) {
        message->payload[index] =
            (uint8_t)(event_mask >> (index * 8));
    }
}

enum micros_ipc_error ipc_find_pending_notification(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_record *destination,
    micros_endpoint_t source_filter,
    struct ipc_pending_notification_plan *plan
)
{
    const struct micros_endpoint_record *source;
    const struct micros_endpoint_record *resolved_source;
    size_t source_slot;
    enum micros_endpoint_error endpoint_error;

    if (
        registry == NULL
        || objects == NULL
        || destination == NULL
        || plan == NULL
        || source_filter == MICROS_ENDPOINT_NONE
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    if (source_filter != MICROS_ENDPOINT_ANY) {
        endpoint_error = micros_endpoint_resolve_active(
            registry,
            objects,
            source_filter,
            &resolved_source
        );
        if (endpoint_error != MICROS_ENDPOINT_OK) {
            return ipc_endpoint_error_to_ipc(endpoint_error);
        }
        source_slot = resolved_source->owner.slot;
        if (
            (
                destination->pending_notification_sources
                & (UINT64_C(1) << source_slot)
            ) == 0
        ) {
            return MICROS_IPC_ERROR_NOT_READY;
        }
        source = resolved_source;
    } else {
        if (destination->pending_kernel_events != 0) {
            plan->kernel_origin = true;
            plan->source_slot = MICROS_PROCESS_CAPACITY;
            plan->source_endpoint = MICROS_ENDPOINT_NONE;
            plan->event_mask =
                destination->pending_kernel_events;
            canonicalize_notification(
                &plan->message,
                plan->source_endpoint,
                plan->event_mask
            );
            return MICROS_IPC_OK;
        }
        for (
            source_slot = 0;
            source_slot < MICROS_PROCESS_CAPACITY;
            ++source_slot
        ) {
            if (
                (
                    destination->pending_notification_sources
                    & (UINT64_C(1) << source_slot)
                ) != 0
            ) {
                break;
            }
        }
        if (source_slot == MICROS_PROCESS_CAPACITY) {
            return MICROS_IPC_ERROR_NOT_READY;
        }
        source = &registry->endpoints[source_slot];
        endpoint_error = micros_endpoint_resolve_active(
            registry,
            objects,
            source->value,
            &resolved_source
        );
        if (
            endpoint_error != MICROS_ENDPOINT_OK
            || resolved_source != source
            || resolved_source->owner.slot != source_slot
        ) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
    }
    if (
        destination->pending_events[source_slot] == 0
        || source->state != MICROS_ENDPOINT_STATE_ACTIVE
        || source->owner.slot != source_slot
        || (
            source_filter != MICROS_ENDPOINT_ANY
            && source->value != source_filter
        )
    ) {
        return MICROS_IPC_ERROR_INVARIANT;
    }

    plan->kernel_origin = false;
    plan->source_slot = source_slot;
    plan->source_endpoint = source->value;
    plan->event_mask = destination->pending_events[source_slot];
    canonicalize_notification(
        &plan->message,
        plan->source_endpoint,
        plan->event_mask
    );
    return MICROS_IPC_OK;
}

void ipc_consume_pending_notification(
    struct micros_endpoint_record *destination,
    const struct ipc_pending_notification_plan *plan
)
{
    if (plan->kernel_origin) {
        destination->pending_kernel_events = 0;
    } else {
        destination->pending_events[plan->source_slot] = 0;
        destination->pending_notification_sources &=
            ~(UINT64_C(1) << plan->source_slot);
    }
}

static enum micros_ipc_error resolve_notification_destination(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle notifier_handle,
    micros_endpoint_t destination_endpoint,
    micros_endpoint_t *source_endpoint,
    struct micros_endpoint_record **destination
)
{
    const struct micros_endpoint_record *resolved_destination;
    enum micros_endpoint_error endpoint_error;
    enum micros_ipc_error error;

    endpoint_error =
        micros_endpoint_registry_validate_objects(registry, objects);
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return ipc_endpoint_error_to_ipc(endpoint_error);
    }
    error = ipc_resolve_operation_thread(
        objects,
        notifier_handle,
        source_endpoint
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    endpoint_error = micros_endpoint_authorize_target(
        registry,
        objects,
        *source_endpoint,
        MICROS_PRIVILEGE_OPERATION_NOTIFY,
        destination_endpoint
    );
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return ipc_endpoint_error_to_ipc(endpoint_error);
    }
    endpoint_error = micros_endpoint_resolve_active(
        registry,
        objects,
        destination_endpoint,
        &resolved_destination
    );
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return ipc_endpoint_error_to_ipc(endpoint_error);
    }
    *destination =
        &registry->endpoints[resolved_destination->owner.slot];
    return MICROS_IPC_OK;
}

static enum micros_ipc_error deliver_notification_to_receiver(
    struct micros_kernel_objects *objects,
    struct micros_endpoint_record *destination,
    struct micros_thread_handle notifier_handle,
    struct micros_thread_handle previous,
    struct micros_thread_handle receiver_handle,
    struct micros_thread *receiver,
    const struct micros_ipc_message *notification
)
{
    enum micros_kernel_object_error scheduler_error;

    if (
        (
            receiver->runtime_flags
            & MICROS_THREAD_RTS_IPC_MASK
        ) != MICROS_THREAD_RTS_IPC_RECEIVE
    ) {
        return MICROS_IPC_ERROR_STATE;
    }
    scheduler_error = micros_scheduler_commit_ipc_wake_pair(
        objects,
        notifier_handle,
        MICROS_THREAD_RTS_INACTIVE,
        receiver_handle,
        MICROS_THREAD_RTS_IPC_RECEIVE
    );
    if (scheduler_error != MICROS_KERNEL_OBJECT_OK) {
        return ipc_scheduler_error_to_ipc(scheduler_error);
    }

    ipc_unlink_thread(
        objects,
        previous,
        receiver_handle,
        &destination->receiver_head,
        &destination->receiver_tail
    );
    receiver->ipc_queue_kind = MICROS_IPC_QUEUE_NONE;
    receiver->ipc_next.slot = 0;
    receiver->ipc_next.generation = 0;
    receiver->ipc_receive_source = 0;
    ipc_stage_delivery(receiver, receiver->ipc_receive_buffer, notification);
    return MICROS_IPC_OK;
}

static enum micros_ipc_error coalesce_notification(
    struct micros_kernel_objects *objects,
    struct micros_endpoint_record *destination,
    struct micros_thread_handle notifier_handle,
    size_t source_slot,
    uint64_t event_mask
)
{
    enum micros_kernel_object_error scheduler_error =
        micros_scheduler_commit_ipc_wake(
            objects,
            notifier_handle,
            MICROS_THREAD_RTS_INACTIVE
        );
    if (scheduler_error != MICROS_KERNEL_OBJECT_OK) {
        return ipc_scheduler_error_to_ipc(scheduler_error);
    }
    destination->pending_events[source_slot] |= event_mask;
    destination->pending_notification_sources |=
        UINT64_C(1) << source_slot;
    return MICROS_IPC_OK;
}

enum micros_ipc_error micros_ipc_notify(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle notifier_handle,
    micros_endpoint_t destination_endpoint,
    uint64_t event_mask
)
{
    struct micros_endpoint_record *destination;
    struct micros_thread_handle previous = {0, 0};
    struct micros_thread_handle receiver_handle = {0, 0};
    struct micros_thread *receiver = NULL;
    struct micros_ipc_message notification;
    micros_endpoint_t source_endpoint;
    size_t source_slot;
    enum micros_ipc_error error;
    bool has_receiver;

    if (
        registry == NULL
        || objects == NULL
        || event_mask == 0
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    error = resolve_notification_destination(
        registry,
        objects,
        notifier_handle,
        destination_endpoint,
        &source_endpoint,
        &destination
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    source_slot = objects->threads[notifier_handle.slot].owner.slot;
    canonicalize_notification(&notification, source_endpoint, event_mask);
    has_receiver = ipc_find_matching_receiver(
        destination,
        objects,
        source_endpoint,
        &previous,
        &receiver_handle,
        &receiver
    );
    if (
        has_receiver
        && (receiver->runtime_flags & MICROS_THREAD_RTS_IPC_REPLY) == 0
    ) {
        return deliver_notification_to_receiver(
            objects,
            destination,
            notifier_handle,
            previous,
            receiver_handle,
            receiver,
            &notification
        );
    }
    return coalesce_notification(
        objects,
        destination,
        notifier_handle,
        source_slot,
        event_mask
    );
}

enum micros_ipc_error micros_ipc_inject_kernel_notification(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    micros_endpoint_t destination_endpoint,
    uint64_t event_mask
)
{
    struct micros_ipc_kernel_notification_plan plan;
    enum micros_ipc_error error;

    error = micros_ipc_prepare_kernel_notification(
        registry,
        objects,
        destination_endpoint,
        event_mask,
        &plan
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    micros_ipc_commit_kernel_notification_prevalidated(
        registry,
        objects,
        &plan
    );
    return MICROS_IPC_OK;
}

enum micros_ipc_error micros_ipc_prepare_kernel_notification(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t destination_endpoint,
    uint64_t event_mask,
    struct micros_ipc_kernel_notification_plan *plan
)
{
    struct micros_ipc_kernel_notification_plan candidate;
    const struct micros_endpoint_record *resolved_destination;
    const struct micros_endpoint_record *destination;
    struct micros_thread_handle previous = {0, 0};
    struct micros_thread_handle receiver_handle = {0, 0};
    const struct micros_thread *receiver = NULL;
    enum micros_endpoint_error endpoint_error;
    enum micros_kernel_object_error scheduler_error;
    bool has_receiver;

    if (
        registry == NULL
        || objects == NULL
        || plan == NULL
        || destination_endpoint == MICROS_ENDPOINT_NONE
        || destination_endpoint == MICROS_ENDPOINT_ANY
        || event_mask == 0
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    ipc_clear_bytes(&candidate, sizeof(candidate));
    endpoint_error =
        micros_endpoint_registry_validate_objects(registry, objects);
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return ipc_endpoint_error_to_ipc(endpoint_error);
    }
    endpoint_error = micros_endpoint_resolve_active(
        registry,
        objects,
        destination_endpoint,
        &resolved_destination
    );
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return ipc_endpoint_error_to_ipc(endpoint_error);
    }
    destination =
        &registry->endpoints[resolved_destination->owner.slot];
    candidate.destination_slot =
        resolved_destination->owner.slot;
    candidate.event_mask = event_mask;
    canonicalize_notification(
        &candidate.notification,
        MICROS_ENDPOINT_NONE,
        event_mask
    );
    has_receiver = ipc_find_matching_receiver_const(
        destination,
        objects,
        MICROS_ENDPOINT_NONE,
        &previous,
        &receiver_handle,
        &receiver
    );
    if (
        has_receiver
        && (
            receiver->runtime_flags
            & MICROS_THREAD_RTS_IPC_REPLY
        ) == 0
    ) {
        if (
            (
                receiver->runtime_flags
                & MICROS_THREAD_RTS_IPC_MASK
            ) != MICROS_THREAD_RTS_IPC_RECEIVE
        ) {
            return MICROS_IPC_ERROR_STATE;
        }
        scheduler_error = micros_scheduler_prepare_ipc_wake(
            objects,
            receiver_handle,
            MICROS_THREAD_RTS_IPC_RECEIVE,
            &candidate.wake
        );
        if (scheduler_error != MICROS_KERNEL_OBJECT_OK) {
            return ipc_scheduler_error_to_ipc(scheduler_error);
        }
        candidate.deliver_to_receiver = true;
        candidate.previous_receiver = previous;
        candidate.receiver = receiver_handle;
        candidate.receive_buffer = receiver->ipc_receive_buffer;
    }
    candidate.active = true;
    ipc_copy_bytes(plan, &candidate, sizeof(*plan));
    return MICROS_IPC_OK;
}

void micros_ipc_commit_kernel_notification_prevalidated(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_ipc_kernel_notification_plan *plan
)
{
    struct micros_endpoint_record *destination =
        &registry->endpoints[plan->destination_slot];

    if (plan->deliver_to_receiver) {
        struct micros_thread *receiver =
            &objects->threads[plan->receiver.slot];

        micros_scheduler_commit_ipc_wake_prevalidated(
            objects,
            &plan->wake
        );
        ipc_unlink_thread(
            objects,
            plan->previous_receiver,
            plan->receiver,
            &destination->receiver_head,
            &destination->receiver_tail
        );
        receiver->ipc_queue_kind = MICROS_IPC_QUEUE_NONE;
        receiver->ipc_next.slot = 0;
        receiver->ipc_next.generation = 0;
        receiver->ipc_receive_source = 0;
        ipc_stage_delivery(
            receiver,
            plan->receive_buffer,
            &plan->notification
        );
    } else {
        destination->pending_kernel_events |= plan->event_mask;
    }
    ipc_clear_bytes(plan, sizeof(*plan));
}
