#ifndef MICROS_KERNEL_OBJECT_RUNTIME_H
#define MICROS_KERNEL_OBJECT_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "micros/kernel_objects.h"

enum micros_kernel_object_error
micros_kernel_object_runtime_initialize(uintptr_t boot_hart_id);

enum micros_kernel_object_error
micros_kernel_object_runtime_install_trap_stacks(
    uintptr_t primary_stack_bottom,
    uintptr_t primary_stack_top,
    uintptr_t emergency_stack_bottom,
    uintptr_t emergency_stack_top
);

const struct micros_kernel_objects *
micros_kernel_object_runtime_registry(void);

struct micros_hart *micros_kernel_object_runtime_boot_hart(void);

struct micros_hart_handle
micros_kernel_object_runtime_boot_hart_handle(void);

struct micros_hart *
micros_kernel_object_runtime_hart_from_context(uintptr_t hart_context);

enum micros_kernel_object_error
micros_kernel_object_runtime_validate(void);

enum micros_kernel_object_error
micros_kernel_object_runtime_attach_address_space(
    struct micros_process_handle process,
    uintptr_t root
);

enum micros_kernel_object_error
micros_kernel_object_runtime_detach_address_space(
    struct micros_process_handle process,
    uintptr_t expected_root
);

#if \
    defined(MICROS_BUILD_OBJECT_MODEL_TEST) \
    || defined(MICROS_BUILD_ENDPOINT_TEST) \
    || defined(MICROS_BUILD_GRANT_TEST) \
    || defined(MICROS_BUILD_IPC_TEST) \
    || defined(MICROS_BUILD_FRAME_OWNERSHIP_TEST) \
    || defined(MICROS_BUILD_USER_ADDRESS_SPACE_TEST) \
    || defined(MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST) \
    || defined(MICROS_BUILD_USER_EXECUTION_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_TEST) \
    || defined(MICROS_BUILD_IPC_ECALL_CORE_TEST) \
    || defined(MICROS_BUILD_IPC_SYSCALL_TEST) \
    || defined(MICROS_BUILD_IPC_SYSCALL_PANIC_TEST) \
    || defined(MICROS_BUILD_GRANT_SYSCALL_TEST) \
    || defined(MICROS_BUILD_USER_RUNTIME_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST)
struct micros_kernel_objects *
micros_kernel_object_runtime_test_registry(void);
#endif

#ifdef MICROS_BUILD_NESTED_TRAP_TEST
bool micros_kernel_object_runtime_set_test_emergency_stack(
    uintptr_t emergency_stack_bottom,
    uintptr_t emergency_stack_top
);
#endif

#endif
