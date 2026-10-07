#include "micros/kernel_object_runtime.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "kernel/kernel_object_runtime_internal.h"

static struct micros_kernel_objects kernel_objects;
static struct micros_hart_handle boot_hart_handle;
static bool runtime_ready;

enum micros_kernel_object_error
micros_kernel_object_runtime_initialize(uintptr_t boot_hart_id)
{
    struct micros_hart_handle handle;
    enum micros_kernel_object_error error;
    uintptr_t saved_status;

    saved_status = riscv_irq_save();
    error = micros_kernel_objects_initialize(&kernel_objects, 1, 1);
    if (error == MICROS_KERNEL_OBJECT_OK) {
        error = micros_hart_register(
            &kernel_objects,
            boot_hart_id,
            &handle
        );
    }
    if (error == MICROS_KERNEL_OBJECT_OK) {
        error = micros_kernel_objects_validate(&kernel_objects);
    }
    if (error == MICROS_KERNEL_OBJECT_OK) {
        boot_hart_handle = handle;
        runtime_ready = true;
    }
    riscv_irq_restore(saved_status);
    return error;
}

enum micros_kernel_object_error
micros_kernel_object_runtime_install_trap_stacks(
    uintptr_t primary_stack_bottom,
    uintptr_t primary_stack_top,
    uintptr_t emergency_stack_bottom,
    uintptr_t emergency_stack_top
)
{
    enum micros_kernel_object_error error;
    uintptr_t saved_status;

    if (!runtime_ready) {
        return MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED;
    }
    saved_status = riscv_irq_save();
    error = micros_hart_install_trap_stacks(
        &kernel_objects,
        boot_hart_handle,
        primary_stack_bottom,
        primary_stack_top,
        emergency_stack_bottom,
        emergency_stack_top
    );
    if (error == MICROS_KERNEL_OBJECT_OK) {
        error = micros_kernel_objects_validate(&kernel_objects);
    }
    riscv_irq_restore(saved_status);
    return error;
}

const struct micros_kernel_objects *
micros_kernel_object_runtime_registry(void)
{
    if (!runtime_ready) {
        return NULL;
    }
    return &kernel_objects;
}

struct micros_hart *micros_kernel_object_runtime_boot_hart(void)
{
    const struct micros_hart *hart;

    if (
        !runtime_ready
        || micros_hart_resolve(
            &kernel_objects,
            boot_hart_handle,
            &hart
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return NULL;
    }
    return &kernel_objects.harts[boot_hart_handle.slot];
}

struct micros_hart_handle
micros_kernel_object_runtime_boot_hart_handle(void)
{
    struct micros_hart_handle invalid = {0, 0};

    if (!runtime_ready) {
        return invalid;
    }
    return boot_hart_handle;
}

struct micros_hart *
micros_kernel_object_runtime_hart_from_context(uintptr_t hart_context)
{
    const struct micros_hart *hart;

    if (
        !runtime_ready
        || micros_hart_resolve_context(
            &kernel_objects,
            hart_context,
            &hart
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return NULL;
    }
    return &kernel_objects.harts[
        (size_t)(hart - &kernel_objects.harts[0])
    ];
}

enum micros_kernel_object_error
micros_kernel_object_runtime_validate(void)
{
    if (!runtime_ready) {
        return MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED;
    }
    return micros_kernel_objects_validate(&kernel_objects);
}

struct micros_kernel_objects *
micros_kernel_object_runtime_authoritative_registry(void)
{
    if (!runtime_ready) {
        return NULL;
    }
    return &kernel_objects;
}

enum micros_kernel_object_error
micros_kernel_object_runtime_attach_address_space(
    struct micros_process_handle process,
    uintptr_t root
)
{
    enum micros_kernel_object_error error;
    uintptr_t saved_status;

    if (!runtime_ready) {
        return MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED;
    }
    saved_status = riscv_irq_save();
    error = micros_process_attach_address_space(
        &kernel_objects,
        process,
        root
    );
    if (error == MICROS_KERNEL_OBJECT_OK) {
        error = micros_kernel_objects_validate(&kernel_objects);
    }
    riscv_irq_restore(saved_status);
    return error;
}

enum micros_kernel_object_error
micros_kernel_object_runtime_detach_address_space(
    struct micros_process_handle process,
    uintptr_t expected_root
)
{
    enum micros_kernel_object_error error;
    uintptr_t saved_status;

    if (!runtime_ready) {
        return MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED;
    }
    saved_status = riscv_irq_save();
    error = micros_process_detach_address_space(
        &kernel_objects,
        process,
        expected_root
    );
    if (error == MICROS_KERNEL_OBJECT_OK) {
        error = micros_kernel_objects_validate(&kernel_objects);
    }
    riscv_irq_restore(saved_status);
    return error;
}

#if defined(MICROS_BUILD_OBJECT_MODEL_TEST) \
    || defined(MICROS_BUILD_ENDPOINT_TEST) \
    || defined(MICROS_BUILD_IPC_TEST) \
    || defined(MICROS_BUILD_FRAME_OWNERSHIP_TEST) \
    || defined(MICROS_BUILD_USER_ADDRESS_SPACE_TEST) \
    || defined(MICROS_BUILD_USER_EXECUTION_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_TEST) \
    || defined(MICROS_BUILD_IPC_ECALL_CORE_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST)
struct micros_kernel_objects *
micros_kernel_object_runtime_test_registry(void)
{
    if (!runtime_ready) {
        return NULL;
    }
    return &kernel_objects;
}
#endif

#ifdef MICROS_BUILD_NESTED_TRAP_TEST
bool micros_kernel_object_runtime_set_test_emergency_stack(
    uintptr_t emergency_stack_bottom,
    uintptr_t emergency_stack_top
)
{
    struct micros_hart *hart;
    uintptr_t saved_status;
    bool valid;

    if (!runtime_ready) {
        return false;
    }
    saved_status = riscv_irq_save();
    hart = micros_kernel_object_runtime_boot_hart();
    valid = (
        hart != NULL
        && hart->trap_installed
        && emergency_stack_bottom != 0
        && emergency_stack_top > emergency_stack_bottom
        && emergency_stack_bottom % MICROS_TRAP_STACK_ALIGNMENT == 0
        && emergency_stack_top % MICROS_TRAP_STACK_ALIGNMENT == 0
        && emergency_stack_top - emergency_stack_bottom
            >= MICROS_EMERGENCY_TRAP_STACK_MIN_SIZE
        && (
            emergency_stack_top
                <= hart->trap.primary_stack_bottom
            || emergency_stack_bottom
                >= hart->trap.primary_stack_top
        )
    );
    if (valid) {
        hart->trap.emergency_stack_bottom =
            emergency_stack_bottom;
        hart->trap.emergency_stack_top = emergency_stack_top;
        valid = (
            micros_kernel_objects_validate(&kernel_objects)
            == MICROS_KERNEL_OBJECT_OK
        );
    }
    riscv_irq_restore(saved_status);
    return valid;
}
#endif
