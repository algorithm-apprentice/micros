#ifndef MICROS_TESTS_HOST_PM_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_PM_TEST_INTERNAL_H

#include <stdio.h>
#include <string.h>

#include "servers/pm/pm_core.h"

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

void pm_test_write_u32_le(uint8_t *bytes, uint32_t value);
void pm_test_write_u64_le(uint8_t *bytes, uint64_t value);
uint32_t pm_test_read_u32_le(const uint8_t *bytes);
uint64_t pm_test_read_u64_le(const uint8_t *bytes);
bool pm_test_handles_equal(
    struct micros_pm_process_handle left,
    struct micros_pm_process_handle right
);
bool pm_test_handle_is_none(struct micros_pm_process_handle handle);
bool pm_test_bytes_are_zero(const void *storage, size_t size);
struct micros_ipc_message pm_test_exit_request(int32_t exit_code);
struct micros_ipc_message pm_test_wait_request(uint32_t flags, uint64_t child_pid);
struct micros_ipc_message pm_test_spawn_request(uint32_t grant, uint32_t request_size);
struct micros_ipc_message pm_test_sealed_event(void);
bool pm_test_complete_spawn(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    micros_endpoint_t endpoint,
    uint64_t expected_reply_token
);
bool pm_test_initialize_running_init(
    struct micros_pm_table *table,
    struct micros_pm_process_handle *init
);
uint32_t pm_test_expected_resources(enum micros_pm_spawn_stage stage);
bool pm_test_advance_to_stage(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    enum micros_pm_spawn_stage stage
);
enum micros_pm_spawn_rollback_resource pm_test_rollback_resource_for_stage(
    enum micros_pm_spawn_stage stage
);
uint32_t pm_test_rollback_resource_bit(enum micros_pm_spawn_rollback_resource resource);
bool pm_test_rollback_spawn(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    enum micros_pm_spawn_stage stage,
    uint64_t parent_reply_token,
    bool parent_lost,
    uint32_t *released_resources
);
bool pm_test_spawn_running_child(
    struct micros_pm_table *table,
    struct micros_pm_process_handle parent,
    micros_endpoint_t endpoint,
    struct micros_pm_process_handle *child,
    uint64_t *pid
);
bool pm_test_complete_exit(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    uint64_t transaction,
    int32_t exit_code,
    struct micros_pm_wait_outcomes *outcomes
);

bool test_runtime_gate_and_spawn(void);
bool test_protocol_contract(void);
bool test_reverse_rollback_and_identity(void);
bool test_rollback_acknowledgment_identity(void);
bool test_generation_and_pid_exhaustion(void);
bool test_failed_init_is_terminal(void);
bool test_orphaned_lifecycle_is_invalid(void);
bool test_endpoint_lookup_and_failure_preservation(void);
bool test_wait_selection(void);
bool test_exit_wait_and_reply_restore(void);
bool test_nested_parent_exit_stages_independent_waits(void);
bool test_parent_exit_reparents_and_aborts(void);
bool test_replayable_transition_model(void);

#endif
