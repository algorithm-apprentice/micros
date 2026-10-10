#ifndef MICROS_KERNEL_VFS_SERVICE_TEST_H
#define MICROS_KERNEL_VFS_SERVICE_TEST_H

#include "micros/kernel_objects.h"
#include "micros/trap.h"

enum micros_vfs_service_test_trap_result {
    MICROS_VFS_SERVICE_TEST_TRAP_MISMATCH = 0,
};

_Noreturn void micros_vfs_service_test_launch(void);

enum micros_vfs_service_test_trap_result
micros_vfs_service_test_handle_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

#endif
