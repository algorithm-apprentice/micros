#include "kernel/user_execution_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/objects/kernel_object_runtime_internal.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"

static void copy_context(
    struct micros_user_context *destination,
    const struct micros_user_context *source
)
{
    unsigned char *destination_bytes =
        (unsigned char *)destination;
    const unsigned char *source_bytes =
        (const unsigned char *)source;
    size_t index;

    for (index = 0; index < sizeof(*destination); ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

static _Noreturn void panic_invariant(const char *reason)
{
    struct micros_hart *hart =
        micros_kernel_object_runtime_boot_hart();
    uintptr_t hart_id = hart == NULL ? 0 : hart->hardware_id;

    MICROS_PANIC(hart_id, reason);
}

enum micros_user_execution_error
micros_user_execution_capture_trap(
    struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    struct micros_kernel_objects *objects;
    struct micros_thread_handle thread_handle;
    struct micros_user_context context;
    enum micros_kernel_object_error object_error;
    enum micros_user_execution_error error;

    error = micros_user_execution_validate_trap_capture(
        hart,
        frame,
        &objects,
        &thread_handle
    );
    if (error != MICROS_USER_EXECUTION_OK) {
        return error;
    }
    copy_context(
        &context,
        (const struct micros_user_context *)frame
    );
    object_error = micros_thread_capture_execution_context(
        objects,
        thread_handle,
        &context
    );
    return micros_user_execution_map_object_error(object_error);
}

enum micros_user_execution_error
micros_user_execution_store_context(
    struct micros_thread_handle thread,
    const struct micros_user_context *context
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_authoritative_registry();
    enum micros_kernel_object_error error;
    uintptr_t saved_status;

    if (context == NULL) {
        return MICROS_USER_EXECUTION_ERROR_ARGUMENT;
    }
    if (objects == NULL) {
        return MICROS_USER_EXECUTION_ERROR_NOT_INITIALIZED;
    }
    saved_status = riscv_irq_save();
    error = micros_thread_capture_execution_context(
        objects,
        thread,
        context
    );
    riscv_irq_restore(saved_status);
    return micros_user_execution_map_object_error(error);
}

void micros_user_execution_install_return_frame(
    struct micros_trap_frame *frame,
    const struct micros_user_context *context,
    struct micros_hart *hart
)
{
    if (frame == NULL || context == NULL || hart == NULL) {
        panic_invariant("user-context-install-argument");
    }
    copy_context(
        (struct micros_user_context *)frame,
        context
    );
    frame->scause = 0;
    frame->stval = 0;
    frame->hart_context = (uintptr_t)hart;
}
