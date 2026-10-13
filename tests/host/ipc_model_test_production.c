#include "ipc_model_test_internal.h"

static enum micros_ipc_error production_drain_thread(struct ipc_model_test_context *fixture, size_t thread_slot)
{
    struct micros_thread *thread;

    if (thread_slot >= MICROS_THREAD_CAPACITY) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    thread = &fixture->objects.threads[thread_slot];
    if (
        thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || !thread->ipc_delivery_pending
    ) {
        return MICROS_IPC_ERROR_STATE;
    }
    thread->ipc_receive_buffer = 0;
    thread->ipc_delivery_pending = false;
    memset(
        &thread->ipc_inbound_message,
        0,
        sizeof(thread->ipc_inbound_message)
    );
    thread->ipc_staged_result = MICROS_IPC_OK;
    return MICROS_IPC_OK;
}

static enum micros_ipc_error production_hold_thread(struct ipc_model_test_context *fixture, size_t thread_slot)
{
    struct micros_thread_handle thread;
    enum micros_kernel_object_error error;

    if (
        thread_slot >= MICROS_THREAD_CAPACITY
        || fixture->objects.threads[thread_slot].slot_state
            != MICROS_KERNEL_OBJECT_SLOT_LIVE
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    thread = (struct micros_thread_handle){
        .slot = (uint16_t)thread_slot,
        .generation = fixture->objects.threads[thread_slot].generation,
    };
    error = micros_thread_scheduler_hold(&fixture->objects, thread);
    if (error == MICROS_KERNEL_OBJECT_OK) {
        return MICROS_IPC_OK;
    }
    return error == MICROS_KERNEL_OBJECT_ERROR_STATE
        ? MICROS_IPC_ERROR_STATE
        : MICROS_IPC_ERROR_INVARIANT;
}

static enum micros_ipc_error release_acceptance_owned_threads(
    struct ipc_model_test_context *fixture,
    const struct micros_thread_handle owned_threads[2],
    size_t owned_count
)
{
    size_t index;

    for (index = 0; index < owned_count; ++index) {
        if (
            micros_thread_scheduler_remove(
                &fixture->objects,
                owned_threads[index]
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_detach_execution_context(
                &fixture->objects,
                owned_threads[index]
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_release(
                &fixture->objects,
                owned_threads[index]
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
    }
    return MICROS_IPC_OK;
}

static enum micros_ipc_error recreate_acceptance_thread_slots(
    struct ipc_model_test_context *fixture,
    struct micros_process_handle new_process,
    const struct micros_thread_handle owned_threads[2],
    size_t owned_count
)
{
    size_t index;

    for (index = 0; index < owned_count; ++index) {
        struct micros_thread_handle thread;
        struct micros_user_context context =
            ipc_model_context_pattern(
                UINT64_C(0x5000)
                + owned_threads[index].slot * UINT64_C(0x100)
                + new_process.generation
            );
        uintptr_t stack_bottom =
            UINT64_C(0x10000000)
            + owned_threads[index].slot * UINT64_C(0x00008000);

        if (
            micros_thread_create(
                &fixture->objects,
                new_process,
                &thread
            ) != MICROS_KERNEL_OBJECT_OK
            || thread.slot != owned_threads[index].slot
            || micros_thread_attach_execution_context(
                &fixture->objects,
                thread,
                stack_bottom,
                stack_bottom + MICROS_THREAD_KERNEL_STACK_SIZE,
                &context
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_scheduler_admit(
                &fixture->objects,
                (struct micros_hart_handle){0, 1},
                thread,
                MODEL_PRIORITY,
                100,
                true
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_scheduler_hold(&fixture->objects, thread)
                != MICROS_KERNEL_OBJECT_OK
        ) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
    }
    return MICROS_IPC_OK;
}

static enum micros_ipc_error production_reuse_process(
    struct ipc_model_test_context *fixture,
    size_t process_slot
)
{
    struct micros_process_handle old_process;
    struct micros_process_handle new_process;
    struct micros_thread_handle owned_threads[2];
    size_t owned_count = 0;
    size_t index;
    micros_endpoint_t endpoint;

    if (
        process_slot >= MICROS_PROCESS_CAPACITY
        || fixture->objects.processes[process_slot].slot_state
            != MICROS_KERNEL_OBJECT_SLOT_LIVE
    ) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    old_process = (struct micros_process_handle){
        .slot = (uint16_t)process_slot,
        .generation = fixture->objects.processes[process_slot].generation,
    };
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &fixture->objects.threads[index];

        if (
            thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && ipc_model_process_handles_equal(thread->owner, old_process)
        ) {
            if (
                owned_count
                    >= sizeof(owned_threads) / sizeof(owned_threads[0])
            ) {
                return MICROS_IPC_ERROR_INVARIANT;
            }
            owned_threads[owned_count] =
                (struct micros_thread_handle){
                    .slot = (uint16_t)index,
                    .generation = thread->generation,
                };
            ++owned_count;
        }
    }
    if (release_acceptance_owned_threads(fixture, owned_threads, owned_count) != MICROS_IPC_OK) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    if (
        micros_process_release(&fixture->objects, old_process)
            != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(&fixture->objects, &new_process)
            != MICROS_KERNEL_OBJECT_OK
        || new_process.slot != process_slot
        || micros_endpoint_reserve(
            &fixture->registry,
            &fixture->objects,
            new_process,
            &endpoint
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_install_profile(
            &fixture->registry,
            &fixture->objects,
            new_process,
            MODEL_FULL_PROFILE
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_activate(
            &fixture->registry,
            &fixture->objects,
            endpoint
        ) != MICROS_ENDPOINT_OK
    ) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    return recreate_acceptance_thread_slots(fixture, new_process, owned_threads, owned_count);
}

static enum micros_ipc_error production_close_reuse(
    struct ipc_model_test_context *fixture,
    size_t process_slot,
    micros_endpoint_t endpoint
)
{
    enum micros_ipc_error error =
        micros_ipc_endpoint_close(&fixture->registry, &fixture->objects, endpoint);

    if (error != MICROS_IPC_OK) {
        return error;
    }
    return production_reuse_process(fixture, process_slot);
}

enum micros_ipc_error ipc_model_production_execute_action(
    struct ipc_model_test_context *fixture,
    const struct model_action *action
)
{
    struct micros_thread_handle thread;

    if (
        action->operation != MODEL_OPERATION_CLOSE_REUSE
        && action->thread_slot < MICROS_THREAD_CAPACITY
        && fixture->objects.threads[action->thread_slot].slot_state
            == MICROS_KERNEL_OBJECT_SLOT_LIVE
    ) {
        thread = (struct micros_thread_handle){
            .slot = (uint16_t)action->thread_slot,
            .generation =
                fixture->objects.threads[action->thread_slot].generation,
        };
    } else {
        thread = ipc_model_null_thread_handle();
    }
    switch (action->operation) {
    case MODEL_OPERATION_SEND:
        return micros_ipc_send(
            &fixture->registry,
            &fixture->objects,
            thread,
            action->endpoint,
            action->message
        );
    case MODEL_OPERATION_RECEIVE:
        return micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            thread,
            action->source,
            action->buffer
        );
    case MODEL_OPERATION_CALL:
        return micros_ipc_call(
            &fixture->registry,
            &fixture->objects,
            thread,
            action->endpoint,
            action->message,
            (uintptr_t)action->message
);
    case MODEL_OPERATION_REPLY:
        return micros_ipc_reply(
            &fixture->registry,
            &fixture->objects,
            thread,
            action->value,
            action->message
        );
    case MODEL_OPERATION_REPLY_RECEIVE:
        return micros_ipc_reply_receive(
            &fixture->registry,
            &fixture->objects,
            thread,
            action->value,
            action->message,
            action->source,
            action->buffer
        );
    case MODEL_OPERATION_NOTIFY:
        return micros_ipc_notify(
            &fixture->registry,
            &fixture->objects,
            thread,
            action->endpoint,
            action->value
        );
    case MODEL_OPERATION_KERNEL_NOTIFY:
        return micros_ipc_inject_kernel_notification(
            &fixture->registry,
            &fixture->objects,
            action->endpoint,
            action->value
        );
    case MODEL_OPERATION_DRAIN:
        return production_drain_thread(fixture, action->thread_slot);
    case MODEL_OPERATION_HOLD:
        return production_hold_thread(fixture, action->thread_slot);
    case MODEL_OPERATION_CLOSE_REUSE:
        return production_close_reuse(
            fixture,
            action->process_slot,
            action->endpoint
        );
    default:
        return MICROS_IPC_ERROR_ARGUMENT;
    }
}
