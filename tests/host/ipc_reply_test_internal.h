#ifndef MICROS_TESTS_HOST_IPC_REPLY_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_IPC_REPLY_TEST_INTERNAL_H

#include "micros/ipc_core.h"

#include <stdio.h>
#include <string.h>

enum {
    REPLY_PROCESS_CLIENT = 0,
    REPLY_PROCESS_SERVER,
    REPLY_PROCESS_OTHER_SERVER,
    REPLY_PROCESS_NO_REPLY_SERVER,
    REPLY_PROCESS_REPLY_ONLY_SERVER,
    REPLY_PROCESS_REPLY_RECEIVE_ONLY_SERVER,
    REPLY_PROCESS_RESERVED_SERVER,
    REPLY_PROCESS_COUNT,
};

struct ipc_reply_test_context {
    struct micros_endpoint_registry registry;
    struct micros_kernel_objects objects;
    struct micros_process_handle processes[REPLY_PROCESS_COUNT];
    struct micros_thread_handle primary_threads[REPLY_PROCESS_COUNT];
    struct micros_thread_handle client_extra_thread;
    struct micros_thread_handle server_extra_thread;
    micros_endpoint_t endpoints[REPLY_PROCESS_COUNT];
    struct micros_hart_handle hart;
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

struct ipc_reply_test_context *ipc_reply_test_context_get(void);
bool micros_ipc_reply_test_run(void);
bool ipc_reply_test_thread_handle_is_zero(struct micros_thread_handle handle);
struct micros_ipc_message ipc_reply_test_message_pattern(
    uint32_t source,
    uint32_t type,
    uint64_t reply_token,
    uint8_t payload_seed
);
struct micros_ipc_message ipc_reply_test_canonical_reply(
    const struct micros_ipc_message *message,
    micros_endpoint_t source
);
bool ipc_reply_test_setup_reply_fixture(struct ipc_reply_test_context *fixture);
bool ipc_reply_test_prepare_server_extra_thread(struct ipc_reply_test_context *fixture);
bool ipc_reply_test_clear_staged_request(
    struct ipc_reply_test_context *fixture,
    struct micros_thread_handle receiver_handle,
    struct micros_ipc_message *delivered
);
bool ipc_reply_test_stage_delivered_call(
    struct ipc_reply_test_context *fixture,
    struct micros_thread_handle caller,
    size_t server_index,
    struct micros_ipc_message *request,
    uintptr_t receive_buffer,
    uint64_t *reply_token
);
bool ipc_reply_test_prepare_delivered_call(
    struct ipc_reply_test_context *fixture,
    struct micros_thread_handle caller,
    size_t server_index,
    struct micros_ipc_message *request,
    uintptr_t receive_buffer,
    uint64_t *reply_token
);
bool ipc_reply_test_complete_state_is_unchanged(
    struct ipc_reply_test_context *fixture,
    const struct micros_endpoint_registry *registry_snapshot,
    const struct micros_kernel_objects *objects_snapshot
);
bool ipc_reply_test_expect_reply_failure_unchanged(
    struct ipc_reply_test_context *fixture,
    enum micros_ipc_error expected,
    struct micros_thread_handle replier,
    uint64_t reply_token,
    const struct micros_ipc_message *message
);
bool ipc_reply_test_expect_reply_receive_failure_unchanged(
    struct ipc_reply_test_context *fixture,
    enum micros_ipc_error expected,
    struct micros_thread_handle replier,
    uint64_t reply_token,
    const struct micros_ipc_message *reply_message,
    micros_endpoint_t source,
    uintptr_t receive_buffer
);
bool ipc_reply_test_expect_send_failure_unchanged(
    struct ipc_reply_test_context *fixture,
    enum micros_ipc_error expected,
    struct micros_thread_handle sender,
    micros_endpoint_t destination,
    const struct micros_ipc_message *message
);
bool test_reply_requires_request_return(struct ipc_reply_test_context *fixture);
bool test_staged_call_tokens_are_unique(struct ipc_reply_test_context *fixture);
bool test_reply_routes_exact_tokens_without_send_target(struct ipc_reply_test_context *fixture);
bool test_reply_preserves_independent_caller_flags(struct ipc_reply_test_context *fixture);
bool test_reply_rejections_are_atomic(struct ipc_reply_test_context *fixture);
bool ipc_reply_test_make_reply_wait_current(
    struct ipc_reply_test_context *fixture,
    struct micros_thread_handle caller_handle
);
bool ipc_reply_test_make_single_queued_sender_current(
    struct ipc_reply_test_context *fixture,
    struct micros_thread_handle sender_handle,
    size_t destination_index
);
bool test_reply_scheduler_failure_preserves_token(struct ipc_reply_test_context *fixture);
bool test_reply_rejects_source_only_caller(struct ipc_reply_test_context *fixture);
bool test_reply_receive_blocks_and_later_receives(struct ipc_reply_test_context *fixture);
bool test_reply_receive_selects_specific_sender_atomically(struct ipc_reply_test_context *fixture);
bool test_reply_receive_requires_combined_operation(struct ipc_reply_test_context *fixture);
bool test_reply_receive_rejections_are_atomic(struct ipc_reply_test_context *fixture);
bool test_reply_receive_scheduler_failures_are_atomic(struct ipc_reply_test_context *fixture);

#endif
