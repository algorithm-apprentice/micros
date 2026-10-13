#include "ipc_model_test_internal.h"

static struct model_action base_action(enum model_operation operation)
{
    struct model_action action;

    memset(&action, 0, sizeof(action));
    action.operation = operation;
    action.thread_slot = MICROS_THREAD_CAPACITY;
    action.process_slot = MICROS_PROCESS_CAPACITY;
    action.endpoint = MICROS_ENDPOINT_NONE;
    action.source = MICROS_ENDPOINT_NONE;
    return action;
}

struct model_action ipc_model_receive_action(
    size_t thread_slot,
    micros_endpoint_t source,
    size_t step
)
{
    struct model_action action =
        base_action(MODEL_OPERATION_RECEIVE);

    action.thread_slot = thread_slot;
    action.source = source;
    action.buffer = ipc_model_receive_buffer_for_step(step, thread_slot);
    return action;
}

struct model_action ipc_model_send_action(
    struct ipc_model_test_context *fixture,
    size_t thread_slot,
    micros_endpoint_t destination,
    size_t step,
    bool kernel_type
)
{
    struct model_action action = base_action(MODEL_OPERATION_SEND);

    action.thread_slot = thread_slot;
    action.endpoint = destination;
    action.message = ipc_model_message_for_step(fixture, step, kernel_type);
    return action;
}

struct model_action ipc_model_call_action(
    struct ipc_model_test_context *fixture,
    size_t thread_slot,
    micros_endpoint_t destination,
    size_t step
)
{
    struct model_action action = base_action(MODEL_OPERATION_CALL);

    action.thread_slot = thread_slot;
    action.endpoint = destination;
    action.message = ipc_model_message_for_step(fixture, step, false);
    return action;
}

struct model_action ipc_model_reply_action(
    struct ipc_model_test_context *fixture,
    size_t thread_slot,
    uint64_t token,
    size_t step
)
{
    struct model_action action = base_action(MODEL_OPERATION_REPLY);

    action.thread_slot = thread_slot;
    action.value = token;
    action.message = ipc_model_message_for_step(fixture, step, false);
    return action;
}

struct model_action ipc_model_reply_receive_action(
    struct ipc_model_test_context *fixture,
    size_t thread_slot,
    uint64_t token,
    micros_endpoint_t source,
    size_t step
)
{
    struct model_action action =
        base_action(MODEL_OPERATION_REPLY_RECEIVE);

    action.thread_slot = thread_slot;
    action.value = token;
    action.message = ipc_model_message_for_step(fixture, step, false);
    action.source = source;
    action.buffer = ipc_model_receive_buffer_for_step(step, thread_slot);
    return action;
}

struct model_action ipc_model_notify_action(
    size_t thread_slot,
    micros_endpoint_t destination,
    uint64_t event_mask
)
{
    struct model_action action = base_action(MODEL_OPERATION_NOTIFY);

    action.thread_slot = thread_slot;
    action.endpoint = destination;
    action.value = event_mask;
    return action;
}

struct model_action ipc_model_kernel_notify_action(
    micros_endpoint_t destination,
    uint64_t event_mask
)
{
    struct model_action action =
        base_action(MODEL_OPERATION_KERNEL_NOTIFY);

    action.endpoint = destination;
    action.value = event_mask;
    return action;
}

struct model_action ipc_model_thread_action(
    enum model_operation operation,
    size_t thread_slot
)
{
    struct model_action action = base_action(operation);

    action.thread_slot = thread_slot;
    return action;
}

struct model_action ipc_model_close_reuse_action(
    const struct model_state *model,
    size_t process_slot
)
{
    struct model_action action =
        base_action(MODEL_OPERATION_CLOSE_REUSE);

    action.process_slot = process_slot;
    action.endpoint = model->processes[process_slot].primary_endpoint;
    return action;
}

