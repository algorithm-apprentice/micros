#ifndef MICROS_TESTS_HOST_GRANT_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_GRANT_TEST_INTERNAL_H

#include "micros/grant.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/grant_copy.h"
#include "micros/ipc_core.h"
#include "micros/scheduler_core.h"
#include "micros/user_address_space.h"

enum {
    GRANT_PROCESS_COUNT = 3,
    GRANT_MODEL_STEPS = 4096,
    GRANT_MODEL_TRACE_COUNT = 32,
};

enum grant_model_operation {
    GRANT_MODEL_CREATE = 0,
    GRANT_MODEL_AUTHORIZE_FROM,
    GRANT_MODEL_AUTHORIZE_TO,
    GRANT_MODEL_AUTHORIZE_DENIAL,
    GRANT_MODEL_AUTHORIZE_STALE,
    GRANT_MODEL_AUTHORIZE_BOUNDS,
    GRANT_MODEL_REVOKE,
    GRANT_MODEL_INSPECT,
    GRANT_MODEL_CANCEL,
    GRANT_MODEL_CLOSE_REUSE,
    GRANT_MODEL_MALFORMED_TOKEN,
    GRANT_MODEL_PLAN_FAILURE,
    GRANT_MODEL_INVALID_CREATE,
    GRANT_MODEL_OPERATION_COUNT,
};

struct grant_model_trace_entry {
    size_t step;
    enum grant_model_operation operation;
    size_t grantor;
    size_t grantee;
    micros_grant_t grant;
    size_t offset;
    size_t length;
    uint32_t permission;
    enum micros_grant_error result;
};

struct grant_test_fixture {
    struct micros_endpoint_registry endpoint_registry;
    struct micros_grant_registry grant_registry;
    struct micros_kernel_objects objects;
    struct micros_process_handle processes[GRANT_PROCESS_COUNT];
    struct micros_thread_handle threads[GRANT_PROCESS_COUNT];
    micros_endpoint_t endpoints[GRANT_PROCESS_COUNT];
    struct micros_hart_handle hart;
    struct grant_model_trace_entry model_trace[GRANT_MODEL_TRACE_COUNT];
    size_t model_trace_count;
};

struct grant_test_fixture *grant_test_context(void);

bool micros_grant_test_run(void);
bool micros_grant_model_test_run(void);

bool grant_test_setup_fixture(
    struct grant_test_fixture *fixture
);

bool grant_test_recreate_process(
    struct grant_test_fixture *fixture,
    size_t index
);

bool grant_test_expect_create_failure_unchanged(
    struct grant_test_fixture *fixture,
    enum micros_grant_error expected,
    struct micros_process_handle grantor,
    micros_endpoint_t grantee,
    uintptr_t base,
    size_t length,
    uint32_t permissions
);

bool grant_test_token_boundaries(void);

bool grant_test_create_inspect_revoke_and_reuse(void);

bool grant_test_create_failures_and_capacity(void);

bool grant_test_terminal_generation_quarantines(void);

bool grant_test_endpoint_cancellation_and_reuse(void);

bool grant_test_failed_ipc_close_preserves_cancel_plan(void);

bool grant_test_error_precedence_and_output_preservation(void);

bool grant_test_validator_rejects_corruption(void);

bool grant_test_process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
);

bool grant_test_records_equal(
    const struct micros_grant_record *left,
    const struct micros_grant_record *right
);

bool grant_test_registries_equal(
    const struct micros_grant_registry *left,
    const struct micros_grant_registry *right
);

uint32_t grant_test_model_next(uint32_t *state);

void grant_test_record_model_trace(
    struct grant_test_fixture *fixture,
    size_t step,
    enum grant_model_operation operation,
    size_t grantor,
    size_t grantee,
    micros_grant_t grant,
    size_t offset,
    size_t length,
    uint32_t permission,
    enum micros_grant_error result
);

bool grant_test_model_fail(
    struct grant_test_fixture *fixture,
    size_t step,
    const char *reason
);

bool grant_test_cancel_plan_matches_reference(
    struct grant_test_fixture *fixture,
    const struct micros_grant_cancel_plan *plan,
    const struct micros_grant_registry *model,
    micros_endpoint_t endpoint
);

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

#define EXPECT_GRANT_ERROR(expected, expression) \
    do { \
        enum micros_grant_error actual = (expression); \
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

#endif
