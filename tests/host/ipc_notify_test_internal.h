#ifndef MICROS_TESTS_HOST_IPC_NOTIFY_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_IPC_NOTIFY_TEST_INTERNAL_H

#include "micros/ipc_core.h"

#include <stdio.h>
#include <string.h>

enum {
    NOTIFY_PROCESS_SOURCE_LOW = 0,
    NOTIFY_PROCESS_SOURCE_HIGH,
    NOTIFY_PROCESS_DESTINATION,
    NOTIFY_PROCESS_SENDER,
    NOTIFY_PROCESS_SERVER,
    NOTIFY_PROCESS_NO_NOTIFY,
    NOTIFY_PROCESS_ISOLATED_NOTIFY,
    NOTIFY_PROCESS_RESERVED,
    NOTIFY_PROCESS_COUNT,
    NOTIFY_MODEL_STEPS = 4096,
    NOTIFY_MODEL_TRACE_COUNT = 32,
};

enum notify_model_operation {
    NOTIFY_MODEL_NOTIFY = 0,
    NOTIFY_MODEL_KERNEL_NOTIFY,
    NOTIFY_MODEL_RECEIVE_ANY,
    NOTIFY_MODEL_RECEIVE_SPECIFIC,
    NOTIFY_MODEL_ZERO_REJECTED,
    NOTIFY_MODEL_OPERATION_REJECTED,
    NOTIFY_MODEL_TARGET_REJECTED,
    NOTIFY_MODEL_OPERATION_COUNT,
};

struct notify_model_trace_entry {
    size_t step;
    enum notify_model_operation operation;
    size_t source;
    uint64_t event_mask;
};

struct notify_model_coverage {
    size_t notified;
    size_t coalesced;
    size_t kernel_notified;
    size_t kernel_coalesced;
    size_t kernel_received_any;
    size_t received_any;
    size_t received_specific;
    size_t zero_rejected;
    size_t operation_rejected;
    size_t target_rejected;
};

struct ipc_notify_test_context {
    struct micros_endpoint_registry registry;
    struct micros_kernel_objects objects;
    struct micros_process_handle processes[NOTIFY_PROCESS_COUNT];
    struct micros_thread_handle primary_threads[NOTIFY_PROCESS_COUNT];
    struct micros_thread_handle destination_extra_threads[2];
    micros_endpoint_t endpoints[NOTIFY_PROCESS_COUNT];
    struct micros_hart_handle hart;
    struct notify_model_trace_entry notify_model_trace[NOTIFY_MODEL_TRACE_COUNT];
    size_t notify_model_trace_count;
};

#define EXPECT_TRUE(expression) \
    do { \
        if (!(expression)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %s\n", \
                __FILE__, \
                __LINE__, \
                #expression \
            ); \
            return false; \
        } \
    } while (false)

#define EXPECT_IPC_ERROR(expected, expression) \
    do { \
        enum micros_ipc_error actual = (expression); \
        if (actual != (expected)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %d got %d\n", \
                __FILE__, \
                __LINE__, \
                (int)(expected), \
                (int)actual \
            ); \
            return false; \
        } \
    } while (false)

#define EXPECT_ENDPOINT_ERROR(expected, expression) \
    do { \
        enum micros_endpoint_error actual = (expression); \
        if (actual != (expected)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %d got %d\n", \
                __FILE__, \
                __LINE__, \
                (int)(expected), \
                (int)actual \
            ); \
            return false; \
        } \
    } while (false)

struct ipc_notify_test_context *ipc_notify_test_context_get(void);
bool micros_ipc_notify_model_test_run(void);
bool micros_ipc_notify_test_run(void);
bool ipc_notify_test_thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
);
bool ipc_notify_test_thread_handle_is_zero(
    struct micros_thread_handle handle
);
struct micros_ipc_message ipc_notify_test_message_pattern(
    uint32_t source,
    uint32_t type,
    uint64_t reply_token,
    uint8_t payload_seed
);
bool ipc_notify_test_setup_notify_fixture(struct ipc_notify_test_context *fixture);
bool ipc_notify_test_prepare_destination_extra_threads(struct ipc_notify_test_context *fixture);
bool ipc_notify_test_state_is_unchanged(
    struct ipc_notify_test_context *fixture,
    const struct micros_endpoint_registry *registry_snapshot,
    const struct micros_kernel_objects *objects_snapshot
);
bool ipc_notify_test_expect_notify_failure_unchanged(
    struct ipc_notify_test_context *fixture,
    enum micros_ipc_error expected,
    struct micros_thread_handle notifier,
    micros_endpoint_t destination,
    uint64_t event_mask
);
bool ipc_notify_test_expect_kernel_notify_failure_unchanged(
    struct ipc_notify_test_context *fixture,
    enum micros_ipc_error expected,
    micros_endpoint_t destination,
    uint64_t event_mask
);
bool ipc_notify_test_notification_message_matches(
    const struct micros_ipc_message *message,
    micros_endpoint_t source,
    uint64_t event_mask
);
bool ipc_notify_test_clear_staged_delivery(
    struct ipc_notify_test_context *fixture,
    struct micros_thread_handle receiver_handle,
    struct micros_ipc_message *delivered
);
bool ipc_notify_test_hold_runnable_thread(struct ipc_notify_test_context *fixture, struct micros_thread_handle thread_handle);
bool ipc_notify_test_consume_staged_and_hold(
    struct ipc_notify_test_context *fixture,
    struct micros_thread_handle receiver_handle,
    struct micros_ipc_message *delivered
);
bool ipc_notify_test_make_current_held(struct ipc_notify_test_context *fixture, struct micros_thread_handle thread_handle);
bool ipc_notify_test_make_reply_wait_current(
    struct ipc_notify_test_context *fixture,
    struct micros_thread_handle caller_handle
);
bool ipc_notify_test_make_single_receiver_current_blocked(
    struct ipc_notify_test_context *fixture,
    struct micros_thread_handle receiver_handle
);
bool test_notify_coalesces_and_never_blocks(struct ipc_notify_test_context *fixture);
bool test_kernel_notify_coalesces_and_any_receives(struct ipc_notify_test_context *fixture);
bool test_kernel_notify_wakes_only_any_receiver(struct ipc_notify_test_context *fixture);
bool test_kernel_notify_prepare_commit_is_retained(struct ipc_notify_test_context *fixture);
bool test_kernel_notify_rejections_are_atomic(struct ipc_notify_test_context *fixture);
bool test_kernel_notify_validation_and_close_rules(struct ipc_notify_test_context *fixture);
bool test_notify_wakes_first_matching_receiver(struct ipc_notify_test_context *fixture);
bool test_call_reply_wait_does_not_consume_notification(struct ipc_notify_test_context *fixture);
bool test_receive_prefers_lowest_notification_before_sender(struct ipc_notify_test_context *fixture);
bool test_reply_receive_prefers_pending_notification(struct ipc_notify_test_context *fixture);
bool test_reply_receive_pending_failure_is_atomic(struct ipc_notify_test_context *fixture);
bool test_notification_validators_and_close_preflight(struct ipc_notify_test_context *fixture);
bool test_notify_rejections_preserve_state(struct ipc_notify_test_context *fixture);
bool test_notify_scheduler_failure_is_atomic(struct ipc_notify_test_context *fixture);
bool test_seeded_notify_model(struct ipc_notify_test_context *fixture);
bool test_staged_kernel_notification_is_detected(struct ipc_notify_test_context *fixture);

#endif