static size_t find_held_full_thread(
    const struct model_state *model,
    size_t start
)
{
    size_t offset;

    for (offset = 0; offset < MODEL_THREAD_COUNT; ++offset) {
        size_t slot = (start + offset) % MODEL_THREAD_COUNT;
        const struct model_thread *thread = &model->threads[slot];

        if (
            ipc_model_thread_is_held_clear(thread)
            && model->processes[thread->owner.slot].privilege_profile
                == MODEL_FULL_PROFILE
        ) {
            return slot;
        }
    }
    return MICROS_THREAD_CAPACITY;
}

static size_t find_staged_thread(
    const struct model_state *model,
    size_t start
)
{
    size_t offset;

    for (offset = 0; offset < MODEL_THREAD_COUNT; ++offset) {
        size_t slot = (start + offset) % MODEL_THREAD_COUNT;

        if (
            model->threads[slot].slot_state
                == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && model->threads[slot].ipc_delivery_pending
        ) {
            return slot;
        }
    }
    return MICROS_THREAD_CAPACITY;
}

static size_t find_holdable_thread(
    const struct model_state *model,
    size_t start
)
{
    size_t offset;

    for (offset = 0; offset < MODEL_THREAD_COUNT; ++offset) {
        size_t slot = (start + offset) % MODEL_THREAD_COUNT;
        const struct model_thread *thread = &model->threads[slot];

        if (
            thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && thread->runtime_flags == 0
            && thread->ready_linked
            && !thread->ipc_delivery_pending
        ) {
            return slot;
        }
    }
    return MICROS_THREAD_CAPACITY;
}

static bool find_reply_action(
    const struct model_state *model,
    size_t start,
    size_t *replier_slot,
    uint64_t *token
)
{
    size_t caller_offset;

    for (
        caller_offset = 0;
        caller_offset < MODEL_THREAD_COUNT;
        ++caller_offset
    ) {
        size_t caller_slot =
            (start + caller_offset) % MODEL_THREAD_COUNT;
        const struct model_thread *caller =
            &model->threads[caller_slot];
        size_t callee_slot;
        size_t thread_offset;

        if (
            caller->ipc_reply_token == 0
            || (
                caller->runtime_flags
                & MICROS_THREAD_RTS_IPC_MASK
            ) != MICROS_THREAD_RTS_IPC_REPLY
            || caller->ipc_queue_kind != MICROS_IPC_QUEUE_NONE
            || !ipc_model_resolve_active_endpoint(
                model,
                caller->ipc_reply_callee,
                &callee_slot
            )
        ) {
            continue;
        }
        {
            size_t staged_offset;
            bool request_pending = false;

            for (
                staged_offset = 0;
                staged_offset < MODEL_THREAD_COUNT;
                ++staged_offset
            ) {
                const struct model_thread *staged =
                    &model->threads[staged_offset];

                if (
                    staged->slot_state
                        == MICROS_KERNEL_OBJECT_SLOT_LIVE
                    && staged->ipc_delivery_pending
                    && staged->ipc_staged_result == MICROS_IPC_OK
                    && staged->ipc_inbound_message.reply_token
                        == caller->ipc_reply_token
                ) {
                    request_pending = true;
                    break;
                }
            }
            if (request_pending) {
                continue;
            }
        }
        for (
            thread_offset = 0;
            thread_offset < MODEL_THREAD_COUNT;
            ++thread_offset
        ) {
            size_t slot =
                (start + thread_offset) % MODEL_THREAD_COUNT;
            const struct model_thread *thread =
                &model->threads[slot];

            if (
                ipc_model_thread_is_held_clear(thread)
                && thread->owner.slot == callee_slot
                && model->processes[callee_slot].privilege_profile
                    == MODEL_FULL_PROFILE
            ) {
                *replier_slot = slot;
                *token = caller->ipc_reply_token;
                return true;
            }
        }
    }
    return false;
}

