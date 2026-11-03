#include "tests/qemu/diagnostic_history_tty.h"

#include <stddef.h>
#include <stdint.h>

#include "servers/tty/tty_control.h"
#include "servers/tty/tty_diagnostic.h"

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

enum micros_diagnostic_test_transfer_result micros_diagnostic_test_tty_transfer(
    struct micros_tty_diagnostic *diagnostic
)
{
    size_t index;
    uint32_t next;
    uint32_t previous;
    micros_runtime_result_t result;

    if (
        diagnostic == NULL || !diagnostic->initialized
        || diagnostic->previous >= 10000
        || diagnostic->length != 0 || diagnostic->offset != 0 || diagnostic->pending
        || (uintptr_t)diagnostic->snapshot % 4096 != 0
    ) {
        return MICROS_DIAGNOSTIC_TEST_TRANSFER_FAILED;
    }
    previous = diagnostic->previous;
    for (index = 0; index < 10016; ++index) {
        diagnostic->snapshot[index] = UINT8_C(0xa5);
    }
    result = micros_tty_control_diagnostic_read(previous);
    if (
        diagnostic->previous != previous || diagnostic->length != 0 || diagnostic->offset != 0
        || diagnostic->pending || !diagnostic->initialized
    ) {
        return MICROS_DIAGNOSTIC_TEST_TRANSFER_FAILED;
    }
    if (result == MICROS_SYSCALL_ABI_STATE) {
        for (index = 0; index < 10016; ++index) {
            if (diagnostic->snapshot[index] != UINT8_C(0xa5)) {
                return MICROS_DIAGNOSTIC_TEST_TRANSFER_FAILED;
            }
        }
        return MICROS_DIAGNOSTIC_TEST_TRANSFER_DEFERRED;
    }
    if (result != 0) {
        return MICROS_DIAGNOSTIC_TEST_TRANSFER_FAILED;
    }
    next = read_u32(diagnostic->snapshot + 4);
    if (
        read_u32(diagnostic->snapshot) != 1
        || next != (previous + 8193) % 10000
        || read_u32(diagnostic->snapshot + 8) != 8193
        || read_u32(diagnostic->snapshot + 12) != 0
    ) {
        return MICROS_DIAGNOSTIC_TEST_TRANSFER_FAILED;
    }
    for (index = 0; index < 8193; ++index) {
        if (diagnostic->snapshot[16 + index] != (uint8_t)(1 + index % 251)) {
            return MICROS_DIAGNOSTIC_TEST_TRANSFER_FAILED;
        }
    }
    for (index = 16 + 8193; index < 10016; ++index) {
        if (diagnostic->snapshot[index] != UINT8_C(0xa5)) {
            return MICROS_DIAGNOSTIC_TEST_TRANSFER_FAILED;
        }
    }
    diagnostic->previous = next;
    diagnostic->length = 8193;
    diagnostic->offset = 8193;
    diagnostic->pending = false;
    return MICROS_DIAGNOSTIC_TEST_TRANSFER_COMPLETE;
}
