#include "ipc_model_test_internal.h"

const char *ipc_model_operation_name(enum model_operation operation)
{
    static const char *const names[MODEL_OPERATION_COUNT] = {
        "send",
        "receive",
        "call",
        "reply",
        "reply_receive",
        "notify",
        "kernel_notify",
        "drain",
        "hold",
        "close_reuse",
    };

    if ((size_t)operation >= MODEL_OPERATION_COUNT) {
        return "unknown";
    }
    return names[operation];
}

void ipc_model_record_trace(
    struct ipc_model_test_context *fixture,
    size_t step,
    const struct model_action *action,
    enum micros_ipc_error result
)
{
    size_t index = fixture->model_trace_count % MODEL_TRACE_COUNT;

    fixture->model_trace[index].step = step;
    fixture->model_trace[index].action = *action;
    fixture->model_trace[index].result = result;
    ++fixture->model_trace_count;
    fixture->model_trace_hash ^=
        ((uint64_t)step << 32)
        ^ ((uint64_t)action->operation << 56)
        ^ ((uint64_t)action->thread_slot << 16)
        ^ ((uint64_t)action->process_slot << 8)
        ^ action->endpoint
        ^ action->source
        ^ action->value
        ^ (uint64_t)result;
    fixture->model_trace_hash =
        (fixture->model_trace_hash << 7) | (fixture->model_trace_hash >> 57);
}

void ipc_model_dump_trace(struct ipc_model_test_context *fixture, uint32_t seed, size_t step, const char *detail)
{
    size_t available =
        fixture->model_trace_count < MODEL_TRACE_COUNT
            ? fixture->model_trace_count
            : MODEL_TRACE_COUNT;
    size_t first = fixture->model_trace_count - available;
    size_t index;

    fprintf(
        stderr,
        "IPC model seed=0x%08x step=%zu: %s\n",
        seed,
        step,
        detail
    );
    for (index = first; index < fixture->model_trace_count; ++index) {
        const struct model_trace_entry *entry =
            &fixture->model_trace[index % MODEL_TRACE_COUNT];

        fprintf(
            stderr,
            "  trace step=%zu op=%s thread=%zu process=%zu "
            "endpoint=0x%08x source=0x%08x "
            "value=0x%016llx result=%d\n",
            entry->step,
            ipc_model_operation_name(entry->action.operation),
            entry->action.thread_slot,
            entry->action.process_slot,
            entry->action.endpoint,
            entry->action.source,
            (unsigned long long)entry->action.value,
            (int)entry->result
        );
    }
}

void ipc_model_dump_state(struct ipc_model_test_context *fixture, const struct model_state *model)
{
    size_t index;

    fprintf(
        stderr,
        "  token model=0x%016llx actual=0x%016llx\n",
        (unsigned long long)model->last_reply_token,
        (unsigned long long)fixture->registry.last_reply_token
    );
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct model_endpoint *expected =
            &model->endpoints[index];
        const struct micros_endpoint_record *actual =
            &fixture->registry.endpoints[index];

        if (
            expected->state == MICROS_ENDPOINT_STATE_FREE
            && actual->state == MICROS_ENDPOINT_STATE_FREE
            && expected->pending_notification_sources == 0
            && actual->pending_notification_sources == 0
            && expected->pending_kernel_events == 0
            && actual->pending_kernel_events == 0
        ) {
            continue;
        }
        fprintf(
            stderr,
            "  endpoint=%zu model-state=%d actual-state=%d "
            "model-value=0x%08x actual-value=0x%08x "
            "model-sender=%u/%u actual-sender=%u/%u "
            "model-receiver=%u/%u actual-receiver=%u/%u "
            "model-pending=0x%016llx actual-pending=0x%016llx "
            "model-kernel=0x%016llx actual-kernel=0x%016llx\n",
            index,
            (int)expected->state,
            (int)actual->state,
            expected->value,
            actual->value,
            expected->sender_head.slot,
            expected->sender_tail.slot,
            actual->sender_head.slot,
            actual->sender_tail.slot,
            expected->receiver_head.slot,
            expected->receiver_tail.slot,
            actual->receiver_head.slot,
            actual->receiver_tail.slot,
            (unsigned long long)expected->pending_notification_sources,
            (unsigned long long)actual->pending_notification_sources,
            (unsigned long long)expected->pending_kernel_events,
            (unsigned long long)actual->pending_kernel_events
        );
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct model_thread *expected = &model->threads[index];
        const struct micros_thread *actual = &fixture->objects.threads[index];

        if (
            expected->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            && actual->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
        ) {
            continue;
        }
        fprintf(
            stderr,
            "  thread=%zu gen=%u/%u rts=0x%08x/0x%08x "
            "ready=%d/%d ready-next=%u:%u/%u:%u "
            "ipc-kind=%d/%d ipc-next=%u:%u/%u:%u "
            "token=0x%016llx/0x%016llx "
            "callee=0x%08x/0x%08x delivery=%d/%d result=%d/%d\n",
            index,
            expected->generation,
            actual->generation,
            expected->runtime_flags,
            actual->runtime_flags,
            expected->ready_linked,
            actual->ready_linked,
            expected->ready_next.slot,
            expected->ready_next.generation,
            actual->ready_next.slot,
            actual->ready_next.generation,
            (int)expected->ipc_queue_kind,
            (int)actual->ipc_queue_kind,
            expected->ipc_next.slot,
            expected->ipc_next.generation,
            actual->ipc_next.slot,
            actual->ipc_next.generation,
            (unsigned long long)expected->ipc_reply_token,
            (unsigned long long)actual->ipc_reply_token,
            expected->ipc_reply_callee,
            actual->ipc_reply_callee,
            expected->ipc_delivery_pending,
            actual->ipc_delivery_pending,
            (int)expected->ipc_staged_result,
            (int)actual->ipc_staged_result
        );
    }
}
