#include "micros/grant_runtime.h"

#include <stdbool.h>
#include <stddef.h>

#include "arch/riscv64/interrupt.h"
#include "kernel/grant_runtime_internal.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"

static struct micros_grant_registry authoritative_registry;
static struct micros_grant_registry initialization_scratch;
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

enum micros_grant_error micros_grant_runtime_initialize(void)
{
    const struct micros_endpoint_registry *endpoint_registry;
    const struct micros_kernel_objects *objects;
    enum micros_grant_error error;
    uintptr_t saved_status;

    saved_status = riscv_irq_save();
    if (runtime_ready) {
        error = MICROS_GRANT_ERROR_ALREADY_INITIALIZED;
        goto done;
    }
    endpoint_registry = micros_ipc_runtime_registry();
    objects = micros_kernel_object_runtime_registry();
    if (endpoint_registry == NULL || objects == NULL) {
        error = MICROS_GRANT_ERROR_NOT_INITIALIZED;
        goto done;
    }
    if (
        micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
        || micros_kernel_object_runtime_validate()
            != MICROS_KERNEL_OBJECT_OK
    ) {
        error = MICROS_GRANT_ERROR_INVARIANT;
        goto done;
    }
    error = micros_grant_registry_initialize(
        &initialization_scratch
    );
    if (error != MICROS_GRANT_OK) {
        goto done;
    }
    error = micros_grant_registry_validate(
        &initialization_scratch,
        endpoint_registry,
        objects
    );
    if (error != MICROS_GRANT_OK) {
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

const struct micros_grant_registry *micros_grant_runtime_registry(void)
{
    return runtime_ready ? &authoritative_registry : NULL;
}

struct micros_grant_registry *
micros_grant_runtime_authoritative_registry(void)
{
    return runtime_ready ? &authoritative_registry : NULL;
}

enum micros_grant_error micros_grant_runtime_validate(void)
{
    const struct micros_endpoint_registry *endpoint_registry;
    const struct micros_kernel_objects *objects;
    enum micros_grant_error error;
    uintptr_t saved_status;

    saved_status = riscv_irq_save();
    if (!runtime_ready) {
        error = MICROS_GRANT_ERROR_NOT_INITIALIZED;
        goto done;
    }
    endpoint_registry = micros_ipc_runtime_registry();
    objects = micros_kernel_object_runtime_registry();
    if (endpoint_registry == NULL || objects == NULL) {
        error = MICROS_GRANT_ERROR_INVARIANT;
        goto done;
    }
    error = micros_grant_registry_validate(
        &authoritative_registry,
        endpoint_registry,
        objects
    );

done:
    riscv_irq_restore(saved_status);
    return error;
}
