#include "micros/ipc_runtime.h"

#include <stdbool.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "kernel/ipc_runtime_internal.h"
#include "micros/kernel_object_runtime.h"

static struct micros_endpoint_registry authoritative_registry;
static struct micros_endpoint_registry initialization_scratch;
static bool runtime_ready;

static void copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

static void clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

enum micros_endpoint_error micros_ipc_runtime_initialize(
    const struct micros_privilege_profile *profiles,
    size_t profile_count
)
{
    const struct micros_kernel_objects *objects;
    enum micros_endpoint_error error;
    uintptr_t saved_status;

    saved_status = riscv_irq_save();
    if (runtime_ready) {
        error = MICROS_ENDPOINT_ERROR_ALREADY_INITIALIZED;
        goto done;
    }
    objects = micros_kernel_object_runtime_registry();
    if (objects == NULL) {
        error = MICROS_ENDPOINT_ERROR_NOT_INITIALIZED;
        goto done;
    }
    if (
        micros_kernel_object_runtime_validate()
            != MICROS_KERNEL_OBJECT_OK
    ) {
        error = MICROS_ENDPOINT_ERROR_INVARIANT;
        goto done;
    }
    error = micros_endpoint_registry_initialize(
        &initialization_scratch,
        profiles,
        profile_count
    );
    if (error != MICROS_ENDPOINT_OK) {
        goto done;
    }
    error = micros_endpoint_registry_validate_objects(
        &initialization_scratch,
        objects
    );
    if (error != MICROS_ENDPOINT_OK) {
        clear_bytes(
            &initialization_scratch,
            sizeof(initialization_scratch)
        );
        goto done;
    }

    copy_bytes(
        &authoritative_registry,
        &initialization_scratch,
        sizeof(authoritative_registry)
    );
    clear_bytes(
        &initialization_scratch,
        sizeof(initialization_scratch)
    );
    runtime_ready = true;

done:
    riscv_irq_restore(saved_status);
    return error;
}

const struct micros_endpoint_registry *micros_ipc_runtime_registry(void)
{
    return runtime_ready ? &authoritative_registry : NULL;
}

struct micros_endpoint_registry *
micros_ipc_runtime_authoritative_registry(void)
{
    return runtime_ready ? &authoritative_registry : NULL;
}

enum micros_endpoint_error micros_ipc_runtime_validate(void)
{
    const struct micros_kernel_objects *objects;
    enum micros_endpoint_error error;
    uintptr_t saved_status;

    saved_status = riscv_irq_save();
    if (!runtime_ready) {
        error = MICROS_ENDPOINT_ERROR_NOT_INITIALIZED;
        goto done;
    }
    objects = micros_kernel_object_runtime_registry();
    if (objects == NULL) {
        error = MICROS_ENDPOINT_ERROR_INVARIANT;
        goto done;
    }
    error = micros_endpoint_registry_validate_objects(
        &authoritative_registry,
        objects
    );

done:
    riscv_irq_restore(saved_status);
    return error;
}