static bool process_is_safe_to_reuse(
    const struct model_state *model,
    size_t process_slot
)
{
    micros_endpoint_t endpoint;
    size_t index;

    if (
        process_slot < MODEL_SINGLETON_FIRST
        || process_slot > MODEL_SINGLETON_LAST
        || model->endpoints[process_slot].state
            != MICROS_ENDPOINT_STATE_ACTIVE
        || model->processes[process_slot].live_thread_count != 1
    ) {
        return false;
    }
    endpoint = model->processes[process_slot].primary_endpoint;
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct model_thread *thread = &model->threads[index];

        if (thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE) {
            continue;
        }
        if (
            thread->owner.slot == process_slot
            && thread->owner.generation
                == model->processes[process_slot].generation
            && !ipc_model_thread_is_held_clear(thread)
        ) {
            return false;
        }
        if (
            thread->owner.slot != process_slot
            && thread->ipc_delivery_pending
            && thread->ipc_staged_result == MICROS_IPC_OK
            && thread->ipc_inbound_message.source == endpoint
        ) {
            return false;
        }
    }
    return true;
}

static size_t find_safe_reuse_process(
    const struct model_state *model,
    size_t start
)
{
    size_t offset;

    for (
        offset = 0;
        offset <= MODEL_SINGLETON_LAST - MODEL_SINGLETON_FIRST;
        ++offset
    ) {
        size_t slot =
            MODEL_SINGLETON_FIRST
            + (
                start + offset
            ) % (
                MODEL_SINGLETON_LAST
                - MODEL_SINGLETON_FIRST
                + 1
            );

        if (process_is_safe_to_reuse(model, slot)) {
            return slot;
        }
    }
    return MICROS_PROCESS_CAPACITY;
}

static bool find_receiver_recovery(
    const struct model_state *model,
    size_t start,
    size_t *sender_slot,
    micros_endpoint_t *destination
)
{
    size_t process_offset;

    for (
        process_offset = 0;
        process_offset < MODEL_PROCESS_COUNT;
        ++process_offset
    ) {
        size_t process_slot =
            (start + process_offset) % MODEL_PROCESS_COUNT;
        struct micros_thread_handle current =
            model->endpoints[process_slot].receiver_head;
        size_t steps;

        for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
            const struct model_thread *receiver;
            size_t candidate_offset;

            if (ipc_model_thread_handle_is_zero(current)) {
                break;
            }
            receiver = &model->threads[current.slot];
            for (
                candidate_offset = 0;
                candidate_offset < MODEL_THREAD_COUNT;
                ++candidate_offset
            ) {
                size_t candidate =
                    (start + candidate_offset)
                    % MODEL_THREAD_COUNT;
                const struct model_thread *thread =
                    &model->threads[candidate];
                micros_endpoint_t source;

                if (
                    !ipc_model_thread_is_held_clear(thread)
                    || model->processes[thread->owner.slot]
                        .privilege_profile != MODEL_FULL_PROFILE
                    || model->processes[process_slot]
                        .privilege_profile != MODEL_FULL_PROFILE
                ) {
                    continue;
                }
                source =
                    model->processes[thread->owner.slot]
                        .primary_endpoint;
                if (
                    receiver->ipc_receive_source
                        == MICROS_ENDPOINT_ANY
                    || receiver->ipc_receive_source == source
                ) {
                    *sender_slot = candidate;
                    *destination =
                        model->processes[process_slot]
                            .primary_endpoint;
                    return true;
                }
            }
            current = receiver->ipc_next;
        }
    }
    return false;
}

static bool find_sender_recovery(
    const struct model_state *model,
    size_t start,
    size_t *receiver_slot
)
{
    size_t process_offset;

    for (
        process_offset = 0;
        process_offset < MODEL_PROCESS_COUNT;
        ++process_offset
    ) {
        size_t process_slot =
            (start + process_offset) % MODEL_PROCESS_COUNT;
        size_t thread_offset;

        if (
            ipc_model_thread_handle_is_zero(
                model->endpoints[process_slot].sender_head
            )
        ) {
            continue;
        }
        for (
            thread_offset = 0;
            thread_offset < MODEL_THREAD_COUNT;
            ++thread_offset
        ) {
            size_t slot =
                (start + thread_offset) % MODEL_THREAD_COUNT;
            const struct model_thread *thread =
                &model->threads[slot];

            if (
                ipc_model_thread_is_held_clear(thread)
                && thread->owner.slot == process_slot
            ) {
                *receiver_slot = slot;
                return true;
            }
        }
    }
    return false;
}

