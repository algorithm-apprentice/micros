#include "ipc_model_test_internal.h"

static bool model_thread_references_endpoint(
    const struct model_thread *thread,
    micros_endpoint_t endpoint
)
{
    return (
        thread->ipc_send_destination == endpoint
        || thread->ipc_receive_source == endpoint
        || thread->ipc_reply_callee == endpoint
        || thread->ipc_outbound_message.source == endpoint
        || (
            thread->ipc_delivery_pending
            && thread->ipc_staged_result == MICROS_IPC_OK
            && thread->ipc_inbound_message.source == endpoint
        )
    );
}

static void model_rebuild_queue(
    struct model_state *model,
    struct micros_thread_handle *head,
    struct micros_thread_handle *tail,
    const bool remove[MICROS_THREAD_CAPACITY]
)
{
    struct micros_thread_handle current = *head;
    struct micros_thread_handle retained_head = ipc_model_null_thread_handle();
    struct micros_thread_handle retained_tail = ipc_model_null_thread_handle();
    size_t steps;

    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        struct micros_thread_handle next;

        if (ipc_model_thread_handle_is_zero(current)) {
            break;
        }
        next = model->threads[current.slot].ipc_next;
        if (!remove[current.slot]) {
            if (ipc_model_thread_handle_is_zero(retained_head)) {
                retained_head = current;
            } else {
                model->threads[retained_tail.slot].ipc_next =
                    current;
            }
            retained_tail = current;
        }
        current = next;
    }
    if (!ipc_model_thread_handle_is_zero(retained_tail)) {
        model->threads[retained_tail.slot].ipc_next =
            ipc_model_null_thread_handle();
    }
    *head = retained_head;
    *tail = retained_tail;
}

enum model_close_action {
    CLOSE_NONE = 0,
    CLOSE_CLEAR,
    CLOSE_DEAD,
};

struct model_close_plan {
    unsigned char actions[MICROS_THREAD_CAPACITY];
    bool remove[MICROS_THREAD_CAPACITY];
    struct model_transition transitions[MICROS_THREAD_CAPACITY];
    size_t transition_count;
    struct model_process *owner;
    micros_endpoint_t endpoint;
};

static enum micros_ipc_error preflight_model_close_threads(
    struct model_state *model,
    size_t process_slot,
    struct model_close_plan *plan
)
{
    size_t index;

    memset(plan->actions, 0, sizeof(plan->actions));
    memset(plan->remove, 0, sizeof(plan->remove));
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct model_thread *thread = &model->threads[index];
        bool owned;
        bool queued_sender;
        bool queued_receiver;
        bool reply_wait;
        bool staged_reference;
        uint32_t clear_flags;

        if (thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE) {
            continue;
        }
        owned =
            thread->owner.slot == process_slot
            && thread->owner.generation == plan->owner->generation;
        queued_sender =
            thread->ipc_queue_kind == MICROS_IPC_QUEUE_SENDER
            && thread->ipc_send_destination == plan->endpoint;
        queued_receiver =
            thread->ipc_queue_kind == MICROS_IPC_QUEUE_RECEIVER
            && thread->ipc_receive_source == plan->endpoint;
        reply_wait =
            thread->ipc_reply_token != 0
            && thread->ipc_reply_callee == plan->endpoint;
        staged_reference =
            thread->ipc_delivery_pending
            && thread->ipc_staged_result == MICROS_IPC_OK
            && thread->ipc_inbound_message.source == plan->endpoint;
        clear_flags =
            thread->runtime_flags & MICROS_THREAD_RTS_IPC_MASK;
        if (owned) {
            if (
                ipc_model_thread_handles_equal(
                    model->hart.current_thread,
                    ipc_model_thread_handle(model, index)
                )
                || (
                    thread->ipc_delivery_pending
                    && thread->ipc_staged_result == MICROS_IPC_OK
                )
                || (
                    thread->runtime_flags
                    & MICROS_THREAD_RTS_INACTIVE
                ) == 0
                || (
                    thread->runtime_flags
                    & ~MICROS_THREAD_RTS_IPC_MASK
                ) != MICROS_THREAD_RTS_INACTIVE
            ) {
                return MICROS_IPC_ERROR_STATE;
            }
            if (!ipc_model_thread_is_held_clear(thread)) {
                plan->actions[index] = CLOSE_CLEAR;
            }
            plan->remove[index] =
                thread->ipc_queue_kind != MICROS_IPC_QUEUE_NONE;
        } else if (queued_sender || queued_receiver || reply_wait) {
            plan->actions[index] = CLOSE_DEAD;
            plan->remove[index] = queued_sender || queued_receiver;
        } else if (staged_reference) {
            return MICROS_IPC_ERROR_STATE;
        }
        if (
            model_thread_references_endpoint(thread, plan->endpoint)
            && plan->actions[index] == CLOSE_NONE
        ) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
        if (plan->actions[index] != CLOSE_NONE && clear_flags != 0) {
            plan->transitions[plan->transition_count] =
                (struct model_transition){
                    .thread_slot = index,
                    .clear_flags = clear_flags,
                };
            ++plan->transition_count;
        }
    }
    return MICROS_IPC_OK;
}

