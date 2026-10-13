#ifndef MICROS_TESTS_HOST_IPC_MODEL_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_IPC_MODEL_TEST_INTERNAL_H

#include "micros/ipc_core.h"

#include <stdio.h>
#include <string.h>

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



enum model_operation {
    MODEL_OPERATION_SEND = 0,
    MODEL_OPERATION_RECEIVE,
    MODEL_OPERATION_CALL,
    MODEL_OPERATION_REPLY,
    MODEL_OPERATION_REPLY_RECEIVE,
    MODEL_OPERATION_NOTIFY,
    MODEL_OPERATION_KERNEL_NOTIFY,
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
    uint64_t pending_kernel_events;
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

struct ipc_model_test_context {
    struct micros_endpoint_registry registry;
    struct micros_kernel_objects objects;
    struct micros_ipc_message messages[MODEL_MESSAGE_COUNT];
    struct model_trace_entry model_trace[MODEL_TRACE_COUNT];
    size_t model_trace_count;
    uint64_t model_trace_hash;
};

struct ipc_model_test_context *ipc_model_test_context_get(void);
bool micros_ipc_model_test_run(void);
struct micros_thread_handle ipc_model_null_thread_handle(void);
bool ipc_model_thread_handle_is_zero(struct micros_thread_handle handle);
bool ipc_model_thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
);
bool ipc_model_process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
);
struct micros_thread_handle ipc_model_thread_handle(
    const struct model_state *model,
    size_t slot
);
struct micros_user_context ipc_model_context_pattern(uint64_t base);
struct micros_ipc_message *ipc_model_message_for_step(
    struct ipc_model_test_context *fixture,
    size_t step,
    bool kernel_type
);
uintptr_t ipc_model_receive_buffer_for_step(size_t step, size_t thread_slot);
const char *ipc_model_operation_name(enum model_operation operation);
void ipc_model_record_trace(
    struct ipc_model_test_context *fixture,
    size_t step,
    const struct model_action *action,
    enum micros_ipc_error result
);
void ipc_model_dump_trace(struct ipc_model_test_context *fixture, uint32_t seed, size_t step, const char *detail);
void ipc_model_dump_state(struct ipc_model_test_context *fixture, const struct model_state *model);
bool ipc_model_messages_equal(
    const struct micros_ipc_message *left,
    const struct micros_ipc_message *right
);
bool ipc_model_matches_production(
    struct ipc_model_test_context *fixture,
    const struct model_state *model,
    size_t step
);
bool ipc_model_setup_model(struct ipc_model_test_context *fixture, struct model_state *model);
bool ipc_model_thread_is_held_clear(
    const struct model_thread *thread
);
enum micros_ipc_error ipc_model_apply_transitions(
    struct model_state *model,
    const struct model_transition *transitions,
    size_t transition_count
);
enum micros_ipc_error ipc_model_hold_thread(
    struct model_state *model,
    size_t thread_slot
);
void ipc_model_clear_delivered_sender(struct model_thread *sender);
void ipc_model_clear_ipc_state(struct model_thread *thread);
void ipc_model_stage_delivery(
    struct model_thread *receiver,
    uintptr_t receive_buffer,
    const struct micros_ipc_message *message
);
enum micros_ipc_error ipc_model_drain_thread(
    struct model_state *model,
    size_t thread_slot
);
bool ipc_model_resolve_active_endpoint(
    const struct model_state *model,
    micros_endpoint_t endpoint,
    size_t *slot
);
enum micros_ipc_error ipc_model_resolve_operation_thread(
    const struct model_state *model,
    size_t thread_slot,
    micros_endpoint_t *source_endpoint
);
enum micros_ipc_error ipc_model_authorize_operation(
    const struct model_state *model,
    micros_endpoint_t source,
    uint32_t operation
);
enum micros_ipc_error ipc_model_authorize_target(
    const struct model_state *model,
    micros_endpoint_t source,
    uint32_t operation,
    micros_endpoint_t destination
);
void ipc_model_canonicalize_message(
    struct micros_ipc_message *destination,
    const struct micros_ipc_message *source,
    micros_endpoint_t source_endpoint,
    uint64_t reply_token
);
void ipc_model_canonicalize_notification(
    struct micros_ipc_message *message,
    micros_endpoint_t source,
    uint64_t event_mask
);
void ipc_model_ipc_append(
    struct model_state *model,
    struct micros_thread_handle *head,
    struct micros_thread_handle *tail,
    size_t thread_slot
);
void ipc_model_ipc_unlink(
    struct model_state *model,
    struct micros_thread_handle *head,
    struct micros_thread_handle *tail,
    struct micros_thread_handle previous,
    size_t thread_slot
);
bool ipc_model_find_receiver(
    const struct model_state *model,
    size_t endpoint_slot,
    micros_endpoint_t source,
    struct micros_thread_handle *previous,
    size_t *receiver_slot
);
bool ipc_model_find_sender(
    const struct model_state *model,
    size_t endpoint_slot,
    micros_endpoint_t source,
    struct micros_thread_handle *previous,
    size_t *sender_slot
);
bool ipc_model_find_pending_notification(
    const struct model_state *model,
    size_t destination_slot,
    micros_endpoint_t source,
    size_t *source_slot,
    uint64_t *event_mask
);
enum micros_ipc_error ipc_model_deadlock_preflight(
    const struct model_state *model,
    size_t candidate_slot,
    micros_endpoint_t dependency,
    size_t cleared_slot
);
enum micros_ipc_error ipc_model_send(
    struct model_state *model,
    size_t sender_slot,
    micros_endpoint_t destination_endpoint,
    const struct micros_ipc_message *message
);
enum micros_ipc_error ipc_model_receive(
    struct model_state *model,
    size_t receiver_slot,
    micros_endpoint_t source_endpoint,
    uintptr_t receive_buffer
);
enum micros_ipc_error ipc_model_call(
    struct model_state *model,
    size_t caller_slot,
    micros_endpoint_t destination_endpoint,
    struct micros_ipc_message *message
);
enum micros_ipc_error ipc_model_reply(
    struct model_state *model,
    size_t replier_slot,
    uint64_t reply_token,
    const struct micros_ipc_message *message
);
enum micros_ipc_error ipc_model_reply_receive(
    struct model_state *model,
    size_t replier_slot,
    uint64_t reply_token,
    const struct micros_ipc_message *reply_message,
    micros_endpoint_t source_endpoint,
    uintptr_t receive_buffer
);
enum micros_ipc_error ipc_model_notify(
    struct model_state *model,
    size_t notifier_slot,
    micros_endpoint_t destination_endpoint,
    uint64_t event_mask
);
enum micros_ipc_error ipc_model_kernel_notify(
    struct model_state *model,
    micros_endpoint_t destination_endpoint,
    uint64_t event_mask
);
enum micros_ipc_error ipc_model_close_reuse(
    struct model_state *model,
    size_t process_slot
);
enum micros_ipc_error ipc_model_reference_execute_action(
    struct model_state *model,
    const struct model_action *action
);
enum micros_ipc_error ipc_model_production_execute_action(
    struct ipc_model_test_context *fixture,
    const struct model_action *action
);
bool ipc_model_execute_step(
    struct ipc_model_test_context *fixture,
    struct model_state *model,
    const struct model_action *action,
    size_t step,
    uint32_t seed,
    struct model_coverage *coverage,
    enum micros_ipc_error *result
);
bool ipc_model_run_checked_action(
    struct ipc_model_test_context *fixture,
    struct model_state *model,
    struct model_action action,
    enum micros_ipc_error expected,
    size_t *step,
    uint32_t seed,
    struct model_coverage *coverage
);
struct model_action ipc_model_receive_action(
    size_t thread_slot,
    micros_endpoint_t source,
    size_t step
);
struct model_action ipc_model_send_action(
    struct ipc_model_test_context *fixture,
    size_t thread_slot,
    micros_endpoint_t destination,
    size_t step,
    bool kernel_type
);
struct model_action ipc_model_call_action(
    struct ipc_model_test_context *fixture,
    size_t thread_slot,
    micros_endpoint_t destination,
    size_t step
);
struct model_action ipc_model_reply_action(
    struct ipc_model_test_context *fixture,
    size_t thread_slot,
    uint64_t token,
    size_t step
);
struct model_action ipc_model_reply_receive_action(
    struct ipc_model_test_context *fixture,
    size_t thread_slot,
    uint64_t token,
    micros_endpoint_t source,
    size_t step
);
struct model_action ipc_model_notify_action(
    size_t thread_slot,
    micros_endpoint_t destination,
    uint64_t event_mask
);
struct model_action ipc_model_kernel_notify_action(
    micros_endpoint_t destination,
    uint64_t event_mask
);
struct model_action ipc_model_thread_action(
    enum model_operation operation,
    size_t thread_slot
);
struct model_action ipc_model_close_reuse_action(
    const struct model_state *model,
    size_t process_slot
);
bool ipc_model_run_model_prelude(
    struct ipc_model_test_context *fixture,
    struct model_state *model,
    size_t *step,
    uint32_t seed,
    struct model_coverage *coverage
);
struct model_action ipc_model_build_random_action(
    struct ipc_model_test_context *fixture,
    const struct model_state *model,
    uint32_t random,
    size_t step
);

#endif
