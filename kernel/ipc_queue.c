#include "ipc_internal.h"

enum micros_ipc_error ipc_resolve_queue_tail(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle tail,
    struct micros_thread **thread
)
{
    const struct micros_thread *resolved;

    if (ipc_thread_handle_is_zero(tail)) {
        *thread = NULL;
        return MICROS_IPC_OK;
    }
    if (
        micros_thread_resolve(objects, tail, &resolved)
            != MICROS_KERNEL_OBJECT_OK
        || !ipc_thread_handle_is_zero(resolved->ipc_next)
    ) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    *thread = &objects->threads[tail.slot];
    return MICROS_IPC_OK;
}

bool ipc_receiver_is_ready_for(
    const struct micros_endpoint_record *destination,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t source
)
{
    struct micros_thread_handle current = destination->receiver_head;
    size_t steps;

    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        const struct micros_thread *receiver;

        if (ipc_thread_handle_is_zero(current)) {
            return false;
        }
        receiver = &objects->threads[current.slot];
        if (
            receiver->ipc_receive_source == MICROS_ENDPOINT_ANY
            || receiver->ipc_receive_source == source
        ) {
            return true;
        }
        current = receiver->ipc_next;
    }
    return false;
}

bool ipc_sender_is_ready_for(
    const struct micros_endpoint_record *receiver_endpoint,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t source
)
{
    struct micros_thread_handle current =
        receiver_endpoint->sender_head;
    size_t steps;

    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        const struct micros_thread *sender;

        if (ipc_thread_handle_is_zero(current)) {
            return false;
        }
        sender = &objects->threads[current.slot];
        if (
            source == MICROS_ENDPOINT_ANY
            || sender->ipc_outbound_message.source == source
        ) {
            return true;
        }
        current = sender->ipc_next;
    }
    return false;
}

void ipc_append_thread(
    struct micros_thread_handle thread_handle,
    struct micros_thread *previous_tail,
    struct micros_thread_handle *head,
    struct micros_thread_handle *tail
)
{
    if (previous_tail == NULL) {
        *head = thread_handle;
    } else {
        previous_tail->ipc_next = thread_handle;
    }
    *tail = thread_handle;
}

bool ipc_find_matching_sender(
    const struct micros_endpoint_record *endpoint,
    struct micros_kernel_objects *objects,
    micros_endpoint_t source,
    struct micros_thread_handle *previous,
    struct micros_thread_handle *matched,
    struct micros_thread **sender
)
{
    struct micros_thread_handle current = endpoint->sender_head;
    struct micros_thread_handle prior = {0, 0};
    size_t steps;

    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        struct micros_thread *candidate;

        if (ipc_thread_handle_is_zero(current)) {
            return false;
        }
        candidate = &objects->threads[current.slot];
        if (
            source == MICROS_ENDPOINT_ANY
            || candidate->ipc_outbound_message.source == source
        ) {
            *previous = prior;
            *matched = current;
            *sender = candidate;
            return true;
        }
        prior = current;
        current = candidate->ipc_next;
    }
    return false;
}

bool ipc_find_matching_receiver_const(
    const struct micros_endpoint_record *endpoint,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t source,
    struct micros_thread_handle *previous,
    struct micros_thread_handle *matched,
    const struct micros_thread **receiver
)
{
    struct micros_thread_handle current = endpoint->receiver_head;
    struct micros_thread_handle prior = {0, 0};
    size_t steps;

    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        const struct micros_thread *candidate;

        if (ipc_thread_handle_is_zero(current)) {
            return false;
        }
        candidate = &objects->threads[current.slot];
        if (
            candidate->ipc_receive_source == MICROS_ENDPOINT_ANY
            || candidate->ipc_receive_source == source
        ) {
            *previous = prior;
            *matched = current;
            *receiver = candidate;
            return true;
        }
        prior = current;
        current = candidate->ipc_next;
    }
    return false;
}

bool ipc_find_matching_receiver(
    const struct micros_endpoint_record *endpoint,
    struct micros_kernel_objects *objects,
    micros_endpoint_t source,
    struct micros_thread_handle *previous,
    struct micros_thread_handle *matched,
    struct micros_thread **receiver
)
{
    const struct micros_thread *resolved;

    if (
        !ipc_find_matching_receiver_const(
            endpoint,
            objects,
            source,
            previous,
            matched,
            &resolved
        )
    ) {
        return false;
    }
    *receiver = &objects->threads[matched->slot];
    return true;
}

void ipc_unlink_thread(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle previous,
    struct micros_thread_handle matched,
    struct micros_thread_handle *head,
    struct micros_thread_handle *tail
)
{
    struct micros_thread *thread = &objects->threads[matched.slot];

    if (ipc_thread_handle_is_zero(previous)) {
        *head = thread->ipc_next;
    } else {
        objects->threads[previous.slot].ipc_next =
            thread->ipc_next;
    }
    if (ipc_thread_handles_equal(*tail, matched)) {
        *tail = previous;
    }
}