static void commit_model_close(
    struct model_state *model,
    size_t process_slot,
    const struct model_close_plan *plan
)
{
    size_t index;

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        model_rebuild_queue(
            model,
            &model->endpoints[index].sender_head,
            &model->endpoints[index].sender_tail,
            plan->remove
        );
        model_rebuild_queue(
            model,
            &model->endpoints[index].receiver_head,
            &model->endpoints[index].receiver_tail,
            plan->remove
        );
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        if (plan->actions[index] == CLOSE_CLEAR) {
            ipc_model_clear_ipc_state(&model->threads[index]);
        } else if (plan->actions[index] == CLOSE_DEAD) {
            ipc_model_clear_ipc_state(&model->threads[index]);
            model->threads[index].ipc_delivery_pending = true;
            model->threads[index].ipc_staged_result =
                MICROS_IPC_ERROR_DEAD_ENDPOINT;
        }
    }
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        model->endpoints[index].pending_notification_sources &=
            ~(UINT64_C(1) << process_slot);
        model->endpoints[index].pending_events[process_slot] = 0;
    }
    model->endpoints[process_slot].pending_notification_sources = 0;
    memset(
        model->endpoints[process_slot].pending_events,
        0,
        sizeof(model->endpoints[process_slot].pending_events)
    );
    memset(
        &model->endpoints[process_slot],
        0,
        sizeof(model->endpoints[process_slot])
    );
    plan->owner->primary_endpoint = MICROS_PROCESS_ENDPOINT_NONE;
    plan->owner->privilege_profile = 0;
}

static enum micros_ipc_error model_close_endpoint(
    struct model_state *model,
    size_t process_slot
)
{
    struct model_close_plan plan;
    enum micros_ipc_error error;

    plan.transition_count = 0;
    if (process_slot >= MICROS_PROCESS_CAPACITY) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    plan.owner = &model->processes[process_slot];
    plan.endpoint = plan.owner->primary_endpoint;
    if (
        plan.owner->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || model->endpoints[process_slot].state != MICROS_ENDPOINT_STATE_ACTIVE
        || model->endpoints[process_slot].value != plan.endpoint
    ) {
        return MICROS_IPC_ERROR_DEAD_ENDPOINT;
    }
    error = preflight_model_close_threads(model, process_slot, &plan);
    if (error != MICROS_IPC_OK) {
        return error;
    }
    if (plan.transition_count != 0) {
        error = ipc_model_apply_transitions(model, plan.transitions, plan.transition_count);
        if (error != MICROS_IPC_OK) {
            return error;
        }
    }
    commit_model_close(model, process_slot, &plan);
    return MICROS_IPC_OK;
}

