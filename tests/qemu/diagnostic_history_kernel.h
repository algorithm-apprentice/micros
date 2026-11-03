#ifndef MICROS_TESTS_QEMU_DIAGNOSTIC_HISTORY_KERNEL_H
#define MICROS_TESTS_QEMU_DIAGNOSTIC_HISTORY_KERNEL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef MICROS_BUILD_PANIC_TEST
void micros_diagnostic_test_seed_panic(void);
#endif

#ifdef MICROS_BUILD_TTY_SERVICE_TEST
void micros_diagnostic_test_observe_thr(void);
void micros_diagnostic_test_registered(uint32_t cursor);
bool micros_diagnostic_test_before_read(uint32_t cursor);
void micros_diagnostic_test_after_read(void);
void micros_diagnostic_test_finish(void);
#endif

#endif
