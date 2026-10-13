#include "ipc_model_test_internal.h"

static bool compare_acceptance_aggregates(
    const struct ipc_model_test_context *fixture,
    const struct model_state *model,
    size_t step
)
{
    if (
        model->live_process_count != fixture->objects.live_process_count
        || model->live_thread_count != fixture->objects.live_thread_count
        || model->last_reply_token != fixture->registry.last_reply_token
    ) {
        fprintf(stderr, "IPC model aggregate mismatch at step %zu\n", step);
        return false;
    }
    return true;
}

static bool compare_acceptance_processes(
    const struct ipc_model_test_context *fixture,
    const struct model_state *model,
    size_t step
)
{
    size_t index;

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct model_process *expected =
            &model->processes[index];
        const struct micros_process *actual =
            &fixture->objects.processes[index];

        if (
            expected->slot_state != actual->slot_state
            || expected->generation != actual->generation
            || expected->live_thread_count != actual->live_thread_count
            || expected->primary_endpoint != actual->primary_endpoint
            || expected->privilege_profile != actual->privilege_profile
            || expected->endpoint_lifecycle_consumed
                != actual->endpoint_lifecycle_consumed
        ) {
            fprintf(
                stderr,
                "IPC model process mismatch at step %zu slot %zu\n",
                step,
                index
            );
            return false;
        }
    }
    return true;
}

static bool compare_acceptance_endpoints_and_events(
    const struct ipc_model_test_context *fixture,
    const struct model_state *model,
    size_t step
)
{
    size_t index;
    size_t other;

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct model_endpoint *expected =
            &model->endpoints[index];
        const struct micros_endpoint_record *actual =
            &fixture->registry.endpoints[index];

        if (
            expected->state != actual->state
            || !ipc_model_process_handles_equal(expected->owner, actual->owner)
            || expected->value != actual->value
            || !ipc_model_thread_handles_equal(
                expected->sender_head,
                actual->sender_head
            )
            || !ipc_model_thread_handles_equal(
                expected->sender_tail,
                actual->sender_tail
            )
            || !ipc_model_thread_handles_equal(
                expected->receiver_head,
                actual->receiver_head
            )
            || !ipc_model_thread_handles_equal(
                expected->receiver_tail,
                actual->receiver_tail
            )
            || expected->pending_notification_sources
                != actual->pending_notification_sources
            || expected->pending_kernel_events
                != actual->pending_kernel_events
        ) {
            fprintf(
                stderr,
                "IPC model endpoint mismatch at step %zu slot %zu\n",
                step,
                index
            );
            return false;
        }
        for (other = 0; other < MICROS_PROCESS_CAPACITY; ++other) {
            if (
                expected->pending_events[other]
                    != actual->pending_events[other]
            ) {
                fprintf(
                    stderr,
                    "IPC model notification mismatch at step %zu "
                    "destination %zu source %zu\n",
                    step,
                    index,
                    other
                );
                return false;
            }
        }
    }
    return true;
}

static bool compare_acceptance_threads(
    const struct ipc_model_test_context *fixture,
    const struct model_state *model,
    size_t step
)
{
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct model_thread *expected = &model->threads[index];
        const struct micros_thread *actual = &fixture->objects.threads[index];

        if (
            expected->slot_state != actual->slot_state
            || expected->generation != actual->generation
            || !ipc_model_process_handles_equal(expected->owner, actual->owner)
            || expected->context_attached != actual->context_attached
            || expected->kernel_stack_bottom
                != actual->kernel_stack_bottom
            || expected->kernel_stack_top != actual->kernel_stack_top
            || expected->runtime_flags != actual->runtime_flags
            || expected->scheduler_assigned
                != actual->scheduler_assigned
            || expected->scheduler_preemptible
                != actual->scheduler_preemptible
            || expected->scheduler_priority
                != actual->scheduler_priority
            || expected->scheduler_hart.slot
                != actual->scheduler_hart.slot
            || expected->scheduler_hart.generation
                != actual->scheduler_hart.generation
            || expected->quantum_counter_ticks
                != actual->quantum_counter_ticks
            || expected->remaining_counter_ticks
                != actual->remaining_counter_ticks
            || expected->ready_linked != actual->ready_linked
            || !ipc_model_thread_handles_equal(
                expected->ready_next,
                actual->ready_next
            )
            || expected->ipc_queue_kind != actual->ipc_queue_kind
            || !ipc_model_thread_handles_equal(
                expected->ipc_next,
                actual->ipc_next
            )
            || !ipc_model_messages_equal(
                &expected->ipc_outbound_message,
                &actual->ipc_outbound_message
            )
            || expected->ipc_send_destination
                != actual->ipc_send_destination
            || expected->ipc_receive_source
                != actual->ipc_receive_source
            || expected->ipc_receive_buffer
                != actual->ipc_receive_buffer
            || expected->ipc_delivery_pending
                != actual->ipc_delivery_pending
            || !ipc_model_messages_equal(
                &expected->ipc_inbound_message,
                &actual->ipc_inbound_message
            )
            || expected->ipc_staged_result
                != actual->ipc_staged_result
            || expected->ipc_reply_token
                != actual->ipc_reply_token
            || expected->ipc_reply_callee
                != actual->ipc_reply_callee
        ) {
            fprintf(
                stderr,
                "IPC model thread mismatch at step %zu slot %zu\n",
                step,
                index
            );
            return false;
        }
    }
    return true;
}

