#ifndef MICROS_TESTS_HOST_KERNEL_OBJECTS_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_KERNEL_OBJECTS_TEST_INTERNAL_H

#include "micros/kernel_objects.h"
#include "micros/scheduler_core.h"

#include <stdbool.h>
#include <stdint.h>
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
        enum micros_kernel_object_error actual_error = (expression); \
        if (actual_error != (expected)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected error %d, got %d\n", \
                __FILE__, \
                __LINE__, \
                (int)(expected), \
                (int)actual_error \
            ); \
            return false; \
        } \
    } while (false)

#define EXPECT_ERROR_UNCHANGED(expected, expression) \
    do { \
        struct micros_kernel_objects expected_objects = *fixture; \
        enum micros_kernel_object_error actual_error = (expression); \
        if (actual_error != (expected)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected error %d, got %d\n", \
                __FILE__, \
                __LINE__, \
                (int)(expected), \
                (int)actual_error \
            ); \
            return false; \
        } \
        if ( \
            memcmp( \
                fixture, \
                &expected_objects, \
                sizeof(*fixture) \
            ) != 0 \
        ) { \
            fprintf( \
                stderr, \
                "%s:%d: failed operation changed registry\n", \
                __FILE__, \
                __LINE__ \
            ); \
            return false; \
        } \
    } while (false)

struct lifecycle_model_process {
    bool live;
    uint32_t generation;
    size_t thread_count;
    uintptr_t address_space_root;
};

struct lifecycle_model_thread {
    bool live;
    uint32_t generation;
    struct micros_process_handle owner;
    bool context_attached;
    uintptr_t kernel_stack_bottom;
    uintptr_t kernel_stack_top;
    struct micros_user_context context;
};

struct lifecycle_model {
    struct lifecycle_model_process processes[MICROS_PROCESS_CAPACITY];
    struct lifecycle_model_thread threads[MICROS_THREAD_CAPACITY];
    size_t process_count;
    size_t thread_count;
};

struct micros_kernel_objects *kernel_object_test_registry(void);

void kernel_object_test_reset_objects(struct micros_kernel_objects *fixture);

bool kernel_object_test_process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
);

struct micros_user_context kernel_object_test_context_pattern(uint64_t base);

bool kernel_object_test_contexts_equal(
    const struct micros_user_context *left,
    const struct micros_user_context *right
);

bool kernel_object_test_initialization_is_checked_and_one_shot(void);

bool kernel_object_test_uninitialized_registry_rejects_every_operation(void);

bool kernel_object_test_fixed_capacities_report_exhaustion_atomically(void);

bool kernel_object_test_process_slots_reuse_with_new_generations(void);

bool kernel_object_test_process_exact_slot_creation_is_isolated(void);

bool kernel_object_test_process_address_space_lifecycle_is_exact(void);

bool kernel_object_test_thread_execution_context_lifecycle_is_exact(void);

bool kernel_object_test_thread_slots_reuse_with_new_generations(void);

bool kernel_object_test_thread_ownership_and_production_limit(void);

bool kernel_object_test_model_policy_supports_multiple_threads(void);

bool kernel_object_test_hart_policy_and_stack_validation_are_atomic(void);

bool kernel_object_test_generation_boundaries_and_quarantine(void);

bool kernel_object_test_invalid_arguments_and_stale_handles_preserve_outputs(void);

uint32_t kernel_object_test_model_random(uint32_t *state);

size_t kernel_object_test_model_lowest_free_process(
    const struct lifecycle_model *model
);

size_t kernel_object_test_model_lowest_free_thread(
    const struct lifecycle_model *model
);

struct micros_process_handle kernel_object_test_model_process_handle(
    const struct lifecycle_model *model,
    size_t slot
);

struct micros_thread_handle kernel_object_test_model_thread_handle(
    const struct lifecycle_model *model,
    size_t slot
);

bool kernel_object_test_model_reports_expected_error(
    enum micros_kernel_object_error expected,
    enum micros_kernel_object_error actual,
    size_t step,
    const char *operation
);

bool kernel_object_test_lifecycle_model_matches_registry(
    const struct micros_kernel_objects *fixture,
    const struct lifecycle_model *model,
    size_t step
);

bool kernel_object_test_seeded_lifecycle_model(void);

bool kernel_object_test_validator_rejects_corrupt_relationships(void);

#endif
