#include "kernel/ipc/ipc_internal.h"

bool ipc_reply_token_is_available(
    const struct micros_kernel_objects *objects,
    uint64_t reply_token
)
{
    if (reply_token == 0) {
        return true;
    }
    for (size_t index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];

        if (thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && thread->ipc_reply_token == reply_token) {
            return false;
        }
    }
    return true;
}

static enum micros_ipc_error resolve_reply_waiter(
    const struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    uint64_t reply_token,
    struct micros_thread_handle *caller_handle,
    struct micros_thread **caller,
    const struct micros_endpoint_record **caller_endpoint
)
{
    struct micros_thread *matched = NULL;
    struct micros_thread_handle handle = {0, 0};

    for (size_t index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        struct micros_thread *candidate = &objects->threads[index];

        if (candidate->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || candidate->ipc_reply_token != reply_token) {
            continue;
        }
        if (matched != NULL) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
        matched = candidate;
        handle.slot = (uint16_t)index;
        handle.generation = candidate->generation;
    }
    if (matched != NULL
        && !micros_endpoint_thread_ipc_state_is_valid(registry, objects, handle)) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    *caller = matched;
    *caller_handle = handle;
    *caller_endpoint = matched == NULL ? NULL : &registry->endpoints[matched->owner.slot];
    return MICROS_IPC_OK;
}

static enum micros_ipc_error reply_request_is_pending(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    uint64_t reply_token,
    const struct micros_thread *caller,
    const struct micros_endpoint_record *caller_endpoint,
    bool *pending
)
{
    bool found = false;

    for (size_t index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];
        struct micros_thread_handle receiver;

        if (thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || thread->ipc_inbound_message.reply_token != reply_token) {
            continue;
        }
        receiver.slot = (uint16_t)index;
        receiver.generation = thread->generation;
        if (caller == NULL || found || !micros_endpoint_staged_request_matches(
            registry, objects, receiver, caller, caller_endpoint
        )) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
        found = true;
    }
    *pending = found;
    return MICROS_IPC_OK;
}

enum micros_ipc_error ipc_preflight_reply(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle replier_handle,
    uint64_t reply_token,
    micros_endpoint_t expected_caller_endpoint,
    const struct micros_ipc_message *message,
    uint32_t required_operation,
    struct ipc_reply_preflight *plan
)
{
    struct ipc_participant replier;
    struct micros_thread_handle caller_handle = {0, 0};
    struct micros_thread *caller;
    const struct micros_endpoint_record *caller_endpoint;
    struct micros_ipc_message snapshot;
    enum micros_ipc_error error;
    bool pending;

    if (
        registry == NULL || objects == NULL || message == NULL || plan == NULL
        || (required_operation != MICROS_PRIVILEGE_OPERATION_REPLY
            && required_operation != MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE)
        || (uintptr_t)message % _Alignof(struct micros_ipc_message) != 0
        || (message->type & MICROS_IPC_TYPE_KERNEL_MASK) != 0
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    error = ipc_admit_operation_thread(registry, objects, replier_handle, &replier);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = ipc_endpoint_error_to_ipc(micros_privilege_profile_allows_operation(
        registry, (uint8_t)replier.endpoint.process->privilege_profile, required_operation
    ));
    if (error != MICROS_IPC_OK) {
        return error;
    }
    if (reply_token == 0) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    error = resolve_reply_waiter(registry, objects, reply_token, &caller_handle, &caller, &caller_endpoint);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = reply_request_is_pending(registry, objects, reply_token, caller, caller_endpoint, &pending);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    if (
        caller == NULL || pending
        || caller->ipc_queue_kind != MICROS_IPC_QUEUE_NONE
        || (caller->runtime_flags & MICROS_THREAD_RTS_IPC_MASK) != MICROS_THREAD_RTS_IPC_REPLY
        || caller->ipc_reply_callee != replier.endpoint.record->value
        || caller_endpoint->state != MICROS_ENDPOINT_STATE_ACTIVE
        || (expected_caller_endpoint != MICROS_ENDPOINT_NONE
            && caller_endpoint->value != expected_caller_endpoint)
    ) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    ipc_canonicalize_message(&snapshot, message, replier.endpoint.record->value, 0);
    plan->replier = replier;
    plan->caller_handle = caller_handle;
    plan->caller = caller;
    plan->message = snapshot;
    plan->reply_buffer = caller->ipc_receive_buffer;
    return MICROS_IPC_OK;
}

void ipc_commit_reply_delivery(const struct ipc_reply_preflight *plan)
{
    ipc_stage_delivery(plan->caller, plan->reply_buffer, &plan->message);
    plan->caller->ipc_reply_token = 0;
    plan->caller->ipc_reply_callee = 0;
}
