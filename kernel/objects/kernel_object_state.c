#include "kernel/objects/kernel_object_private.h"

static bool bytes_are_zero(const void *storage, size_t size)
{
    const unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static void clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

bool kernel_object_storage_is_zero(
    const struct micros_kernel_objects *objects
)
{
    return bytes_are_zero(objects, sizeof(*objects));
}

bool kernel_object_thread_scheduler_metadata_is_zero(
    const struct micros_thread *thread
)
{
    return (
        thread->runtime_flags == 0
        && !thread->scheduler_assigned
        && !thread->scheduler_preemptible
        && thread->scheduler_priority == 0
        && kernel_object_hart_handle_is_null(thread->scheduler_hart)
        && thread->quantum_counter_ticks == 0
        && thread->remaining_counter_ticks == 0
        && !thread->ready_linked
        && kernel_object_thread_handle_is_null(thread->ready_next)
    );
}

void kernel_object_clear_thread_scheduler_metadata(
    struct micros_thread *thread
)
{
    thread->runtime_flags = 0;
    thread->scheduler_assigned = false;
    thread->scheduler_preemptible = false;
    thread->scheduler_priority = 0;
    thread->scheduler_hart.slot = 0;
    thread->scheduler_hart.generation = 0;
    thread->quantum_counter_ticks = 0;
    thread->remaining_counter_ticks = 0;
    thread->ready_linked = false;
    thread->ready_next.slot = 0;
    thread->ready_next.generation = 0;
}

bool micros_thread_ipc_state_is_clear(
    const struct micros_thread *thread
)
{
    if (thread == NULL) {
        return false;
    }
    return (
        thread->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && thread->ipc_next.slot == 0
        && thread->ipc_next.generation == 0
        && bytes_are_zero(
            &thread->ipc_outbound_message,
            sizeof(thread->ipc_outbound_message)
        )
        && thread->ipc_send_destination == 0
        && thread->ipc_receive_source == 0
        && thread->ipc_receive_buffer == 0
        && !thread->ipc_delivery_pending
        && bytes_are_zero(
            &thread->ipc_inbound_message,
            sizeof(thread->ipc_inbound_message)
        )
        && thread->ipc_staged_result == MICROS_IPC_OK
        && thread->ipc_reply_token == 0
        && thread->ipc_reply_callee == 0
    );
}

void kernel_object_clear_thread_ipc_state(struct micros_thread *thread)
{
    thread->ipc_queue_kind = MICROS_IPC_QUEUE_NONE;
    thread->ipc_next.slot = 0;
    thread->ipc_next.generation = 0;
    clear_bytes(
        &thread->ipc_outbound_message,
        sizeof(thread->ipc_outbound_message)
    );
    thread->ipc_send_destination = 0;
    thread->ipc_receive_source = 0;
    thread->ipc_receive_buffer = 0;
    thread->ipc_delivery_pending = false;
    clear_bytes(
        &thread->ipc_inbound_message,
        sizeof(thread->ipc_inbound_message)
    );
    thread->ipc_staged_result = MICROS_IPC_OK;
    thread->ipc_reply_token = 0;
    thread->ipc_reply_callee = 0;
}

void kernel_object_copy_user_context(
    struct micros_user_context *destination,
    const struct micros_user_context *source
)
{
    unsigned char *destination_bytes =
        (unsigned char *)destination;
    const unsigned char *source_bytes =
        (const unsigned char *)source;
    size_t index;

    for (index = 0; index < sizeof(*destination); ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

void kernel_object_clear_user_context(struct micros_user_context *context)
{
    unsigned char *bytes = (unsigned char *)context;
    size_t index;

    for (index = 0; index < sizeof(*context); ++index) {
        bytes[index] = 0;
    }
}

bool kernel_object_user_context_is_zero(
    const struct micros_user_context *context
)
{
    const unsigned char *bytes =
        (const unsigned char *)context;
    size_t index;

    for (index = 0; index < sizeof(*context); ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

bool kernel_object_trap_anchor_is_zero(
    const struct micros_hart_trap_anchor *trap
)
{
    return (
        trap->primary_stack_top == 0
        && trap->primary_stack_bottom == 0
        && trap->emergency_stack_top == 0
        && trap->emergency_stack_bottom == 0
        && trap->entry_t0 == 0
        && trap->entry_t1 == 0
        && trap->entry_t2 == 0
    );
}

bool kernel_object_timer_state_is_initial(
    const struct micros_hart_timer_state *timer
)
{
    return (
        !timer->initialized
        && !timer->active
        && timer->interval == 0
        && timer->deadline == 0
        && timer->ticks == 0
        && timer->test_stop_after == 0
    );
}