static bool compare_acceptance_hart_and_ready_queues(
    const struct ipc_model_test_context *fixture,
    const struct model_state *model,
    size_t step
)
{
    size_t index;

    if (
        !ipc_model_thread_handles_equal(
            model->hart.current_thread,
            fixture->objects.harts[model->hart_handle.slot].current_thread
        )
        || model->hart.accounting_owner
            != fixture->objects.harts[model->hart_handle.slot].accounting_owner
        || model->hart.accounting_started_at
            != fixture->objects.harts[model->hart_handle.slot].accounting_started_at
        || !ipc_model_thread_handles_equal(
            model->hart.accounted_thread,
            fixture->objects.harts[model->hart_handle.slot].accounted_thread
        )
        || model->hart.kernel_counter_ticks
            != fixture->objects.harts[model->hart_handle.slot].kernel_counter_ticks
        || model->hart.idle_counter_ticks
            != fixture->objects.harts[model->hart_handle.slot].idle_counter_ticks
        || model->hart.reschedule_pending
            != fixture->objects.harts[model->hart_handle.slot].reschedule_pending
    ) {
        fprintf(stderr, "IPC model hart mismatch at step %zu\n", step);
        return false;
    }
    for (index = 0; index < MICROS_SCHEDULER_PRIORITY_COUNT; ++index) {
        if (
            !ipc_model_thread_handles_equal(
                model->hart.ready_head[index],
                fixture->objects.harts[
                    model->hart_handle.slot
                ].ready_head[index]
            )
            || !ipc_model_thread_handles_equal(
                model->hart.ready_tail[index],
                fixture->objects.harts[
                    model->hart_handle.slot
                ].ready_tail[index]
            )
        ) {
            fprintf(
                stderr,
                "IPC model ready queue mismatch at step %zu priority %zu\n",
                step,
                index
            );
            return false;
        }
    }
    return true;
}

static bool compare_acceptance_public_invariants(
    const struct ipc_model_test_context *fixture,
    size_t step
)
{
    if (
        micros_kernel_objects_validate(&fixture->objects)
            != MICROS_KERNEL_OBJECT_OK
        || micros_scheduler_core_validate(&fixture->objects)
            != MICROS_KERNEL_OBJECT_OK
        || micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) != MICROS_ENDPOINT_OK
    ) {
        fprintf(stderr, "IPC model validator mismatch at step %zu\n", step);
        return false;
    }
    return true;
}

bool ipc_model_matches_production(
    struct ipc_model_test_context *fixture,
    const struct model_state *model,
    size_t step
)
{
    return (
        compare_acceptance_aggregates(fixture, model, step)
        && compare_acceptance_processes(fixture, model, step)
        && compare_acceptance_endpoints_and_events(fixture, model, step)
        && compare_acceptance_threads(fixture, model, step)
        && compare_acceptance_hart_and_ready_queues(fixture, model, step)
        && compare_acceptance_public_invariants(fixture, step)
    );
}

enum micros_ipc_error ipc_model_reference_execute_action(
    struct model_state *model,
    const struct model_action *action
)
{
    switch (action->operation) {
    case MODEL_OPERATION_SEND:
        return ipc_model_send(
            model,
            action->thread_slot,
            action->endpoint,
            action->message
        );
    case MODEL_OPERATION_RECEIVE:
        return ipc_model_receive(
            model,
            action->thread_slot,
            action->source,
            action->buffer
        );
    case MODEL_OPERATION_CALL:
        return ipc_model_call(
            model,
            action->thread_slot,
            action->endpoint,
            action->message
        );
    case MODEL_OPERATION_REPLY:
        return ipc_model_reply(
            model,
            action->thread_slot,
            action->value,
            action->message
        );
    case MODEL_OPERATION_REPLY_RECEIVE:
        return ipc_model_reply_receive(
            model,
            action->thread_slot,
            action->value,
            action->message,
            action->source,
            action->buffer
        );
    case MODEL_OPERATION_NOTIFY:
        return ipc_model_notify(
            model,
            action->thread_slot,
            action->endpoint,
            action->value
        );
    case MODEL_OPERATION_KERNEL_NOTIFY:
        return ipc_model_kernel_notify(
            model,
            action->endpoint,
            action->value
        );
    case MODEL_OPERATION_DRAIN:
        return ipc_model_drain_thread(model, action->thread_slot);
    case MODEL_OPERATION_HOLD:
        return ipc_model_hold_thread(model, action->thread_slot);
    case MODEL_OPERATION_CLOSE_REUSE:
        return ipc_model_close_reuse(model, action->process_slot);
    default:
        return MICROS_IPC_ERROR_ARGUMENT;
    }
}
