#ifndef MICROS_HOST_SCHEDULER_TEST_INTERNAL_H
#define MICROS_HOST_SCHEDULER_TEST_INTERNAL_H

#include "micros/scheduler_core.h"

#include <stdio.h>
#include <string.h>

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

#define EXPECT_ERROR(expected, expression) \
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

#define EXPECT_UNCHANGED(expected, expression) \
    do { \
        struct micros_kernel_objects before = fixture->objects; \
        EXPECT_ERROR((expected), (expression)); \
        EXPECT_TRUE(memcmp(&fixture->objects, &before, sizeof(fixture->objects)) == 0); \
    } while (false)

enum {
    TEST_THREAD_COUNT = 8,
};

struct scheduler_host_fixture {
    struct micros_kernel_objects objects;
    struct micros_process_handle process;
    struct micros_thread_handle threads[TEST_THREAD_COUNT];
    struct micros_hart_handle harts[2];
};

struct scheduler_host_fixture *scheduler_host_fixture(void);
bool scheduler_host_setup_fixture(struct scheduler_host_fixture *fixture);
bool scheduler_host_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
);

bool scheduler_host_test_priority_queues_and_tail_order(void);
bool scheduler_host_test_runtime_flag_zero_boundary(void);
bool scheduler_host_test_policy_replacement_is_atomic(void);
bool scheduler_host_test_current_selection_and_preemption_repair(void);
bool scheduler_host_test_quantum_rotation_and_nonpreemptible_current(void);
bool scheduler_host_test_current_block_and_stale_plan_are_atomic(void);
bool scheduler_host_test_accounting_owners_are_separate(void);
bool scheduler_host_test_trap_accounting_uses_minimal_route(void);
bool scheduler_host_test_corrupt_queue_state_is_rejected(void);

bool micros_scheduler_model_test_run(void);
bool micros_ipc_current_guard_test_run(void);

#endif