static struct model_action recovery_action(
    struct ipc_model_test_context *fixture,
    const struct model_state *model,
    uint32_t random,
    size_t step
)
{
    size_t slot =
        find_staged_thread(model, random % MODEL_THREAD_COUNT);

    if (slot != MICROS_THREAD_CAPACITY) {
        return ipc_model_thread_action(MODEL_OPERATION_DRAIN, slot);
    }
    slot = find_holdable_thread(model, random % MODEL_THREAD_COUNT);
    if (slot != MICROS_THREAD_CAPACITY) {
        return ipc_model_thread_action(MODEL_OPERATION_HOLD, slot);
    }
    {
        size_t sender_slot;
        micros_endpoint_t destination;

        if (
            find_receiver_recovery(
                model,
                random % MODEL_PROCESS_COUNT,
                &sender_slot,
                &destination
            )
        ) {
            return ipc_model_send_action(
                fixture,
                sender_slot,
                destination,
                step,
                false
            );
        }
    }
    if (
        find_sender_recovery(
            model,
            random % MODEL_PROCESS_COUNT,
            &slot
        )
    ) {
        return ipc_model_receive_action(slot, MICROS_ENDPOINT_ANY, step);
    }
    slot = find_safe_reuse_process(
        model,
        random % (
            MODEL_SINGLETON_LAST - MODEL_SINGLETON_FIRST + 1
        )
    );
    if (slot != MICROS_PROCESS_CAPACITY) {
        return ipc_model_close_reuse_action(model, slot);
    }
    slot = find_held_full_thread(model, random % MODEL_THREAD_COUNT);
    if (slot != MICROS_THREAD_CAPACITY) {
        return ipc_model_notify_action(
            slot,
            model->processes[0].primary_endpoint,
            0
        );
    }
    return ipc_model_send_action(
        fixture,
        9,
        model->processes[0].primary_endpoint,
        step,
        false
    );
}

static bool select_random_send(
    struct ipc_model_test_context *fixture,
    const struct model_state *model,
    uint32_t random,
    size_t step,
    struct model_action *selected_action
)
{
    size_t thread_slot;
    size_t process_slot;

    thread_slot = find_held_full_thread(
        model,
        (random >> 8) % MODEL_THREAD_COUNT
    );
    if (thread_slot != MICROS_THREAD_CAPACITY) {
        process_slot =
            (random >> 16) % MODEL_DENIED_PROCESS;
        *selected_action = ipc_model_send_action(
            fixture,
            thread_slot,
            model->processes[process_slot].primary_endpoint,
            step,
            false
        );
        return true;
    }
    return false;
}

static bool select_random_receive(
    const struct model_state *model,
    uint32_t random,
    size_t step,
    struct model_action *selected_action
)
{
    size_t thread_slot;
    size_t process_slot;

    thread_slot = find_held_full_thread(
        model,
        (random >> 8) % MODEL_THREAD_COUNT
    );
    if (thread_slot != MICROS_THREAD_CAPACITY) {
        micros_endpoint_t source = MICROS_ENDPOINT_ANY;

        if ((random & UINT32_C(0x10000)) != 0) {
            process_slot =
                (random >> 17) % MODEL_DENIED_PROCESS;
            source =
                model->processes[process_slot].primary_endpoint;
        }
        *selected_action = ipc_model_receive_action(thread_slot, source, step);
        return true;
    }
    return false;
}

static bool select_random_call(
    struct ipc_model_test_context *fixture,
    const struct model_state *model,
    uint32_t random,
    size_t step,
    struct model_action *selected_action
)
{
    size_t thread_slot;
    size_t process_slot;

    thread_slot = find_held_full_thread(
        model,
        (random >> 8) % MODEL_THREAD_COUNT
    );
    if (thread_slot != MICROS_THREAD_CAPACITY) {
        process_slot =
            (random >> 16) % MODEL_DENIED_PROCESS;
        *selected_action = ipc_model_call_action(
            fixture,
            thread_slot,
            model->processes[process_slot].primary_endpoint,
            step
        );
        return true;
    }
    return false;
}

