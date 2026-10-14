#ifndef MICROS_TESTS_HOST_RUNTIME_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_RUNTIME_TEST_INTERNAL_H

#include "micros/runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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

enum {
    RAW_WRITE_NONE = 0,
    RAW_WRITE_A1,
    RAW_WRITE_A3,
};

struct raw_capture {
    uint64_t arguments[8];
    int64_t result;
    size_t calls;
    int write_argument;
    const struct micros_ipc_message *expected_input;
    struct micros_ipc_message input_snapshot;
    struct micros_ipc_message output;
    bool input_observed;
};

struct raw_capture *runtime_test_capture(void);
void runtime_test_reset_capture(int64_t result);
bool runtime_test_arguments_match(
    uint64_t a0,
    uint64_t a1,
    uint64_t a2,
    uint64_t a3,
    uint64_t a4,
    uint64_t a5,
    uint64_t a6,
    uint64_t a7
);
struct micros_ipc_message runtime_test_message_pattern(
    uint32_t type,
    uint8_t seed
);

bool test_ipc_wrappers(void);
bool test_grant_wrappers(void);
bool test_bootstrap_launcher_console_begin(void);
bool test_memory_support(void);

#endif
