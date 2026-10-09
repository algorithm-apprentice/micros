#include "kernel/pm_control_syscall.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/trap_context.h"
#include "kernel/bootstrap_runtime.h"
#include "kernel/ipc_runtime_internal.h"
#include "kernel/pm_control_core.h"
#include "kernel/pm_control_runtime.h"
#include "kernel/pm_control_syscall_core.h"
#include "kernel/vm_handoff_runtime.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_runtime.h"
#include "micros/panic.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"

static _Noreturn void panic_pm_control(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const char *reason
)
{
    MICROS_TRAP_PANIC(
        hart == NULL ? 0 : hart->hardware_id,
        reason,
        frame
    );
}

static enum micros_syscall_return return_result(
    struct micros_trap_frame *frame,
    enum micros_syscall_abi_result result
)
{
    frame->a0 = (uint64_t)(int64_t)result;
    return MICROS_SYSCALL_RETURN_NORMAL;
}

static _Noreturn void fail_pm_control(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_pm_control_state *state
)
{
    const struct micros_bootstrap_control_state *bootstrap =
        micros_bootstrap_runtime_state();

    if (
        state != NULL
        && bootstrap != NULL
        && bootstrap->phase == MICROS_BOOTSTRAP_PHASE_RUNNING
    ) {
        micros_bootstrap_runtime_fail(
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state->service_id,
            state->endpoint,
            0
        );
    }
    panic_pm_control(hart, frame, "pm-control-invariant");
}

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

enum micros_syscall_return
micros_pm_control_handle_captured_user_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_syscall_arguments *arguments
)
{
    struct micros_pm_control_request request;
    struct micros_pm_control_state *state;
    struct micros_bootstrap_control_state *bootstrap;
    struct micros_endpoint_registry *registry;
    struct micros_kernel_objects *objects;
    const struct micros_vm_handoff_state *handoff;
    const struct micros_frame_ownership *ownership;
    enum micros_pm_control_authority_result authority;
    enum micros_pm_control_error control_error;

    if (
        hart == NULL
        || frame == NULL
        || context == NULL
        || arguments == NULL
        || context->objects == NULL
    ) {
        panic_pm_control(hart, frame, "pm-control-argument");
    }
    if (
        micros_pm_control_decode(arguments, &request)
            != MICROS_SYSCALL_ABI_OK
    ) {
        return return_result(frame, MICROS_SYSCALL_ABI_ARGUMENT);
    }
    state = micros_pm_control_runtime_authoritative_state();
    bootstrap = micros_bootstrap_runtime_authoritative_state();
    registry = micros_ipc_runtime_authoritative_registry();
    objects = context->objects;
    authority = micros_pm_control_syscall_authority_resolve(
        state,
        bootstrap,
        registry,
        objects,
        context
    );
    if (authority == MICROS_PM_CONTROL_AUTHORITY_INVARIANT) {
        fail_pm_control(hart, frame, state);
    }
    if (authority == MICROS_PM_CONTROL_AUTHORITY_UNAUTHORIZED) {
        return return_result(
            frame,
            MICROS_SYSCALL_ABI_UNAUTHORIZED
        );
    }
    if (authority != MICROS_PM_CONTROL_AUTHORITY_AUTHORIZED) {
        fail_pm_control(hart, frame, state);
    }
    handoff = micros_vm_handoff_runtime_state();
    ownership = micros_frame_ownership_runtime_ledger();
    if (
        handoff == NULL
        || ownership == NULL
        || micros_bootstrap_runtime_validate()
            != MICROS_BOOTSTRAP_OK
        || micros_vm_handoff_runtime_validate(
            bootstrap,
            registry,
            objects
        ) != MICROS_VM_HANDOFF_OK
        || micros_pm_control_runtime_validate(
            bootstrap,
            registry,
            objects
        ) != MICROS_PM_CONTROL_OK
        || micros_grant_runtime_validate() != MICROS_GRANT_OK
    ) {
        fail_pm_control(hart, frame, state);
    }
    if (!micros_pm_control_syscall_phase_is_ready(
        bootstrap->phase,
        handoff->phase,
        ownership->phase
    )) {
        return return_result(frame, MICROS_SYSCALL_ABI_STATE);
    }
    if (request.command == MICROS_PM_CONTROL_RESERVE) {
        struct micros_pm_control_reserve_plan reserve;
        struct micros_pm_control_output_plan output;
        enum micros_pm_control_output_error output_error;

        if (state->active_transaction != 0) {
            return return_result(frame, MICROS_SYSCALL_ABI_STATE);
        }
        output_error = micros_pm_control_output_prepare(
            request.output_address,
            translate_output,
            (void *)&context->process,
            &output
        );
        if (
            output_error
                == MICROS_PM_CONTROL_OUTPUT_ERROR_MEMORY_FAULT
        ) {
            return return_result(
                frame,
                MICROS_SYSCALL_ABI_MEMORY_FAULT
            );
        }
        if (output_error != MICROS_PM_CONTROL_OUTPUT_OK) {
            fail_pm_control(hart, frame, state);
        }
        control_error = micros_pm_control_reserve_preflight(
            state,
            objects,
            &reserve
        );
        if (control_error == MICROS_PM_CONTROL_ERROR_CAPACITY) {
            return return_result(
                frame,
                MICROS_SYSCALL_ABI_CAPACITY
            );
        }
        if (control_error == MICROS_PM_CONTROL_ERROR_STATE) {
            return return_result(frame, MICROS_SYSCALL_ABI_STATE);
        }
        if (control_error != MICROS_PM_CONTROL_OK) {
            fail_pm_control(hart, frame, state);
        }
        micros_pm_control_reserve_commit_prevalidated(
            state,
            objects,
            &reserve
        );
        micros_pm_control_output_commit_prevalidated(
            &output,
            &reserve.result
        );
        return return_result(frame, MICROS_SYSCALL_ABI_OK);
    }
    if (request.command == MICROS_PM_CONTROL_ABORT_RESERVED) {
        struct micros_pm_control_abort_plan abort;

        control_error = micros_pm_control_abort_preflight(
            state,
            objects,
            request.transaction,
            &abort
        );
        if (control_error == MICROS_PM_CONTROL_ERROR_STATE) {
            return return_result(frame, MICROS_SYSCALL_ABI_STATE);
        }
        if (control_error != MICROS_PM_CONTROL_OK) {
            fail_pm_control(hart, frame, state);
        }
        micros_pm_control_abort_commit_prevalidated(
            state,
            objects,
            &abort
        );
        return return_result(frame, MICROS_SYSCALL_ABI_OK);
    }
    fail_pm_control(hart, frame, state);
}
