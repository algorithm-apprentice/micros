#ifndef MICROS_KERNEL_IPC_TEST_INTERNAL_H
#define MICROS_KERNEL_IPC_TEST_INTERNAL_H

#include "micros/ipc_core.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "kernel/ipc/ipc_runtime_internal.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/scheduler_core.h"
#include "micros/user_address_space.h"

enum {
    IPC_TEST_CLIENT = 0,
    IPC_TEST_SERVER,
    IPC_TEST_PEER,
    IPC_TEST_PROCESS_COUNT,
    IPC_TEST_CLIENT_PROFILE = 1,
    IPC_TEST_SERVER_PROFILE = 2,
    IPC_TEST_PEER_PROFILE = 3,
};

struct ipc_test_context {
    _Alignas(4096) unsigned char ipc_test_stacks[IPC_TEST_PROCESS_COUNT][MICROS_THREAD_KERNEL_STACK_SIZE];
    struct micros_endpoint_registry *registry;
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;
    struct micros_hart hart_snapshot;
    struct micros_ipc_message receive_buffers[IPC_TEST_PROCESS_COUNT];
    struct micros_ipc_message send_messages[8];
    struct micros_user_context context_snapshots[IPC_TEST_PROCESS_COUNT];
    uintptr_t root_snapshots[IPC_TEST_PROCESS_COUNT];
};

struct ipc_test_run_state {
    struct micros_kernel_objects *objects;
    const struct micros_frame_ownership *ledger;
    struct micros_hart *boot_hart;
    struct micros_hart_handle hart;
    struct micros_process_handle processes[IPC_TEST_PROCESS_COUNT];
    struct micros_thread_handle threads[IPC_TEST_PROCESS_COUNT];
    micros_endpoint_t endpoints[IPC_TEST_PROCESS_COUNT];
    struct micros_process_handle replacement_process;
    struct micros_thread_handle replacement_thread;
    micros_endpoint_t replacement_endpoint;
    struct micros_ipc_message expected;
    struct micros_ipc_message reply;
    struct micros_ipc_message *request;
    uint64_t reply_token;
    uint64_t close_token;
    size_t baseline_processes;
    size_t baseline_threads;
    size_t baseline_harts;
    uint64_t baseline_owned;
    uint64_t baseline_free;
    uintptr_t saved_status;
    bool passed;
    size_t index;
    uint64_t failure_stage;
};

struct ipc_test_context *ipc_test_context_get(void);
bool micros_ipc_runtime_run_self_test(void);
void ipc_test_copy_bytes(void *destination, const void *source, size_t size);
bool ipc_test_bytes_equal(const void *left, const void *right, size_t size);
bool ipc_test_thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
);
bool ipc_test_thread_handle_is_zero(struct micros_thread_handle handle);
struct micros_user_context ipc_test_context_pattern(uint64_t base);
bool ipc_test_snapshot_execution_state(
    struct ipc_test_context *fixture,
    const struct micros_kernel_objects *objects,
    const struct micros_process_handle processes[IPC_TEST_PROCESS_COUNT],
    const struct micros_thread_handle threads[IPC_TEST_PROCESS_COUNT]
);
bool ipc_test_execution_state_matches(
    struct ipc_test_context *fixture,
    const struct micros_kernel_objects *objects,
    const struct micros_process_handle processes[IPC_TEST_PROCESS_COUNT],
    const struct micros_thread_handle threads[IPC_TEST_PROCESS_COUNT]
);
void ipc_test_fill_message(
    struct micros_ipc_message *message,
    uint32_t type,
    uint8_t seed
);
struct micros_ipc_message ipc_test_canonical_message(
    const struct micros_ipc_message *source,
    micros_endpoint_t source_endpoint,
    uint64_t reply_token
);
bool ipc_test_notification_matches(
    const struct micros_ipc_message *message,
    micros_endpoint_t source,
    uint64_t event_mask
);
bool ipc_test_prepare_thread(
    struct ipc_test_context *fixture,
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    struct micros_process_handle process,
    size_t stack_index,
    struct micros_thread_handle *thread
);
bool ipc_test_hold_thread(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread
);
bool ipc_test_consume_staged(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread,
    enum micros_ipc_error expected_result,
    const struct micros_ipc_message *expected_message
);
void ipc_test_snapshot_state(struct ipc_test_context *fixture, const struct micros_kernel_objects *objects);
bool ipc_test_state_matches_snapshot(
    struct ipc_test_context *fixture,
    const struct micros_kernel_objects *objects
);
bool ipc_test_ready_queue_matches(
    const struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    const struct micros_thread_handle *threads,
    size_t thread_count
);
bool ipc_test_endpoint_records_are_clear(struct ipc_test_context *fixture);
bool ipc_test_cleanup_thread(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread
);
bool ipc_test_prepare_environment(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
);
bool ipc_test_immediate_send(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
);
bool ipc_test_blocked_send(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
);
bool ipc_test_call_reply_and_stale_token(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
);
bool ipc_test_prepare_combined_reply(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
);
bool ipc_test_reply_receive_and_peer_send(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
);
bool ipc_test_finish_combined_delivery(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
);
bool ipc_test_notification_reply_exclusion(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
);
bool ipc_test_kernel_notifications(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
);
bool ipc_test_deadlock_and_matching_recovery(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
);
bool ipc_test_close_cancellation(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
);
bool ipc_test_generation_reuse_and_stale_identity(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
);
bool ipc_test_cleanup_resources(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
);
bool ipc_test_validate_final_baseline(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
);
bool ipc_test_report_result(const struct ipc_test_run_state *state);

#endif
