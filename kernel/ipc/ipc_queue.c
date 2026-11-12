#include "kernel/ipc/ipc_internal.h"

static enum micros_ipc_error preflight_queue_endpoints(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_selection *destination,
    enum micros_ipc_queue_kind kind,
    const struct micros_thread **head,
    const struct micros_thread **tail
)
{
    struct micros_thread_handle first = kind == MICROS_IPC_QUEUE_SENDER
        ? destination->record->sender_head : destination->record->receiver_head;
    struct micros_thread_handle last = kind == MICROS_IPC_QUEUE_SENDER
        ? (struct micros_thread_handle){0, 0} : destination->record->receiver_tail;

    if (ipc_thread_handle_is_zero(first)) {
        if (!ipc_thread_handle_is_zero(last)) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
        *head = NULL;
        *tail = NULL;
        return MICROS_IPC_OK;
    }
    if (!micros_endpoint_queue_member_is_valid(registry, objects, destination, first, kind, head)) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    if (kind == MICROS_IPC_QUEUE_SENDER) {
        *tail = NULL;
        return MICROS_IPC_OK;
    }
    if (ipc_thread_handles_equal(first, last)) {
        *tail = *head;
    } else if (!micros_endpoint_queue_member_is_valid(registry, objects, destination, last, kind, tail)) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    return ipc_thread_handle_is_zero((*tail)->ipc_next)
        ? MICROS_IPC_OK : MICROS_IPC_ERROR_INVARIANT;
}

enum micros_ipc_error ipc_prepare_queue_append(
    const struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    const struct micros_endpoint_selection *destination,
    enum micros_ipc_queue_kind kind,
    struct micros_thread **tail
)
{
    const struct micros_thread *first;
    const struct micros_thread *last;
    struct micros_thread_handle handle = kind == MICROS_IPC_QUEUE_SENDER
        ? destination->record->sender_head : destination->record->receiver_tail;
    enum micros_ipc_error error =
        preflight_queue_endpoints(registry, objects, destination, kind, &first, &last);

    if (error != MICROS_IPC_OK) {
        return error;
    }
    if (kind != MICROS_IPC_QUEUE_SENDER || first == NULL) {
        *tail = last == NULL ? NULL : &objects->threads[handle.slot];
        return MICROS_IPC_OK;
    }
    for (size_t steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        const struct micros_thread *thread = first;

        if (steps != 0 && !micros_endpoint_queue_member_is_valid(
            registry, objects, destination, handle, kind, &thread
        )) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
        if (ipc_thread_handle_is_zero(thread->ipc_next)) {
            *tail = &objects->threads[handle.slot];
            return MICROS_IPC_OK;
        }
        handle = thread->ipc_next;
    }
    return MICROS_IPC_ERROR_INVARIANT;
}

static bool successor_reenters_prefix(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle first,
    struct micros_thread_handle successor,
    size_t count
)
{
    struct micros_thread_handle current = first;

    for (size_t index = 0; index < count; ++index) {
        if (ipc_thread_handles_equal(current, successor)) {
            return true;
        }
        current = objects->threads[current.slot].ipc_next;
    }
    return false;
}

enum micros_ipc_error ipc_select_queue(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_selection *destination,
    enum micros_ipc_queue_kind kind,
    micros_endpoint_t source,
    struct ipc_queue_selection *selection
)
{
    struct micros_thread_handle first = kind == MICROS_IPC_QUEUE_SENDER
        ? destination->record->sender_head : destination->record->receiver_head;
    struct micros_thread_handle last = kind == MICROS_IPC_QUEUE_SENDER
        ? (struct micros_thread_handle){0, 0} : destination->record->receiver_tail;
    struct micros_thread_handle current = first;
    struct micros_thread_handle previous = {0, 0};
    const struct micros_thread *head;
    const struct micros_thread *tail;
    enum micros_ipc_error error =
        preflight_queue_endpoints(registry, objects, destination, kind, &head, &tail);

    if (error != MICROS_IPC_OK || head == NULL) {
        return error != MICROS_IPC_OK ? error : MICROS_IPC_ERROR_NOT_READY;
    }
    for (size_t steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        const struct micros_thread *thread;
        bool matches;

        if (steps == 0) {
            thread = head;
        } else if (ipc_thread_handles_equal(current, first)) {
            return MICROS_IPC_ERROR_INVARIANT;
        } else if (kind == MICROS_IPC_QUEUE_RECEIVER && ipc_thread_handles_equal(current, last)) {
            thread = tail;
        } else if (!micros_endpoint_queue_member_is_valid(
            registry, objects, destination, current, kind, &thread
        )) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
        matches = kind == MICROS_IPC_QUEUE_SENDER
            ? source == MICROS_ENDPOINT_ANY || thread->ipc_outbound_message.source == source
            : thread->ipc_receive_source == MICROS_ENDPOINT_ANY || thread->ipc_receive_source == source;
        if (matches) {
            const struct micros_thread *successor;

            if (kind == MICROS_IPC_QUEUE_SENDER
                ? !ipc_thread_handle_is_zero(thread->ipc_next)
                : !ipc_thread_handles_equal(current, last)) {
                if (
                    ipc_thread_handle_is_zero(thread->ipc_next)
                    || successor_reenters_prefix(objects, first, thread->ipc_next, steps + 1)
                    || !micros_endpoint_queue_member_is_valid(
                        registry, objects, destination, thread->ipc_next, kind, &successor
                    )
                ) {
                    return MICROS_IPC_ERROR_INVARIANT;
                }
            }
            selection->previous = previous;
            selection->handle = current;
            selection->thread = thread;
            return MICROS_IPC_OK;
        }
        if (kind == MICROS_IPC_QUEUE_SENDER
            ? ipc_thread_handle_is_zero(thread->ipc_next)
            : ipc_thread_handles_equal(current, last)) {
            return MICROS_IPC_ERROR_NOT_READY;
        }
        previous = current;
        current = thread->ipc_next;
    }
    return MICROS_IPC_ERROR_INVARIANT;
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
    if (tail != NULL) {
        *tail = thread_handle;
    }
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
    if (tail != NULL && ipc_thread_handles_equal(*tail, matched)) {
        *tail = previous;
    }
}
