#ifndef MICROS_TESTS_HOST_IPC_FIXTURE_TOKENS_H
#define MICROS_TESTS_HOST_IPC_FIXTURE_TOKENS_H

#include "micros/kernel_objects.h"

static inline uint64_t ipc_fixture_next_token(uint64_t previous, uint16_t slot)
{
    if (slot >= MICROS_THREAD_CAPACITY || slot >= 128) {
        return 0;
    }
    for (size_t step = 0; step < 128 && previous != UINT64_MAX; ++step) {
        ++previous;
        if (previous % 128 == slot) {
            return previous;
        }
    }
    return 0;
}

static inline bool ipc_fixture_return_request(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle receiver_handle
)
{
    const struct micros_thread *receiver;
    struct micros_thread *caller = NULL;

    if (micros_thread_resolve(objects, receiver_handle, &receiver) != MICROS_KERNEL_OBJECT_OK
        || !receiver->ipc_delivery_pending) {
        return false;
    }
    if (receiver->ipc_inbound_message.reply_token == 0) {
        return true;
    }
    for (size_t slot = 0; slot < MICROS_THREAD_CAPACITY; ++slot) {
        struct micros_thread *thread = &objects->threads[slot];

        if (thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || thread->ipc_reply_token != receiver->ipc_inbound_message.reply_token) {
            continue;
        }
        if (caller != NULL) {
            return false;
        }
        caller = thread;
    }
    if (caller == NULL || caller->ipc_reply_phase != MICROS_IPC_REPLY_PHASE_STAGED
        || caller->ipc_request_receiver.slot != receiver_handle.slot
        || caller->ipc_request_receiver.generation != receiver_handle.generation) {
        return false;
    }
    caller->ipc_reply_phase = MICROS_IPC_REPLY_PHASE_RETURNED;
    caller->ipc_request_receiver = (struct micros_thread_handle){0, 0};
    return true;
}

#endif
