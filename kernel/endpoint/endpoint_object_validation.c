#include "kernel/endpoint/endpoint_private.h"

static bool thread_has_no_ipc_flags(
    const struct micros_thread *thread
)
{
    return (
        thread->runtime_flags
        & MICROS_THREAD_RTS_IPC_MASK
    ) == 0;
}

static bool process_relationships_are_canonical(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    bool queued_threads[MICROS_THREAD_CAPACITY]
)
{
    size_t index;

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct micros_endpoint_record *endpoint =
            &registry->endpoints[index];
        const struct micros_process *process =
            &objects->processes[index];
        const struct micros_privilege_profile *unused_profile;

        if (endpoint->state == MICROS_ENDPOINT_STATE_FREE) {
            if (
                process->slot_state
                    == MICROS_KERNEL_OBJECT_SLOT_LIVE
                && (
                    process->primary_endpoint
                        != MICROS_PROCESS_ENDPOINT_NONE
                    || process->privilege_profile != 0
                )
            ) {
                return false;
            }
            continue;
        }
        if (
            process->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || process->generation != endpoint->owner.generation
            || process->primary_endpoint != endpoint->value
            || !process->endpoint_lifecycle_consumed
        ) {
            return false;
        }
        if (process->privilege_profile != 0) {
            if (
                process->privilege_profile
                    >= MICROS_PRIVILEGE_PROFILE_CAPACITY
                || micros_privilege_profile_resolve(
                    registry,
                    (uint8_t)process->privilege_profile,
                    &unused_profile
                ) != MICROS_ENDPOINT_OK
            ) {
                return false;
            }
        } else if (
            endpoint->state == MICROS_ENDPOINT_STATE_ACTIVE
            || endpoint->state
                == MICROS_ENDPOINT_STATE_SOURCE_ONLY
        ) {
            return false;
        }
        if (
            endpoint->state == MICROS_ENDPOINT_STATE_SOURCE_ONLY
            && !endpoint_source_only_process_threads_are_valid(
                registry,
                objects,
                endpoint->owner
            )
        ) {
            return false;
        }
        if (
            (
                endpoint->state == MICROS_ENDPOINT_STATE_ACTIVE
                || endpoint->state
                    == MICROS_ENDPOINT_STATE_SOURCE_ONLY
            )
            && (
                !endpoint_queue_is_valid(
                    registry,
                    objects,
                    endpoint,
                    endpoint->sender_head,
                    endpoint->sender_tail,
                    MICROS_IPC_QUEUE_SENDER,
                    queued_threads
                )
                || !endpoint_queue_is_valid(
                    registry,
                    objects,
                    endpoint,
                    endpoint->receiver_head,
                    endpoint->receiver_tail,
                    MICROS_IPC_QUEUE_RECEIVER,
                    queued_threads
                )
                || !endpoint_pending_notification_state_is_valid(
                    registry,
                    objects,
                    endpoint
                )
                || endpoint_has_matchable_work(
                    registry,
                    objects,
                    endpoint
                )
            )
        ) {
            return false;
        }
    }
    return true;
}

static bool thread_relationships_are_canonical(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const bool queued_threads[MICROS_THREAD_CAPACITY]
)
{
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];
        size_t other_index;

        if (thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE) {
            continue;
        }
        if (thread->ipc_queue_kind == MICROS_IPC_QUEUE_NONE) {
            if (
                queued_threads[index]
                || (
                    micros_thread_ipc_state_is_clear(thread)
                        ? !thread_has_no_ipc_flags(thread)
                        : (
                            !endpoint_reply_wait_state_is_valid(
                                registry,
                                thread
                            )
                            && !endpoint_staged_delivery_state_is_valid(
                                registry,
                                objects,
                                thread
                            )
                            && !endpoint_staged_error_state_is_valid(
                                registry,
                                thread
                            )
                            && !endpoint_staged_no_message_success_state_is_valid(
                                registry,
                                thread
                            )
                        )
                )
            ) {
                return false;
            }
        } else if (
            (
                thread->ipc_queue_kind != MICROS_IPC_QUEUE_SENDER
                && thread->ipc_queue_kind
                    != MICROS_IPC_QUEUE_RECEIVER
            )
            || !queued_threads[index]
        ) {
            return false;
        }
        if (thread->ipc_reply_token == 0) {
            continue;
        }
        if (
            registry->last_reply_token == 0
            || thread->ipc_reply_token
                > registry->last_reply_token
        ) {
            return false;
        }
        for (other_index = 0; other_index < index; ++other_index) {
            const struct micros_thread *other =
                &objects->threads[other_index];

            if (
                other->slot_state
                    == MICROS_KERNEL_OBJECT_SLOT_LIVE
                && other->ipc_reply_token
                    == thread->ipc_reply_token
            ) {
                return false;
            }
        }
    }
    return true;
}

enum micros_endpoint_error micros_endpoint_registry_validate_objects(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
)
{
    enum micros_endpoint_error endpoint_error;
    bool queued_threads[MICROS_THREAD_CAPACITY];
    size_t index;

    if (registry == NULL || objects == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    endpoint_error = micros_endpoint_registry_validate(registry);
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return endpoint_error;
    }
    if (
        micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return MICROS_ENDPOINT_ERROR_INVARIANT;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        queued_threads[index] = false;
    }

    if (!process_relationships_are_canonical(registry, objects, queued_threads)) {
        return MICROS_ENDPOINT_ERROR_INVARIANT;
    }
    if (!thread_relationships_are_canonical(registry, objects, queued_threads)) {
        return MICROS_ENDPOINT_ERROR_INVARIANT;
    }
    return MICROS_ENDPOINT_OK;
}
