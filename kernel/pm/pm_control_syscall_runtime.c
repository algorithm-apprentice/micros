#include "kernel/pm/pm_control_syscall_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/trap_context.h"
#include "kernel/boot/bootstrap_runtime.h"
#include "micros/panic.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"

static enum micros_pm_control_output_error translate_output(
    void *context,
    uint64_t user_address,
    size_t requested_size,
    uintptr_t *retained_physical_address,
    size_t *contiguous_size
)
{
    const struct micros_process_handle *process = context;
    uint64_t physical_address;
    uint32_t permissions;
    size_t contiguous;
    enum micros_user_address_space_error error;

    if (
        process == NULL
        || retained_physical_address == NULL
        || contiguous_size == NULL
    ) {
        return MICROS_PM_CONTROL_OUTPUT_ERROR_INVARIANT;
    }
    if (
        user_address < MICROS_USER_VIRTUAL_BASE
        || requested_size > MICROS_USER_VIRTUAL_END
        || user_address > MICROS_USER_VIRTUAL_END - requested_size
    ) {
        return MICROS_PM_CONTROL_OUTPUT_ERROR_MEMORY_FAULT;
    }
    error = micros_user_address_space_translate(
        *process,
        user_address,
        &physical_address,
        &permissions,
        &contiguous
    );
    if (
        error == MICROS_USER_ADDRESS_SPACE_ERROR_RANGE
        || error == MICROS_USER_ADDRESS_SPACE_ERROR_PERMISSION
        || error == MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED
        || (
            error == MICROS_USER_ADDRESS_SPACE_OK
            && (permissions & MICROS_SV39_PERMISSION_WRITE) == 0
        )
    ) {
        return MICROS_PM_CONTROL_OUTPUT_ERROR_MEMORY_FAULT;
    }
    if (error != MICROS_USER_ADDRESS_SPACE_OK || contiguous == 0) {
        return MICROS_PM_CONTROL_OUTPUT_ERROR_INVARIANT;
    }
    *retained_physical_address = (uintptr_t)physical_address;
    *contiguous_size = contiguous;
    return MICROS_PM_CONTROL_OUTPUT_OK;
}

_Noreturn void micros_pm_control_syscall_panic(
    const struct micros_pm_control_syscall_runtime *runtime,
    const char *reason
)
{
    MICROS_TRAP_PANIC(
        runtime == NULL || runtime->hart == NULL
            ? 0
            : runtime->hart->hardware_id,
        reason,
        runtime == NULL ? NULL : runtime->frame
    );
}

enum micros_syscall_return micros_pm_control_syscall_return_result(
    const struct micros_pm_control_syscall_runtime *runtime,
    enum micros_syscall_abi_result result
)
{
    runtime->frame->a0 = (uint64_t)(int64_t)result;
    return MICROS_SYSCALL_RETURN_NORMAL;
}

_Noreturn void micros_pm_control_syscall_fail(
    const struct micros_pm_control_syscall_runtime *runtime
)
{
    if (
        runtime->state != NULL
        && runtime->bootstrap != NULL
        && runtime->bootstrap->phase == MICROS_BOOTSTRAP_PHASE_RUNNING
    ) {
        micros_bootstrap_runtime_fail(
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            runtime->state->service_id,
            runtime->state->endpoint,
            0
        );
    }
    micros_pm_control_syscall_panic(
        runtime,
        "pm-control-invariant"
    );
}

enum micros_pm_control_output_error
micros_pm_control_syscall_prepare_output(
    const struct micros_pm_control_syscall_runtime *runtime,
    uint64_t user_address,
    struct micros_pm_control_output_plan *plan
)
{
    return micros_pm_control_output_prepare(
        user_address,
        translate_output,
        (void *)&runtime->context->process,
        plan
    );
}
