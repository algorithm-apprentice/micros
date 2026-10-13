#ifndef MICROS_TESTS_HOST_IPC_OPERATION_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_IPC_OPERATION_TEST_INTERNAL_H

#include "micros/ipc_core.h"

#include <stdio.h>
#include <string.h>

enum {
    OPERATION_PROCESS_SOURCE_A = 0,
    OPERATION_PROCESS_SOURCE_B,
    OPERATION_PROCESS_DESTINATION,
    OPERATION_PROCESS_NO_SEND,
    OPERATION_PROCESS_ISOLATED_SEND,
    OPERATION_PROCESS_RESERVED,
    OPERATION_PROCESS_COUNT,
};

struct ipc_operation_test_context {
    struct micros_endpoint_registry registry;
    struct micros_kernel_objects objects;
    struct micros_process_handle processes[OPERATION_PROCESS_COUNT];
    struct micros_thread_handle primary_threads[OPERATION_PROCESS_COUNT];
    struct micros_thread_handle destination_extra_thread;
    micros_endpoint_t endpoints[OPERATION_PROCESS_COUNT];
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

struct ipc_operation_test_context *ipc_operation_test_context_get(void);
bool micros_ipc_operation_test_run(void);
bool ipc_operation_test_thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
);
struct micros_ipc_message ipc_operation_test_message_pattern(
    uint32_t source,
    uint32_t type,
    uint64_t reply_token,
    uint8_t payload_seed
);
struct micros_ipc_message ipc_operation_test_canonical_message(
    const struct micros_ipc_message *message,
    micros_endpoint_t source
);
bool ipc_operation_test_setup_operation_fixture(struct ipc_operation_test_context *context);
bool ipc_operation_test_prepare_destination_extra_thread(struct ipc_operation_test_context *context);
bool ipc_operation_test_expect_send_failure_unchanged(
    struct ipc_operation_test_context *context,
    enum micros_ipc_error expected,
    struct micros_thread_handle sender,
    micros_endpoint_t destination,
    const struct micros_ipc_message *message
);
bool ipc_operation_test_expect_receive_failure_unchanged(
    struct ipc_operation_test_context *context,
    enum micros_ipc_error expected,
    struct micros_thread_handle receiver,
    micros_endpoint_t source,
    uintptr_t receive_buffer
);
bool ipc_operation_test_make_current_held(
    struct ipc_operation_test_context *context,
    struct micros_thread_handle thread
);
bool test_authorized_send_blocks_with_snapshot(struct ipc_operation_test_context *context);
bool test_authorized_send_matches_first_receiver(struct ipc_operation_test_context *context);
bool test_send_rejections_are_atomic(struct ipc_operation_test_context *context);
bool test_authorized_receive_blocks(struct ipc_operation_test_context *context);
bool test_receive_specific_and_any_fifo(struct ipc_operation_test_context *context);
bool test_receive_rejections_are_atomic(struct ipc_operation_test_context *context);

#endif