static bool select_random_reply(
    struct ipc_model_test_context *fixture,
    const struct model_state *model,
    uint32_t random,
    size_t step,
    struct model_action *selected_action
)
{
    size_t thread_slot;

    {
        uint64_t token;

        if (
            find_reply_action(
                model,
                (random >> 8) % MODEL_THREAD_COUNT,
                &thread_slot,
                &token
            )
        ) {
            if (random % 13 == 3) {
                *selected_action = ipc_model_reply_action(fixture, thread_slot, token, step);
                return true;
            }
            *selected_action = ipc_model_reply_receive_action(
                fixture,
                thread_slot,
                token,
                MICROS_ENDPOINT_ANY,
                step
            );
            return true;
        }
    }
    thread_slot = find_held_full_thread(
        model,
        (random >> 8) % MODEL_THREAD_COUNT
    );
    if (thread_slot != MICROS_THREAD_CAPACITY) {
        *selected_action = random % 13 == 3
            ? ipc_model_reply_action(
                fixture,
                thread_slot,
                UINT64_C(0xf000000000000000)
                    | (uint64_t)step,
                step
            )
            : ipc_model_reply_receive_action(
                fixture,
                thread_slot,
                UINT64_C(0xf000000000000000)
                    | (uint64_t)step,
                MICROS_ENDPOINT_ANY,
                step
            );
        return true;
    }
    return false;
}

static bool select_random_notification(
    const struct model_state *model,
    uint32_t random,
    struct model_action *selected_action
)
{
    size_t thread_slot;
    size_t process_slot;

    thread_slot = find_held_full_thread(
        model,
        (random >> 8) % MODEL_THREAD_COUNT
    );
    if (thread_slot != MICROS_THREAD_CAPACITY) {
        process_slot =
            (random >> 16) % MODEL_DENIED_PROCESS;
        *selected_action = ipc_model_notify_action(
            thread_slot,
            model->processes[process_slot].primary_endpoint,
            UINT64_C(1) << ((random >> 24) % 63)
        );
        return true;
    }
    return false;
}

static bool select_random_completion_drain(
    const struct model_state *model,
    uint32_t random,
    struct model_action *selected_action
)
{
    size_t thread_slot;

    thread_slot = find_staged_thread(
        model,
        (random >> 8) % MODEL_THREAD_COUNT
    );
    if (thread_slot != MICROS_THREAD_CAPACITY) {
        *selected_action = ipc_model_thread_action(MODEL_OPERATION_DRAIN, thread_slot);
        return true;
    }
    return false;
}

static bool select_random_thread_hold(
    const struct model_state *model,
    uint32_t random,
    struct model_action *selected_action
)
{
    size_t thread_slot;

    thread_slot = find_holdable_thread(
        model,
        (random >> 8) % MODEL_THREAD_COUNT
    );
    if (thread_slot != MICROS_THREAD_CAPACITY) {
        *selected_action = ipc_model_thread_action(MODEL_OPERATION_HOLD, thread_slot);
        return true;
    }
    return false;
}

static bool select_random_generation_reuse(
    const struct model_state *model,
    uint32_t random,
    struct model_action *selected_action
)
{
    size_t process_slot;

    process_slot = find_safe_reuse_process(
        model,
        (random >> 8) % (
            MODEL_SINGLETON_LAST
            - MODEL_SINGLETON_FIRST
            + 1
        )
    );
    if (process_slot != MICROS_PROCESS_CAPACITY) {
        *selected_action = ipc_model_close_reuse_action(model, process_slot);
        return true;
    }
    return false;
}

