#include "kernel/endpoint/endpoint_private.h"

#include "kernel/scheduler/scheduler_core_internal.h"
#include "micros/work_observation.h"

static bool thread_has_no_ipc_flags(
    const struct micros_thread *thread
)
{
    return (
        thread->runtime_flags
        & MICROS_THREAD_RTS_IPC_MASK
    ) == 0;
}

bool micros_endpoint_thread_ipc_state_is_valid(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle handle
)
{
    const struct micros_thread *thread;
    const struct micros_process *process;
    struct micros_endpoint_selection owner;
    struct micros_endpoint_selection peer;
    uint16_t token_slot;

    if (
        scheduler_core_preflight_thread(objects, handle, &thread)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    process = &objects->processes[thread->owner.slot];
    if (process->primary_endpoint == MICROS_PROCESS_ENDPOINT_NONE) {
        return process->privilege_profile == 0
            && micros_thread_ipc_state_is_clear(thread)
            && thread_has_no_ipc_flags(thread);
    }
    if (
        micros_endpoint_select(registry, objects, process->primary_endpoint, &owner)
            != MICROS_ENDPOINT_OK
        || (owner.record->state == MICROS_ENDPOINT_STATE_SOURCE_ONLY
            && (scheduler_core_thread_is_current(objects, handle)
                || (thread->runtime_flags != MICROS_THREAD_RTS_INACTIVE
                    && thread->runtime_flags
                        != (MICROS_THREAD_RTS_INACTIVE | MICROS_THREAD_RTS_IPC_REPLY))))
    ) {
        return false;
    }
    if (micros_thread_ipc_state_is_clear(thread)) {
        return thread_has_no_ipc_flags(thread);
    }
    if (thread->ipc_reply_token != 0
        && (thread->ipc_reply_token > registry->last_reply_token
            || !micros_endpoint_reply_token_slot(thread->ipc_reply_token, &token_slot)
            || token_slot != handle.slot)) {
        return false;
    }
    if (thread->ipc_queue_kind == MICROS_IPC_QUEUE_SENDER) {
        return micros_endpoint_select(
            registry, objects, thread->ipc_send_destination, &peer
        ) == MICROS_ENDPOINT_OK && micros_endpoint_queue_member_is_valid(
            registry, objects, &peer, handle, MICROS_IPC_QUEUE_SENDER, &thread
        );
    }
    if (thread->ipc_queue_kind == MICROS_IPC_QUEUE_RECEIVER) {
        return micros_endpoint_queue_member_is_valid(
            registry, objects, &owner, handle, MICROS_IPC_QUEUE_RECEIVER, &thread
        );
    }
    if (thread->ipc_queue_kind != MICROS_IPC_QUEUE_NONE) {
        return false;
    }
    if (thread->ipc_reply_token != 0) {
        return micros_endpoint_select(
                registry, objects, thread->ipc_reply_callee, &peer
            ) == MICROS_ENDPOINT_OK
            && endpoint_reply_wait_state_is_valid(registry, thread)
            && (thread->ipc_reply_phase != MICROS_IPC_REPLY_PHASE_STAGED
                || micros_endpoint_staged_request_matches(
                    registry, objects, thread->ipc_request_receiver, thread, owner.record
                ));
    }
    return endpoint_staged_error_state_is_valid(registry, thread)
        || endpoint_staged_no_message_success_state_is_valid(registry, thread)
        || endpoint_staged_delivery_selected_is_valid(registry, objects, handle, thread, &owner);
}

enum micros_endpoint_error endpoint_preflight_owned_threads(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    bool require_unassigned
)
{
    size_t owned = 0;

    for (size_t index = 0; index < MICROS_HART_CAPACITY; ++index) {
        const struct micros_hart *hart = &objects->harts[index];
        const struct micros_thread *thread;

        if (hart->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || hart->current_thread.generation == 0) {
            continue;
        }
        if (
            hart->generation == 0
            || scheduler_core_preflight_thread(objects, hart->current_thread, &thread)
                != MICROS_KERNEL_OBJECT_OK
            || !thread->scheduler_assigned
            || thread->scheduler_hart.slot != index
            || thread->scheduler_hart.generation != hart->generation
        ) {
            return MICROS_ENDPOINT_ERROR_INVARIANT;
        }
        if (endpoint_process_handles_equal(thread->owner, process)) {
            return MICROS_ENDPOINT_ERROR_STATE;
        }
    }
    for (size_t index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];
        struct micros_thread_handle handle;

        if (thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE || thread->owner.slot != process.slot) {
            continue;
        }
        handle.slot = (uint16_t)index;
        handle.generation = thread->generation;
        if (!endpoint_process_handles_equal(thread->owner, process)
            || !micros_endpoint_thread_ipc_state_is_valid(registry, objects, handle)) {
            return MICROS_ENDPOINT_ERROR_INVARIANT;
        }
        ++owned;
        if (thread->runtime_flags != MICROS_THREAD_RTS_INACTIVE
            || (require_unassigned && thread->scheduler_assigned)
            || !micros_thread_ipc_state_is_clear(thread)) {
            return MICROS_ENDPOINT_ERROR_STATE;
        }
    }
    return owned == objects->processes[process.slot].live_thread_count
        ? MICROS_ENDPOINT_OK : MICROS_ENDPOINT_ERROR_INVARIANT;
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
        struct micros_thread_handle handle = {(uint16_t)index, thread->generation};
        uint16_t token_slot;

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
                                handle,
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
            if (thread->ipc_reply_phase != MICROS_IPC_REPLY_PHASE_NONE
                || !endpoint_thread_handle_is_zero(thread->ipc_request_receiver)) {
                return false;
            }
            continue;
        }
        if (
            registry->last_reply_token == 0
            || thread->ipc_reply_token
                > registry->last_reply_token
            || !micros_endpoint_reply_token_slot(thread->ipc_reply_token, &token_slot)
            || token_slot != index
            || (thread->ipc_reply_phase == MICROS_IPC_REPLY_PHASE_STAGED
                && !micros_endpoint_staged_request_matches(
                    registry, objects, thread->ipc_request_receiver, thread,
                    &registry->endpoints[thread->owner.slot]
                ))
        ) {
            return false;
        }
    }
    return true;
}

enum micros_endpoint_error micros_endpoint_registry_validate_objects(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
)
{
#ifdef MICROS_BUILD_IPC_SYSCALL_TEST
    MICROS_TEST_WORK_HIT("ipc01-target-endpoints", MICROS_WORK_OWNER_KERNEL_ENDPOINT,
                         MICROS_WORK_KIND_COMPLETE_VALIDATOR_CALL, 1);
#endif
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
