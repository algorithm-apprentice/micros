#ifndef MICROS_SELECTED_IPC_VALIDATION_TEST_H
#define MICROS_SELECTED_IPC_VALIDATION_TEST_H

#include "micros/ipc_core.h"

struct selected_ipc_fixture {
    struct micros_kernel_objects objects;
    struct micros_kernel_objects before_objects;
    struct micros_endpoint_registry registry;
    struct micros_endpoint_registry before_registry;
    struct micros_process_handle processes[MICROS_PROCESS_CAPACITY];
    micros_endpoint_t endpoints[3];
    struct micros_thread_handle threads[MICROS_THREAD_CAPACITY];
    struct micros_hart_handle hart;
    size_t thread_count;
    uint64_t lookups;
    uint64_t translations;
    size_t plan_bytes;
};

struct selected_ipc_fixture *selected_ipc_fixture_state(void);
void selected_ipc_test_name(const char *name);
bool selected_ipc_setup(size_t max_threads);
bool selected_ipc_prepare_thread(size_t process_slot);
bool selected_ipc_begin_capture(size_t index);
bool selected_ipc_finish_capture(void);
bool selected_ipc_report_case(
    size_t index, const char *name, unsigned result, uint64_t value, bool preserved
);
bool selected_ipc_completion_cases(size_t first_case);
size_t selected_ipc_completion_case_count(void);
bool selected_ipc_deadlock_cases(size_t first_case);
size_t selected_ipc_deadlock_case_count(void);
bool selected_ipc_token_cases(size_t first_case);
size_t selected_ipc_token_case_count(void);
bool selected_ipc_queue_cases(size_t first_case);
size_t selected_ipc_queue_case_count(void);

#endif
