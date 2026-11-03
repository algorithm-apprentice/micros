#ifndef MICROS_TESTS_HOST_DIAGNOSTIC_HISTORY_TEST_H
#define MICROS_TESTS_HOST_DIAGNOSTIC_HISTORY_TEST_H

#include <stdbool.h>
#include <stdio.h>

#define DIAGNOSTIC_EXPECT(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "%s:%d: expected %s\n", __FILE__, __LINE__, #expression); \
            return false; \
        } \
    } while (false)

bool diagnostic_control_cases(void);
bool tty_diagnostic_cases(void);

#endif
