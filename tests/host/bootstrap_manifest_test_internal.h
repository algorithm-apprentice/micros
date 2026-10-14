#ifndef MICROS_TESTS_HOST_BOOTSTRAP_MANIFEST_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_BOOTSTRAP_MANIFEST_TEST_INTERNAL_H

#include "micros/bootstrap.h"

#include <stdbool.h>
#include <stddef.h>
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

struct validation_fixture {
    struct micros_bootstrap_manifest manifest;
    struct micros_bootstrap_expected_service expected[2];
    struct micros_bootstrap_image_info images[2];
    struct micros_privilege_profile profiles[2];
};

void bootstrap_manifest_test_set_name(
    char output[MICROS_BOOTSTRAP_NAME_SIZE],
    const char *name
);
struct micros_privilege_profile bootstrap_manifest_test_profile(
    uint8_t id,
    const char *name
);
void bootstrap_manifest_test_initialize_manifest(
    struct micros_bootstrap_manifest *manifest
);
void bootstrap_manifest_test_initialize_validation_fixture(
    struct validation_fixture *fixture
);
bool bootstrap_manifest_test_expect_validation_error(
    const struct validation_fixture *fixture,
    uint64_t available_pages,
    enum micros_bootstrap_error expected_error
);

bool test_manifest_contract(void);
bool test_manifest_validation(void);
bool test_manifest_rejections(void);
bool test_vm_mapping_capacity(void);
bool test_three_service_topology(void);
bool test_tty_service_contract(void);
bool test_runtime_transitions(void);
bool test_retained_runtime_deadline(void);
bool test_replayable_runtime_model(void);

#endif
