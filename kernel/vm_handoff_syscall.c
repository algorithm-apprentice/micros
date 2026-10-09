#include "kernel/vm_handoff_syscall.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/trap_context.h"
#include "kernel/bootstrap_runtime.h"
#include "kernel/endpoint_internal.h"
#include "kernel/ipc_runtime_internal.h"
#include "kernel/vm_handoff_core.h"
#include "kernel/vm_handoff_runtime.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/panic.h"
#include "micros/user_address_space.h"

static _Noreturn void panic_vm_handoff(
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

static _Noreturn void fail_vm_handoff(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_bootstrap_diagnostic_reason reason,
    const struct micros_vm_handoff_state *state
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
            reason,
            state->service_id,
            state->endpoint,
            0
        );
    }
    panic_vm_handoff(hart, frame, "vm-handoff-invariant");
}

static bool vm_is_authorized(
    const struct micros_vm_handoff_state *state,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_syscall_context *context
)
{
    const struct micros_process *process;
    const struct micros_endpoint_record *endpoint;

    return (
        state != NULL
        && registry != NULL
        && objects != NULL
        && context->process.slot == state->process.slot
        && context->process.generation == state->process.generation
        && context->current.slot == state->thread.slot
        && context->current.generation == state->thread.generation
        && micros_process_resolve(
            objects,
            context->process,
            &process
        ) == MICROS_KERNEL_OBJECT_OK
        && process->primary_endpoint == state->endpoint
        && process->privilege_profile == state->profile_id
        && micros_endpoint_resolve_active(
            registry,
            objects,
            state->endpoint,
            &endpoint
        ) == MICROS_ENDPOINT_OK
        && endpoint->owner.slot == context->process.slot
        && endpoint->owner.generation
            == context->process.generation
        && micros_privilege_profile_allows_kernel_operation(
            registry,
            (uint8_t)state->profile_id,
            1
        ) == MICROS_ENDPOINT_OK
    );
}

static bool vm_phase_is_ready(
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_vm_handoff_state *state,
    const struct micros_frame_ownership *ownership
)
{
    size_t index;

    if (
        bootstrap == NULL
        || state == NULL
        || ownership == NULL
        || bootstrap->phase != MICROS_BOOTSTRAP_PHASE_RUNNING
        || bootstrap->transitions.starting_service_id
            != state->service_id
        || state->phase != MICROS_VM_HANDOFF_PHASE_PREPARED
        || ownership->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
    ) {
        return false;
    }
    for (
        index = 0;
        index < bootstrap->transitions.entry_count;
        ++index
    ) {
        const struct micros_bootstrap_runtime_entry *entry =
            &bootstrap->transitions.entries[index];

        if (entry->service_id == state->service_id) {
            return entry->state
                == MICROS_BOOTSTRAP_SERVICE_STARTING;
        }
    }
    return false;
}

enum micros_syscall_return
micros_vm_handoff_handle_captured_user_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_syscall_arguments *arguments
)
{
    struct micros_vm_handoff_request request;
    struct micros_vm_handoff_state *state;
    struct micros_bootstrap_control_state *bootstrap;
    struct micros_endpoint_registry *registry;
    const struct micros_frame_ownership *ownership;

    if (
        hart == NULL
        || frame == NULL
        || context == NULL
        || arguments == NULL
        || context->objects == NULL
    ) {
        panic_vm_handoff(hart, frame, "vm-handoff-argument");
    }
    if (
        micros_vm_handoff_decode(arguments, &request)
            != MICROS_SYSCALL_ABI_OK
    ) {
        return return_result(frame, MICROS_SYSCALL_ABI_ARGUMENT);
    }
    state = micros_vm_handoff_runtime_authoritative_state();
    bootstrap = micros_bootstrap_runtime_authoritative_state();
    registry = micros_ipc_runtime_authoritative_registry();
    if (
        !vm_is_authorized(
            state,
            registry,
            context->objects,
            context
        )
    ) {
        return return_result(
            frame,
            MICROS_SYSCALL_ABI_UNAUTHORIZED
        );
    }
    ownership = micros_frame_ownership_runtime_ledger();
    if (ownership == NULL) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    if (
        bootstrap != NULL
        && bootstrap->transitions.entry_count
            > MICROS_BOOTSTRAP_SERVICE_CAPACITY
    ) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    if (!vm_phase_is_ready(bootstrap, state, ownership)) {
        return return_result(frame, MICROS_SYSCALL_ABI_STATE);
    }
    if (
        micros_bootstrap_runtime_validate()
            != MICROS_BOOTSTRAP_OK
        || micros_bootstrap_runtime_validate_vm_prepared()
            != MICROS_BOOTSTRAP_OK
    ) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    if (!micros_vm_handoff_summary_matches(state, &request)) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_READY_ROLE_GATE,
            state
        );
    }
    if (
        micros_user_address_space_complete_wired_handoff()
            != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_READY_ROLE_GATE,
            state
        );
    }
    micros_vm_handoff_commit_prevalidated(state);
    return return_result(frame, MICROS_SYSCALL_ABI_OK);
}
