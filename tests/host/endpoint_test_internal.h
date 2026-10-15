#ifndef MICROS_TESTS_HOST_ENDPOINT_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_ENDPOINT_TEST_INTERNAL_H

#include "micros/endpoint.h"

#include <stdio.h>

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
        enum micros_endpoint_error actual = (expression); \
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

#define EXPECT_OBJECT_ERROR(expected, expression) \
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

struct endpoint_test_fixture {
    struct micros_endpoint_registry registry;
    struct micros_kernel_objects objects;
    struct micros_process_handle processes[2];
    struct micros_thread_handle threads[2];
    struct micros_hart_handle hart;
};

struct endpoint_test_fixture *endpoint_test_context(void);

struct micros_privilege_profile endpoint_test_profile(
    uint8_t id,
    const char *name,
    uint32_t operations,
    uint32_t call_targets,
    uint32_t send_targets,
    uint32_t notify_targets,
    uint64_t kernel_operations
);

struct micros_user_context endpoint_test_context_pattern(uint64_t base);

void endpoint_test_write_u32_le(uint8_t *bytes, uint32_t value);

bool endpoint_test_setup_lifecycle_fixture(struct endpoint_test_fixture *fixture);

bool endpoint_test_encoding_boundaries(void);

bool endpoint_test_profile_table_is_immutable_and_specific(void);

bool endpoint_test_invalid_profile_tables_preserve_storage(void);

bool endpoint_test_validator_rejects_corruption(void);

bool endpoint_test_lifecycle_and_authorization(void);

bool endpoint_test_profile_install_and_close_require_held_threads(void);

bool endpoint_test_resolution_rejects_unimplemented_slots(void);

bool endpoint_test_reuse_rejects_stale_generation(void);

bool endpoint_test_source_only_seal_preserves_staged_source(void);

bool endpoint_test_relationship_validator_rejects_corruption(void);

#endif
