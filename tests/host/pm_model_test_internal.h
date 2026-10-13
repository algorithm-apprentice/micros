#ifndef MICROS_TESTS_HOST_PM_MODEL_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_PM_MODEL_TEST_INTERNAL_H

#include "tests/host/pm_test_internal.h"

enum {
    PM_MODEL_STEP_COUNT = 4096,
    PM_MODEL_NO_SLOT = UINT16_MAX,
};

struct pm_model_record {
    bool present;
    bool reaper;
    bool reap_pending;
    enum micros_pm_process_state state;
    enum micros_pm_spawn_stage spawn_stage;
    enum micros_pm_exit_stage exit_stage;
    struct micros_pm_process_handle handle;
    uint64_t pid;
    uint64_t transaction;
    uint64_t reply_token;
    micros_endpoint_t endpoint;
    int32_t exit_code;
};

struct pm_model_wait {
    bool active;
    bool result_ready;
    uint32_t flags;
    uint64_t selector;
    uint64_t reply_token;
    uint16_t pending_slot;
};

struct pm_model_reference {
    uint64_t last_pid;
    uint32_t generations[MICROS_PM_PROCESS_CAPACITY];
    struct pm_model_record records[MICROS_PM_PROCESS_CAPACITY];
    struct pm_model_wait wait;
};

struct pm_model_context {
    uint64_t seed;
    uint64_t random_state;
    size_t step;
    struct micros_pm_table table;
    struct micros_pm_process_handle init;
    struct pm_model_reference reference;
    uint32_t operation_trace[PM_MODEL_STEP_COUNT];
};

bool pm_model_failure(
    const char *expression,
    int line,
    uint64_t seed,
    size_t step,
    const uint32_t *trace
);

#define PM_MODEL_EXPECT(context, expression) \
    do { \
        if (!(expression)) { \
            return pm_model_failure( \
                #expression, \
                __LINE__, \
                (context)->seed, \
                (context)->step, \
                (context)->operation_trace \
            ); \
        } \
    } while (false)

bool pm_model_run_operation(
    struct pm_model_context *context,
    uint32_t operation,
    uint64_t choice
);

uint16_t pm_model_select_state(
    const struct pm_model_reference *reference,
    enum micros_pm_process_state state,
    uint64_t choice
);
uint16_t pm_model_select_present(
    const struct pm_model_reference *reference,
    uint64_t choice
);
bool pm_model_find_zombie(
    const struct pm_model_reference *reference,
    uint64_t selector,
    uint16_t *slot
);
bool pm_model_has_child(
    const struct pm_model_reference *reference,
    uint64_t selector
);
void pm_model_set_wait_result(struct pm_model_reference *reference, uint16_t slot);
bool pm_model_compare(
    const struct micros_pm_table *table,
    const struct pm_model_reference *reference,
    struct micros_pm_process_handle init
);

#endif