static bool model_reset_reused_thread(
    struct model_state *model,
    size_t thread_slot,
    struct micros_process_handle owner
)
{
    struct model_thread *thread = &model->threads[thread_slot];
    uint32_t generation = thread->generation + 1;
    uintptr_t stack_bottom =
        UINT64_C(0x10000000)
        + thread_slot * UINT64_C(0x00008000);

    if (generation == 0) {
        return false;
    }
    memset(thread, 0, sizeof(*thread));
    thread->slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE;
    thread->generation = generation;
    thread->owner = owner;
    thread->context_attached = true;
    thread->kernel_stack_bottom = stack_bottom;
    thread->kernel_stack_top =
        stack_bottom + MICROS_THREAD_KERNEL_STACK_SIZE;
    thread->runtime_flags = MICROS_THREAD_RTS_INACTIVE;
    thread->scheduler_assigned = true;
    thread->scheduler_preemptible = true;
    thread->scheduler_priority = MODEL_PRIORITY;
    thread->scheduler_hart = model->hart_handle;
    thread->quantum_counter_ticks = 100;
    thread->remaining_counter_ticks = 100;
    thread->ipc_staged_result = MICROS_IPC_OK;
    return true;
}

static enum micros_ipc_error model_reuse_process(
    struct model_state *model,
    size_t process_slot
)
{
    struct model_process *process = &model->processes[process_slot];
    struct micros_process_handle owner;
    size_t owned_slots[2];
    size_t owned_count = 0;
    size_t index;
    uint32_t generation;
    micros_endpoint_t endpoint;

    if (
        process->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || process->primary_endpoint != MICROS_PROCESS_ENDPOINT_NONE
        || process->privilege_profile != 0
    ) {
        return MICROS_IPC_ERROR_STATE;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        if (
            model->threads[index].slot_state
                == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && model->threads[index].owner.slot == process_slot
            && model->threads[index].owner.generation
                == process->generation
        ) {
            if (
                owned_count >= sizeof(owned_slots) / sizeof(owned_slots[0])
                || !ipc_model_thread_is_held_clear(&model->threads[index])
            ) {
                return MICROS_IPC_ERROR_STATE;
            }
            owned_slots[owned_count] = index;
            ++owned_count;
        }
    }
    if (owned_count != process->live_thread_count) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    generation = process->generation + 1;
    if (generation == 0 || generation > MICROS_PROCESS_GENERATION_MAX) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    owner = (struct micros_process_handle){
        .slot = (uint16_t)process_slot,
        .generation = generation,
    };
    endpoint =
        (generation << MICROS_ENDPOINT_SLOT_BITS)
        | (uint32_t)process_slot;
    memset(process, 0, sizeof(*process));
    process->slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE;
    process->generation = generation;
    process->live_thread_count = owned_count;
    process->primary_endpoint = endpoint;
    process->privilege_profile = MODEL_FULL_PROFILE;
    process->endpoint_lifecycle_consumed = true;
    memset(
        &model->endpoints[process_slot],
        0,
        sizeof(model->endpoints[process_slot])
    );
    model->endpoints[process_slot].state =
        MICROS_ENDPOINT_STATE_ACTIVE;
    model->endpoints[process_slot].owner = owner;
    model->endpoints[process_slot].value = endpoint;
    for (index = 0; index < owned_count; ++index) {
        if (!model_reset_reused_thread(
            model,
            owned_slots[index],
            owner
        )) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
    }
    return MICROS_IPC_OK;
}

enum micros_ipc_error ipc_model_close_reuse(
    struct model_state *model,
    size_t process_slot
)
{
    enum micros_ipc_error error =
        model_close_endpoint(model, process_slot);

    if (error != MICROS_IPC_OK) {
        return error;
    }
    return model_reuse_process(model, process_slot);
}
