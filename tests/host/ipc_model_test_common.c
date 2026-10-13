#include "ipc_model_test_internal.h"

struct micros_thread_handle ipc_model_null_thread_handle(void)
{
    return (struct micros_thread_handle){0, 0};
}

bool ipc_model_thread_handle_is_zero(struct micros_thread_handle handle)
{
    return handle.slot == 0 && handle.generation == 0;
}

bool ipc_model_thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

bool ipc_model_process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

struct micros_thread_handle ipc_model_thread_handle(
    const struct model_state *model,
    size_t slot
)
{
    return (struct micros_thread_handle){
        .slot = (uint16_t)slot,
        .generation = model->threads[slot].generation,
    };
}

struct micros_user_context ipc_model_context_pattern(uint64_t base)
{
    struct micros_user_context context;
    uint64_t words[
        sizeof(struct micros_user_context) / sizeof(uint64_t)
    ];
    size_t index;

    for (
        index = 0;
        index < sizeof(words) / sizeof(words[0]);
        ++index
    ) {
        words[index] = base + index;
    }
    memcpy(&context, words, sizeof(context));
    return context;
}

bool ipc_model_messages_equal(
    const struct micros_ipc_message *left,
    const struct micros_ipc_message *right
)
{
    return memcmp(left, right, sizeof(*left)) == 0;
}

bool ipc_model_thread_is_held_clear(
    const struct model_thread *thread
)
{
    static const struct micros_ipc_message zero_message;

    return (
        thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
        && thread->scheduler_assigned
        && thread->runtime_flags == MICROS_THREAD_RTS_INACTIVE
        && !thread->ready_linked
        && thread->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && ipc_model_thread_handle_is_zero(thread->ipc_next)
        && ipc_model_messages_equal(
            &thread->ipc_outbound_message,
            &zero_message
        )
        && thread->ipc_send_destination == 0
        && thread->ipc_receive_source == 0
        && thread->ipc_receive_buffer == 0
        && !thread->ipc_delivery_pending
        && ipc_model_messages_equal(
            &thread->ipc_inbound_message,
            &zero_message
        )
        && thread->ipc_staged_result == MICROS_IPC_OK
        && thread->ipc_reply_token == 0
        && thread->ipc_reply_callee == 0
    );
}
