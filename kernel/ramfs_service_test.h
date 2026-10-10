#ifndef MICROS_KERNEL_RAMFS_SERVICE_TEST_H
#define MICROS_KERNEL_RAMFS_SERVICE_TEST_H

#include "micros/kernel_objects.h"
#include "micros/trap.h"

enum micros_ramfs_service_test_trap_result {
    MICROS_RAMFS_SERVICE_TEST_TRAP_MISMATCH = 0,
    MICROS_RAMFS_SERVICE_TEST_TRAP_USER_RETURN,
};

_Noreturn void micros_ramfs_service_test_launch(void);

enum micros_ramfs_service_test_trap_result
micros_ramfs_service_test_handle_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

#endif
