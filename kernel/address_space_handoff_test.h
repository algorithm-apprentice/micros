#ifndef MICROS_KERNEL_ADDRESS_SPACE_HANDOFF_TEST_H
#define MICROS_KERNEL_ADDRESS_SPACE_HANDOFF_TEST_H

#include <stdbool.h>

struct micros_hart;
struct micros_trap_frame;

enum micros_address_space_handoff_test_trap_result {
    MICROS_ADDRESS_SPACE_HANDOFF_TEST_TRAP_INACTIVE = 0,
    MICROS_ADDRESS_SPACE_HANDOFF_TEST_TRAP_SUPERVISOR_RETURN,
    MICROS_ADDRESS_SPACE_HANDOFF_TEST_TRAP_MISMATCH,
};

bool micros_address_space_handoff_runtime_run_self_test(void);

enum micros_address_space_handoff_test_trap_result
micros_address_space_handoff_test_handle_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

#endif
