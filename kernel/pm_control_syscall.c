#include "kernel/pm_control_syscall.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/trap_context.h"
#include "kernel/bootstrap_runtime.h"
#include "kernel/grant_runtime_internal.h"
#include "kernel/ipc_runtime_internal.h"
#include "kernel/pm_control_core.h"
#include "kernel/pm_control_runtime.h"
#include "kernel/pm_control_runtime_core.h"
#include "kernel/pm_control_syscall_core.h"
#include "kernel/vm_handoff_runtime.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_core.h"
#include "micros/ipc_runtime.h"
#include "micros/panic.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

static void copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

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
    struct micros_vm_handoff_state *handoff;
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
    handoff = micros_vm_handoff_runtime_authoritative_state();
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

        control_error =
            micros_pm_control_runtime_reserve_preflight(
                state,
                handoff
            );
        if (control_error == MICROS_PM_CONTROL_ERROR_STATE) {
            return return_result(frame, MICROS_SYSCALL_ABI_STATE);
        }
        if (control_error != MICROS_PM_CONTROL_OK) {
            fail_pm_control(hart, frame, state);
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

        control_error =
            micros_pm_control_runtime_abort_preflight(
                state,
                handoff
            );
        if (control_error == MICROS_PM_CONTROL_ERROR_STATE) {
            return return_result(frame, MICROS_SYSCALL_ABI_STATE);
        }
        if (control_error != MICROS_PM_CONTROL_OK) {
            fail_pm_control(hart, frame, state);
        }
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
    if (request.command == MICROS_PM_CONTROL_PREPARE_EXECUTION) {
        struct micros_pm_control_prepare_plan prepare;
        struct micros_pm_control_execution_binding binding;
        struct micros_user_execution_binding execution;
        struct micros_pm_control_output_plan output;
        enum micros_pm_control_output_error output_error;
        enum micros_user_execution_error execution_error;

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
        control_error = micros_pm_control_prepare_preflight(
            state,
            objects,
            registry,
            handoff,
            request.transaction,
            request.load_token,
            request.mapping_generation,
            &prepare
        );
        if (control_error == MICROS_PM_CONTROL_ERROR_STATE) {
            return return_result(frame, MICROS_SYSCALL_ABI_STATE);
        }
        if (control_error == MICROS_PM_CONTROL_ERROR_CAPACITY) {
            return return_result(
                frame,
                MICROS_SYSCALL_ABI_CAPACITY
            );
        }
        if (control_error != MICROS_PM_CONTROL_OK) {
            fail_pm_control(hart, frame, state);
        }
        execution_error =
            micros_user_execution_preflight_post_handoff(
                prepare.process,
                prepare.thread,
                &prepare.initial_context,
                &execution
            );
        if (execution_error != MICROS_USER_EXECUTION_OK) {
            fail_pm_control(hart, frame, state);
        }
        binding.thread = execution.thread;
        binding.hart = execution.hart;
        binding.kernel_stack_bottom =
            execution.kernel_stack_bottom;
        binding.kernel_stack_top = execution.kernel_stack_top;
        copy_bytes(
            &binding.context,
            &execution.context,
            sizeof(binding.context)
        );
        micros_user_execution_commit_post_handoff_prevalidated(
            &execution
        );
        micros_pm_control_prepare_commit_prevalidated(
            state,
            objects,
            registry,
            handoff,
            &prepare,
            &binding
        );
        __asm__ volatile("fence.i" : : : "memory");
        micros_pm_control_output_commit_prevalidated(
            &output,
            &prepare.result
        );
        return return_result(frame, MICROS_SYSCALL_ABI_OK);
    }
    if (request.command == MICROS_PM_CONTROL_ABORT_PREPARED) {
        struct micros_pm_control_abort_prepared_plan abort;

        control_error =
            micros_pm_control_abort_prepared_preflight(
                state,
                objects,
                registry,
                handoff,
                request.transaction,
                request.load_token,
                request.mapping_generation,
                request.prepared_token,
                &abort
            );
        if (control_error == MICROS_PM_CONTROL_ERROR_STATE) {
            return return_result(frame, MICROS_SYSCALL_ABI_STATE);
        }
        if (control_error != MICROS_PM_CONTROL_OK) {
            fail_pm_control(hart, frame, state);
        }
        micros_pm_control_abort_prepared_commit_prevalidated(
            state,
            objects,
            registry,
            handoff,
            &abort
        );
        return return_result(frame, MICROS_SYSCALL_ABI_OK);
    }
    if (request.command == MICROS_PM_CONTROL_ACTIVATE) {
        struct micros_pm_control_activate_plan activate;

        control_error = micros_pm_control_activate_preflight(
            state,
            objects,
            registry,
            handoff,
            request.transaction,
            request.load_token,
            request.mapping_generation,
            request.prepared_token,
            &activate
        );
        if (control_error == MICROS_PM_CONTROL_ERROR_STATE) {
            return return_result(frame, MICROS_SYSCALL_ABI_STATE);
        }
        if (control_error != MICROS_PM_CONTROL_OK) {
            fail_pm_control(hart, frame, state);
        }
        micros_pm_control_activate_commit_prevalidated(
            state,
            objects,
            registry,
            handoff,
            &activate
        );
        return return_result(frame, MICROS_SYSCALL_ABI_OK);
    }
    if (request.command == MICROS_PM_CONTROL_EXIT_BEGIN) {
        struct micros_pm_control_exit_begin_plan begin;
        struct micros_pm_control_output_plan output;
        enum micros_pm_control_output_error output_error;

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
        control_error = micros_pm_control_exit_begin_preflight(
            state,
            objects,
            registry,
            handoff,
            request.endpoint,
            &begin
        );
        if (control_error == MICROS_PM_CONTROL_ERROR_STATE) {
            return return_result(frame, MICROS_SYSCALL_ABI_STATE);
        }
        if (control_error != MICROS_PM_CONTROL_OK) {
            fail_pm_control(hart, frame, state);
        }
        micros_pm_control_exit_begin_commit_prevalidated(
            state,
            objects,
            registry,
            &begin
        );
        micros_pm_control_output_commit_prevalidated(
            &output,
            &begin.result
        );
        return return_result(frame, MICROS_SYSCALL_ABI_OK);
    }
    if (request.command == MICROS_PM_CONTROL_EXIT_DETACH) {
        struct micros_pm_control_exit_detach_plan detach;
        struct micros_grant_cancel_plan grants;
        struct micros_grant_registry *grant_registry =
            micros_grant_runtime_authoritative_registry();

        control_error = micros_pm_control_exit_detach_preflight(
            state,
            objects,
            registry,
            handoff,
            request.transaction,
            request.endpoint,
            &detach
        );
        if (control_error == MICROS_PM_CONTROL_ERROR_STATE) {
            return return_result(frame, MICROS_SYSCALL_ABI_STATE);
        }
        if (
            control_error != MICROS_PM_CONTROL_OK
            || grant_registry == NULL
            || micros_grant_prepare_endpoint_cancel(
                grant_registry,
                registry,
                objects,
                request.endpoint,
                &grants
            ) != MICROS_GRANT_OK
        ) {
            fail_pm_control(hart, frame, state);
        }
        if (
            micros_ipc_endpoint_close(
                registry,
                objects,
                request.endpoint
            ) != MICROS_IPC_OK
            || micros_grant_commit_endpoint_cancel(
                grant_registry,
                &grants
            ) != MICROS_GRANT_OK
        ) {
            fail_pm_control(hart, frame, state);
        }
        micros_pm_control_exit_detach_commit_prevalidated(
            state,
            objects,
            handoff,
            &detach
        );
        return return_result(frame, MICROS_SYSCALL_ABI_OK);
    }
    if (
        request.command
            == MICROS_PM_CONTROL_RELEASE_EXITED_PROCESS
    ) {
        struct micros_pm_control_release_exited_plan release;

        control_error = micros_pm_control_release_exited_preflight(
            state,
            objects,
            handoff,
            request.transaction,
            &release
        );
        if (control_error == MICROS_PM_CONTROL_ERROR_STATE) {
            return return_result(frame, MICROS_SYSCALL_ABI_STATE);
        }
        if (control_error != MICROS_PM_CONTROL_OK) {
            fail_pm_control(hart, frame, state);
        }
        micros_pm_control_release_exited_commit_prevalidated(
            state,
            objects,
            &release
        );
        return return_result(frame, MICROS_SYSCALL_ABI_OK);
    }
    fail_pm_control(hart, frame, state);
}
