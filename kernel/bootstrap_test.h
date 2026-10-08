#ifndef MICROS_KERNEL_BOOTSTRAP_TEST_H
#define MICROS_KERNEL_BOOTSTRAP_TEST_H

struct micros_hart;
struct micros_trap_frame;

_Noreturn void micros_bootstrap_test_launch(void);

_Noreturn void micros_bootstrap_test_handle_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

#endif
