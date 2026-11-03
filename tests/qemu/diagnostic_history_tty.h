#ifndef MICROS_TESTS_QEMU_DIAGNOSTIC_HISTORY_TTY_H
#define MICROS_TESTS_QEMU_DIAGNOSTIC_HISTORY_TTY_H

struct micros_tty_diagnostic;

enum micros_diagnostic_test_transfer_result {
    MICROS_DIAGNOSTIC_TEST_TRANSFER_FAILED = 0,
    MICROS_DIAGNOSTIC_TEST_TRANSFER_DEFERRED,
    MICROS_DIAGNOSTIC_TEST_TRANSFER_COMPLETE,
};

enum micros_diagnostic_test_transfer_result micros_diagnostic_test_tty_transfer(
    struct micros_tty_diagnostic *diagnostic
);

#endif