static bool select_random_permission_denial(
    struct ipc_model_test_context *fixture,
    const struct model_state *model,
    size_t step,
    struct model_action *selected_action
)
{
    *selected_action = ipc_model_send_action(
        fixture,
        9,
        model->processes[0].primary_endpoint,
        step,
        false
    );
    return true;
}

static bool select_random_malformed_request(
    const struct model_state *model,
    uint32_t random,
    size_t step,
    struct model_action *selected_action
)
{
    size_t thread_slot;

    thread_slot = find_held_full_thread(
        model,
        (random >> 8) % MODEL_THREAD_COUNT
    );
    if (thread_slot != MICROS_THREAD_CAPACITY) {
        if ((random & 1) == 0) {
            *selected_action = ipc_model_notify_action(
                thread_slot,
                model->processes[0].primary_endpoint,
                0
            );
            return true;
        }
        {
            struct model_action action =
                ipc_model_receive_action(
                    thread_slot,
                    MICROS_ENDPOINT_ANY,
                    step
                );

            action.buffer |= 1;
            *selected_action = action;
            return true;
        }
    }
    return false;
}

static bool select_random_singleton_dependency(
    struct ipc_model_test_context *fixture,
    const struct model_state *model,
    uint32_t random,
    size_t step,
    struct model_action *selected_action
)
{
    {
        size_t singleton =
            MODEL_SINGLETON_FIRST
            + (
                (random >> 8)
                % (
                    MODEL_SINGLETON_LAST
                    - MODEL_SINGLETON_FIRST
                    + 1
                )
            );
        size_t destination =
            MODEL_SINGLETON_FIRST
            + (
                (random >> 16)
                % (
                    MODEL_SINGLETON_LAST
                    - MODEL_SINGLETON_FIRST
                    + 1
                )
            );
        size_t index;

        for (index = 0; index < MODEL_THREAD_COUNT; ++index) {
            if (
                model->threads[index].owner.slot == singleton
                && ipc_model_thread_is_held_clear(
                    &model->threads[index]
                )
            ) {
                *selected_action = ipc_model_send_action(
                    fixture,
                    index,
                    model->processes[destination]
                        .primary_endpoint,
                    step,
                    false
                );
                return true;
            }
        }
    }
    return false;
}

static bool select_random_kernel_event(
    const struct model_state *model,
    uint32_t random,
    struct model_action *selected_action
)
{
    size_t process_slot;

    process_slot =
        (random >> 16) % MODEL_DENIED_PROCESS;
    *selected_action = ipc_model_kernel_notify_action(
        model->processes[process_slot].primary_endpoint,
        UINT64_C(1) << ((random >> 24) % 63)
    );
    return true;
}

struct model_action ipc_model_build_random_action(
    struct ipc_model_test_context *fixture,
    const struct model_state *model,
    uint32_t random,
    size_t step
)
{
    struct model_action selected_action;
    bool selected = false;

    switch (random % 13) {
    case 0:
        selected = select_random_send(fixture, model, random, step, &selected_action);
        break;
    case 1:
        selected = select_random_receive(model, random, step, &selected_action);
        break;
    case 2:
        selected = select_random_call(fixture, model, random, step, &selected_action);
        break;
    case 3:
    case 4:
        selected = select_random_reply(fixture, model, random, step, &selected_action);
        break;
    case 5:
        selected = select_random_notification(model, random, &selected_action);
        break;
    case 6:
        selected = select_random_completion_drain(model, random, &selected_action);
        break;
    case 7:
        selected = select_random_thread_hold(model, random, &selected_action);
        break;
    case 8:
        selected = select_random_generation_reuse(model, random, &selected_action);
        break;
    case 9:
        selected = select_random_permission_denial(fixture, model, step, &selected_action);
        break;
    case 10:
        selected = select_random_malformed_request(model, random, step, &selected_action);
        break;
    case 11:
        selected = select_random_singleton_dependency(fixture, model, random, step, &selected_action);
        break;
    case 12:
        selected = select_random_kernel_event(model, random, &selected_action);
        break;
    default:
        break;
    }
    return selected ? selected_action : recovery_action(fixture, model, random, step);
}
