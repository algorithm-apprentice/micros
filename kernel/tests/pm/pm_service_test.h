#ifndef MICROS_KERNEL_PM_SERVICE_TEST_H
#define MICROS_KERNEL_PM_SERVICE_TEST_H

#include "arch/riscv64/trap_context.h"
#include "micros/kernel_objects.h"

_Noreturn void micros_pm_service_test_launch(void);

_Noreturn void micros_pm_service_test_handle_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

#endif
