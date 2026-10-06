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

#ifdef MICROS_BUILD_OBJECT_MODEL_TEST
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
