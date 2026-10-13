#include "ipc_internal.h"

bool ipc_reply_token_is_available(
    const struct micros_kernel_objects *objects,
    uint64_t reply_token
)
{
    size_t index;

    if (reply_token == 0) {
        return true;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];

        if (
            thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && thread->ipc_reply_token == reply_token
        ) {
            return false;
        }
    }
    return true;
}

static enum micros_ipc_error resolve_reply_waiter(
    struct micros_kernel_objects *objects,
    uint64_t reply_token,
    micros_endpoint_t replying_endpoint,
    struct micros_thread_handle *caller_handle,
    struct micros_thread **caller
)
{
    struct micros_thread *matched = NULL;
    struct micros_thread_handle matched_handle = {0, 0};
    size_t index;

    if (reply_token == 0) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        struct micros_thread *candidate = &objects->threads[index];

        if (
            candidate->slot_state
                != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || candidate->ipc_reply_token != reply_token
        ) {
            continue;
        }
        if (matched != NULL) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
        matched = candidate;
        matched_handle.slot = (uint16_t)index;
        matched_handle.generation = candidate->generation;
    }
    if (
        matched == NULL
        || matched->ipc_queue_kind != MICROS_IPC_QUEUE_NONE
        || !ipc_thread_handle_is_zero(matched->ipc_next)
        || (
            matched->runtime_flags
            & MICROS_THREAD_RTS_IPC_MASK
        ) != MICROS_THREAD_RTS_IPC_REPLY
        || matched->ipc_reply_callee != replying_endpoint
    ) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    *caller_handle = matched_handle;
    *caller = matched;
    return MICROS_IPC_OK;
}

static bool reply_request_is_pending(
    const struct micros_kernel_objects *objects,
    uint64_t reply_token
)
{
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];

        if (
            thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && thread->ipc_delivery_pending
            && thread->ipc_staged_result == MICROS_IPC_OK
            && thread->ipc_inbound_message.reply_token
                == reply_token
        ) {
            return true;
        }
    }
    return false;
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
    struct micros_thread_handle caller_handle = {0, 0};
    struct micros_thread *caller;
    struct micros_ipc_message snapshot;
    micros_endpoint_t replying_endpoint;
    enum micros_endpoint_error endpoint_error;
    enum micros_ipc_error error;

    if (
        registry == NULL
        || objects == NULL
        || message == NULL
        || plan == NULL
        || (
            required_operation
                != MICROS_PRIVILEGE_OPERATION_REPLY
            && required_operation
                != MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE
        )
        || (uintptr_t)message % _Alignof(struct micros_ipc_message)
            != 0
        || (
            message->type
            & MICROS_IPC_TYPE_KERNEL_MASK
        ) != 0
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    endpoint_error =
        micros_endpoint_registry_validate_objects(registry, objects);
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return ipc_endpoint_error_to_ipc(endpoint_error);
    }
    error = ipc_resolve_operation_thread(
        objects,
        replier_handle,
        &replying_endpoint
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    endpoint_error = micros_endpoint_authorize_operation(
        registry,
        objects,
        replying_endpoint,
        required_operation
    );
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return ipc_endpoint_error_to_ipc(endpoint_error);
    }
    error = resolve_reply_waiter(
        objects,
        reply_token,
        replying_endpoint,
        &caller_handle,
        &caller
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    {
        const struct micros_endpoint_record *caller_endpoint =
            &registry->endpoints[caller->owner.slot];

        if (
            caller_endpoint->state
                != MICROS_ENDPOINT_STATE_ACTIVE
            || (
                expected_caller_endpoint
                    != MICROS_ENDPOINT_NONE
                && caller_endpoint->value
                    != expected_caller_endpoint
            )
            || caller_endpoint->owner.slot != caller->owner.slot
            || caller_endpoint->owner.generation
                != caller->owner.generation
        ) {
            return MICROS_IPC_ERROR_REPLY_TOKEN;
        }
    }
    if (reply_request_is_pending(objects, reply_token)) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    ipc_canonicalize_message(
        &snapshot,
        message,
        replying_endpoint,
        0
    );

    plan->caller_handle = caller_handle;
    plan->caller = caller;
    plan->message = snapshot;
    plan->replying_endpoint = replying_endpoint;
    plan->reply_buffer = caller->ipc_receive_buffer;
    return MICROS_IPC_OK;
}

void ipc_commit_reply_delivery(
    const struct ipc_reply_preflight *plan
)
{
    ipc_stage_delivery(
        plan->caller,
        plan->reply_buffer,
        &plan->message
    );
    plan->caller->ipc_reply_token = 0;
    plan->caller->ipc_reply_callee = 0;
}
