#ifndef MICROS_TESTS_HOST_APPLICATION_RUNTIME_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_APPLICATION_RUNTIME_TEST_INTERNAL_H

#include "lib/runtime/application_internal.h"
#include "tests/host/spawn_abi_test_internal.h"

struct application_runtime_fixture {
    uint8_t page[MICROS_APPLICATION_STACK_PAGE_SIZE];
    struct micros_application_stack_plan plan;
    struct micros_application_stack_values values;
};

bool application_test_prepare_stack(
    struct application_runtime_fixture *fixture,
    uint16_t argument_count,
    uint16_t environment_count
);
bool application_test_expect_rejection(
    const uint8_t *page,
    size_t page_size,
    uint64_t stack_pointer,
    enum micros_application_startup_error expected
);
bool application_test_decode_fixture(
    const struct application_runtime_fixture *fixture,
    uint32_t argument_count,
    uint32_t environment_count
);
bool test_application_frontend_valid(void);
bool test_application_frontend_invalid(void);
bool test_application_exit_return_failure(void);
bool test_application_frontend_argument_errors(void);

#endif
