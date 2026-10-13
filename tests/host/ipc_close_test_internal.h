#ifndef MICROS_TESTS_HOST_IPC_CLOSE_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_IPC_CLOSE_TEST_INTERNAL_H

#include "micros/ipc_core.h"

#include <stdio.h>
#include <string.h>

enum {
    CLOSE_PROCESS_CLOSING = 0,
    CLOSE_PROCESS_FOREIGN_SEND,
    CLOSE_PROCESS_FOREIGN_CALL_QUEUED,
    CLOSE_PROCESS_FOREIGN_CALL_DELIVERED,
    CLOSE_PROCESS_FOREIGN_SPECIFIC,
    CLOSE_PROCESS_FOREIGN_ANY,
    CLOSE_PROCESS_FOREIGN_DESTINATION,
    CLOSE_PROCESS_NOTIFY_DESTINATION,
    CLOSE_PROCESS_NOTIFY_SOURCE,
    CLOSE_PROCESS_STAGED_DESTINATION,
    CLOSE_PROCESS_WAIT_SOURCE,
    CLOSE_PROCESS_COUNT,
    CLOSE_EXTRA_THREAD_COUNT = 4,
    CLOSE_SCENARIO_CASE_COUNT = 512,
    CLOSE_SCENARIO_TRACE_COUNT = 16,
};

enum close_scenario_operation {
    CLOSE_SCENARIO_FOREIGN_SEND = 0,
    CLOSE_SCENARIO_FOREIGN_CALL,
    CLOSE_SCENARIO_SPECIFIC_RECEIVE,
    CLOSE_SCENARIO_ANY_RECEIVE,
    CLOSE_SCENARIO_CLOSING_SEND,
    CLOSE_SCENARIO_STAGED_SEND,
    CLOSE_SCENARIO_NOTIFICATION_FROM_CLOSING,
    CLOSE_SCENARIO_NOTIFICATION_TO_CLOSING,
    CLOSE_SCENARIO_OPERATION_COUNT,
};

struct close_scenario_trace_entry {
    size_t case_index;
    enum close_scenario_operation operation;
};

struct ipc_close_test_context {
    struct micros_endpoint_registry registry;
    struct micros_kernel_objects objects;
    struct micros_process_handle processes[MICROS_PROCESS_CAPACITY];
    struct micros_thread_handle primary_threads[MICROS_PROCESS_CAPACITY];
    micros_endpoint_t endpoints[MICROS_PROCESS_CAPACITY];
    struct micros_hart_handle hart;
    struct close_scenario_trace_entry close_scenario_trace[CLOSE_SCENARIO_TRACE_COUNT];
    size_t close_scenario_trace_count;
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

#define EXPECT_OBJECT_ERROR(expected, expression) \
    do { \
        enum micros_kernel_object_error actual = (expression); \
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

struct ipc_close_test_context *ipc_close_test_context_get(void);
bool micros_ipc_close_model_test_run(void);
bool micros_ipc_close_test_run(void);
bool ipc_close_test_thread_handle_is_zero(struct micros_thread_handle handle);
struct micros_ipc_message ipc_close_test_message_pattern(
    uint32_t type,
    uint8_t payload_seed
);
bool ipc_close_test_prepare_thread(
    struct ipc_close_test_context *fixture,
    size_t process_index,
    uint8_t priority,
    struct micros_thread_handle *thread
);
bool ipc_close_test_setup_fixture(
    struct ipc_close_test_context *fixture,
    size_t process_count,
    size_t max_threads_per_process
);
bool ipc_close_test_hold_thread_for_close(struct ipc_close_test_context *fixture, struct micros_thread_handle thread);
bool ipc_close_test_thread_has_dead_endpoint_result(
    struct ipc_close_test_context *fixture,
    struct micros_thread_handle handle
);
bool ipc_close_test_closing_thread_is_detached(
    struct ipc_close_test_context *fixture,
    struct micros_thread_handle handle
);
bool ipc_close_test_clear_staged_delivery(
    struct ipc_close_test_context *fixture,
    struct micros_thread_handle handle
);
bool ipc_close_test_endpoint_queue_is_empty(
    const struct micros_endpoint_record *endpoint
);
bool ipc_close_test_expect_close_failure_unchanged(
    struct ipc_close_test_context *fixture,
    enum micros_ipc_error expected,
    micros_endpoint_t endpoint
);
bool ipc_close_test_make_single_sender_current_blocked(
    struct ipc_close_test_context *fixture,
    struct micros_thread_handle sender,
    micros_endpoint_t destination
);
bool test_close_cancels_complete_transaction(struct ipc_close_test_context *fixture);
bool test_cascading_close_discards_owned_dead_endpoint_result(struct ipc_close_test_context *fixture);
bool test_source_only_close_cancels_queued_sender(struct ipc_close_test_context *fixture);
bool test_source_only_notification_dependency_blocks_strict_close(struct ipc_close_test_context *fixture);
bool test_successful_staged_deliveries_block_close_atomically(struct ipc_close_test_context *fixture);
bool test_close_failures_are_atomic(struct ipc_close_test_context *fixture);
bool test_seeded_close_scenarios(struct ipc_close_test_context *fixture);

#endif
