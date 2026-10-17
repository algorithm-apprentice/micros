#ifndef MICROS_TESTS_HOST_TTY_HANDOFF_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_TTY_HANDOFF_TEST_INTERNAL_H

#include "kernel/tty/tty_handoff_core.h"
#include "kernel/tty/tty_handoff_runtime.h"
#include "kernel/tty/tty_control_syscall_core.h"
#include "kernel/tty/uart_console_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/sv39.h"

bool micros_tty_handoff_test_run(void);

struct tty_handoff_test_fixture {
    struct micros_process runtime_process;
    struct micros_thread runtime_thread;
    struct micros_endpoint_record runtime_endpoint;
    struct micros_process_handle runtime_process_handle;
    struct micros_thread_handle runtime_thread_handle;
};

struct tty_handoff_test_fixture *tty_handoff_test_fixture(void);

bool tty_handoff_test_device_leaf_classification(void);

bool tty_handoff_test_runtime_lifecycle_validation(void);

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

#endif
