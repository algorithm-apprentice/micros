#ifndef MICROS_HOST_SCHEDULER_MODEL_INTERNAL_H
#define MICROS_HOST_SCHEDULER_MODEL_INTERNAL_H

#include "micros/scheduler_core.h"

#include <stdio.h>
#include <string.h>

enum {
    MODEL_THREAD_COUNT = 8,
    MODEL_HART_COUNT = 2,
    MODEL_STEPS = 4096,
    MODEL_NONE = MODEL_THREAD_COUNT,
};

struct model_thread {
    bool assigned;
    bool preemptible;
    uint32_t flags;
    uint8_t priority;
    uint64_t quantum;
    uint64_t remaining;
    size_t hart;
};

struct model_hart {
    size_t queues[MICROS_SCHEDULER_PRIORITY_COUNT][MODEL_THREAD_COUNT];
    size_t queue_counts[MICROS_SCHEDULER_PRIORITY_COUNT];
    size_t current;
    enum micros_scheduler_accounting_owner accounting_owner;
    uint64_t accounting_started_at;
    size_t accounted_thread;
    uint64_t kernel_ticks;
    uint64_t idle_ticks;
    uint64_t counter;
};

struct model_coverage {
    size_t preempt_higher;
    size_t enqueue_same;
    size_t enqueue_lower;
    size_t remove_middle;
    size_t flag_first;
    size_t flag_additional;
    size_t flag_partial_clear;
    size_t flag_final_clear;
    size_t return_keep;
    size_t return_switch;
    size_t return_idle;
    size_t quantum_renew;
    size_t thread_to_kernel;
    size_t kernel_to_thread;
    size_t kernel_to_idle;
    size_t idle_to_kernel;
    size_t policy_runnable;
    size_t policy_blocked;
};

struct model_state {
    struct model_thread threads[MODEL_THREAD_COUNT];
    struct model_hart harts[MODEL_HART_COUNT];
    struct model_coverage coverage;
};

struct scheduler_model_fixture {
    struct micros_kernel_objects objects;
    struct micros_process_handle process;
    struct micros_thread_handle threads[MODEL_THREAD_COUNT];
    struct micros_hart_handle harts[MODEL_HART_COUNT];
};

#define MODEL_FAIL(step, operation, value, message) \
    do { \
        fprintf( \
            stderr, \
            "scheduler model seed=0x5c4ed026 step=%zu " \
            "operation=%s value=0x%08x: %s\n", \
            (step), \
            (operation), \
            (unsigned)(value), \
            (message) \
        ); \
        return false; \
    } while (false)

struct scheduler_model_fixture *scheduler_model_fixture(void);
bool micros_scheduler_model_test_run(void);

bool scheduler_model_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
);

bool scheduler_model_handle_is_null(struct micros_thread_handle handle);

bool scheduler_model_setup_fixture(struct scheduler_model_fixture *fixture);

size_t scheduler_model_queue_find(
    const struct model_state *model,
    size_t hart,
    uint8_t priority,
    size_t slot
);

void scheduler_model_queue_remove(
    struct model_state *model,
    size_t hart,
    uint8_t priority,
    size_t slot
);

void scheduler_model_queue_tail(
    struct model_state *model,
    size_t hart,
    uint8_t priority,
    size_t slot
);

bool scheduler_model_admit(
    struct scheduler_model_fixture *fixture,
    struct model_state *model,
    size_t slot,
    size_t hart,
    uint8_t priority,
    uint64_t quantum
);

bool scheduler_model_plan_and_commit(
    struct scheduler_model_fixture *fixture,
    struct model_state *model,
    size_t hart
);

bool scheduler_model_model_matches(
    struct scheduler_model_fixture *fixture,
    const struct model_state *model,
    size_t step,
    const char *operation,
    uint32_t value
);

bool scheduler_model_initialize_model(
    struct scheduler_model_fixture *fixture,struct model_state *model);

bool scheduler_model_run_required_prelude(
    struct scheduler_model_fixture *fixture,struct model_state *model);

#endif
