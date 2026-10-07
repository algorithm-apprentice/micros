#include "micros/ipc_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/scheduler_core.h"

enum {
    MODEL_STEPS = 8192,
    MODEL_TRACE_COUNT = 96,
    MODEL_PROCESS_COUNT = 7,
    MODEL_THREAD_COUNT = 10,
    MODEL_MESSAGE_COUNT = 64,
    MODEL_FULL_PROFILE = 1,
    MODEL_DENIED_PROFILE = 2,
    MODEL_SINGLETON_FIRST = 3,
    MODEL_SINGLETON_LAST = 5,
    MODEL_DENIED_PROCESS = 6,
    MODEL_PRIORITY = MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
};

static const size_t model_initial_thread_counts[MODEL_PROCESS_COUNT] = {
    2,
    2,
    2,
    1,
    1,
    1,
    1,
};

enum model_operation {
    MODEL_OPERATION_SEND = 0,
    MODEL_OPERATION_RECEIVE,
    MODEL_OPERATION_CALL,
    MODEL_OPERATION_REPLY,
    MODEL_OPERATION_REPLY_RECEIVE,
    MODEL_OPERATION_NOTIFY,
    MODEL_OPERATION_DRAIN,
    MODEL_OPERATION_HOLD,
    MODEL_OPERATION_CLOSE_REUSE,
    MODEL_OPERATION_COUNT,
};

struct model_process {
    enum micros_kernel_object_slot_state slot_state;
    uint32_t generation;
    size_t live_thread_count;
    micros_endpoint_t primary_endpoint;
    uint32_t privilege_profile;
    bool endpoint_lifecycle_consumed;
};

struct model_thread {
    enum micros_kernel_object_slot_state slot_state;
    uint32_t generation;
    struct micros_process_handle owner;
    bool context_attached;
    uintptr_t kernel_stack_bottom;
    uintptr_t kernel_stack_top;
    uint32_t runtime_flags;
    bool scheduler_assigned;
    bool scheduler_preemptible;
    uint8_t scheduler_priority;
    struct micros_hart_handle scheduler_hart;
    uint64_t quantum_counter_ticks;
    uint64_t remaining_counter_ticks;
    bool ready_linked;
    struct micros_thread_handle ready_next;
    enum micros_ipc_queue_kind ipc_queue_kind;
    struct micros_thread_handle ipc_next;
    struct micros_ipc_message ipc_outbound_message;
    micros_endpoint_t ipc_send_destination;
    micros_endpoint_t ipc_receive_source;
    uintptr_t ipc_receive_buffer;
    bool ipc_delivery_pending;
    struct micros_ipc_message ipc_inbound_message;
    enum micros_ipc_error ipc_staged_result;
    uint64_t ipc_reply_token;
    micros_endpoint_t ipc_reply_callee;
};

struct model_endpoint {
    enum micros_endpoint_state state;
    struct micros_process_handle owner;
    micros_endpoint_t value;
    struct micros_thread_handle sender_head;
    struct micros_thread_handle sender_tail;
    struct micros_thread_handle receiver_head;
    struct micros_thread_handle receiver_tail;
    uint64_t pending_notification_sources;
    uint64_t pending_events[MICROS_PROCESS_CAPACITY];
};

struct model_hart {
    struct micros_thread_handle current_thread;
    struct micros_thread_handle
        ready_head[MICROS_SCHEDULER_PRIORITY_COUNT];
    struct micros_thread_handle
        ready_tail[MICROS_SCHEDULER_PRIORITY_COUNT];
    enum micros_scheduler_accounting_owner accounting_owner;
    uint64_t accounting_started_at;
    struct micros_thread_handle accounted_thread;
    uint64_t kernel_counter_ticks;
    uint64_t idle_counter_ticks;
    bool reschedule_pending;
};

struct model_state {
    size_t live_process_count;
    size_t live_thread_count;
    uint64_t last_reply_token;
    struct micros_hart_handle hart_handle;
    struct model_hart hart;
    struct model_process processes[MICROS_PROCESS_CAPACITY];
    struct model_thread threads[MICROS_THREAD_CAPACITY];
    struct model_endpoint endpoints[MICROS_PROCESS_CAPACITY];
};

struct model_action {
    enum model_operation operation;
    size_t thread_slot;
    size_t process_slot;
    micros_endpoint_t endpoint;
    micros_endpoint_t source;
    uint64_t value;
    uintptr_t buffer;
    struct micros_ipc_message *message;
};

struct model_trace_entry {
    size_t step;
    struct model_action action;
    enum micros_ipc_error result;
};

struct model_coverage {
    size_t operation_count[MODEL_OPERATION_COUNT];
    size_t operation_success[MODEL_OPERATION_COUNT];
    size_t deadlock;
    size_t unauthorized;
    size_t argument;
};

struct model_transition {
    size_t thread_slot;
    uint32_t clear_flags;
    uint32_t set_flags;
};

static struct micros_endpoint_registry registry;
static struct micros_kernel_objects objects;
static struct micros_ipc_message messages[MODEL_MESSAGE_COUNT];
static struct model_trace_entry model_trace[MODEL_TRACE_COUNT];
static size_t model_trace_count;
static uint64_t model_trace_hash;

bool micros_ipc_model_test_run(void);

static struct micros_thread_handle null_thread_handle(void)
{
    return (struct micros_thread_handle){0, 0};
}

static bool thread_handle_is_zero(struct micros_thread_handle handle)
{
    return handle.slot == 0 && handle.generation == 0;
}

static bool thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

static bool process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

static struct micros_thread_handle model_thread_handle(
    const struct model_state *model,
    size_t slot
)
{
    return (struct micros_thread_handle){
        .slot = (uint16_t)slot,
        .generation = model->threads[slot].generation,
    };
}

static struct micros_user_context context_pattern(uint64_t base)
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

static void fill_message(
    struct micros_ipc_message *message,
    size_t step,
    bool kernel_type
)
{
    size_t index;

    memset(message, 0, sizeof(*message));
    message->source = MICROS_ENDPOINT_ANY;
    message->type =
        (uint32_t)(UINT32_C(0x1000) + (uint32_t)step);
    if (kernel_type) {
        message->type |= MICROS_IPC_TYPE_KERNEL_MASK;
    }
    message->reply_token =
        UINT64_C(0xa5a5000000000000) | (uint64_t)step;
    for (index = 0; index < sizeof(message->payload); ++index) {
        message->payload[index] =
            (uint8_t)(step + index * 3);
    }
}

static struct micros_ipc_message *message_for_step(
    size_t step,
    bool kernel_type
)
{
    struct micros_ipc_message *message =
        &messages[step % MODEL_MESSAGE_COUNT];

    fill_message(message, step, kernel_type);
    return message;
}

static uintptr_t receive_buffer_for_step(size_t step, size_t thread_slot)
{
    return (
        UINT64_C(0x40000000)
        + (uint64_t)thread_slot * UINT64_C(0x00100000)
        + (uint64_t)(step & 0xffff) * UINT64_C(0x80)
    );
}

