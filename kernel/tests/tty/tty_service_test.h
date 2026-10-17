#ifndef MICROS_KERNEL_TTY_SERVICE_TEST_H
#define MICROS_KERNEL_TTY_SERVICE_TEST_H

#include <stdbool.h>

#include "arch/riscv64/trap_context.h"
#include "micros/kernel_objects.h"

void micros_tty_service_test_record_claim(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);
void micros_tty_service_test_record_completion(void);
void micros_tty_service_test_record_ready(bool retained_claim);

_Noreturn void micros_tty_service_test_launch(void);

_Noreturn void micros_tty_service_test_handle_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

#endif
