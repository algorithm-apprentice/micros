#ifndef MICROS_TESTS_HOST_IPC_CALL_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_IPC_CALL_TEST_INTERNAL_H

#include "micros/ipc_core.h"

#include <stdio.h>
#include <string.h>

enum {
    CALL_PROCESS_CLIENT = 0,
    CALL_PROCESS_SERVER,
    CALL_PROCESS_NO_CALL,
    CALL_PROCESS_ISOLATED_CALL,
    CALL_PROCESS_RESERVED,
    CALL_PROCESS_COUNT,
};

struct ipc_call_test_context {
    struct micros_endpoint_registry registry;
    struct micros_kernel_objects objects;
    struct micros_process_handle processes[CALL_PROCESS_COUNT];
    struct micros_thread_handle primary_threads[CALL_PROCESS_COUNT];
    struct micros_thread_handle client_extra_thread;
    micros_endpoint_t endpoints[CALL_PROCESS_COUNT];
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

struct ipc_call_test_context *ipc_call_test_context_get(void);
bool micros_ipc_call_test_run(void);
bool ipc_call_test_thread_handle_is_zero(struct micros_thread_handle handle);
bool ipc_call_test_thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
);
bool ipc_call_test_bytes_are_zero(const void *storage, size_t size);
struct micros_ipc_message ipc_call_test_message_pattern(
    uint32_t source,
    uint32_t type,
    uint64_t reply_token,
    uint8_t payload_seed
);
struct micros_ipc_message ipc_call_test_canonical_request(
    const struct micros_ipc_message *message,
    micros_endpoint_t source,
    uint64_t reply_token
);
bool ipc_call_test_setup_call_fixture(struct ipc_call_test_context *fixture);
bool ipc_call_test_complete_state_is_unchanged(
    struct ipc_call_test_context *fixture,
    const struct micros_endpoint_registry *registry_snapshot,
    const struct micros_kernel_objects *objects_snapshot
);
bool ipc_call_test_expect_call_failure_unchanged(
    struct ipc_call_test_context *fixture,
    enum micros_ipc_error expected,
    struct micros_thread_handle caller,
    micros_endpoint_t destination,
    struct micros_ipc_message *message
);
bool ipc_call_test_make_current_held(struct ipc_call_test_context *fixture, struct micros_thread_handle thread);
bool test_call_delivers_to_waiting_receiver(struct ipc_call_test_context *fixture);
bool test_call_preserves_independent_receiver_flags(struct ipc_call_test_context *fixture);
bool test_queued_calls_bind_exact_threads_and_deliver_fifo(struct ipc_call_test_context *fixture);
bool test_low_level_enqueue_rejects_reply_tokens(struct ipc_call_test_context *fixture);
bool test_token_exhaustion_is_preflighted(struct ipc_call_test_context *fixture);
bool test_call_rejections_are_atomic(struct ipc_call_test_context *fixture);
bool test_staged_call_token_binding_is_validated(struct ipc_call_test_context *fixture);
bool test_allocator_validator_rejects_unissued_token(struct ipc_call_test_context *fixture);

#endif