static uint32_t model_random_next(uint32_t *state)
{
    uint32_t value = *state;

    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static const char *model_operation_name(enum model_operation operation)
{
    static const char *const names[MODEL_OPERATION_COUNT] = {
        "send",
        "receive",
        "call",
        "reply",
        "reply_receive",
        "notify",
        "drain",
        "hold",
        "close_reuse",
    };

    if ((size_t)operation >= MODEL_OPERATION_COUNT) {
        return "unknown";
    }
    return names[operation];
}

static void record_trace(
    size_t step,
    const struct model_action *action,
    enum micros_ipc_error result
)
{
    size_t index = model_trace_count % MODEL_TRACE_COUNT;

    model_trace[index].step = step;
    model_trace[index].action = *action;
    model_trace[index].result = result;
    ++model_trace_count;
    model_trace_hash ^=
        ((uint64_t)step << 32)
        ^ ((uint64_t)action->operation << 56)
        ^ ((uint64_t)action->thread_slot << 16)
        ^ ((uint64_t)action->process_slot << 8)
        ^ action->endpoint
        ^ action->source
        ^ action->value
        ^ (uint64_t)result;
    model_trace_hash =
        (model_trace_hash << 7) | (model_trace_hash >> 57);
}

static void dump_trace(uint32_t seed, size_t step, const char *detail)
{
    size_t available =
        model_trace_count < MODEL_TRACE_COUNT
            ? model_trace_count
            : MODEL_TRACE_COUNT;
    size_t first = model_trace_count - available;
    size_t index;

    fprintf(
        stderr,
        "IPC model seed=0x%08x step=%zu: %s\n",
        seed,
        step,
        detail
    );
    for (index = first; index < model_trace_count; ++index) {
        const struct model_trace_entry *entry =
            &model_trace[index % MODEL_TRACE_COUNT];

        fprintf(
            stderr,
            "  trace step=%zu op=%s thread=%zu process=%zu "
            "endpoint=0x%08x source=0x%08x "
            "value=0x%016llx result=%d\n",
            entry->step,
            model_operation_name(entry->action.operation),
            entry->action.thread_slot,
            entry->action.process_slot,
            entry->action.endpoint,
            entry->action.source,
            (unsigned long long)entry->action.value,
            (int)entry->result
        );
    }
}

static void dump_state(const struct model_state *model)
{
    size_t index;

    fprintf(
        stderr,
        "  token model=0x%016llx actual=0x%016llx\n",
        (unsigned long long)model->last_reply_token,
        (unsigned long long)registry.last_reply_token
    );
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct model_endpoint *expected =
            &model->endpoints[index];
        const struct micros_endpoint_record *actual =
            &registry.endpoints[index];

        if (
            expected->state == MICROS_ENDPOINT_STATE_FREE
            && actual->state == MICROS_ENDPOINT_STATE_FREE
            && expected->pending_notification_sources == 0
            && actual->pending_notification_sources == 0
        ) {
            continue;
        }
        fprintf(
            stderr,
            "  endpoint=%zu model-state=%d actual-state=%d "
            "model-value=0x%08x actual-value=0x%08x "
            "model-sender=%u/%u actual-sender=%u/%u "
            "model-receiver=%u/%u actual-receiver=%u/%u "
            "model-pending=0x%016llx actual-pending=0x%016llx\n",
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
            (unsigned long long)actual->pending_notification_sources
        );
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct model_thread *expected = &model->threads[index];
        const struct micros_thread *actual = &objects.threads[index];

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

static bool messages_equal(
    const struct micros_ipc_message *left,
    const struct micros_ipc_message *right
)
{
    return memcmp(left, right, sizeof(*left)) == 0;
}

static bool model_matches_production(
    const struct model_state *model,
    size_t step
)
{
    size_t index;
    size_t other;

    if (
        model->live_process_count != objects.live_process_count
        || model->live_thread_count != objects.live_thread_count
        || model->last_reply_token != registry.last_reply_token
    ) {
        fprintf(stderr, "IPC model aggregate mismatch at step %zu\n", step);
        return false;
    }
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct model_process *expected =
            &model->processes[index];
        const struct micros_process *actual =
            &objects.processes[index];

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
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct model_endpoint *expected =
            &model->endpoints[index];
        const struct micros_endpoint_record *actual =
            &registry.endpoints[index];

        if (
            expected->state != actual->state
            || !process_handles_equal(expected->owner, actual->owner)
            || expected->value != actual->value
            || !thread_handles_equal(
                expected->sender_head,
                actual->sender_head
            )
            || !thread_handles_equal(
                expected->sender_tail,
                actual->sender_tail
            )
            || !thread_handles_equal(
                expected->receiver_head,
                actual->receiver_head
            )
            || !thread_handles_equal(
                expected->receiver_tail,
                actual->receiver_tail
            )
            || expected->pending_notification_sources
                != actual->pending_notification_sources
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
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct model_thread *expected = &model->threads[index];
        const struct micros_thread *actual = &objects.threads[index];

        if (
            expected->slot_state != actual->slot_state
            || expected->generation != actual->generation
            || !process_handles_equal(expected->owner, actual->owner)
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
            || !thread_handles_equal(
                expected->ready_next,
                actual->ready_next
            )
            || expected->ipc_queue_kind != actual->ipc_queue_kind
            || !thread_handles_equal(
                expected->ipc_next,
                actual->ipc_next
            )
            || !messages_equal(
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
            || !messages_equal(
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
    if (
        !thread_handles_equal(
            model->hart.current_thread,
            objects.harts[model->hart_handle.slot].current_thread
        )
        || model->hart.accounting_owner
            != objects.harts[model->hart_handle.slot].accounting_owner
        || model->hart.accounting_started_at
            != objects.harts[model->hart_handle.slot].accounting_started_at
        || !thread_handles_equal(
            model->hart.accounted_thread,
            objects.harts[model->hart_handle.slot].accounted_thread
        )
        || model->hart.kernel_counter_ticks
            != objects.harts[model->hart_handle.slot].kernel_counter_ticks
        || model->hart.idle_counter_ticks
            != objects.harts[model->hart_handle.slot].idle_counter_ticks
        || model->hart.reschedule_pending
            != objects.harts[model->hart_handle.slot].reschedule_pending
    ) {
        fprintf(stderr, "IPC model hart mismatch at step %zu\n", step);
        return false;
    }
    for (index = 0; index < MICROS_SCHEDULER_PRIORITY_COUNT; ++index) {
        if (
            !thread_handles_equal(
                model->hart.ready_head[index],
                objects.harts[
                    model->hart_handle.slot
                ].ready_head[index]
            )
            || !thread_handles_equal(
                model->hart.ready_tail[index],
                objects.harts[
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
    if (
        micros_kernel_objects_validate(&objects)
            != MICROS_KERNEL_OBJECT_OK
        || micros_scheduler_core_validate(&objects)
            != MICROS_KERNEL_OBJECT_OK
        || micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) != MICROS_ENDPOINT_OK
    ) {
        fprintf(stderr, "IPC model validator mismatch at step %zu\n", step);
        return false;
    }
    return true;
}

static bool setup_model(struct model_state *model)
{
    static const struct micros_privilege_profile profiles[] = {
        {
            .id = MODEL_FULL_PROFILE,
            .name = "MODEL_FULL",
            .operations = MICROS_PRIVILEGE_OPERATION_DEFINED_MASK,
            .call_targets = UINT32_C(1) << MODEL_FULL_PROFILE,
            .send_targets = UINT32_C(1) << MODEL_FULL_PROFILE,
            .notify_targets = UINT32_C(1) << MODEL_FULL_PROFILE,
        },
        {
            .id = MODEL_DENIED_PROFILE,
            .name = "MODEL_DENIED",
            .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
        },
    };
    struct micros_hart_handle hart;
    size_t process_slot;
    size_t next_thread_slot = 0;

    memset(model, 0, sizeof(*model));
    memset(&registry, 0, sizeof(registry));
    memset(&objects, 0, sizeof(objects));
    memset(messages, 0, sizeof(messages));
    memset(model_trace, 0, sizeof(model_trace));
    model_trace_count = 0;
    model_trace_hash = UINT64_C(0x30accee57ace0001);
    for (
        process_slot = 0;
        process_slot < MICROS_PROCESS_CAPACITY;
        ++process_slot
    ) {
        model->processes[process_slot].primary_endpoint =
            MICROS_PROCESS_ENDPOINT_NONE;
    }
    if (
        micros_endpoint_registry_initialize(
            &registry,
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) != MICROS_ENDPOINT_OK
        || micros_kernel_objects_initialize(&objects, 2, 1)
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_register(&objects, 0, &hart)
            != MICROS_KERNEL_OBJECT_OK
        || hart.slot != 0
        || hart.generation != 1
        || micros_hart_install_trap_stacks(
            &objects,
            hart,
            UINT64_C(0x01000000),
            UINT64_C(0x01004000),
            UINT64_C(0x02000000),
            UINT64_C(0x02001000)
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    model->hart_handle = hart;
    for (process_slot = 0; process_slot < MODEL_PROCESS_COUNT; ++process_slot) {
        struct micros_process_handle process;
        micros_endpoint_t endpoint;
        uint8_t profile_id =
            process_slot == MODEL_DENIED_PROCESS
                ? MODEL_DENIED_PROFILE
                : MODEL_FULL_PROFILE;
        size_t local_index;

        if (
            micros_process_create(&objects, &process)
                != MICROS_KERNEL_OBJECT_OK
            || process.slot != process_slot
            || process.generation != 1
            || micros_endpoint_reserve(
                &registry,
                &objects,
                process,
                &endpoint
            ) != MICROS_ENDPOINT_OK
        ) {
            return false;
        }
        model->processes[process_slot].slot_state =
            MICROS_KERNEL_OBJECT_SLOT_LIVE;
        model->processes[process_slot].generation = 1;
        model->processes[process_slot].primary_endpoint = endpoint;
        model->processes[process_slot].privilege_profile = profile_id;
        model->processes[process_slot].endpoint_lifecycle_consumed = true;
        model->endpoints[process_slot].state =
            MICROS_ENDPOINT_STATE_RESERVED;
        model->endpoints[process_slot].owner = process;
        model->endpoints[process_slot].value = endpoint;
        for (
            local_index = 0;
            local_index < model_initial_thread_counts[process_slot];
            ++local_index
        ) {
            struct micros_thread_handle thread;

            if (
                next_thread_slot >= MODEL_THREAD_COUNT
                || micros_thread_create(
                    &objects,
                    process,
                    &thread
                ) != MICROS_KERNEL_OBJECT_OK
                || thread.slot != next_thread_slot
                || thread.generation != 1
            ) {
                return false;
            }
            model->threads[next_thread_slot].slot_state =
                MICROS_KERNEL_OBJECT_SLOT_LIVE;
            model->threads[next_thread_slot].generation = 1;
            model->threads[next_thread_slot].owner = process;
            model->threads[next_thread_slot].runtime_flags =
                MICROS_THREAD_RTS_INACTIVE;
            model->threads[next_thread_slot].ipc_staged_result =
                MICROS_IPC_OK;
            ++next_thread_slot;
            ++model->processes[process_slot].live_thread_count;
            ++model->live_thread_count;
        }
        if (
            micros_endpoint_install_profile(
                &registry,
                &objects,
                process,
                profile_id
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_activate(
                &registry,
                &objects,
                endpoint
            ) != MICROS_ENDPOINT_OK
        ) {
            return false;
        }
        model->endpoints[process_slot].state =
            MICROS_ENDPOINT_STATE_ACTIVE;
        ++model->live_process_count;
    }
    if (next_thread_slot != MODEL_THREAD_COUNT) {
        return false;
    }
    for (next_thread_slot = 0; next_thread_slot < MODEL_THREAD_COUNT;
         ++next_thread_slot) {
        struct micros_thread_handle thread =
            model_thread_handle(model, next_thread_slot);
        struct micros_user_context context =
            context_pattern(UINT64_C(0x1000) + next_thread_slot * 0x100);
        uintptr_t stack_bottom =
            UINT64_C(0x10000000)
            + next_thread_slot * UINT64_C(0x00008000);

        if (
            micros_thread_attach_execution_context(
                &objects,
                thread,
                stack_bottom,
                stack_bottom + MICROS_THREAD_KERNEL_STACK_SIZE,
                &context
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_scheduler_admit(
                &objects,
                hart,
                thread,
                MODEL_PRIORITY,
                100,
                true
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_scheduler_hold(&objects, thread)
                != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
        model->threads[next_thread_slot].context_attached = true;
        model->threads[next_thread_slot].kernel_stack_bottom =
            stack_bottom;
        model->threads[next_thread_slot].kernel_stack_top =
            stack_bottom + MICROS_THREAD_KERNEL_STACK_SIZE;
        model->threads[next_thread_slot].scheduler_assigned = true;
        model->threads[next_thread_slot].scheduler_preemptible = true;
        model->threads[next_thread_slot].scheduler_priority =
            MODEL_PRIORITY;
        model->threads[next_thread_slot].scheduler_hart = hart;
        model->threads[next_thread_slot].quantum_counter_ticks = 100;
        model->threads[next_thread_slot].remaining_counter_ticks = 100;
    }
    return model_matches_production(model, 0);
}

static bool model_thread_is_held_clear(
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
        && thread_handle_is_zero(thread->ipc_next)
        && messages_equal(
            &thread->ipc_outbound_message,
            &zero_message
        )
        && thread->ipc_send_destination == 0
        && thread->ipc_receive_source == 0
        && thread->ipc_receive_buffer == 0
        && !thread->ipc_delivery_pending
        && messages_equal(
            &thread->ipc_inbound_message,
            &zero_message
        )
        && thread->ipc_staged_result == MICROS_IPC_OK
        && thread->ipc_reply_token == 0
        && thread->ipc_reply_callee == 0
    );
}

static void model_ready_append(
    struct model_state *model,
    size_t thread_slot
)
{
    struct model_thread *thread = &model->threads[thread_slot];
    uint8_t priority = thread->scheduler_priority;
    struct micros_thread_handle handle =
        model_thread_handle(model, thread_slot);
    struct micros_thread_handle tail =
        model->hart.ready_tail[priority];

    thread->ready_linked = true;
    thread->ready_next = null_thread_handle();
    if (thread_handle_is_zero(tail)) {
        model->hart.ready_head[priority] = handle;
    } else {
        model->threads[tail.slot].ready_next = handle;
    }
    model->hart.ready_tail[priority] = handle;
}

static bool model_ready_remove(
    struct model_state *model,
    size_t thread_slot
)
{
    struct model_thread *thread = &model->threads[thread_slot];
    uint8_t priority = thread->scheduler_priority;
    struct micros_thread_handle current =
        model->hart.ready_head[priority];
    struct micros_thread_handle previous = null_thread_handle();
    struct micros_thread_handle target =
        model_thread_handle(model, thread_slot);
    size_t steps;

    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        struct micros_thread_handle next;

        if (thread_handle_is_zero(current)) {
            return false;
        }
        next = model->threads[current.slot].ready_next;
        if (thread_handles_equal(current, target)) {
            if (thread_handle_is_zero(previous)) {
                model->hart.ready_head[priority] = next;
            } else {
                model->threads[previous.slot].ready_next = next;
            }
            if (
                thread_handles_equal(
                    model->hart.ready_tail[priority],
                    target
                )
            ) {
                model->hart.ready_tail[priority] = previous;
            }
            thread->ready_linked = false;
            thread->ready_next = null_thread_handle();
            return true;
        }
        previous = current;
        current = next;
    }
    return false;
}

static enum micros_ipc_error model_apply_transitions(
    struct model_state *model,
    const struct model_transition *transitions,
    size_t transition_count
)
{
    size_t index;
    size_t other;

    for (index = 0; index < transition_count; ++index) {
        const struct model_transition *transition =
            &transitions[index];
        const struct model_thread *thread;
        uint32_t resulting_flags;

        if (
            transition->thread_slot >= MICROS_THREAD_CAPACITY
            || transition->clear_flags == 0
        ) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
        for (other = 0; other < index; ++other) {
            if (
                transitions[other].thread_slot
                    == transition->thread_slot
            ) {
                return MICROS_IPC_ERROR_INVARIANT;
            }
        }
        thread = &model->threads[transition->thread_slot];
        if (
            thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || !thread->scheduler_assigned
            || (
                thread->runtime_flags
                & transition->clear_flags
            ) != transition->clear_flags
            || (
                thread->runtime_flags
                & transition->set_flags
            ) != 0
        ) {
            return MICROS_IPC_ERROR_STATE;
        }
        resulting_flags =
            (
                thread->runtime_flags
                & ~transition->clear_flags
            )
            | transition->set_flags;
        if (
            resulting_flags == 0
            && (
                thread->ready_linked
                || !thread_handle_is_zero(thread->ready_next)
                || thread_handles_equal(
                    model->hart.current_thread,
                    model_thread_handle(
                        model,
                        transition->thread_slot
                    )
                )
            )
        ) {
            return MICROS_IPC_ERROR_STATE;
        }
    }
    for (index = 0; index < transition_count; ++index) {
        const struct model_transition *transition =
            &transitions[index];
        struct model_thread *thread =
            &model->threads[transition->thread_slot];

        thread->runtime_flags =
            (
                thread->runtime_flags
                & ~transition->clear_flags
            )
            | transition->set_flags;
        if (thread->runtime_flags == 0) {
            model_ready_append(model, transition->thread_slot);
        }
    }
    return MICROS_IPC_OK;
}

static enum micros_ipc_error model_hold_thread(
    struct model_state *model,
    size_t thread_slot
)
{
    struct model_thread *thread;

    if (thread_slot >= MICROS_THREAD_CAPACITY) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    thread = &model->threads[thread_slot];
    if (
        thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || !thread->scheduler_assigned
        || thread->runtime_flags != 0
        || !thread->ready_linked
    ) {
        return MICROS_IPC_ERROR_STATE;
    }
    if (!model_ready_remove(model, thread_slot)) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    thread->runtime_flags = MICROS_THREAD_RTS_INACTIVE;
    return MICROS_IPC_OK;
}

static void clear_model_message(struct micros_ipc_message *message)
{
    memset(message, 0, sizeof(*message));
}

static void model_clear_delivered_sender(struct model_thread *sender)
{
    bool completes_send = sender->ipc_reply_token == 0;

    sender->ipc_queue_kind = MICROS_IPC_QUEUE_NONE;
    sender->ipc_next = null_thread_handle();
    clear_model_message(&sender->ipc_outbound_message);
    sender->ipc_send_destination = 0;
    if (completes_send) {
        sender->ipc_receive_buffer = 0;
        sender->ipc_delivery_pending = true;
        clear_model_message(&sender->ipc_inbound_message);
        sender->ipc_staged_result = MICROS_IPC_OK;
    }
}

static void model_clear_ipc_state(struct model_thread *thread)
{
    thread->ipc_queue_kind = MICROS_IPC_QUEUE_NONE;
    thread->ipc_next = null_thread_handle();
    clear_model_message(&thread->ipc_outbound_message);
    thread->ipc_send_destination = 0;
    thread->ipc_receive_source = 0;
    thread->ipc_receive_buffer = 0;
    thread->ipc_delivery_pending = false;
    clear_model_message(&thread->ipc_inbound_message);
    thread->ipc_staged_result = MICROS_IPC_OK;
    thread->ipc_reply_token = 0;
    thread->ipc_reply_callee = 0;
}

static void model_stage_delivery(
    struct model_thread *receiver,
    uintptr_t receive_buffer,
    const struct micros_ipc_message *message
)
{
    receiver->ipc_receive_buffer = receive_buffer;
    receiver->ipc_delivery_pending = true;
    receiver->ipc_inbound_message = *message;
    receiver->ipc_staged_result = MICROS_IPC_OK;
}

static enum micros_ipc_error model_drain_thread(
    struct model_state *model,
    size_t thread_slot
)
{
    struct model_thread *thread;

    if (thread_slot >= MICROS_THREAD_CAPACITY) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    thread = &model->threads[thread_slot];
    if (
        thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || !thread->ipc_delivery_pending
    ) {
        return MICROS_IPC_ERROR_STATE;
    }
    thread->ipc_receive_buffer = 0;
    thread->ipc_delivery_pending = false;
    clear_model_message(&thread->ipc_inbound_message);
    thread->ipc_staged_result = MICROS_IPC_OK;
    return MICROS_IPC_OK;
}

static bool model_resolve_active_endpoint(
    const struct model_state *model,
    micros_endpoint_t endpoint,
    size_t *slot
)
{
    uint32_t generation;
    size_t decoded_slot;
    const struct model_endpoint *record;
    const struct model_process *process;

    if (
        endpoint == MICROS_ENDPOINT_NONE
        || endpoint == MICROS_ENDPOINT_ANY
    ) {
        return false;
    }
    decoded_slot =
        endpoint & ((UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) - 1);
    generation = endpoint >> MICROS_ENDPOINT_SLOT_BITS;
    if (
        generation == 0
        || decoded_slot >= MICROS_PROCESS_CAPACITY
    ) {
        return false;
    }
    record = &model->endpoints[decoded_slot];
    process = &model->processes[decoded_slot];
    if (
        record->state != MICROS_ENDPOINT_STATE_ACTIVE
        || record->value != endpoint
        || record->owner.slot != decoded_slot
        || record->owner.generation != generation
        || process->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || process->generation != generation
        || process->primary_endpoint != endpoint
    ) {
        return false;
    }
    *slot = decoded_slot;
    return true;
}

static enum micros_ipc_error model_resolve_operation_thread(
    const struct model_state *model,
    size_t thread_slot,
    micros_endpoint_t *source_endpoint
)
{
    const struct model_thread *thread;
    const struct model_process *process;

    if (thread_slot >= MICROS_THREAD_CAPACITY) {
        return MICROS_IPC_ERROR_DEAD_ENDPOINT;
    }
    thread = &model->threads[thread_slot];
    if (!model_thread_is_held_clear(thread)) {
        return thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            ? MICROS_IPC_ERROR_STATE
            : MICROS_IPC_ERROR_DEAD_ENDPOINT;
    }
    process = &model->processes[thread->owner.slot];
    if (
        process->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || process->generation != thread->owner.generation
    ) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    *source_endpoint = process->primary_endpoint;
    if (
        *source_endpoint == MICROS_ENDPOINT_NONE
        || *source_endpoint == MICROS_ENDPOINT_ANY
    ) {
        return MICROS_IPC_ERROR_STATE;
    }
    return MICROS_IPC_OK;
}

static uint32_t model_profile_operations(uint32_t profile)
{
    if (profile == MODEL_FULL_PROFILE) {
        return MICROS_PRIVILEGE_OPERATION_DEFINED_MASK;
    }
    if (profile == MODEL_DENIED_PROFILE) {
        return MICROS_PRIVILEGE_OPERATION_RECEIVE;
    }
    return 0;
}

static enum micros_ipc_error model_authorize_operation(
    const struct model_state *model,
    micros_endpoint_t source,
    uint32_t operation
)
{
    size_t source_slot;
    uint32_t profile;

    if (!model_resolve_active_endpoint(model, source, &source_slot)) {
        return MICROS_IPC_ERROR_STATE;
    }
    profile = model->processes[source_slot].privilege_profile;
    return (
        model_profile_operations(profile) & operation
    ) != 0
        ? MICROS_IPC_OK
        : MICROS_IPC_ERROR_UNAUTHORIZED;
}

static enum micros_ipc_error model_authorize_target(
    const struct model_state *model,
    micros_endpoint_t source,
    uint32_t operation,
    micros_endpoint_t destination
)
{
    size_t source_slot;
    size_t destination_slot;
    uint32_t source_profile;
    uint32_t destination_profile;

    if (!model_resolve_active_endpoint(model, source, &source_slot)) {
        return MICROS_IPC_ERROR_STATE;
    }
    if (!model_resolve_active_endpoint(
        model,
        destination,
        &destination_slot
    )) {
        return MICROS_IPC_ERROR_DEAD_ENDPOINT;
    }
    source_profile =
        model->processes[source_slot].privilege_profile;
    destination_profile =
        model->processes[destination_slot].privilege_profile;
    if (
        (
            model_profile_operations(source_profile)
            & operation
        ) == 0
    ) {
        return MICROS_IPC_ERROR_UNAUTHORIZED;
    }
    if (
        source_profile != MODEL_FULL_PROFILE
        || destination_profile != MODEL_FULL_PROFILE
    ) {
        return MICROS_IPC_ERROR_UNAUTHORIZED;
    }
    return MICROS_IPC_OK;
}

static void model_canonicalize_message(
    struct micros_ipc_message *destination,
    const struct micros_ipc_message *source,
    micros_endpoint_t source_endpoint,
    uint64_t reply_token
)
{
    *destination = *source;
    destination->source = source_endpoint;
    destination->reply_token = reply_token;
}

static void model_canonicalize_notification(
    struct micros_ipc_message *message,
    micros_endpoint_t source,
    uint64_t event_mask
)
{
    size_t index;

    clear_model_message(message);
    message->source = source;
    message->type = MICROS_IPC_TYPE_KERNEL_NOTIFICATION;
    for (index = 0; index < sizeof(event_mask); ++index) {
        message->payload[index] =
            (uint8_t)(event_mask >> (index * 8));
    }
}

static void model_ipc_append(
    struct model_state *model,
    struct micros_thread_handle *head,
    struct micros_thread_handle *tail,
    size_t thread_slot
)
{
    struct micros_thread_handle handle =
        model_thread_handle(model, thread_slot);

    model->threads[thread_slot].ipc_next = null_thread_handle();
    if (thread_handle_is_zero(*head)) {
        *head = handle;
    } else {
        model->threads[tail->slot].ipc_next = handle;
    }
    *tail = handle;
}

static void model_ipc_unlink(
    struct model_state *model,
    struct micros_thread_handle *head,
    struct micros_thread_handle *tail,
    struct micros_thread_handle previous,
    size_t thread_slot
)
{
    struct micros_thread_handle handle =
        model_thread_handle(model, thread_slot);
    struct micros_thread_handle next =
        model->threads[thread_slot].ipc_next;

    if (thread_handle_is_zero(previous)) {
        *head = next;
    } else {
        model->threads[previous.slot].ipc_next = next;
    }
    if (thread_handles_equal(*tail, handle)) {
        *tail = previous;
    }
}

static bool model_find_receiver(
    const struct model_state *model,
    size_t endpoint_slot,
    micros_endpoint_t source,
    struct micros_thread_handle *previous,
    size_t *receiver_slot
)
{
    struct micros_thread_handle current =
        model->endpoints[endpoint_slot].receiver_head;
    struct micros_thread_handle prior = null_thread_handle();
    size_t steps;

    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        const struct model_thread *receiver;

        if (thread_handle_is_zero(current)) {
            return false;
        }
        receiver = &model->threads[current.slot];
        if (
            receiver->ipc_receive_source == MICROS_ENDPOINT_ANY
            || receiver->ipc_receive_source == source
        ) {
            *previous = prior;
            *receiver_slot = current.slot;
            return true;
        }
        prior = current;
        current = receiver->ipc_next;
    }
    return false;
}

static bool model_find_sender(
    const struct model_state *model,
    size_t endpoint_slot,
    micros_endpoint_t source,
    struct micros_thread_handle *previous,
    size_t *sender_slot
)
{
    struct micros_thread_handle current =
        model->endpoints[endpoint_slot].sender_head;
    struct micros_thread_handle prior = null_thread_handle();
    size_t steps;

    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        const struct model_thread *sender;

        if (thread_handle_is_zero(current)) {
            return false;
        }
        sender = &model->threads[current.slot];
        if (
            source == MICROS_ENDPOINT_ANY
            || sender->ipc_outbound_message.source == source
        ) {
            *previous = prior;
            *sender_slot = current.slot;
            return true;
        }
        prior = current;
        current = sender->ipc_next;
    }
    return false;
}

static bool model_find_pending_notification(
    const struct model_state *model,
    size_t destination_slot,
    micros_endpoint_t source,
    size_t *source_slot,
    uint64_t *event_mask
)
{
    const struct model_endpoint *destination =
        &model->endpoints[destination_slot];
    size_t resolved_slot;
    size_t index;

    if (source != MICROS_ENDPOINT_ANY) {
        if (
            !model_resolve_active_endpoint(
                model,
                source,
                &resolved_slot
            )
            || (
                destination->pending_notification_sources
                & (UINT64_C(1) << resolved_slot)
            ) == 0
        ) {
            return false;
        }
        *source_slot = resolved_slot;
        *event_mask = destination->pending_events[resolved_slot];
        return true;
    }
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        if (
            (
                destination->pending_notification_sources
                & (UINT64_C(1) << index)
            ) != 0
        ) {
            *source_slot = index;
            *event_mask = destination->pending_events[index];
            return true;
        }
    }
    return false;
}

static enum micros_ipc_error model_sole_live_thread(
    const struct model_state *model,
    size_t process_slot,
    size_t *thread_slot
)
{
    const struct model_process *process =
        &model->processes[process_slot];
    size_t matched = MICROS_THREAD_CAPACITY;
    size_t index;

    if (
        process->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || process->live_thread_count != 1
    ) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct model_thread *thread = &model->threads[index];

        if (
            thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && thread->owner.slot == process_slot
            && thread->owner.generation == process->generation
        ) {
            if (matched != MICROS_THREAD_CAPACITY) {
                return MICROS_IPC_ERROR_INVARIANT;
            }
            matched = index;
        }
    }
    if (matched == MICROS_THREAD_CAPACITY) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    *thread_slot = matched;
    return MICROS_IPC_OK;
}

static enum micros_ipc_error model_deadlock_preflight(
    const struct model_state *model,
    size_t candidate_slot,
    micros_endpoint_t dependency,
    size_t cleared_slot
)
{
    struct micros_thread_handle visited[MICROS_THREAD_CAPACITY];
    size_t visited_count = 0;
    size_t steps;

    if (dependency == MICROS_ENDPOINT_ANY) {
        return MICROS_IPC_OK;
    }
    if (dependency == MICROS_ENDPOINT_NONE) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        size_t process_slot;
        size_t thread_slot;
        const struct model_thread *thread;
        micros_endpoint_t next = MICROS_ENDPOINT_NONE;
        bool has_dependency = true;
        size_t index;
        enum micros_ipc_error error;

        if (
            !model_resolve_active_endpoint(
                model,
                dependency,
                &process_slot
            )
        ) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
        error = model_sole_live_thread(
            model,
            process_slot,
            &thread_slot
        );
        if (error != MICROS_IPC_OK) {
            return error;
        }
        if (thread_slot == candidate_slot) {
            return MICROS_IPC_ERROR_DEADLOCK;
        }
        for (index = 0; index < visited_count; ++index) {
            if (
                visited[index].slot == thread_slot
                && visited[index].generation
                    == model->threads[thread_slot].generation
            ) {
                return MICROS_IPC_ERROR_INVARIANT;
            }
        }
        visited[visited_count] =
            model_thread_handle(model, thread_slot);
        ++visited_count;
        if (thread_slot == cleared_slot) {
            has_dependency = false;
        } else {
            thread = &model->threads[thread_slot];
            if (
                (
                    thread->runtime_flags
                    & MICROS_THREAD_RTS_IPC_SEND
                ) != 0
            ) {
                next = thread->ipc_send_destination;
            } else if (
                (
                    thread->runtime_flags
                    & MICROS_THREAD_RTS_IPC_REPLY
                ) != 0
            ) {
                next = thread->ipc_reply_callee;
            } else if (
                (
                    thread->runtime_flags
                    & MICROS_THREAD_RTS_IPC_RECEIVE
                ) != 0
            ) {
                if (
                    thread->ipc_receive_source
                    == MICROS_ENDPOINT_ANY
                ) {
                    has_dependency = false;
                } else {
                    next = thread->ipc_receive_source;
                }
            } else {
                has_dependency = false;
            }
        }
        if (!has_dependency) {
            return MICROS_IPC_OK;
        }
        if (
            next == MICROS_ENDPOINT_NONE
            || next == MICROS_ENDPOINT_ANY
        ) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
        dependency = next;
    }
    return MICROS_IPC_ERROR_INVARIANT;
}

static enum micros_ipc_error model_send(
    struct model_state *model,
    size_t sender_slot,
    micros_endpoint_t destination_endpoint,
    const struct micros_ipc_message *message
)
{
    struct model_thread *sender;
    struct model_endpoint *destination;
    struct micros_thread_handle previous = null_thread_handle();
    struct micros_ipc_message snapshot;
    micros_endpoint_t source_endpoint;
    size_t destination_slot;
    size_t receiver_slot;
    enum micros_ipc_error error;

    if (
        message == NULL
        || (uintptr_t)message % _Alignof(struct micros_ipc_message)
            != 0
        || (
            message->type
            & MICROS_IPC_TYPE_KERNEL_MASK
        ) != 0
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    error = model_resolve_operation_thread(
        model,
        sender_slot,
        &source_endpoint
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = model_authorize_target(
        model,
        source_endpoint,
        MICROS_PRIVILEGE_OPERATION_SEND,
        destination_endpoint
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    (void)model_resolve_active_endpoint(
        model,
        destination_endpoint,
        &destination_slot
    );
    sender = &model->threads[sender_slot];
    destination = &model->endpoints[destination_slot];
    model_canonicalize_message(
        &snapshot,
        message,
        source_endpoint,
        0
    );
    if (
        model_find_receiver(
            model,
            destination_slot,
            source_endpoint,
            &previous,
            &receiver_slot
        )
    ) {
        struct model_thread *receiver =
            &model->threads[receiver_slot];
        const struct model_transition transitions[2] = {
            {
                .thread_slot = sender_slot,
                .clear_flags = MICROS_THREAD_RTS_INACTIVE,
            },
            {
                .thread_slot = receiver_slot,
                .clear_flags = MICROS_THREAD_RTS_IPC_RECEIVE,
            },
        };
        uintptr_t receive_buffer = receiver->ipc_receive_buffer;

        error = model_apply_transitions(
            model,
            transitions,
            sizeof(transitions) / sizeof(transitions[0])
        );
        if (error != MICROS_IPC_OK) {
            return error;
        }
        model_ipc_unlink(
            model,
            &destination->receiver_head,
            &destination->receiver_tail,
            previous,
            receiver_slot
        );
        receiver->ipc_queue_kind = MICROS_IPC_QUEUE_NONE;
        receiver->ipc_next = null_thread_handle();
        receiver->ipc_receive_source = 0;
        model_stage_delivery(receiver, receive_buffer, &snapshot);
        return MICROS_IPC_OK;
    }
    error = model_deadlock_preflight(
        model,
        sender_slot,
        destination_endpoint,
        MICROS_THREAD_CAPACITY
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    sender->runtime_flags = MICROS_THREAD_RTS_IPC_SEND;
    sender->ipc_queue_kind = MICROS_IPC_QUEUE_SENDER;
    sender->ipc_outbound_message = snapshot;
    sender->ipc_send_destination = destination_endpoint;
    model_ipc_append(
        model,
        &destination->sender_head,
        &destination->sender_tail,
        sender_slot
    );
    return MICROS_IPC_OK;
}

static enum micros_ipc_error model_receive(
    struct model_state *model,
    size_t receiver_slot,
    micros_endpoint_t source_endpoint,
    uintptr_t receive_buffer
)
{
    struct model_thread *receiver;
    struct model_endpoint *destination;
    struct micros_thread_handle previous = null_thread_handle();
    struct micros_ipc_message incoming;
    micros_endpoint_t receiver_endpoint;
    size_t destination_slot;
    size_t notification_source_slot;
    size_t sender_slot;
    uint64_t event_mask;
    enum micros_ipc_error error;

    if (
        receive_buffer == 0
        || receive_buffer % 8 != 0
        || source_endpoint == MICROS_ENDPOINT_NONE
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    error = model_resolve_operation_thread(
        model,
        receiver_slot,
        &receiver_endpoint
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = model_authorize_operation(
        model,
        receiver_endpoint,
        MICROS_PRIVILEGE_OPERATION_RECEIVE
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    if (
        source_endpoint != MICROS_ENDPOINT_ANY
        && !model_resolve_active_endpoint(
            model,
            source_endpoint,
            &destination_slot
        )
    ) {
        return MICROS_IPC_ERROR_DEAD_ENDPOINT;
    }
    receiver = &model->threads[receiver_slot];
    destination_slot = receiver->owner.slot;
    destination = &model->endpoints[destination_slot];
    if (
        model_find_pending_notification(
            model,
            destination_slot,
            source_endpoint,
            &notification_source_slot,
            &event_mask
        )
    ) {
        const struct model_transition transition = {
            .thread_slot = receiver_slot,
            .clear_flags = MICROS_THREAD_RTS_INACTIVE,
        };

        model_canonicalize_notification(
            &incoming,
            model->endpoints[notification_source_slot].value,
            event_mask
        );
        error = model_apply_transitions(model, &transition, 1);
        if (error != MICROS_IPC_OK) {
            return error;
        }
        destination->pending_events[notification_source_slot] = 0;
        destination->pending_notification_sources &=
            ~(UINT64_C(1) << notification_source_slot);
        model_stage_delivery(receiver, receive_buffer, &incoming);
        return MICROS_IPC_OK;
    }
    if (
        model_find_sender(
            model,
            destination_slot,
            source_endpoint,
            &previous,
            &sender_slot
        )
    ) {
        struct model_thread *sender = &model->threads[sender_slot];
        const struct model_transition transitions[2] = {
            {
                .thread_slot = receiver_slot,
                .clear_flags = MICROS_THREAD_RTS_INACTIVE,
            },
            {
                .thread_slot = sender_slot,
                .clear_flags = MICROS_THREAD_RTS_IPC_SEND,
            },
        };

        incoming = sender->ipc_outbound_message;
        error = model_apply_transitions(
            model,
            transitions,
            sizeof(transitions) / sizeof(transitions[0])
        );
        if (error != MICROS_IPC_OK) {
            return error;
        }
        model_ipc_unlink(
            model,
            &destination->sender_head,
            &destination->sender_tail,
            previous,
            sender_slot
        );
        model_clear_delivered_sender(sender);
        model_stage_delivery(receiver, receive_buffer, &incoming);
        return MICROS_IPC_OK;
    }
    error = model_deadlock_preflight(
        model,
        receiver_slot,
        source_endpoint,
        MICROS_THREAD_CAPACITY
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    receiver->runtime_flags = MICROS_THREAD_RTS_IPC_RECEIVE;
    receiver->ipc_queue_kind = MICROS_IPC_QUEUE_RECEIVER;
    receiver->ipc_receive_source = source_endpoint;
    receiver->ipc_receive_buffer = receive_buffer;
    model_ipc_append(
        model,
        &destination->receiver_head,
        &destination->receiver_tail,
        receiver_slot
    );
    return MICROS_IPC_OK;
}

static enum micros_ipc_error model_call(
    struct model_state *model,
    size_t caller_slot,
    micros_endpoint_t destination_endpoint,
    struct micros_ipc_message *message
)
{
    struct model_thread *caller;
    struct model_endpoint *destination;
    struct micros_thread_handle previous = null_thread_handle();
    struct micros_ipc_message snapshot;
    micros_endpoint_t source_endpoint;
    uint64_t reply_token;
    size_t destination_slot;
    size_t receiver_slot;
    enum micros_ipc_error error;

    if (
        message == NULL
        || (uintptr_t)message % _Alignof(struct micros_ipc_message)
            != 0
        || (
            message->type
            & MICROS_IPC_TYPE_KERNEL_MASK
        ) != 0
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    error = model_resolve_operation_thread(
        model,
        caller_slot,
        &source_endpoint
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = model_authorize_target(
        model,
        source_endpoint,
        MICROS_PRIVILEGE_OPERATION_CALL,
        destination_endpoint
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    if (model->last_reply_token == UINT64_MAX) {
        return MICROS_IPC_ERROR_REPLY_TOKEN_EXHAUSTED;
    }
    reply_token = model->last_reply_token + 1;
    (void)model_resolve_active_endpoint(
        model,
        destination_endpoint,
        &destination_slot
    );
    caller = &model->threads[caller_slot];
    destination = &model->endpoints[destination_slot];
    model_canonicalize_message(
        &snapshot,
        message,
        source_endpoint,
        reply_token
    );
    if (
        model_find_receiver(
            model,
            destination_slot,
            source_endpoint,
            &previous,
            &receiver_slot
        )
    ) {
        struct model_thread *receiver =
            &model->threads[receiver_slot];
        const struct model_transition transitions[2] = {
            {
                .thread_slot = caller_slot,
                .clear_flags = MICROS_THREAD_RTS_INACTIVE,
                .set_flags = MICROS_THREAD_RTS_IPC_REPLY,
            },
            {
                .thread_slot = receiver_slot,
                .clear_flags = MICROS_THREAD_RTS_IPC_RECEIVE,
            },
        };
        uintptr_t receive_buffer = receiver->ipc_receive_buffer;

        error = model_apply_transitions(
            model,
            transitions,
            sizeof(transitions) / sizeof(transitions[0])
        );
        if (error != MICROS_IPC_OK) {
            return error;
        }
        model_ipc_unlink(
            model,
            &destination->receiver_head,
            &destination->receiver_tail,
            previous,
            receiver_slot
        );
        receiver->ipc_queue_kind = MICROS_IPC_QUEUE_NONE;
        receiver->ipc_next = null_thread_handle();
        receiver->ipc_receive_source = 0;
        model_stage_delivery(receiver, receive_buffer, &snapshot);
        caller->ipc_receive_buffer = (uintptr_t)message;
        caller->ipc_reply_token = reply_token;
        caller->ipc_reply_callee = destination_endpoint;
        model->last_reply_token = reply_token;
        return MICROS_IPC_OK;
    }
    error = model_deadlock_preflight(
        model,
        caller_slot,
        destination_endpoint,
        MICROS_THREAD_CAPACITY
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    caller->runtime_flags =
        MICROS_THREAD_RTS_IPC_SEND | MICROS_THREAD_RTS_IPC_REPLY;
    caller->ipc_queue_kind = MICROS_IPC_QUEUE_SENDER;
    caller->ipc_outbound_message = snapshot;
    caller->ipc_send_destination = destination_endpoint;
    caller->ipc_receive_buffer = (uintptr_t)message;
    caller->ipc_reply_token = reply_token;
    caller->ipc_reply_callee = destination_endpoint;
    model_ipc_append(
        model,
        &destination->sender_head,
        &destination->sender_tail,
        caller_slot
    );
    model->last_reply_token = reply_token;
    return MICROS_IPC_OK;
}

static enum micros_ipc_error model_resolve_reply_waiter(
    struct model_state *model,
    uint64_t reply_token,
    micros_endpoint_t replying_endpoint,
    size_t *caller_slot
)
{
    size_t matched = MICROS_THREAD_CAPACITY;
    size_t index;

    if (reply_token == 0) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct model_thread *thread = &model->threads[index];

        if (
            thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && thread->ipc_reply_token == reply_token
        ) {
            if (matched != MICROS_THREAD_CAPACITY) {
                return MICROS_IPC_ERROR_INVARIANT;
            }
            matched = index;
        }
    }
    if (matched == MICROS_THREAD_CAPACITY) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    if (
        model->threads[matched].ipc_queue_kind
            != MICROS_IPC_QUEUE_NONE
        || !thread_handle_is_zero(model->threads[matched].ipc_next)
        || (
            model->threads[matched].runtime_flags
            & MICROS_THREAD_RTS_IPC_MASK
        ) != MICROS_THREAD_RTS_IPC_REPLY
        || model->threads[matched].ipc_reply_callee
            != replying_endpoint
    ) {
        return MICROS_IPC_ERROR_REPLY_TOKEN;
    }
    *caller_slot = matched;
    return MICROS_IPC_OK;
}

static enum micros_ipc_error model_reply_preflight(
    struct model_state *model,
    size_t replier_slot,
    uint64_t reply_token,
    const struct micros_ipc_message *message,
    uint32_t operation,
    micros_endpoint_t *replying_endpoint,
    size_t *caller_slot,
    struct micros_ipc_message *snapshot
)
{
    enum micros_ipc_error error;
    size_t index;

    if (
        message == NULL
        || (uintptr_t)message % _Alignof(struct micros_ipc_message)
            != 0
        || (
            message->type
            & MICROS_IPC_TYPE_KERNEL_MASK
        ) != 0
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    error = model_resolve_operation_thread(
        model,
        replier_slot,
        replying_endpoint
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = model_authorize_operation(
        model,
        *replying_endpoint,
        operation
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = model_resolve_reply_waiter(
        model,
        reply_token,
        *replying_endpoint,
        caller_slot
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct model_thread *thread = &model->threads[index];

        if (
            thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && thread->ipc_delivery_pending
            && thread->ipc_staged_result == MICROS_IPC_OK
            && thread->ipc_inbound_message.reply_token
                == reply_token
        ) {
            return MICROS_IPC_ERROR_REPLY_TOKEN;
        }
    }
    model_canonicalize_message(
        snapshot,
        message,
        *replying_endpoint,
        0
    );
    return MICROS_IPC_OK;
}

static enum micros_ipc_error model_reply(
    struct model_state *model,
    size_t replier_slot,
    uint64_t reply_token,
    const struct micros_ipc_message *message
)
{
    struct micros_ipc_message snapshot;
    micros_endpoint_t replying_endpoint;
    size_t caller_slot;
    uintptr_t reply_buffer;
    enum micros_ipc_error error;
    struct model_transition transitions[2];

    error = model_reply_preflight(
        model,
        replier_slot,
        reply_token,
        message,
        MICROS_PRIVILEGE_OPERATION_REPLY,
        &replying_endpoint,
        &caller_slot,
        &snapshot
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    (void)replying_endpoint;
    transitions[0] = (struct model_transition){
        .thread_slot = replier_slot,
        .clear_flags = MICROS_THREAD_RTS_INACTIVE,
    };
    transitions[1] = (struct model_transition){
        .thread_slot = caller_slot,
        .clear_flags = MICROS_THREAD_RTS_IPC_REPLY,
    };
    error = model_apply_transitions(
        model,
        transitions,
        sizeof(transitions) / sizeof(transitions[0])
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    reply_buffer = model->threads[caller_slot].ipc_receive_buffer;
    model_stage_delivery(
        &model->threads[caller_slot],
        reply_buffer,
        &snapshot
    );
    model->threads[caller_slot].ipc_reply_token = 0;
    model->threads[caller_slot].ipc_reply_callee = 0;
    return MICROS_IPC_OK;
}

static enum micros_ipc_error model_reply_receive(
    struct model_state *model,
    size_t replier_slot,
    uint64_t reply_token,
    const struct micros_ipc_message *reply_message,
    micros_endpoint_t source_endpoint,
    uintptr_t receive_buffer
)
{
    struct micros_ipc_message reply_snapshot;
    struct micros_ipc_message incoming;
    struct model_endpoint *destination;
    struct micros_thread_handle previous = null_thread_handle();
    micros_endpoint_t replying_endpoint;
    size_t caller_slot;
    size_t destination_slot;
    size_t notification_source_slot;
    size_t sender_slot;
    uint64_t event_mask;
    bool has_notification;
    bool has_sender;
    enum micros_ipc_error error;

    if (
        receive_buffer == 0
        || receive_buffer % 8 != 0
        || source_endpoint == MICROS_ENDPOINT_NONE
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    error = model_reply_preflight(
        model,
        replier_slot,
        reply_token,
        reply_message,
        MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
        &replying_endpoint,
        &caller_slot,
        &reply_snapshot
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    if (
        source_endpoint != MICROS_ENDPOINT_ANY
        && !model_resolve_active_endpoint(
            model,
            source_endpoint,
            &destination_slot
        )
    ) {
        return MICROS_IPC_ERROR_DEAD_ENDPOINT;
    }
    destination_slot = model->threads[replier_slot].owner.slot;
    destination = &model->endpoints[destination_slot];
    has_notification = model_find_pending_notification(
        model,
        destination_slot,
        source_endpoint,
        &notification_source_slot,
        &event_mask
    );
    has_sender = false;
    sender_slot = MICROS_THREAD_CAPACITY;
    if (!has_notification) {
        has_sender = model_find_sender(
            model,
            destination_slot,
            source_endpoint,
            &previous,
            &sender_slot
        );
    }
    if (!has_notification && !has_sender) {
        error = model_deadlock_preflight(
            model,
            replier_slot,
            source_endpoint,
            caller_slot
        );
        if (error != MICROS_IPC_OK) {
            return error;
        }
    }
    if (has_notification) {
        const struct model_transition transitions[2] = {
            {
                .thread_slot = caller_slot,
                .clear_flags = MICROS_THREAD_RTS_IPC_REPLY,
            },
            {
                .thread_slot = replier_slot,
                .clear_flags = MICROS_THREAD_RTS_INACTIVE,
            },
        };

        error = model_apply_transitions(
            model,
            transitions,
            sizeof(transitions) / sizeof(transitions[0])
        );
    } else if (has_sender) {
        const struct model_transition transitions[3] = {
            {
                .thread_slot = caller_slot,
                .clear_flags = MICROS_THREAD_RTS_IPC_REPLY,
            },
            {
                .thread_slot = replier_slot,
                .clear_flags = MICROS_THREAD_RTS_INACTIVE,
            },
            {
                .thread_slot = sender_slot,
                .clear_flags = MICROS_THREAD_RTS_IPC_SEND,
            },
        };

        incoming = model->threads[sender_slot].ipc_outbound_message;
        error = model_apply_transitions(
            model,
            transitions,
            sizeof(transitions) / sizeof(transitions[0])
        );
    } else {
        const struct model_transition transitions[2] = {
            {
                .thread_slot = caller_slot,
                .clear_flags = MICROS_THREAD_RTS_IPC_REPLY,
            },
            {
                .thread_slot = replier_slot,
                .clear_flags = MICROS_THREAD_RTS_INACTIVE,
                .set_flags = MICROS_THREAD_RTS_IPC_RECEIVE,
            },
        };

        error = model_apply_transitions(
            model,
            transitions,
            sizeof(transitions) / sizeof(transitions[0])
        );
    }
    if (error != MICROS_IPC_OK) {
        return error;
    }
    model_stage_delivery(
        &model->threads[caller_slot],
        model->threads[caller_slot].ipc_receive_buffer,
        &reply_snapshot
    );
    model->threads[caller_slot].ipc_reply_token = 0;
    model->threads[caller_slot].ipc_reply_callee = 0;
    if (has_notification) {
        model_canonicalize_notification(
            &incoming,
            model->endpoints[notification_source_slot].value,
            event_mask
        );
        destination->pending_events[notification_source_slot] = 0;
        destination->pending_notification_sources &=
            ~(UINT64_C(1) << notification_source_slot);
        model_stage_delivery(
            &model->threads[replier_slot],
            receive_buffer,
            &incoming
        );
    } else if (has_sender) {
        model_ipc_unlink(
            model,
            &destination->sender_head,
            &destination->sender_tail,
            previous,
            sender_slot
        );
        model_clear_delivered_sender(&model->threads[sender_slot]);
        model_stage_delivery(
            &model->threads[replier_slot],
            receive_buffer,
            &incoming
        );
    } else {
        struct model_thread *replier =
            &model->threads[replier_slot];

        replier->ipc_queue_kind = MICROS_IPC_QUEUE_RECEIVER;
        replier->ipc_receive_source = source_endpoint;
        replier->ipc_receive_buffer = receive_buffer;
        model_ipc_append(
            model,
            &destination->receiver_head,
            &destination->receiver_tail,
            replier_slot
        );
    }
    return MICROS_IPC_OK;
}

static enum micros_ipc_error model_notify(
    struct model_state *model,
    size_t notifier_slot,
    micros_endpoint_t destination_endpoint,
    uint64_t event_mask
)
{
    struct model_endpoint *destination;
    struct micros_thread_handle previous = null_thread_handle();
    struct micros_ipc_message notification;
    micros_endpoint_t source_endpoint;
    size_t destination_slot;
    size_t receiver_slot;
    size_t source_slot;
    enum micros_ipc_error error;

    if (event_mask == 0) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    error = model_resolve_operation_thread(
        model,
        notifier_slot,
        &source_endpoint
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    error = model_authorize_target(
        model,
        source_endpoint,
        MICROS_PRIVILEGE_OPERATION_NOTIFY,
        destination_endpoint
    );
    if (error != MICROS_IPC_OK) {
        return error;
    }
    (void)model_resolve_active_endpoint(
        model,
        destination_endpoint,
        &destination_slot
    );
    source_slot = model->threads[notifier_slot].owner.slot;
    destination = &model->endpoints[destination_slot];
    model_canonicalize_notification(
        &notification,
        source_endpoint,
        event_mask
    );
    if (
        model_find_receiver(
            model,
            destination_slot,
            source_endpoint,
            &previous,
            &receiver_slot
        )
        && (
            model->threads[receiver_slot].runtime_flags
            & MICROS_THREAD_RTS_IPC_REPLY
        ) == 0
    ) {
        struct model_thread *receiver =
            &model->threads[receiver_slot];
        const struct model_transition transitions[2] = {
            {
                .thread_slot = notifier_slot,
                .clear_flags = MICROS_THREAD_RTS_INACTIVE,
            },
            {
                .thread_slot = receiver_slot,
                .clear_flags = MICROS_THREAD_RTS_IPC_RECEIVE,
            },
        };
        uintptr_t receive_buffer = receiver->ipc_receive_buffer;

        error = model_apply_transitions(
            model,
            transitions,
            sizeof(transitions) / sizeof(transitions[0])
        );
        if (error != MICROS_IPC_OK) {
            return error;
        }
        model_ipc_unlink(
            model,
            &destination->receiver_head,
            &destination->receiver_tail,
            previous,
            receiver_slot
        );
        receiver->ipc_queue_kind = MICROS_IPC_QUEUE_NONE;
        receiver->ipc_next = null_thread_handle();
        receiver->ipc_receive_source = 0;
        model_stage_delivery(
            receiver,
            receive_buffer,
            &notification
        );
        return MICROS_IPC_OK;
    }
    {
        const struct model_transition transition = {
            .thread_slot = notifier_slot,
            .clear_flags = MICROS_THREAD_RTS_INACTIVE,
        };

        error = model_apply_transitions(model, &transition, 1);
    }
    if (error != MICROS_IPC_OK) {
        return error;
    }
    destination->pending_events[source_slot] |= event_mask;
    destination->pending_notification_sources |=
        UINT64_C(1) << source_slot;
    return MICROS_IPC_OK;
}

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
    struct micros_thread_handle retained_head = null_thread_handle();
    struct micros_thread_handle retained_tail = null_thread_handle();
    size_t steps;

    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        struct micros_thread_handle next;

        if (thread_handle_is_zero(current)) {
            break;
        }
        next = model->threads[current.slot].ipc_next;
        if (!remove[current.slot]) {
            if (thread_handle_is_zero(retained_head)) {
                retained_head = current;
            } else {
                model->threads[retained_tail.slot].ipc_next =
                    current;
            }
            retained_tail = current;
        }
        current = next;
    }
    if (!thread_handle_is_zero(retained_tail)) {
        model->threads[retained_tail.slot].ipc_next =
            null_thread_handle();
    }
    *head = retained_head;
    *tail = retained_tail;
}

static enum micros_ipc_error model_close_endpoint(
    struct model_state *model,
    size_t process_slot
)
{
    enum {
        CLOSE_NONE = 0,
        CLOSE_CLEAR,
        CLOSE_DEAD,
    };
    unsigned char actions[MICROS_THREAD_CAPACITY];
    bool remove[MICROS_THREAD_CAPACITY];
    struct model_transition transitions[MICROS_THREAD_CAPACITY];
    size_t transition_count = 0;
    struct model_process *owner;
    micros_endpoint_t endpoint;
    size_t index;
    enum micros_ipc_error error;

    if (process_slot >= MICROS_PROCESS_CAPACITY) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    owner = &model->processes[process_slot];
    endpoint = owner->primary_endpoint;
    if (
        owner->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || model->endpoints[process_slot].state
            != MICROS_ENDPOINT_STATE_ACTIVE
        || model->endpoints[process_slot].value != endpoint
    ) {
        return MICROS_IPC_ERROR_DEAD_ENDPOINT;
    }
    memset(actions, 0, sizeof(actions));
    memset(remove, 0, sizeof(remove));
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
            && thread->owner.generation == owner->generation;
        queued_sender =
            thread->ipc_queue_kind == MICROS_IPC_QUEUE_SENDER
            && thread->ipc_send_destination == endpoint;
        queued_receiver =
            thread->ipc_queue_kind == MICROS_IPC_QUEUE_RECEIVER
            && thread->ipc_receive_source == endpoint;
        reply_wait =
            thread->ipc_reply_token != 0
            && thread->ipc_reply_callee == endpoint;
        staged_reference =
            thread->ipc_delivery_pending
            && thread->ipc_staged_result == MICROS_IPC_OK
            && thread->ipc_inbound_message.source == endpoint;
        clear_flags =
            thread->runtime_flags & MICROS_THREAD_RTS_IPC_MASK;
        if (owned) {
            if (
                thread_handles_equal(
                    model->hart.current_thread,
                    model_thread_handle(model, index)
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
            if (!model_thread_is_held_clear(thread)) {
                actions[index] = CLOSE_CLEAR;
            }
            remove[index] =
                thread->ipc_queue_kind != MICROS_IPC_QUEUE_NONE;
        } else if (queued_sender || queued_receiver || reply_wait) {
            actions[index] = CLOSE_DEAD;
            remove[index] = queued_sender || queued_receiver;
        } else if (staged_reference) {
            return MICROS_IPC_ERROR_STATE;
        }
        if (
            model_thread_references_endpoint(thread, endpoint)
            && actions[index] == CLOSE_NONE
        ) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
        if (actions[index] != CLOSE_NONE && clear_flags != 0) {
            transitions[transition_count] =
                (struct model_transition){
                    .thread_slot = index,
                    .clear_flags = clear_flags,
                };
            ++transition_count;
        }
    }
    if (transition_count != 0) {
        error = model_apply_transitions(
            model,
            transitions,
            transition_count
        );
        if (error != MICROS_IPC_OK) {
            return error;
        }
    }
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        model_rebuild_queue(
            model,
            &model->endpoints[index].sender_head,
            &model->endpoints[index].sender_tail,
            remove
        );
        model_rebuild_queue(
            model,
            &model->endpoints[index].receiver_head,
            &model->endpoints[index].receiver_tail,
            remove
        );
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        if (actions[index] == CLOSE_CLEAR) {
            model_clear_ipc_state(&model->threads[index]);
        } else if (actions[index] == CLOSE_DEAD) {
            model_clear_ipc_state(&model->threads[index]);
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
    owner->primary_endpoint = MICROS_PROCESS_ENDPOINT_NONE;
    owner->privilege_profile = 0;
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
                || !model_thread_is_held_clear(&model->threads[index])
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

static enum micros_ipc_error model_close_reuse(
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

static enum micros_ipc_error production_drain_thread(size_t thread_slot)
{
    struct micros_thread *thread;

    if (thread_slot >= MICROS_THREAD_CAPACITY) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    thread = &objects.threads[thread_slot];
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

static enum micros_ipc_error production_hold_thread(size_t thread_slot)
{
    struct micros_thread_handle thread;
    enum micros_kernel_object_error error;

    if (
        thread_slot >= MICROS_THREAD_CAPACITY
        || objects.threads[thread_slot].slot_state
            != MICROS_KERNEL_OBJECT_SLOT_LIVE
    ) {
        return MICROS_IPC_ERROR_ARGUMENT;
    }
    thread = (struct micros_thread_handle){
        .slot = (uint16_t)thread_slot,
        .generation = objects.threads[thread_slot].generation,
    };
    error = micros_thread_scheduler_hold(&objects, thread);
    if (error == MICROS_KERNEL_OBJECT_OK) {
        return MICROS_IPC_OK;
    }
    return error == MICROS_KERNEL_OBJECT_ERROR_STATE
        ? MICROS_IPC_ERROR_STATE
        : MICROS_IPC_ERROR_INVARIANT;
}

static enum micros_ipc_error production_reuse_process(
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
        || objects.processes[process_slot].slot_state
            != MICROS_KERNEL_OBJECT_SLOT_LIVE
    ) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    old_process = (struct micros_process_handle){
        .slot = (uint16_t)process_slot,
        .generation = objects.processes[process_slot].generation,
    };
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects.threads[index];

        if (
            thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && process_handles_equal(thread->owner, old_process)
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
    for (index = 0; index < owned_count; ++index) {
        if (
            micros_thread_scheduler_remove(
                &objects,
                owned_threads[index]
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_detach_execution_context(
                &objects,
                owned_threads[index]
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_release(
                &objects,
                owned_threads[index]
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
    }
    if (
        micros_process_release(&objects, old_process)
            != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(&objects, &new_process)
            != MICROS_KERNEL_OBJECT_OK
        || new_process.slot != process_slot
        || micros_endpoint_reserve(
            &registry,
            &objects,
            new_process,
            &endpoint
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_install_profile(
            &registry,
            &objects,
            new_process,
            MODEL_FULL_PROFILE
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_activate(
            &registry,
            &objects,
            endpoint
        ) != MICROS_ENDPOINT_OK
    ) {
        return MICROS_IPC_ERROR_INVARIANT;
    }
    for (index = 0; index < owned_count; ++index) {
        struct micros_thread_handle thread;
        struct micros_user_context context =
            context_pattern(
                UINT64_C(0x5000)
                + owned_threads[index].slot * UINT64_C(0x100)
                + new_process.generation
            );
        uintptr_t stack_bottom =
            UINT64_C(0x10000000)
            + owned_threads[index].slot * UINT64_C(0x00008000);

        if (
            micros_thread_create(
                &objects,
                new_process,
                &thread
            ) != MICROS_KERNEL_OBJECT_OK
            || thread.slot != owned_threads[index].slot
            || micros_thread_attach_execution_context(
                &objects,
                thread,
                stack_bottom,
                stack_bottom + MICROS_THREAD_KERNEL_STACK_SIZE,
                &context
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_scheduler_admit(
                &objects,
                (struct micros_hart_handle){0, 1},
                thread,
                MODEL_PRIORITY,
                100,
                true
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_scheduler_hold(&objects, thread)
                != MICROS_KERNEL_OBJECT_OK
        ) {
            return MICROS_IPC_ERROR_INVARIANT;
        }
    }
    return MICROS_IPC_OK;
}

static enum micros_ipc_error production_close_reuse(
    size_t process_slot,
    micros_endpoint_t endpoint
)
{
    enum micros_ipc_error error =
        micros_ipc_endpoint_close(&registry, &objects, endpoint);

    if (error != MICROS_IPC_OK) {
        return error;
    }
    return production_reuse_process(process_slot);
}

static enum micros_ipc_error model_execute_action(
    struct model_state *model,
    const struct model_action *action
)
{
    switch (action->operation) {
    case MODEL_OPERATION_SEND:
        return model_send(
            model,
            action->thread_slot,
            action->endpoint,
            action->message
        );
    case MODEL_OPERATION_RECEIVE:
        return model_receive(
            model,
            action->thread_slot,
            action->source,
            action->buffer
        );
    case MODEL_OPERATION_CALL:
        return model_call(
            model,
            action->thread_slot,
            action->endpoint,
            action->message
        );
    case MODEL_OPERATION_REPLY:
        return model_reply(
            model,
            action->thread_slot,
            action->value,
            action->message
        );
    case MODEL_OPERATION_REPLY_RECEIVE:
        return model_reply_receive(
            model,
            action->thread_slot,
            action->value,
            action->message,
            action->source,
            action->buffer
        );
    case MODEL_OPERATION_NOTIFY:
        return model_notify(
            model,
            action->thread_slot,
            action->endpoint,
            action->value
        );
    case MODEL_OPERATION_DRAIN:
        return model_drain_thread(model, action->thread_slot);
    case MODEL_OPERATION_HOLD:
        return model_hold_thread(model, action->thread_slot);
    case MODEL_OPERATION_CLOSE_REUSE:
        return model_close_reuse(model, action->process_slot);
    default:
        return MICROS_IPC_ERROR_ARGUMENT;
    }
}

static enum micros_ipc_error production_execute_action(
    const struct model_action *action
)
{
    struct micros_thread_handle thread;

    if (
        action->operation != MODEL_OPERATION_CLOSE_REUSE
        && action->thread_slot < MICROS_THREAD_CAPACITY
        && objects.threads[action->thread_slot].slot_state
            == MICROS_KERNEL_OBJECT_SLOT_LIVE
    ) {
        thread = (struct micros_thread_handle){
            .slot = (uint16_t)action->thread_slot,
            .generation =
                objects.threads[action->thread_slot].generation,
        };
    } else {
        thread = null_thread_handle();
    }
    switch (action->operation) {
    case MODEL_OPERATION_SEND:
        return micros_ipc_send(
            &registry,
            &objects,
            thread,
            action->endpoint,
            action->message
        );
    case MODEL_OPERATION_RECEIVE:
        return micros_ipc_receive(
            &registry,
            &objects,
            thread,
            action->source,
            action->buffer
        );
    case MODEL_OPERATION_CALL:
        return micros_ipc_call(
            &registry,
            &objects,
            thread,
            action->endpoint,
            action->message,
            (uintptr_t)action->message
);
    case MODEL_OPERATION_REPLY:
        return micros_ipc_reply(
            &registry,
            &objects,
            thread,
            action->value,
            action->message
        );
    case MODEL_OPERATION_REPLY_RECEIVE:
        return micros_ipc_reply_receive(
            &registry,
            &objects,
            thread,
            action->value,
            action->message,
            action->source,
            action->buffer
        );
    case MODEL_OPERATION_NOTIFY:
        return micros_ipc_notify(
            &registry,
            &objects,
            thread,
            action->endpoint,
            action->value
        );
    case MODEL_OPERATION_DRAIN:
        return production_drain_thread(action->thread_slot);
    case MODEL_OPERATION_HOLD:
        return production_hold_thread(action->thread_slot);
    case MODEL_OPERATION_CLOSE_REUSE:
        return production_close_reuse(
            action->process_slot,
            action->endpoint
        );
    default:
        return MICROS_IPC_ERROR_ARGUMENT;
    }
}

static void update_coverage(
    struct model_coverage *coverage,
    enum model_operation operation,
    enum micros_ipc_error result
)
{
    if ((size_t)operation < MODEL_OPERATION_COUNT) {
        ++coverage->operation_count[operation];
        if (result == MICROS_IPC_OK) {
            ++coverage->operation_success[operation];
        }
    }
    if (result == MICROS_IPC_ERROR_DEADLOCK) {
        ++coverage->deadlock;
    } else if (result == MICROS_IPC_ERROR_UNAUTHORIZED) {
        ++coverage->unauthorized;
    } else if (result == MICROS_IPC_ERROR_ARGUMENT) {
        ++coverage->argument;
    }
}

static bool execute_action(
    struct model_state *model,
    const struct model_action *action,
    size_t step,
    uint32_t seed,
    struct model_coverage *coverage,
    enum micros_ipc_error *result
)
{
    enum micros_ipc_error expected =
        model_execute_action(model, action);
    enum micros_ipc_error actual =
        production_execute_action(action);

    record_trace(step, action, expected);
    update_coverage(coverage, action->operation, expected);
    *result = expected;
    if (expected != actual) {
        dump_trace(seed, step, "operation result mismatch");
        fprintf(
            stderr,
            "  expected=%d actual=%d\n",
            (int)expected,
            (int)actual
        );
        dump_state(model);
        return false;
    }
    if (!model_matches_production(model, step)) {
        dump_trace(seed, step, "complete state mismatch");
        dump_state(model);
        return false;
    }
    return true;
}

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

static bool run_checked_action(
    struct model_state *model,
    struct model_action action,
    enum micros_ipc_error expected,
    size_t *step,
    uint32_t seed,
    struct model_coverage *coverage
)
{
    enum micros_ipc_error result;

    if (
        !execute_action(
            model,
            &action,
            *step,
            seed,
            coverage,
            &result
        )
    ) {
        return false;
    }
    if (result != expected) {
        dump_trace(seed, *step, "prelude expectation mismatch");
        fprintf(
            stderr,
            "  expected=%d actual=%d\n",
            (int)expected,
            (int)result
        );
        return false;
    }
    ++*step;
    return true;
}

static struct model_action receive_action(
    size_t thread_slot,
    micros_endpoint_t source,
    size_t step
)
{
    struct model_action action =
        base_action(MODEL_OPERATION_RECEIVE);

    action.thread_slot = thread_slot;
    action.source = source;
    action.buffer = receive_buffer_for_step(step, thread_slot);
    return action;
}

static struct model_action send_action(
    size_t thread_slot,
    micros_endpoint_t destination,
    size_t step,
    bool kernel_type
)
{
    struct model_action action = base_action(MODEL_OPERATION_SEND);

    action.thread_slot = thread_slot;
    action.endpoint = destination;
    action.message = message_for_step(step, kernel_type);
    return action;
}

static struct model_action call_action(
    size_t thread_slot,
    micros_endpoint_t destination,
    size_t step
)
{
    struct model_action action = base_action(MODEL_OPERATION_CALL);

    action.thread_slot = thread_slot;
    action.endpoint = destination;
    action.message = message_for_step(step, false);
    return action;
}

static struct model_action reply_action(
    size_t thread_slot,
    uint64_t token,
    size_t step
)
{
    struct model_action action = base_action(MODEL_OPERATION_REPLY);

    action.thread_slot = thread_slot;
    action.value = token;
    action.message = message_for_step(step, false);
    return action;
}

static struct model_action reply_receive_action(
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
    action.message = message_for_step(step, false);
    action.source = source;
    action.buffer = receive_buffer_for_step(step, thread_slot);
    return action;
}

static struct model_action notify_action(
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

static struct model_action thread_action(
    enum model_operation operation,
    size_t thread_slot
)
{
    struct model_action action = base_action(operation);

    action.thread_slot = thread_slot;
    return action;
}

static struct model_action close_reuse_action(
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

static bool run_model_prelude(
    struct model_state *model,
    size_t *step,
    uint32_t seed,
    struct model_coverage *coverage
)
{
    uint64_t token_one;
    uint64_t token_two;
    uint64_t token_three;
    uint64_t token_four;
    uint64_t token_five;
    micros_endpoint_t old_endpoint;
    struct model_action malformed;

#define RUN(action, expected) \
    do { \
        if (!run_checked_action( \
            model, \
            (action), \
            (expected), \
            step, \
            seed, \
            coverage \
        )) { \
            return false; \
        } \
    } while (false)

    RUN(
        receive_action(
            2,
            MICROS_ENDPOINT_ANY,
            *step
        ),
        MICROS_IPC_OK
    );
    RUN(
        send_action(
            0,
            model->processes[1].primary_endpoint,
            *step,
            false
        ),
        MICROS_IPC_OK
    );
    RUN(thread_action(MODEL_OPERATION_DRAIN, 2), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 0), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 2), MICROS_IPC_OK);

    RUN(
        send_action(
            6,
            model->processes[4].primary_endpoint,
            *step,
            false
        ),
        MICROS_IPC_OK
    );
    RUN(
        receive_action(7, MICROS_ENDPOINT_ANY, *step),
        MICROS_IPC_OK
    );
    RUN(thread_action(MODEL_OPERATION_DRAIN, 7), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 7), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_DRAIN, 6), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 6), MICROS_IPC_OK);

    RUN(
        receive_action(2, MICROS_ENDPOINT_ANY, *step),
        MICROS_IPC_OK
    );
    RUN(
        call_action(
            0,
            model->processes[1].primary_endpoint,
            *step
        ),
        MICROS_IPC_OK
    );
    token_one = model->threads[0].ipc_reply_token;
    RUN(
        reply_action(3, token_one, *step),
        MICROS_IPC_ERROR_REPLY_TOKEN
    );
    RUN(
        reply_receive_action(
            3,
            token_one,
            MICROS_ENDPOINT_ANY,
            *step
        ),
        MICROS_IPC_ERROR_REPLY_TOKEN
    );
    RUN(
        receive_action(3, MICROS_ENDPOINT_ANY, *step),
        MICROS_IPC_OK
    );
    RUN(
        call_action(
            1,
            model->processes[1].primary_endpoint,
            *step
        ),
        MICROS_IPC_OK
    );
    token_two = model->threads[1].ipc_reply_token;
    RUN(thread_action(MODEL_OPERATION_DRAIN, 2), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 2), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_DRAIN, 3), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 3), MICROS_IPC_OK);
    RUN(reply_action(2, token_two, *step), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_DRAIN, 1), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 1), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 2), MICROS_IPC_OK);
    RUN(reply_action(3, token_one, *step), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_DRAIN, 0), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 0), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 3), MICROS_IPC_OK);
    RUN(
        reply_action(3, token_one, *step),
        MICROS_IPC_ERROR_REPLY_TOKEN
    );

    RUN(
        receive_action(2, MICROS_ENDPOINT_ANY, *step),
        MICROS_IPC_OK
    );
    RUN(
        call_action(
            0,
            model->processes[1].primary_endpoint,
            *step
        ),
        MICROS_IPC_OK
    );
    token_three = model->threads[0].ipc_reply_token;
    RUN(thread_action(MODEL_OPERATION_DRAIN, 2), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 2), MICROS_IPC_OK);
    RUN(
        reply_receive_action(
            2,
            token_three,
            MICROS_ENDPOINT_ANY,
            *step
        ),
        MICROS_IPC_OK
    );
    RUN(
        send_action(
            4,
            model->processes[1].primary_endpoint,
            *step,
            false
        ),
        MICROS_IPC_OK
    );
    RUN(thread_action(MODEL_OPERATION_DRAIN, 0), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 0), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_DRAIN, 2), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 2), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 4), MICROS_IPC_OK);

    RUN(
        receive_action(2, MICROS_ENDPOINT_ANY, *step),
        MICROS_IPC_OK
    );
    RUN(
        call_action(
            0,
            model->processes[1].primary_endpoint,
            *step
        ),
        MICROS_IPC_OK
    );
    token_four = model->threads[0].ipc_reply_token;
    RUN(thread_action(MODEL_OPERATION_DRAIN, 2), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 2), MICROS_IPC_OK);
    RUN(
        notify_action(
            4,
            model->processes[0].primary_endpoint,
            UINT64_C(0x01)
        ),
        MICROS_IPC_OK
    );
    RUN(thread_action(MODEL_OPERATION_HOLD, 4), MICROS_IPC_OK);
    RUN(
        notify_action(
            4,
            model->processes[0].primary_endpoint,
            UINT64_C(0x04)
        ),
        MICROS_IPC_OK
    );
    RUN(thread_action(MODEL_OPERATION_HOLD, 4), MICROS_IPC_OK);
    RUN(reply_action(2, token_four, *step), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_DRAIN, 0), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 0), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 2), MICROS_IPC_OK);
    RUN(
        receive_action(0, MICROS_ENDPOINT_ANY, *step),
        MICROS_IPC_OK
    );
    RUN(thread_action(MODEL_OPERATION_DRAIN, 0), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 0), MICROS_IPC_OK);

    RUN(
        send_action(
            6,
            model->processes[4].primary_endpoint,
            *step,
            false
        ),
        MICROS_IPC_OK
    );
    RUN(
        send_action(
            7,
            model->processes[3].primary_endpoint,
            *step,
            false
        ),
        MICROS_IPC_ERROR_DEADLOCK
    );
    RUN(
        receive_action(7, MICROS_ENDPOINT_ANY, *step),
        MICROS_IPC_OK
    );
    RUN(thread_action(MODEL_OPERATION_DRAIN, 7), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 7), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_DRAIN, 6), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 6), MICROS_IPC_OK);

    RUN(
        send_action(
            9,
            model->processes[0].primary_endpoint,
            *step,
            false
        ),
        MICROS_IPC_ERROR_UNAUTHORIZED
    );
    RUN(
        notify_action(
            0,
            model->processes[1].primary_endpoint,
            0
        ),
        MICROS_IPC_ERROR_ARGUMENT
    );
    RUN(
        send_action(
            0,
            model->processes[1].primary_endpoint,
            *step,
            true
        ),
        MICROS_IPC_ERROR_ARGUMENT
    );
    malformed = receive_action(0, MICROS_ENDPOINT_ANY, *step);
    malformed.buffer |= 1;
    RUN(malformed, MICROS_IPC_ERROR_ARGUMENT);

    RUN(
        call_action(
            6,
            model->processes[5].primary_endpoint,
            *step
        ),
        MICROS_IPC_OK
    );
    token_five = model->threads[6].ipc_reply_token;
    (void)token_five;
    RUN(
        receive_action(
            7,
            model->processes[5].primary_endpoint,
            *step
        ),
        MICROS_IPC_OK
    );
    RUN(
        notify_action(
            8,
            model->processes[0].primary_endpoint,
            UINT64_C(0x10)
        ),
        MICROS_IPC_OK
    );
    RUN(thread_action(MODEL_OPERATION_HOLD, 8), MICROS_IPC_OK);
    old_endpoint = model->processes[5].primary_endpoint;
    RUN(close_reuse_action(model, 5), MICROS_IPC_OK);
    RUN(
        send_action(0, old_endpoint, *step, false),
        MICROS_IPC_ERROR_DEAD_ENDPOINT
    );
    RUN(thread_action(MODEL_OPERATION_DRAIN, 6), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 6), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_DRAIN, 7), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 7), MICROS_IPC_OK);

    RUN(
        receive_action(7, MICROS_ENDPOINT_ANY, *step),
        MICROS_IPC_OK
    );
    RUN(
        call_action(
            6,
            model->processes[4].primary_endpoint,
            *step
        ),
        MICROS_IPC_OK
    );
    token_five = model->threads[6].ipc_reply_token;
    RUN(thread_action(MODEL_OPERATION_DRAIN, 7), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 7), MICROS_IPC_OK);
    RUN(
        send_action(
            8,
            model->processes[4].primary_endpoint,
            *step,
            false
        ),
        MICROS_IPC_OK
    );
    RUN(
        reply_receive_action(
            7,
            token_five,
            MICROS_ENDPOINT_ANY,
            *step
        ),
        MICROS_IPC_OK
    );
    RUN(thread_action(MODEL_OPERATION_DRAIN, 6), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 6), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_DRAIN, 7), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 7), MICROS_IPC_OK);
    RUN(thread_action(MODEL_OPERATION_HOLD, 8), MICROS_IPC_OK);

#undef RUN
    return true;
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
            model_thread_is_held_clear(thread)
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
            || !model_resolve_active_endpoint(
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
                model_thread_is_held_clear(thread)
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
            && !model_thread_is_held_clear(thread)
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

            if (thread_handle_is_zero(current)) {
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
                    !model_thread_is_held_clear(thread)
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
            thread_handle_is_zero(
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
                model_thread_is_held_clear(thread)
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
    const struct model_state *model,
    uint32_t random,
    size_t step
)
{
    size_t slot =
        find_staged_thread(model, random % MODEL_THREAD_COUNT);

    if (slot != MICROS_THREAD_CAPACITY) {
        return thread_action(MODEL_OPERATION_DRAIN, slot);
    }
    slot = find_holdable_thread(model, random % MODEL_THREAD_COUNT);
    if (slot != MICROS_THREAD_CAPACITY) {
        return thread_action(MODEL_OPERATION_HOLD, slot);
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
            return send_action(
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
        return receive_action(slot, MICROS_ENDPOINT_ANY, step);
    }
    slot = find_safe_reuse_process(
        model,
        random % (
            MODEL_SINGLETON_LAST - MODEL_SINGLETON_FIRST + 1
        )
    );
    if (slot != MICROS_PROCESS_CAPACITY) {
        return close_reuse_action(model, slot);
    }
    slot = find_held_full_thread(model, random % MODEL_THREAD_COUNT);
    if (slot != MICROS_THREAD_CAPACITY) {
        return notify_action(
            slot,
            model->processes[0].primary_endpoint,
            0
        );
    }
    return send_action(
        9,
        model->processes[0].primary_endpoint,
        step,
        false
    );
}

static struct model_action build_random_action(
    const struct model_state *model,
    uint32_t random,
    size_t step
)
{
    size_t thread_slot;
    size_t process_slot;

    switch (random % 12) {
    case 0:
        thread_slot = find_held_full_thread(
            model,
            (random >> 8) % MODEL_THREAD_COUNT
        );
        if (thread_slot != MICROS_THREAD_CAPACITY) {
            process_slot =
                (random >> 16) % MODEL_DENIED_PROCESS;
            return send_action(
                thread_slot,
                model->processes[process_slot].primary_endpoint,
                step,
                false
            );
        }
        break;
    case 1:
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
            return receive_action(thread_slot, source, step);
        }
        break;
    case 2:
        thread_slot = find_held_full_thread(
            model,
            (random >> 8) % MODEL_THREAD_COUNT
        );
        if (thread_slot != MICROS_THREAD_CAPACITY) {
            process_slot =
                (random >> 16) % MODEL_DENIED_PROCESS;
            return call_action(
                thread_slot,
                model->processes[process_slot].primary_endpoint,
                step
            );
        }
        break;
    case 3:
    case 4:
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
                if (random % 12 == 3) {
                    return reply_action(thread_slot, token, step);
                }
                return reply_receive_action(
                    thread_slot,
                    token,
                    MICROS_ENDPOINT_ANY,
                    step
                );
            }
        }
        thread_slot = find_held_full_thread(
            model,
            (random >> 8) % MODEL_THREAD_COUNT
        );
        if (thread_slot != MICROS_THREAD_CAPACITY) {
            return random % 12 == 3
                ? reply_action(
                    thread_slot,
                    UINT64_C(0xf000000000000000)
                        | (uint64_t)step,
                    step
                )
                : reply_receive_action(
                    thread_slot,
                    UINT64_C(0xf000000000000000)
                        | (uint64_t)step,
                    MICROS_ENDPOINT_ANY,
                    step
                );
        }
        break;
    case 5:
        thread_slot = find_held_full_thread(
            model,
            (random >> 8) % MODEL_THREAD_COUNT
        );
        if (thread_slot != MICROS_THREAD_CAPACITY) {
            process_slot =
                (random >> 16) % MODEL_DENIED_PROCESS;
            return notify_action(
                thread_slot,
                model->processes[process_slot].primary_endpoint,
                UINT64_C(1) << ((random >> 24) % 63)
            );
        }
        break;
    case 6:
        thread_slot = find_staged_thread(
            model,
            (random >> 8) % MODEL_THREAD_COUNT
        );
        if (thread_slot != MICROS_THREAD_CAPACITY) {
            return thread_action(MODEL_OPERATION_DRAIN, thread_slot);
        }
        break;
    case 7:
        thread_slot = find_holdable_thread(
            model,
            (random >> 8) % MODEL_THREAD_COUNT
        );
        if (thread_slot != MICROS_THREAD_CAPACITY) {
            return thread_action(MODEL_OPERATION_HOLD, thread_slot);
        }
        break;
    case 8:
        process_slot = find_safe_reuse_process(
            model,
            (random >> 8) % (
                MODEL_SINGLETON_LAST
                - MODEL_SINGLETON_FIRST
                + 1
            )
        );
        if (process_slot != MICROS_PROCESS_CAPACITY) {
            return close_reuse_action(model, process_slot);
        }
        break;
    case 9:
        return send_action(
            9,
            model->processes[0].primary_endpoint,
            step,
            false
        );
    case 10:
        thread_slot = find_held_full_thread(
            model,
            (random >> 8) % MODEL_THREAD_COUNT
        );
        if (thread_slot != MICROS_THREAD_CAPACITY) {
            if ((random & 1) == 0) {
                return notify_action(
                    thread_slot,
                    model->processes[0].primary_endpoint,
                    0
                );
            }
            {
                struct model_action action =
                    receive_action(
                        thread_slot,
                        MICROS_ENDPOINT_ANY,
                        step
                    );

                action.buffer |= 1;
                return action;
            }
        }
        break;
    case 11:
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
                    && model_thread_is_held_clear(
                        &model->threads[index]
                    )
                ) {
                    return send_action(
                        index,
                        model->processes[destination]
                            .primary_endpoint,
                        step,
                        false
                    );
                }
            }
        }
        break;
    default:
        break;
    }
    return recovery_action(model, random, step);
}

static bool coverage_is_complete(const struct model_coverage *coverage)
{
    size_t index;

    for (index = 0; index < MODEL_OPERATION_COUNT; ++index) {
        if (
            coverage->operation_count[index] == 0
            || coverage->operation_success[index] == 0
        ) {
            fprintf(
                stderr,
                "IPC model coverage missing op=%s count=%zu success=%zu\n",
                model_operation_name((enum model_operation)index),
                coverage->operation_count[index],
                coverage->operation_success[index]
            );
            return false;
        }
    }
    if (
        coverage->deadlock == 0
        || coverage->unauthorized == 0
        || coverage->argument == 0
    ) {
        fprintf(
            stderr,
            "IPC model error coverage incomplete deadlock=%zu "
            "unauthorized=%zu argument=%zu\n",
            coverage->deadlock,
            coverage->unauthorized,
            coverage->argument
        );
        return false;
    }
    return true;
}

bool micros_ipc_model_test_run(void)
{
    const uint32_t seed = UINT32_C(0x30accee5);
    struct model_state model;
    struct model_coverage coverage;
    uint32_t random_state = seed;
    size_t step = 0;
    size_t index;

    memset(&coverage, 0, sizeof(coverage));
    if (!setup_model(&model)) {
        fprintf(stderr, "IPC model setup failed seed=0x%08x\n", seed);
        return false;
    }
    for (index = 0; index < 3; ++index) {
        if (model.processes[index].live_thread_count < 2) {
            fprintf(
                stderr,
                "IPC model requires multiple threads for process %zu\n",
                index
            );
            return false;
        }
    }
    if (!run_model_prelude(
        &model,
        &step,
        seed,
        &coverage
    )) {
        return false;
    }
    while (step < MODEL_STEPS) {
        uint32_t random = model_random_next(&random_state);
        struct model_action action =
            build_random_action(&model, random, step);
        enum micros_ipc_error result;

        if (
            !execute_action(
                &model,
                &action,
                step,
                seed,
                &coverage,
                &result
            )
        ) {
            return false;
        }
        (void)result;
        ++step;
    }
    if (!coverage_is_complete(&coverage)) {
        dump_trace(seed, step, "coverage incomplete");
        return false;
    }
    printf(
        "# IPC acceptance model seed=0x%08x transitions=%zu "
        "trace=0x%016llx\n",
        seed,
        step,
        (unsigned long long)model_trace_hash
    );
    return true;
}
